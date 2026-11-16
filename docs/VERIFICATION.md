# Verification Report

All numbers below were produced on the machine that built this port (Windows 11, RTX-class GPU,
UE 5.9.0 / CL 60351404 / `++UE5+Release-5.9`).

Every claim is backed by a file in [`evidence/`](../evidence) — the log is named next to it.

---

## 1. Compilation

| Target | Configuration | Result |
|---|---|---|
| `PICOHost59Editor` | Win64 Development | **Succeeded** |
| `PICOHost59` | Win64 Shipping | **Succeeded** |
| `PICOHost59` | Android Shipping (arm64, ASTC) | **Succeeded** |

Toolchain reported by UBT (quoted verbatim, Win64 from `build_win64_game_shipping.log:12`,
Android from `build_android_shipping.log:10-14`):

```
Using Visual Studio 14.44.35229 toolchain
  (F:\GameEngine\UE5.8.3Source\Tools\VS2022\VC\Tools\MSVC\14.44.35207)
  and Windows 10.0.22621.0 SDK (C:\Program Files (x86)\Windows Kits\10).
NDK toolchain: r27c, NDK version: 270300, ClangVersion: 18.0.3
Compiling Native 64-bit code with NDK API 'android-26'
```

The "14.44.35229 toolchain / 14.44.35207 directory" split is UBT's own wording: the *toolchain
label* it prints and the *MSVC directory* it resolves differ in the last component on this
install. Both come from the same VS 2022 17.14 installation.

### Clean-room install

A **brand-new empty UE 5.9 project** (one blank Game module, no prior state) was created, the
delivered `PICOOpenXR` folder was copied into its `Plugins/`, and `Build.bat` was run from
scratch with the plugin's `Binaries/` and `Intermediate/` deleted first:

```
Using Unreal Build Accelerator local executor to run 111 action(s)
Total time in Unreal Build Accelerator local executor: 414.01 seconds
Result: Succeeded
```

→ `evidence/clean_install_editor.log`, `evidence/clean_install_verify.json`

This is the check that matters: it proves the plugin compiles and mounts in a project that has
never seen it before, not just in the development host.

---

## 2. Module load (editor)

All 10 plugin modules load, and **all 7 `IOpenXRExtensionPlugin` implementations register**:

```
LogHMD: Extensions for IOpenXRExtensionPlugins:
  PICOOpenXRPassthrough, PICOOpenXRHandInteraction, HandTrackingPICO,
  MRModuleExtensionPICO, MovementExtensionPICO, ControllerPICO, HMDPICO
```

→ `evidence/win64_packaged_run.log`

---

## 3. Reflection surface

`PICODumpReflectionSurface` (see [`tools/PICOValidate`](../tools/PICOValidate)) walks every
`/Script/PICO*` `UClass` / `UScriptStruct` / `UEnum` and dumps the full surface:

```
SUMMARY|classes=86|funcs=273|props=314|structs=51|structprops=183|enums=63|enumvalues=418|cdo_failures=0
```

`cdo_failures=0` means every one of the 86 classes produced a usable Class Default Object —
i.e. no class has a broken constructor or a missing native parent.

→ `evidence/ReflectionSurface.txt`

---

## 4. Assets and Blueprints

| Check | Result |
|---|---|
| Content assets load | **121 / 121** |
| Blueprints compile to `BS_UP_TO_DATE` | **17 / 17**, 0 errors |

→ `evidence/verify_blueprints.json`

## 5. Types, instantiation, function calls

| Check | Result |
|---|---|
| Classes load + CDO retrieved | 86 / 86 |
| Structs load | 51 / 51 |
| Enums load | 63 / 63 |
| C++ actors spawn | 6 / 6 |
| Components instantiate | 12 / 12 |
| Example Blueprints instantiate | 8 / 8 |
| Zero-arg Blueprint-callable functions invoked | **10 / 10 succeeded** |

→ `evidence/verify_all_types.json`, `verify_spawn.json`, `verify_calls3.json`, `verify_testmap.json`

> The 16 unmatched entries are the MR async-action node factories, which the engine's Python
> glue deliberately excludes from script exposure (`PyGenUtil.cpp` — `BlueprintInternalUseOnly`
> static factories are not script-exposed by design). They are reachable from Blueprints.

## 6. Shaders

Real D3D12 device, global shader map built, plugin shader types resolved by name:

```
SUMMARY|known=2|unregistered=0|compiled=3|missing=0
```

`FPICOCubemapPS` (`/Plugin/PICOOpenXR/Private/PICOShaders.usf`, entry `MainForCubemap`) and
`FDisplayMappingPixelShader` both compile.

→ `evidence/PICOShaderCheck.txt`

---

## 7. Cooking and packaging

| Platform | Cooked packages | Result |
|---|---|---|
| Win64 | 737 | `Success - 0 error(s), 31 warning(s)`, 49 files / 1,039,623,486 bytes |
| Android (arm64) | 737 | `Success - 0 error(s), 31 warning(s)`, APK 131,193,373 B + OBB 85,153,006 B |

The plugin's materials compile on **both** Android shader platforms
(`VULKAN_ES3_1_ANDROID` and `OPENGL_ES3_1_ANDROID`), and the APK ships both
`libopenxr_loader.so` and `libopenxr_loader_pico.so`.

→ `evidence/android_buildcookrun_full.log`, `win64_buildcookrun_full.log`

---

## 8. Runtime — Win64, packaged build

The staged package (`Win64Package/`, built by `BuildCookRun` above) was launched from its
own executable, with the test map passed on the command line because the cooked
`Saved\Config\Windows\Engine.ini` carries no `GameDefaultMap` (without it the engine falls
back to `/Engine/Maps/Templates/OpenWorld`):

```
PICOHost59.exe /Game/Maps/PICOTestMap -game -log -abslog=... \
    -nosplash -NoSound -nop4 -windowed -ResX=1280 -ResY=720
```

| Observation | Value |
|---|---|
| `Mounting Project plugin PICOOpenXR` | 1 |
| `IOpenXRExtensionPlugin` implementations registered | **7 / 7** |
| `LogLoad: LoadMap: /Game/Maps/PICOTestMap?Name=Player` | 1 |
| `LogPICOCameraPreview` lines (`BeginPlay` → `ShutdownCamera`) | 5 |
| `PICO ET Debug` eye-tracking tick lines | 51, spanning 50.2 s |
| Process alive at the 10 / 20 / 30 / 45 / 60 s samples | **5 / 5**, CPU time grew 13.06 s → **198.44 s** |
| Graceful shutdown | `WM_CLOSE` → `RequestExit(0, UGameEngine::Tick.ViewportClosed)` → exit code **0** |
| `Fatal error` / `Assertion failed` / `Ensure condition failed` / `LogOutputDevice: Error` / `XR_ERROR_PATH_UNSUPPORTED` | **0 / 0 / 0 / 0 / 0** |

→ `evidence/win64_packaged_run.log` (1,304 lines), summary in
`evidence/win64_runtime_evidence.txt`, raw process samples in `evidence/win64_run_sampling.json`.

Like the Android summary, that evidence file is **generated, not hand-written** — every counter
and quoted line is recomputed from the raw engine log and the sampling JSON by
[`tools/evidence/gen_win64_evidence.ps1`](../tools/evidence/gen_win64_evidence.ps1). The
sampling JSON itself comes from
[`tools/evidence/run_win64_packaged_test.ps1`](../tools/evidence/run_win64_packaged_test.ps1),
which launches the exe, samples it, and closes it with `WM_CLOSE` instead of killing it:

```
pwsh -File tools/evidence/run_win64_packaged_test.ps1 \
    -ExePath <staging>/PICOHost59/Binaries/Win64/PICOHost59.exe \
    -Map /Game/Maps/PICOTestMap \
    -LogPath evidence/win64_packaged_run.log \
    -SamplePath evidence/win64_run_sampling.json
pwsh -File tools/evidence/gen_win64_evidence.ps1 -LogPath evidence/win64_packaged_run.log \
    -SamplePath evidence/win64_run_sampling.json -OutPath evidence/win64_runtime_evidence.txt
```

**What this does not prove:** no PC OpenXR session was ever created — this machine has no working
PC OpenXR runtime (see §10), so the loader returns `XR_ERROR_VALIDATION_FAILURE` and every
`XR_PICO_*` extension reports *not available*. The extension list above comes from the plugin
side. The session-level proof is the Android capture in §9.

---

## 9. Runtime — Android, end-to-end OpenXR

This is the strongest evidence: the packaged APK was installed on a **PICO device-class Android
runtime** (PICO OS emulator image, Android 16, Pico XRRuntime `31258.43072.16384`) and driven
through the full OpenXR lifecycle.

```
LogHMD: OpenXRHMDModule: App is packaged for Android OpenXR ImmersiveHMD
LogHMD: xrCreateInstance succeeded with XR_API_VERSION_1_1
LogHMD: Initialized OpenXR on Pico XRRuntime() runtime version 31258.43072.16384
LogHMD: Extensions for IOpenXRExtensionPlugins: <all 7 plugins>
LogPICOOpenXRHMD: XR session state changed: 1 (IDLE)
LogPICOOpenXRHMD: XR session state changed: 2 (READY)
LogPICOOpenXRHMD: XR session state changed: 3 (SYNCHRONIZED)
LogPICOOpenXRHMD: XR session state changed: 4 (VISIBLE)
LogPICOOpenXRHMD: XR session state changed: 5 (FOCUSED)
LogPICOOpenXRHMD: PICO disable RHI thread requested (r.RHIThread.Enable 0), issued: 1
```

Counters over the whole 26,075-line session log:

| Metric | Value |
|---|---|
| `xrCreateInstance succeeded` | 1 |
| `not packaged for OpenXR` | **0** |
| XR session state transitions (1→5) | 5 |
| `XR_ERROR_PATH_UNSUPPORTED` | **0** |
| `Skipping haptic bindings` (G3 gating active) | 1 |
| `PxrMetric` submit lines, whole logcat | 102 (app process: 97; the other 5 come from the PICO launcher / spatial runtime) |
| App frame ids submitted | 2 → 1214 (monotonically increasing) |
| UE-side `Error:` / `Assertion failed` / `Critical error` | **0** |
| UE `LogOutputDevice: Error` / `Ensure condition failed` | **0** |

→ `evidence/android_openxr_vr_session.log` (5.0 MB), summary in
`evidence/android_runtime_evidence.txt`.

That summary file is **generated, not hand-written**: every counter and quoted line in it is
recomputed from the raw logcat and from the APK on disk by
[`tools/evidence/gen_android_evidence.ps1`](../tools/evidence/gen_android_evidence.ps1).

```
pwsh -File tools/evidence/gen_android_evidence.ps1 `
  -LogPath evidence/android_openxr_vr_session.log `
  -ApkPath <build>/PICOHost59-arm64.apk `
  -OutPath evidence/android_runtime_evidence.txt
```

The `Skipping haptic bindings` line is the direct proof that the G3 interaction-profile fix
(documented in [`PATCHES.md`](PATCHES.md) §4–5) works: the profile that has no haptic output path
is skipped instead of poisoning the whole `xrSuggestInteractionProfileBindings` call.

### Regression signature (before → after the haptic fix)

| | Before | After |
|---|---|---|
| `xrSuggestInteractionProfileBindings` failure + `Ensure condition failed` | 2 + 2 | **0 + 0** |
| `XR_ERROR_PATH_UNSUPPORTED` | 2 | **0** |
| `is not a valid path` | 1 | **0** |

---

## 10. What could NOT be verified here

| Item | Why |
|---|---|
| PC VR (Win64) OpenXR session | No working PC OpenXR runtime on the build machine. SteamVR is installed and registered as the active runtime, but its `vrserver` exits immediately (empty `SteamVR/logs`), so the loader returns `XR_ERROR_VALIDATION_FAILURE`. The packaged Win64 build is verified to *start, mount the plugin, register all 7 extension plugins, load the test map, run the plugin actors and shut down with exit code 0* with 0 errors — §8 above and `evidence/win64_runtime_evidence.txt` — but the XR instance itself cannot be created on this box. **Android is the target platform for this plugin, and it is fully verified.** |
| Physical PICO headset | Not available; the PICO OS emulator was used instead. It ships the real PICO OpenXR runtime, so the API-level path is genuine, but comfort/display characteristics are not testable. |

---

## Reproducing these checks

```bash
# 1. build
"<UE5.9>/Engine/Build/BatchFiles/Build.bat" <YourProject>Editor Win64 Development \
    -Project="<abs>/<YourProject>.uproject" -WaitMutex

# 2. copy tools/PICOValidate into <YourProject>/Source/ and build again.
#    It registers two console commands. Reflection dump works headless:
UnrealEditor-Cmd.exe <abs>/<YourProject>.uproject \
    -ExecCmds="PICODumpReflectionSurface <abs>/Saved/PICOSurface.txt,quit" \
    -unattended -nosplash -nullrhi -NoSound -nop4

#    The shader check needs a real RHI (a null RHI device compiles nothing and
#    reports every shader MISSING), so drop -nullrhi for this one:
UnrealEditor-Cmd.exe <abs>/<YourProject>.uproject \
    -ExecCmds="PICOShaderCheck <abs>/Saved/PICOShaderCheck.txt,quit" \
    -unattended -nosplash -NoSound -nop4

# 3. editor-side self-check: loads every /Script/PICO* type, every asset under
#    /PICOOpenXR and every Blueprint, then writes a JSON verdict (exit code 0/1).
#    PICO_SURFACE points at the PICODumpReflectionSurface output from step 2.
set PICO_SURFACE=<abs>/Saved/PICOSurface.txt
set PICO_OUT=<abs>/Saved/PICOPluginVerify.json
UnrealEditor-Cmd.exe <abs>/<YourProject>.uproject \
    -ExecutePythonScript="<abs>/tools/python/verify_plugin.py" \
    -unattended -nosplash -nullrhi -NoSound -nop4
```
