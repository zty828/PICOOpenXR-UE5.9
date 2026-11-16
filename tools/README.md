# tools/

Small, project-agnostic helpers used to verify the port. Nothing here is part of the plugin —
you do not need any of it to *use* PICOOpenXR.

## `evidence/gen_android_evidence.ps1` — regenerate the Android runtime summary

`evidence/android_runtime_evidence.txt` is a derived artefact: it is rebuilt from the raw logcat
(`evidence/android_openxr_vr_session.log`) and from the APK on disk on every run, so its numbers can
never drift away from the log the way a hand-edited summary does.

```powershell
pwsh -File tools/evidence/gen_android_evidence.ps1 `
    -LogPath evidence/android_openxr_vr_session.log `
    -ApkPath <build>/PICOHost59-arm64.apk `
    -OutPath evidence/android_runtime_evidence.txt `
    -AppPid  3647                     # PID of com.YourCompany.PICOHost59 in that capture
```

Everything it prints is computed: file sizes, line counts, the engine/OS/command-line banner, the
extension-plugin list, the XR state chain, frame-submission counts, the nine plugin-failure
signatures (all must be 0 for `Verdict: PASS`), and the platform-side error histogram. The only
literals in the script are section titles.

## `evidence/run_win64_packaged_test.ps1` + `evidence/gen_win64_evidence.ps1` — the Win64 pair

Same idea for the staged Windows build. The first script launches the packaged exe, samples it
(CPU time, log growth, working set) and then closes it with `WM_CLOSE` instead of `Stop-Process`,
so the shutdown path is the one a real user would take. The second turns the resulting engine log
plus the sampling JSON into `evidence/win64_runtime_evidence.txt`.

```powershell
pwsh -File tools/evidence/run_win64_packaged_test.ps1 `
    -ExePath <staging>/PICOHost59/Binaries/Win64/PICOHost59.exe `
    -Map /Game/Maps/PICOTestMap `
    -LogPath evidence/win64_packaged_run.log `
    -SamplePath evidence/win64_run_sampling.json

pwsh -File tools/evidence/gen_win64_evidence.ps1 `
    -LogPath evidence/win64_packaged_run.log `
    -SamplePath evidence/win64_run_sampling.json `
    -OutPath evidence/win64_runtime_evidence.txt
```

`-Map` matters: `BuildCookRun` cooks the map you pass to it, but the cooked
`Saved\Config\Windows\Engine.ini` has no `GameDefaultMap`, so a packaged build launched without
an explicit map falls back to `/Engine/Maps/Templates/OpenWorld`. Pass the map on the command line
(or set `GameDefaultMap` in your own project's config).

The generated file states plainly what it does and does not prove. On a machine with no PC OpenXR
runtime the plugin still mounts and its actors still run, but no XR session is created — the
`Verdict` block only claims the packaged-exe facts (mount, 7 extension plugins, map load, actor
tick, clean exit 0, zero failure signatures).

## `PICOValidate/` — a UE module that dumps the plugin's reflected surface

Copy this folder into your project's `Source/`, next to your game module, and add `"PICOValidate"`
to the `ExtraModuleNames` of the **Editor** target only:

```csharp
// <Project>Editor.Target.cs
ExtraModuleNames.AddRange(new string[] { "<Project>", "PICOValidate" });
```

It gives you two console commands:

| Command | What it does |
|---|---|
| `PICODumpReflectionSurface [OutPath]` | Walks every `/Script/PICO*` `UClass` / `UScriptStruct` / `UEnum` and writes a pipe-delimited dump: classes, CDOs, UFUNCTIONs, UPROPERTYs, struct properties, enum values, plus a `SUMMARY|` line. Default output: `<Project>/Saved/PICOSurface.txt`. |
| `PICOShaderCheck [OutPath]` | Resolves the plugin's shader types by name and asks the *global* shader map whether each one actually compiled. Default output: `<Project>/Saved/PICOShaders.txt`. |

Why a separate module instead of just calling `TObjectIterator` from existing code? Two engine
constraints shaped the design, both documented in `PICOValidate.Build.cs`:

1. **Do not depend on anything that pulls in `Kismet`/`BlueprintEditor`.** `BlueprintEditorModule.cpp`
   has a `check(GEditor)` that fires when a standalone/commandlet process loads it, which kills any
   headless run.
2. **Do not depend on `PICOOpenXRHMD`.** Its shader header lives in a `Private/` directory and is not
   exportable.

The module builds against `Core`, `CoreUObject`, `Engine` only. `RenderCore` and `RHI` are added as
*private* dependencies for the shader check.

## `python/verify_plugin.py` — an end-to-end smoke test

Runs inside the editor's Python environment and checks, in one pass:

1. every class from the reflection dump loads **and** produces a Class Default Object,
2. every struct and enum loads,
3. every asset under `/PICOOpenXR` loads, and every Blueprint compiles to `BS_UP_TO_DATE`,
4. one representative type per module resolves (proof the module actually mounted, not just that
   the `.uplugin` listed it).

```bash
set PICO_SURFACE=<abs>/Saved/PICOSurface.txt   # output of PICODumpReflectionSurface
set PICO_OUT=<abs>/Saved/PICOPluginVerify.json
UnrealEditor-Cmd.exe <abs>/<Project>.uproject \
    -ExecutePythonScript="<abs>/tools/python/verify_plugin.py" \
    -unattended -nosplash -nullrhi -NoSound -nop4
```

`PICO_SURFACE` is required — it is the reflection dump produced by `PICODumpReflectionSurface` in
step 1. `PICO_OUT` is optional and defaults to `<Project>/Saved/PICOPluginVerify.json`.
Exit code 0 = pass, 1 = fail; the JSON report is always written so you can see what broke.

### Notes on two non-obvious API quirks

* `unreal.Blueprint.get_editor_property("status")` returns an enum *object*, and `str()` on it
  renders as `<BlueprintStatus.BS_UP_TO_DATE: 3>` — compare `int(status.value)` against `(3, 4)`
  (up-to-date, up-to-date-with-warnings), never the string.
* `dir()` on a `UClass` gives you only generic `Object` methods. To enumerate the real
  `UFUNCTION`s / `UPROPERTY`s you must go through `unreal.get_type_from_class(cls)`.
