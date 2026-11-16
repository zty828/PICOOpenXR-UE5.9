# tools/

Small, project-agnostic helpers used to verify the port. Nothing here is part of the plugin —
you do not need any of it to *use* PICOOpenXR.

## `PICOValidate/` — a UE module that dumps the plugin's reflected surface

Drop this folder into your project's `Source/` next to your game module, add
`"PICOValidate"` to your `.Target.cs` `ExtraModuleNames`, and it gives you two console commands:

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
UnrealEditor-Cmd.exe <abs>/<Project>.uproject \
    -run=pythonscript -script="<abs>/tools/python/verify_plugin.py" \
    -unattended -nosplash -nullrhi -nop4
```

Environment overrides: `PICO_SURFACE` (input dump) and `PICO_OUT` (JSON report path).
Exit code 0 = pass, 1 = fail; the JSON report is always written so you can see what broke.

### Notes on two non-obvious API quirks

* `unreal.Blueprint.get_editor_property("status")` returns an enum *object*, and `str()` on it
  renders as `<BlueprintStatus.BS_UP_TO_DATE: 3>` — compare `int(status.value)` against `(3, 4)`
  (up-to-date, up-to-date-with-warnings), never the string.
* `dir()` on a `UClass` gives you only generic `Object` methods. To enumerate the real
  `UFUNCTION`s / `UPROPERTY`s you must go through `unreal.get_type_from_class(cls)`.
