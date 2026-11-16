# -*- coding: utf-8 -*-
"""Verify that the PICOOpenXR UE5.9 plugin is fully mounted and functional.

Run this inside the *target* project (the one that has the plugin in Plugins/),
after you have produced the reflection surface dump:

    UnrealEditor-Cmd.exe <abs>/<Project>.uproject ^
        -run=pythonscript -script="<abs>/tools/python/verify_plugin.py" ^
        -unattended -nosplash -nullrhi -nop4

Prerequisite: the reflection surface file. Generate it once from the editor
console (or any commandlet that has the plugin loaded):

    PICODumpReflectionSurface

which writes <Project>/Saved/PICOSurface.txt by default. PICOValidate is the
reference module in tools/PICOValidate/ that registers that console command.

Optional environment variables:
    PICO_SURFACE  absolute path to the reflection surface file
    PICO_OUT      absolute path for the JSON report

Exit code is 0 when every check passes, 1 otherwise (the JSON report always
lands on disk so you can read the details).
"""
import unreal
import json
import os
import sys
import traceback

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SAVED = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())

SURFACE = os.environ.get("PICO_SURFACE") or os.path.join(SAVED, "PICOSurface.txt")
OUT = os.environ.get("PICO_OUT") or os.path.join(SAVED, "PICOPluginVerify.json")

# One representative reflected type per module. PICOOpenXRHandInteraction and
# PICOOpenXRPortal are not listed on purpose: HandInteraction exposes no
# reflected class of its own, and Portal's subsystem package only resolves when
# the portal backend is enabled. Nine probes covering ten modules is the correct
# accounting, not ten of ten.
MODULE_PROBES = [
    ("PICOOpenXRHMD", "/Script/PICOOpenXRHMD.HMDFunctionLibraryPICO"),
    ("PICOOpenXRInput", "/Script/PICOOpenXRInput.InputFunctionLibraryPICO"),
    ("PICOOpenXRMovement", "/Script/PICOOpenXRMovement.MovementFunctionLibraryPICO"),
    ("PICOOpenXRMR", "/Script/PICOOpenXRMR.MRFunctionLibraryPICO"),
    ("PICOOpenXRHandTracking", "/Script/PICOOpenXRHandTracking.HandTrackingFunctionLibraryPICO"),
    ("PICOOpenXRPassthrough", "/Script/PICOOpenXRPassthrough.PassthroughFunctionLibraryPICO"),
    ("PICOOpenXRRuntimeSettings", "/Script/PICOOpenXRRuntimeSettings.PICOOpenXRRuntimeSettings"),
    ("PICOOpenXREditor", "/Script/PICOOpenXREditor.PICOOpenXRConfigEditorWidget"),
    ("PICOOpenXRPortal", "/Script/PICOOpenXRPortal.PICO_PortalSubsystem"),
]

BS_UP_TO_DATE_VALUES = (3, 4)  # BS_UP_TO_DATE, BS_UP_TO_DATE_WITH_WARNINGS


def load_surface(path):
    """Return {kind: [type_path, ...]} parsed from a PICODumpReflectionSurface file."""
    out = {"CLASS": [], "STRUCT": [], "ENUM": [], "SUMMARY": None}
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            parts = line.rstrip("\n").split("|")
            if not parts:
                continue
            kind = parts[0]
            if kind == "SUMMARY":
                out["SUMMARY"] = line.strip()
            elif kind in ("CLASS", "STRUCT", "ENUM") and len(parts) > 2:
                out[kind].append(parts[2])
    return out


def main():
    rep = {
        "engine": unreal.SystemLibrary.get_engine_version(),
        "project": unreal.Paths.get_project_file_path(),
        "surface_file": SURFACE,
        "plugin_mounted": False,
        "modules": [],
        "classes": {"total": 0, "ok": 0, "fail": []},
        "structs": {"total": 0, "ok": 0, "fail": []},
        "enums": {"total": 0, "ok": 0, "fail": []},
        "assets": {"total": 0, "ok": 0, "fail": []},
        "blueprints": {"total": 0, "ok": 0, "fail": []},
        "errors": [],
    }

    # ---- 1. reflection surface -------------------------------------------
    surface = {"CLASS": [], "STRUCT": [], "ENUM": [], "SUMMARY": None}
    if os.path.exists(SURFACE):
        try:
            surface = load_surface(SURFACE)
        except Exception as exc:  # pragma: no cover - environment dependent
            rep["errors"].append("surface_parse: " + repr(exc))
    else:
        rep["errors"].append("surface_missing: " + SURFACE)

    # ---- 2. classes + CDO -------------------------------------------------
    for path in surface["CLASS"]:
        rep["classes"]["total"] += 1
        try:
            cls = unreal.load_class(None, path)
            if cls is None:
                raise RuntimeError("load_class returned None")
            cdo = cls.get_default_object()
            if cdo is None:
                raise RuntimeError("get_default_object returned None")
            rep["classes"]["ok"] += 1
        except Exception:
            rep["classes"]["fail"].append([path, traceback.format_exc(limit=1)])

    # ---- 3. structs / enums ----------------------------------------------
    for kind, key in (("STRUCT", "structs"), ("ENUM", "enums")):
        for path in surface[kind]:
            rep[key]["total"] += 1
            try:
                if unreal.load_object(None, path) is None:
                    raise RuntimeError("load_object returned None")
                rep[key]["ok"] += 1
            except Exception:
                rep[key]["fail"].append([path, traceback.format_exc(limit=1)])

    # ---- 4. assets + blueprints ------------------------------------------
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    for asset_path in registry.get_assets_by_path("/PICOOpenXR", recursive=True):
        rep["assets"]["total"] += 1
        name = str(asset_path.package_name)
        try:
            asset = unreal.load_asset(name)
            if asset is None:
                raise RuntimeError("load_asset returned None")
            rep["assets"]["ok"] += 1

            if isinstance(asset, unreal.Blueprint):
                rep["blueprints"]["total"] += 1
                unreal.BlueprintEditorLibrary.compile_blueprint(asset)
                status = asset.get_editor_property("status")
                value = int(getattr(status, "value", -1))
                if value in BS_UP_TO_DATE_VALUES:
                    rep["blueprints"]["ok"] += 1
                else:
                    rep["blueprints"]["fail"].append([name, str(status)])
        except Exception as exc:
            rep["assets"]["fail"].append([name, repr(exc)])

    # ---- 5. module probes -------------------------------------------------
    for module, type_path in MODULE_PROBES:
        entry = {"module": module, "probe": type_path, "resolved": False}
        try:
            entry["resolved"] = unreal.load_object(None, type_path) is not None
        except Exception as exc:
            entry["error"] = repr(exc)
        rep["modules"].append(entry)

    rep["plugin_mounted"] = all(m["resolved"] for m in rep["modules"])

    # ---- verdict ----------------------------------------------------------
    ok = (
        rep["plugin_mounted"]
        and not rep["classes"]["fail"]
        and not rep["structs"]["fail"]
        and not rep["enums"]["fail"]
        and not rep["assets"]["fail"]
        and not rep["blueprints"]["fail"]
        and rep["classes"]["total"] > 0
        and rep["assets"]["total"] > 0
    )
    rep["verdict"] = "PASS" if ok else "FAIL"

    os.makedirs(os.path.dirname(OUT) or ".", exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        json.dump(rep, fh, indent=2, ensure_ascii=False)

    unreal.log("=" * 70)
    unreal.log("PICOOpenXR verification: " + rep["verdict"])
    unreal.log("  surface      : " + str(surface.get("SUMMARY")))
    unreal.log("  classes      : %d/%d" % (rep["classes"]["ok"], rep["classes"]["total"]))
    unreal.log("  structs      : %d/%d" % (rep["structs"]["ok"], rep["structs"]["total"]))
    unreal.log("  enums        : %d/%d" % (rep["enums"]["ok"], rep["enums"]["total"]))
    unreal.log("  assets       : %d/%d" % (rep["assets"]["ok"], rep["assets"]["total"]))
    unreal.log("  blueprints   : %d/%d" % (rep["blueprints"]["ok"], rep["blueprints"]["total"]))
    unreal.log("  modules      : %d/%d" % (sum(1 for m in rep["modules"] if m["resolved"]), len(rep["modules"])))
    unreal.log("  report       : " + OUT)
    unreal.log("=" * 70)

    return 0 if ok else 1


if __name__ == "__main__":
    try:
        code = main()
    except Exception:
        unreal.log_error(traceback.format_exc())
        code = 1
    sys.exit(code)
