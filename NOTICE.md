# NOTICE — copyright, licensing and redistribution

## 1. This repository is not an official PICO or Epic release

Everything under `PICOOpenXR/` originates from the **PICO OpenXR** plugin that ships inside
**PICO Unreal SDK 6.1.4** (the `UE_5.8` build, `EngineVersion 5.8.0`). It is copyright PICO Inc.

```
// Copyright 2023 PICO Inc. All Rights Reserved.
```

This repository applies source-level modifications to that plugin so that it compiles and runs on
Unreal Engine 5.9. The modifications are documented file by file in [docs/PATCHES.md](docs/PATCHES.md)
and as a full unified diff in [docs/PATCH_REPORT.txt](docs/PATCH_REPORT.txt).

## 2. Upstream is public, but carries no license

PICO publishes the same plugin source publicly in its own GitHub organisation:
[`Pico-Developer/PICO-Unreal-Integration-SDK`](https://github.com/Pico-Developer/PICO-Unreal-Integration-SDK)
(`UE_5.7/Plugins/PICOOpenXR/`, several engine branches). That repository ships **no LICENSE,
EULA or terms file** — neither does the SDK download. So hosting a source-level port matches what
PICO already does with this code, but nobody has ever granted explicit redistribution terms.

Practical reading:

- **Personal / internal / evaluation use** — same footing as using the official SDK. Nothing here
  makes it more or less restricted than the vendor's own download.
- **Redistribution or shipping in a product** — check the terms that came with your SDK download,
  or ask PICO. Do not treat this repository as granting you rights to PICO's code.
- **If you are acting for PICO and want this taken down** — open an issue and it goes.

If PICO publishes an official UE 5.9 build, prefer that over this repository.

## 3. Third-party components

### OpenXR headers and loader — The Khronos Group

`PICOOpenXR/Source/ThirdParty/PICOOpenXRLoader/include/openxr/` is derived from
[OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) and
[OpenXR-SDK-Source](https://github.com/KhronosGroup/OpenXR-SDK-Source) and carries
`SPDX-License-Identifier: Apache-2.0 OR MIT` in its headers, preserved verbatim. Full texts:

- `PICOOpenXR/Source/ThirdParty/Licenses/OpenXR-SDK.txt` (Apache-2.0)
- `PICOOpenXR/Source/ThirdParty/Licenses/OpenXR-SDK-MIT.txt` (MIT)

`libopenxr_loader_pico.so` (`Source/ThirdParty/PICOOpenXRLoader/libs/arm64-v8a/`) is PICO's
loader build with the PICO extension set; provenance in
`PICOOpenXR/Source/ThirdParty/PICOOpenXRLoader/NOTICE.txt`.

### PICO AppLog SDK

`PICOOpenXR/Source/PICOOpenXRPortal/AppLogSDK/` holds `applogrs.dll` / `applogrs.dll.lib`, a
prebuilt PICO telemetry client used by the editor portal module. Redistributed exactly as received
from PICO Unreal SDK 6.1.4, unmodified.

### Unreal Engine

Unreal Engine is not distributed here. The plugin links against it at build time and is subject to
the [Unreal Engine EULA](https://www.unrealengine.com/eula). The pure-Python verification scripts
in `tools/` call the engine's own Python API, so they inherit the same terms.

## 4. What this repository adds

The UE 5.9 port itself — the 14 modified files and the added `Config/DefaultPICOOpenXR.ini` — plus
the documentation in `docs/` and the verification harness in `tools/`. Treat those the same as the
rest of the plugin (see section 2).