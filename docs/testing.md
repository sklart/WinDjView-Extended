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

`tools\\tests\\run-pathutil-regression.cmd` also accepts configuration and
platform. It creates a temporary Unicode directory tree longer than `MAX_PATH`
and verifies `GetFullPath`, `FileExists`, `DirectoryExists`, `Combine`, and
`ReplaceExtension` against that path.

GitHub Actions repeats these four legacy NMAKE builds, runs the matching JPEG,
libdjvu core, PathUtil, and long-path DjVu regressions, then builds and verifies
the Russian resource DLL. Each Release job also runs
`tools\tests\run-startup-smoke.ps1`, which invokes `WinDjView.exe
/StartupSmokeTest`; that mode completes real application and main-frame
initialization, then closes normally. A separate staged native MSBuild matrix builds
`WinDjView.Native` and `libdjvu.Modern` in Debug/Release for Win32/x64, verifies
their PE architecture, rejects external JPEG/DjVu imports in Release, and runs
the long-path DjVu regression against the native library output. The startup
smoke is run for both native Release architectures as well.

The Release x64 startup blocker was an x86 Common Controls dependency embedded
by the native resource compile: `WIN64` reached C++ compilation but not the
resource compiler, so `WinDjView.rc2` selected the x86 manifest. The native
project now supplies that define to `ResourceCompile`; cdb confirms that x64
loads the `amd64_microsoft.windows.common-controls` assembly and the smoke
process exits with status zero.

## Long-path DjVu regression

`tools\tests\fixtures\minimal.djvu` is a two-page GPL DjVuLibre fixture. The
runner copies it to a temporary Unicode directory deeper than `MAX_PATH`, uses
the ordinary non-prefixed path, initializes a real libdjvu document, and checks
that page zero decodes with non-zero dimensions. A minimal DjVu fixture is
checked in for this regression; a broader representative corpus is not yet
included.

`djvusource_long_path_regression` uses a minimal `IApplication` implementation
because `DjVuSource` normally receives this application service during real
WinDjView startup. The test passes a normal Unicode path longer than 260
characters to `DjVuSource::FromFile()`, then verifies page zero through the
same production backend. The bridge keeps the MFC-facing test ABI separate
from the static DjVuLibre interface.
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

## Long-path manual regression

See [long-path-support.md](long-path-support.md) for the 4.3 path-layer scope,
known legacy limits, and the required Win32/x64 manual smoke checklist. The
checklist deliberately includes MRU and session restoration, since a dialog
result alone does not prove that the application preserves a long document
path end-to-end.
