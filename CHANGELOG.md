# Changelog

## Unreleased

### Fixed

- Corrected bookmark ownership, tree-node deletion, empty contents handling, annotation deletion,
  settings reload, document path validation, and several x64 pointer/index
  conversions.
- Restored the wait cursor after selection operations and fixed printing-unit
  comparison logic.

### Security

- Hardened XML and settings parsing, DjVu chunk/JPEG validation, page and
  rectangle bounds, and external link handling.

### Changed

- Updated the bundled DjVuLibre to 3.5.30 with required WinDjView extensions.
- Updated the static JPEG implementation to libjpeg-turbo 3.2.0 and decode
  JPEG scanlines directly into `GPixmap`.
- Expanded JPEGDecoder regression coverage and run it for every Debug/Release
  Win32/x64 CI configuration.
- Hardened `GPixmap` and JPEG output dimension arithmetic before allocation.
- Hardened `GBitmap` and `GPBuffer` size arithmetic, image memory accounting,
  PNM/PPM numeric parsing, exception-cause comparison, and JPEG input skipping.
- Added libdjvu core regressions for arithmetic, parser, and exception behavior.
- Recorded a post-fix PVS-Studio scan and hardened malformed page-description
  handling.

### Build

- Added reproducible Debug/Release x86/x64 NMAKE support, including a source build of
  libjpeg-turbo, and CI for all four build configurations.
- Release CI now requires NASM SIMD for both architectures; Debug retains the
  portable non-SIMD JPEG build.
- Release CI verifies that WinDjView has no external JPEG DLL dependency.
- Visual Studio Release Makefile commands now use the same SIMD policy as CI;
  a non-SIMD NMAKE build remains available by omitting `SIMD=1`.
- Added a local current-path JPEGDecoder benchmark harness and documented its
  SIMD ON/OFF results separately from the historical 3.1.4.1 benchmark.
- Improved the benchmark with excluded warm-ups, median-first reporting, and
  fixture size/CRC32 identity output.
