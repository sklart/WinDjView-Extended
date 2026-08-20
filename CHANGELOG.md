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

### Build

- Added reproducible Debug/Release x86/x64 NMAKE support, including a source build of
  libjpeg-turbo, and CI for all four build configurations.
- Release CI now requires NASM SIMD for both architectures; Debug retains the
  portable non-SIMD JPEG build.
