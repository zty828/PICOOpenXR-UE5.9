# Patch reference

Baseline: `PICO Unreal SDK 6.1.4` → `UE_5.8/Plugins/PICOOpenXR`, 350 files.
Result: 351 files — **14 modified, 1 added, 0 deleted**. The 121 assets in `Content/` are untouched.

Generated diff: the original delivery bundle includes `PATCH_REPORT.txt` with every hunk.

---

## 1. `PICOOpenXR.uplugin`

```diff
-  "EngineVersion": "5.8.0",
+  "EngineVersion": "5.9.0",
```

---

## 2. `Source/PICOOpenXRHMD/Private/PICO_HMD.cpp`

### 2a. RHI thread shutdown (API removed in 5.9)

In 5.8 the plugin assigned the exported globals `ERHIThreadMode` / `GPendingRHIThreadMode`
directly from `RHI/Public/RHICommandList.h`. In 5.9 those moved into
`RenderCore/Private/RHIThread.h` and lost `RHI_API`, so the plugin cannot link to them at all.

The only remaining public path is the console command registered in `RHIThread.cpp`
(`HandleRHIThreadEnableChanged` + `FAutoConsoleCommand`), consumed by
`LatchRenderThreadConfiguration()` at the top of `FEngineLoop::Tick`.

```cpp
GEngine->Exec(nullptr, TEXT("r.RHIThread.Enable 0"));
```

`0 = None`, `1 = DedicatedThread`, `2 = Tasks`. Semantics match the 5.8 behaviour. This still works
in Shipping because `TargetRules.bUseExecCommandsInShipping` defaults to `true`.

Runtime proof: `LogPICOOpenXRHMD: PICO disable RHI thread requested (r.RHIThread.Enable 0), issued: 1`

### 2b. Two `UE_LOG` format-string casts

See [section A](#a-format-string-validation-buildsettingsversion--v5) below.

---

## 3. `Source/PICOOpenXREditor/Private/PICO_HandGestureEditorLibrary.cpp`

`UE_ARRAY_COUNT(...)` yields `size_t`; passing it to `%d` trips the 5.9 compile-time check.
One `static_cast<int32>`.

---

## 4–5. `Source/PICOOpenXRInput/Private/PICO_Controller.h` / `.cpp`

### Upstream defect: PICO G3 controller loses *all* input

**Symptom.** On a real PICO runtime, `xrSuggestInteractionProfileBindings` returns
`XR_ERROR_PATH_UNSUPPORTED` for `/interaction_profiles/bytedance/pico_g3_controller` and fires an
`ensure`. The call submits *per interaction profile*, so one bad path discards the **entire G3
binding set** — on a G3 controller every input stops working.

**Root cause.** Engine side (`OpenXRInput.cpp`, `FOpenXRInputPlugin::AddInteractionProfile`) only
appends `/user/hand/<hand>/output/haptic` when `Profile.HasHaptics` is true. The plugin's own
`FControllerPICO::GetSuggestedBindings` checked only `bParametricHapticsExtensionEnabled` and
ignored whether the profile exposes a haptic output path — while `GetInteractionProfiles` already
recorded `OutHasHaptics.Add(false)` for G3. Result: a haptic path suggested for a profile that has
none.

**Fix.**

```cpp
// PICO_Controller.h
void AddProfile(XrPath Profile, bool bHasHaptics = true);
TArray<bool> ProfileHasHaptics;   // parallel to Profiles

// PICO_Controller.cpp — GetSuggestedBindings
const int32 ProfileIndex = Profiles.Find(InInteractionProfile);
const bool bProfileHasHaptics =
    ProfileIndex == INDEX_NONE ||
    !ProfileHasHaptics.IsValidIndex(ProfileIndex) ||
    ProfileHasHaptics[ProfileIndex];

if (bParametricHapticsExtensionEnabled && bProfileHasHaptics) { ... }
else if (!bParametricHapticsExtensionEnabled) { /* verbose: extension off */ }
else {
    UE_LOG(LogPICOOpenXRInput, Log,
        TEXT("Skipping haptic bindings - interaction profile %u does not expose a haptic output path"),
        static_cast<uint32>(InInteractionProfile));
}
```

Profiles not registered by the plugin (engine built-ins) keep their previous behaviour.
Parametric haptics itself is unaffected — only the illegal binding is skipped.

**Proof.** Before: 2 × `XR_ERROR_PATH_UNSUPPORTED` + 2 × ensure. After: 0 failures, 0 invalid paths,
and `Skipping haptic bindings - interaction profile 23 ...` is logged. Haptics still load:
`XR_EXT_haptic_parametric extension loaded successfully`.

> This is a 6.1.4 bug, present on UE 5.8 too. UE 5.9's stricter runtime merely surfaces it.

---

## 6–13. `Source/PICOOpenXRMR/Private/*.cpp`

Eight files, changes are mechanical: 30 format-string casts plus one removed include.

| File | Format casts |
|---|---|
| `PICO_DynamicObject.cpp` | 7 |
| `PICO_LightEstimation.cpp` | 5 |
| `PICO_MR.cpp` | 2 |
| `PICO_MRAsyncActions.cpp` | 2 |
| `PICO_SceneCapture.cpp` | 7 |
| `PICO_SpatialAnchor.cpp` | 1 |
| `PICO_SpatialMesh.cpp` | 5 |
| `PICO_SpatialPlane.cpp` | 6 |

### 13b. `PICO_LightEstimation.cpp` — removed `#include "OculusShaders.h"`

`RenderCore/Public/OculusShaders.h` no longer exists in 5.9. The plugin never referenced any
`FOculus*` shader class from it — only the `VULKAN_CUBEMAP_*` constants, which are defined locally
in the file. Plain removal, no replacement needed.

---

## 14. `Source/PICOOpenXRRuntimeSettings/Private/PICOOpenXRRuntimeSettings.cpp`

### UE 5.9 gates OpenXR on Android behind a packaging flag

This is the single most important change in the port, because **without it the plugin silently does
nothing on Android** while the editor looks perfectly healthy.

`Engine/Source/ThirdParty/OpenXR/OpenXR_APL.xml` reads `AndroidRuntimeSettings::bPackageForOpenXRImmersive`
(default `false`). Only when it is true does the APL inject
`org.khronos.openxr.intent.category.IMMERSIVE_HMD` and
`org.khronos.openxr.permission.OPENXR_SYSTEM` into the manifest. Otherwise
`OpenXRHMDModule.cpp` gives up:

```
LogHMD: OpenXR disabled because this app is not packaged for OpenXR
```

UE 5.8 had no such gate (`OpenXRHMD` there is header-only, no module `.cpp`), which is why a
straight recompile of the 6.1.4 plugin appears to work and then fails only on device.

**Fix.** New helper, called at the end of the existing `WITH_EDITOR` block in
`UPICOOpenXRRuntimeSettings::PostInitProperties()`:

```cpp
void EnsureOpenXRImmersivePackagingEnabled()
{
    // read [/Script/AndroidRuntimeSettings.AndroidRuntimeSettings] bPackageForOpenXRImmersive
    // write TEXT("True") + Flush ONLY when the key is absent
    // never overrides an explicit user value
}
```

It follows the same "write an ini key from `PostInitProperties`" pattern the class already used for
`pvr.app.type`, so no engine modification is required.

Log line:

```
PICO OpenXR: enabled bPackageForOpenXRImmersive in %s (required by UE5.9 to keep OpenXR active on Android).
```

**Proof.** Host project and a clean empty project both go `0 → 1`, and the device log then shows
`App is packaged for Android OpenXR ImmersiveHMD` followed by `xrCreateInstance succeeded`.

---

## 15. `Config/DefaultPICOOpenXR.ini` — **added file**

```ini
[CoreRedirects]
+StructRedirects=(OldName="SceneCaptureGeneratorActor",NewName="/Script/PICOOpenXRMR.SceneCaptureGeneratorActorPICO")
+StructRedirects=(OldName="PICOUVAdjustment",NewName="/Script/PICOOpenXRMR.UVAdjustmentPICO")
```

`BP_SceneCapturesGenerator_Sample` in the 6.1.4 content stores pre-rename struct tags while the C++
side only defines the new names, producing `Struct Property ... has a struct type mismatch` warnings
on load (6 warnings → 0 after the redirects). Upstream issue, present on 5.8 as well.

Why a plugin-level ini works: `FCoreRedirects::Initialize()` iterates every ini registered in
`GConfig`, and `PluginManager` registers plugin `Config/` automatically. PICO's own `PICOSpatial`
plugin uses the same mechanism.

---

## A. Format string validation (BuildSettingsVersion ≥ V5)

`TargetRules.bValidateFormatStrings` defaults to `true` from `BuildSettingsVersion.V5`, and
`UBuildModuleCPP.cs` injects `UE_VALIDATE_FORMAT_STRINGS=1`, which turns mismatched `UE_LOG`
arguments into a hard `static_assert`:

```
error C2338: static_assert failed: "'%d' expects integral arg (eg. char, int, long, etc.)"
        in FormatStringSanErrors.inl(41,1)
```

Only `enum class` and `size_t` arguments are rejected — C-style enums and `bool` are promoted to
`int` and pass. **38 sites** across 10 files were fixed with `static_cast<int32>`:

| File | Casts |
|---|---|
| `PICO_HandGestureEditorLibrary.cpp` | 1 |
| `PICO_HMD.cpp` | 2 |
| `PICO_DynamicObject.cpp` | 7 |
| `PICO_LightEstimation.cpp` | 5 |
| `PICO_MR.cpp` | 2 |
| `PICO_MRAsyncActions.cpp` | 2 |
| `PICO_SceneCapture.cpp` | 7 |
| `PICO_SpatialAnchor.cpp` | 1 |
| `PICO_SpatialMesh.cpp` | 5 |
| `PICO_SpatialPlane.cpp` | 6 |
| **Total** | **38** |

Disabling `bValidateFormatStrings` was deliberately **not** done: these format strings were real
bugs (enum / 64-bit argument passed to `%d` is undefined behaviour, and it aborts on Android), and
a target-level opt-out would only silence the symptom for one project.