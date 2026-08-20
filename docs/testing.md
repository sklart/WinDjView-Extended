# Testing

Verified locally:

- Debug and Release Win32 NMAKE builds;
- Debug and Release x64 NMAKE builds, including a clean rebuild of the bundled x64 JPEG
  static library.

`tools\\tests\\run-jpegdecoder-regression.cmd` accepts a configuration and
platform, for example `Release x64`, and compiles against the corresponding
static libraries. It is run for Debug Win32, Debug x64, Release Win32 SIMD,
and Release x64 SIMD. The test creates JPEG streams through the classic
libjpeg API and verifies the direct `JPEGDecoder` output: baseline color,
progressive color, grayscale channel expansion, BGR order, bottom-to-top row
mapping, 1×1 and 2×2 images, and controlled rejection of truncated, empty,
and malformed streams.

GitHub Actions repeats these four builds, runs the matching JPEG regression,
and publishes the resulting executables as workflow artifacts. No automated
corpus of DjVu samples is currently checked in, so opening real DjVu documents,
printing, and GUI flows remain manual regression tests. PVS-Studio monitoring
is run separately with `tools\\pvs\\run-monitoring.ps1`; Release SIMD needs
NASM on `PATH` or the script's `-NasmDirectory` option.

`tools\\tests\\jpegdecoder_benchmark.cpp` is a local performance harness for
the current direct decoder. Build the same Release configuration twice (first
without `SIMD=1`, then with it) before comparing results; it does not restore
or benchmark the historical PPM intermediary path.
