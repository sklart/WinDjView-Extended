# Testing

Verified locally:

- Debug and Release Win32 NMAKE builds;
- Debug and Release x64 NMAKE builds, including a clean rebuild of the bundled x64 JPEG
  static library.

`tools\\tests\\run-jpegdecoder-regression.cmd` accepts a configuration and
platform, for example `Release x64`, and compiles against the corresponding
static libraries. It is run for Debug Win32, Debug x64, Release Win32 SIMD,
and Release x64 SIMD. The test creates JPEG streams through the classic
libjpeg API and verifies the direct `JPEGDecoder` output: baseline and
progressive RGB 4:4:4 and 4:2:0 color, grayscale channel expansion, BGR order,
bottom-to-top row mapping, 1×1 and 2×2 images, controlled rejection of a
50000×50000 `GPixmap` before allocation, and controlled rejection of truncated,
empty, and malformed streams.

`tools\\tests\\run-libdjvu-core-regression.cmd` accepts the same arguments
and runs for the same four configurations. It covers `GBitmap` size limits and
invalid donation inputs, `GPBuffer` multiplication overflow, overflowing
PNM/PPM integer fields, and `GException::cmp_cause()` prefix plus legacy
null/empty behavior. It also verifies exact/saturated DjVu cache accounting,
eviction after saturated deletion (including the `>20` item path and unlimited
to finite cache transition), normal image memory accounting, and PNM/PPM
dimension limits before narrowing to `int`.

GitHub Actions repeats these four builds, runs the matching JPEG and libdjvu
core regressions,
prints the imported DLLs for Release, rejects every external DLL whose name
contains `jpeg`, and publishes the resulting executables as workflow artifacts.
No automated corpus of DjVu samples is currently checked in, so opening real
DjVu documents, printing, and GUI flows remain manual regression tests.
PVS-Studio monitoring is run separately with
`tools\\pvs\\run-monitoring.ps1`; Release SIMD needs NASM on `PATH` or the
script's `-NasmDirectory` option.

`tools\\tests\\jpegdecoder_benchmark.cpp` is a local performance harness for
the current direct decoder. Build the same Release configuration twice (first
without `SIMD=1`, then with it) before comparing results; it does not restore
or benchmark the historical PPM intermediary path. It generates fixtures before
timing, performs one excluded warm-up decode, reports average and median decode
time, uses median throughput as the primary result, and prints the encoded size
and CRC32 so SIMD OFF/ON runs can be matched.
