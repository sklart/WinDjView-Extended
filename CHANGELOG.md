# Changelog

## Unreleased

### Fixed

- Corrected bookmark ownership, empty contents handling, annotation deletion,
  settings reload, document path validation, and several x64 pointer/index
  conversions.
- Restored the wait cursor after selection operations and fixed printing-unit
  comparison logic.

### Security

- Hardened XML and settings parsing, DjVu chunk/JPEG validation, page and
  rectangle bounds, and external link handling.

### Changed

- Updated the bundled DjVuLibre to 3.5.30 with required WinDjView extensions.

### Build

- Added reproducible Release x64 NMAKE support, including a source build of
  IJG JPEG 6b, and CI for Release Win32/x64.