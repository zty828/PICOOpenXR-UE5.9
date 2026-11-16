# PICO OpenXR for Unreal Engine 5.9

Unofficial adaptation of **PICO OpenXR** (shipped inside *PICO Unreal SDK 6.1.4*, which targets Unreal Engine 5.8) so that it compiles and runs on **Unreal Engine 5.9**.

> **This is not an official PICO or Epic product.** It is a community port built by applying source-level fixes to the 6.1.4 plugin. PICO-authored code remains `Copyright PICO Inc. All Rights Reserved.` — see [NOTICE.md](NOTICE.md) before redistributing.

Verified on **UE 5.9.0 (CL 60351404, `++UE5+Release-5.9`)** — editor, Win64 packaging and a full Android OpenXR session on a real PICO runtime.

<img src="docs/images/android-run.jpg" width="420" alt="The packaged APK rendering through PICO OpenXR on a PICO Android runtime">

*The packaged APK running through PICO OpenXR on a PICO OS Android runtime — the session reached `FOCUSED` and stereo layers were submitted every frame. This is a frame captured from the device, not a render target. Frame rate on the PICO emulator image is CPU-bound at ~12 FPS (`FrmCpu` ≈ 50 ms, `FrmGpu` ≈ 2.4 ms); that is the emulator's software path, not the plugin.*

---

## Why a port is needed

UE 5.9 breaks the 6.1.4 plugin in four places:

| # | Breakage | Fix |
|---|---|---|
| 1 | UE 5.9 enables `bValidateFormatStrings` by default (`BuildSettingsVersion >= V5`). 38 `UE_LOG` calls pass `enum class` / `size_t` to `%d` and now fail `static_assert`. | `static_cast<int32>` on each argument (38 sites). **Not** bypassed by disabling the check — the format strings were genuine 64-bit/enum UB on Android. |
| 2 | `ERHIThreadMode` / `GPendingRHIThreadMode` moved from `RHI/Public/RHICommandList.h` into `RenderCore/Private/RHIThread.h` and are no longer exported. | Drive the existing console command `r.RHIThread.Enable 0` through `GEngine->Exec()`. |
| 3 | `RenderCore/Public/OculusShaders.h` was deleted in 5.9. | Removed the include — the plugin never used the `FOculus*` classes, only the `VULKAN_CUBEMAP_*` constants. |
| 4 | **UE 5.9 gates OpenXR on Android behind a new packaging flag.** Without `bPackageForOpenXRImmersive=True`, `OpenXRHMDModule` prints *"OpenXR disabled because this app is not packaged for OpenXR"* and never initialises — every PICO feature silently disappears on device while the editor still looks fine. | The plugin now checks `[/Script/AndroidRuntimeSettings.AndroidRuntimeSettings]` in `DefaultEngine.ini` and writes `True` **only when the key is absent**, never overriding an explicit user setting. |

Two upstream defects that also affect 6.1.4 on UE 5.8 were fixed while porting:

- **PICO G3 controller loses all input.** The G3 interaction profile has no haptic output path, yet the plugin still suggested `/user/hand/*/output/haptic` for it. Because `xrSuggestInteractionProfileBindings` submits per profile, the runtime rejected the *whole* G3 set with `XR_ERROR_PATH_UNSUPPORTED`. The plugin now tracks per-profile haptics capability and skips the haptic binding only for profiles that do not expose one.
- **`BP_SceneCapturesGenerator_Sample` logs struct-type-mismatch warnings.** The sample asset stores pre-rename struct tags; `Config/DefaultPICOOpenXR.ini` adds two `+StructRedirects` so old tags resolve to `SceneCaptureGeneratorActorPICO` / `UVAdjustmentPICO`.

See [docs/PATCHES.md](docs/PATCHES.md) for the per-file breakdown (14 files modified, 1 added, 0 deleted).

---

## Requirements

| Component | Version used for verification |
|---|---|
| Unreal Engine | 5.9.0 (CL 60351404) |
| Android NDK | 27.2.12479018 |
| Android SDK | API level 36, build-tools 36.0.0 |
| Visual Studio | 2022 17.14, MSVC 14.44.35207 |
| Windows SDK | 10.0.22621.0 |

Required engine plugins (all bundled with UE 5.9): `OpenXR`, `XRBase`, `PICOController`, `ProceduralMeshComponent`, `AndroidPermission`, `LiveLink`.

---

## Installation

1. Copy the `PICOOpenXR` folder into `<YourProject>/Plugins/`.
2. Enable it in the `.uproject` (or via the Plugins panel).
3. Restart the editor. On first load the plugin writes `bPackageForOpenXRImmersive=True` into `Config/DefaultEngine.ini` if you have not set it. Keep that value — without it OpenXR will not start on Android.
4. For Android, package normally (`BuildCookRun`) and launch with the VR intent extra:

   ```
   adb shell am start -W -n <package>/com.epicgames.unreal.GameActivity --es cmdline "-vr"
   ```

   `-vr` is read by `LaunchAndroid.cpp`; alternatively set `bStartInVR=True` in `Config/DefaultGame.ini`. Without one of the two, `FOpenXRHMD::EnableStereo(true)` is never called and the HMD stays dormant.

---

## Verification summary

Everything below was executed on UE 5.9.0. The raw logs and reports behind each number are in [`evidence/`](evidence/); the method behind them is in [docs/VERIFICATION.md](docs/VERIFICATION.md).

| Check | Result |
|---|---|
| Compile: Win64 Editor Development / Win64 Game Shipping / Android Shipping | `Result: Succeeded`, 0 errors |
| Editor load | 10/10 modules loaded, **7/7** `IOpenXRExtensionPlugin` implementations registered |
| Reflection surface | classes 86 · UFUNCTION 273 · UPROPERTY 314 · structs 51 · struct props 183 · enums 63 · enum values 418 · CDO failures 0 |
| Content / blueprints | 121/121 assets load · 17/17 blueprints compile to `BS_UP_TO_DATE` |
| Instantiation & calls | 6 C++ actors + 12 components + 8 sample blueprints instantiate; 10/10 zero-arg BlueprintCallable functions invoke successfully |
| Shaders (real D3D12) | known 2 · unregistered 0 · compiled 3 · missing 0 |
| Win64 BuildCookRun | 737 packages · 0 errors · 31 warnings · `BUILD SUCCESSFUL` |
| Win64 packaged runtime | exe mounts the plugin, loads `/Game/Maps/PICOTestMap`, 7/7 extension plugins register, plugin actors run `BeginPlay` and tick (5 `LogPICOCameraPreview` lines, 51 eye-tracking tick lines over 50.2 s), alive through 60 s with 198 s CPU consumed, exits 0 via `WM_CLOSE`; fatal/assert/ensure/error signatures all 0 |
| Android BuildCookRun | 737 packages · 0 errors · `BUILD SUCCESSFUL` (APK 131,193,373 B + OBB 85,153,006 B) |
| Clean install (fresh empty project, full rebuild) | 111 actions / 414.01 s · `Result: Succeeded` · 86/51/63/121/17 all pass |
| **Android runtime, real PICO runtime** | `xrCreateInstance succeeded` (XR_API_VERSION_1_1) · session `IDLE→READY→SYNCHRONIZED→VISIBLE→FOCUSED` · 97 frame submissions from the app process (102 `PxrMetric` lines in the capture) · `XR_ERROR_PATH_UNSUPPORTED` 0 · `Critical error` 0 · `Assertion failed` 0 |

---

## Repository layout

```
PICOOpenXR/                      the plugin itself (drop into <Project>/Plugins/)
  PICOOpenXR.uplugin             EngineVersion 5.9.0
  Config/DefaultPICOOpenXR.ini   added — CoreRedirects for renamed structs
  Content/                       121 assets, identical to upstream 6.1.4
  Shaders/  Resources/  Source/  10 modules + ThirdParty loader
docs/
  PATCHES.md                     every change, file by file
  VERIFICATION.md                what was tested, how, and the evidence
  PATCH_REPORT.txt               generated per-hunk diff against upstream 6.1.4
  images/android-run.jpg         the Android session this port was validated on
evidence/                        raw logs and reports behind every number above
tools/
  evidence/gen_android_evidence.ps1   regenerates evidence/android_runtime_evidence.txt
  evidence/gen_win64_evidence.ps1     regenerates evidence/win64_runtime_evidence.txt
  evidence/run_win64_packaged_test.ps1  launches the staged exe, samples CPU/log/working
                                 set, then closes it with WM_CLOSE
  PICOValidate/                  the standalone UE module that dumps the reflection
                                 surface and checks that plugin shaders compile
  python/verify_plugin.py        editor-side self-check (Maps all types, assets,
                                 blueprints, modules) — run it in your own project
NOTICE.md                        upstream copyright and third-party licenses
```

To re-run the verification in your own project, see [tools/README.md](tools/README.md).

---

## Known limitations

1. **No PC OpenXR runtime was available on the build machine**, so the Windows build is verified as far as "packaged exe starts, mounts the plugin, registers 7 extension plugins, loads the test map, runs the plugin actors, and shuts down cleanly with exit code 0" — the OpenXR instance itself fails with `XR_ERROR_VALIDATION_FAILURE` because SteamVR's `vrserver` exits immediately on that machine, so no XR session is ever created and every `XR_PICO_*` extension reports "not available". Full OpenXR session evidence comes from Android. If you have a PICO headset, please re-test on Win64 and open an issue with the log.
2. **Two upstream assets are genuinely missing** in 6.1.4: `/PICOOpenXR/Meshes/MixedReality/S_Gnomon` and `SceneCaptureBounds`. A recursive search of the whole SDK returns 0 hits. No placeholder assets were added, to stay byte-identical to the official content.
3. **Android emulator flakes.** After several install/crash cycles the emulator's `RHICache` ownership or EGL state gets corrupted, producing a `VulkanChunkedPipelineCache.cpp` `bWriteSuccess` assert or a `libEGL` SIGSEGV. `adb emu kill` + cold boot fixes it; the same APK then runs fine. Neither failure is in plugin code.

---

## Credits

- **PICO OpenXR plugin** — PICO Inc., shipped in PICO Unreal SDK 6.1.4. Copyright PICO Inc., all rights reserved.
- **OpenXR headers and loader** — Khronos Group, `Apache-2.0 OR MIT`; see `PICOOpenXR/Source/ThirdParty/Licenses/`.
- **UE 5.9 port, G3 haptics fix, CoreRedirects, verification harness** — this repository.

Read [NOTICE.md](NOTICE.md) before redistributing.