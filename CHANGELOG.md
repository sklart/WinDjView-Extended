# Changelog

## 4.4.0 — unreleased

### Added / Changed / Fixed

- Added a user-visible **Check for Updates** command to the Help menu.
- The About dialog identifies 64-bit builds with an `(x64)` suffix.
- Separated retained bitmap-cache policy from the DjVu view implementation.
- Added a single-worker 512 px tile-conversion foundation for large raster targets,
  with full-page fallback and pixel-equivalence regression coverage.
- Scheduled large viewport tiles as individual single-worker jobs and publish
  their assembled bitmap only after the complete tile set finishes.
- Rendered supported large viewport tiles from independent DjVuLibre raster
  regions without a full-page staging bitmap.

## 4.3.0 — 2026-09-22

### Added

- Native MSBuild production builds for Win32 and x64, portable release packaging,
  startup smoke tests, real-world DjVu corpus coverage, and production cache regressions.
- Golden Render regression baselines and deterministic malformed-DjVu AddressSanitizer
  coverage for the Native x64 build.

### Performance

- Added bounded page-cache and bitmap reuse coverage, priority render scheduling,
  adjacent prefetch regression coverage, and informational performance benchmarks.

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

- Modernized the Open/Save dialog layer for supported Windows versions and
  removed historical fixed-size path buffers from document opening and
  already-open document detection.
- Added High-DPI and Per-Monitor DPI handling, plus the long-path foundation
  used by document opening and already-open document detection.
- Updated the bundled DjVuLibre to 3.5.30 with required WinDjView extensions.
- Updated the static JPEG implementation to libjpeg-turbo 3.2.0 and decode
  JPEG scanlines directly into `GPixmap`.
- Expanded JPEGDecoder regression coverage and run it for every Debug/Release
  Win32/x64 CI configuration.
- Hardened `GPixmap` and JPEG output dimension arithmetic before allocation.
- Hardened `GBitmap` and `GPBuffer` size arithmetic, image memory accounting,
  PNM/PPM numeric parsing, exception-cause comparison, and JPEG input skipping.
- Corrected saturated DjVu cache accounting during eviction and removal, and
  validate PNM/PPM dimensions before converting them to `int`.
- Added libdjvu core regressions for arithmetic, parser, and exception behavior.
- Recorded a post-fix PVS-Studio scan and hardened malformed page-description
  handling.

### Build

- Native MSBuild is the primary production build; legacy NMAKE remains a
  compatibility build.
- Added a native MSBuild project for the Russian resource DLL and publish its
  Release Win32/x64 artifacts from CI.
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
