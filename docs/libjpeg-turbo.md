# libjpeg-turbo integration

WinDjView Extended vendors [libjpeg-turbo 3.2.0](https://github.com/libjpeg-turbo/libjpeg-turbo/releases/tag/3.2.0)
in `src/third_party/libjpeg-turbo`.  The vendored source is the upstream
release commit `c85e6b905bf237038faa936dab160ebfc5da0344`.

The application uses the classic libjpeg decoder API, not the TurboJPEG API.
The build explicitly sets `WITH_JPEG7=OFF` and `WITH_JPEG8=OFF`, therefore
libjpeg-turbo exposes the libjpeg 6.2 API/ABI required by the existing
DjVuLibre decoder.  `JPEGDecoder.cpp` includes the generated `jconfig.h` and
public headers from this one source tree; it no longer has a second copy of the
IJG 6b headers in `src/libdjvu`.

## Normal NMAKE build

The project NMAKE interface is unchanged.  Run the ordinary commands from a
Visual Studio Developer Command Prompt; the adapter in
`src/third_party/libjpeg-turbo/makefile.wdj` invokes the CMake distributed with
Visual Studio only to generate and build the private static JPEG library.
CMake is not a replacement for the project build system.

```bat
cd src\libdjvu
nmake /nologo /f makefile
cd ..
nmake /nologo /f makefile
```

Pass `X64=1` for x64 and `DEBUG=1` for Debug.  The four generated libraries
are `jpeg.lib`, `jpeg64.lib`, `jpegd.lib`, and `jpegd64.lib`; all are temporary
build artifacts and are linked statically into `WinDjView.exe`.

The adapter sets:

```text
ENABLE_SHARED=OFF  ENABLE_STATIC=ON
WITH_JPEG7=OFF     WITH_JPEG8=OFF
WITH_TURBOJPEG=OFF WITH_TOOLS=OFF
```

Release uses `/MT`; Debug uses `/MTd`.  No `jpeg*.dll` is produced or required.
The target minimum remains Windows 7; testing on a real Windows 7 x86/x64
installation is still required before a release.

## SIMD

The default is a portable non-SIMD build, so NASM is not a normal build
prerequisite.  To require SIMD, install NASM, make it available on `PATH`, and
pass `SIMD=1` to the JPEG adapter (or propagate it to the project NMAKE
command).  In that mode the configuration requires SIMD and CMake reports a
clear failure if NASM is unavailable.

GitHub Actions builds both Release architectures with `SIMD=1`.  It installs
NASM 2.16.03 through the commit-pinned `ilammy/setup-nasm` action; Debug builds
remain non-SIMD and therefore do not require NASM.

## Updating

Replace the vendored source with an upstream release, update the integration
metadata in this document and `makefile.wdj`, then run all four NMAKE
configurations. Keep the upstream `LICENSE.md`,
`README.ijg`, and source copyright notices intact.  Binary distributions must
state: “This software is based in part on the work of the Independent JPEG
Group.”

The library source and license notices are included under
`src/third_party/libjpeg-turbo`; its libjpeg API implementation is covered by
the IJG license, with additional upstream BSD-style notices documented in
`LICENSE.md`.
## Local benchmark

A Win32 Release smoke benchmark was run on 2026-08-01 using the same classic
libjpeg API loop and the legacy `testimg.jpg` fixture (5,000 decodes). The
legacy IJG JPEG 6b library took 1,391 ms; libjpeg-turbo 3.1.4.1 built with NASM
SIMD took 687 ms (about 2.0x faster in this local run). This historical result
predates both the libjpeg-turbo 3.2.0 update and the direct
JPEGDecoder-to-GPixmap path. It is directional only: it is not a release-
performance guarantee and does not represent all JPEG sizes, formats, CPUs, or
Windows versions. A current benchmark must be measured separately.

## Current direct-decoder benchmark (2026-08-20)

The current direct `JPEGDecoder -> GPixmap` path was measured on an Intel Core
i5-13400F under Windows 10 Pro (version 2009, build 26200, 64-bit OS), using a
Win32 Release `/MT` build of libjpeg-turbo 3.2.0. The harness generates the
quality-85 fixtures before measurement; JPEG encoding is excluded. For every
fixture it performs one excluded warm-up decode, then reports both average and
median decode time. Median is the primary result and is used for MP/s. The
same encoded input is proven by the matching byte size and CRC32 in both runs.

| Fixture | JPEG bytes / CRC32 | Warm-up / runs | SIMD OFF avg / median | SIMD ON avg / median | SIMD ON median MP/s | Median speedup |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| baseline RGB, 1200×1600 | 994124 / `CD4AE52A` | 1 / 25 | 18.489 / 18.400 ms | 12.120 / 11.905 ms | 161.28 | 1.55× |
| progressive RGB, 2500×3500 | 4017850 / `7FA0797E` | 1 / 15 | 124.904 / 123.228 ms | 95.369 / 95.061 ms | 92.05 | 1.30× |
| grayscale, 4000×6000 | 9794674 / `9310BEAF` | 1 / 7 | 204.069 / 204.553 ms | 92.035 / 91.225 ms | 263.09 | 2.24× |

This isolates the SIMD choice on the current 3.2.0 direct path. It does not
isolate the direct-GPixmap effect from the historical RGB-to-PPM path, because
that path is intentionally not restored to production code. Results are local
measurements, not a cross-machine performance promise or a peak-memory claim.

## Corrupt-input smoke check

On 2026-08-01, the temporary Win32 SIMD djpeg-static build rejected both an
empty input (`Empty input file`) and a 128-byte truncation of `testorig.jpg`
(`Premature end of JPEG file` followed by `JPEG datastream contains no image`)
with exit code 1. This confirms controlled error reporting by libjpeg-turbo;
the WinDjView JPEGDecoder adds its own setjmp error translation around the
same libjpeg API.


## Windows 7 release validation

Run the Win32 binary on Windows 7 x86 and the x64 binary on Windows 7 x64.
Open baseline, progressive, grayscale, ICC/EXIF JPEG-backed DjVu documents and
repeat the corrupt-input cases. Confirm that the application starts and that
dumpbin /imports WinDjView.exe contains no JPEG DLL import. Record the OS
edition, CPU, NASM/SIMD build choice, input corpus, and observed result before
publishing a release.
