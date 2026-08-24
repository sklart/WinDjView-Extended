# PVS-Studio audit

## Analyzed configurations

- Release Win32 (`Windjview_x32.plog`)
- Release x64 (`Windjview_x64.plog`)

The originally supplied reports were compared. The previous x64 report also
contained 397 diagnostics from `D:\Download\FBeditor`; those records were not
part of WinDjView and are excluded below.

## Running PVS-Studio from Visual Studio builds

`WinDjView.Modern` is intentionally a Makefile/NMake project. The Visual Studio
PVS-Studio command cannot analyze Makefile projects and reports V010. Do not
change the project type just to satisfy the plugin.

Use compiler monitoring instead. From a Developer PowerShell, run:

```powershell
.\tools\pvs\run-monitoring.ps1 -Configuration Release -Platform Win32
```

The script monitors the NMake rebuild and writes its report under `out\pvs`.
Open the generated `.plog` with **PVS-Studio → Open/Save → Open Analysis
Report** in Visual Studio. Repeat for `-Platform x64`.

## Confirmed bugs

| Diagnostic | File | Problem | Resolution |
| --- | --- | --- | --- |
| V595 | `DjVuView.cpp` | A text-zone parent was dereferenced before its null check. | Check the parent before deriving its last child. |
| V521 | `DjVuView.cpp` | The comma operator discarded the page-count bound for a custom crop range. | Use `&&`; indices cannot reach `m_nPageCount`. |
| V614 | `WinDjView.cpp` | The active document/frame could be uninitialized when no enabled frame exists. | Initialize both pointers and activate only a valid saved document. |
| V1051 | `DjVuFileCache.cpp` | A disabled unlimited cache could still add files. | `add_file()` and `del_file()` now return while disabled, as documented. |

## Hardening changes

| Diagnostic | File | Problem | Resolution |
| --- | --- | --- | --- |
| V576 | `DjVuView.cpp` | Description text from DjVu used unbounded `%s` conversions and unchecked results. | Use `%999s`, initialize parse storage, check every result, and retain the original line on an incomplete parse. |
| V730 | `PrintDlg.cpp` | `m_pDevMode` had no initial value. | Initialize it to `NULL`; control paths that require it also verify it. |
| V547 | `Drawing.cpp` | A negative transparency value converted to an unsigned integer before clamping. | Validate finiteness and clamp the `double` to `[0, 1]` before conversion; non-finite values become fully transparent. |
| V575 | `Drawing.cpp` | Bitmap-info allocations could be dereferenced after an allocation failure. | Throw the existing MFC memory exception before use. |

The existing `>> 8` transparency blend divisor is retained: it is an established integer approximation and this audit found no evidence that changing it would be correct.

## False positives

- `WinDjView.cpp` V1004 around `CInternetFile`: `AfxThrowInternetException()` throws, so the following read is unreachable after a null result.
- `MyScrollView.cpp` V614: the scroll-mode invariant covers the vertical and horizontal result assignments; no change was made solely to silence the analyzer.
- `Drawing.cpp` V506 for GDI+ encoder parameters: the save call completes while the pointed-to local values are alive.

## Upstream / third-party findings

- `src/third_party/libjpeg-turbo/` findings are third-party diagnostics and were not locally refactored.
- `src/libdjvu/` was reviewed separately because this repository carries DjVuLibre code; the cache defect above was fixed locally.

## Tool noise

- V1042 license notices are informational and intentionally retained.
- Diagnostics for CMake `TryCompile`/`CMakeScratch` paths are build-artifact noise.

## Post-fix scan (2026-08-20)

Both configurations were run again using `run-monitoring.ps1`, Release SIMD,
and NASM 2.16.03. Counts include only paths under the WinDjView repository.

| Configuration | Before | After | Application | libdjvu | third_party |
| --- | ---: | ---: | ---: | ---: | ---: |
| Release Win32 | 826 | 818 | 405 | 411 | 2 |
| Release x64 | 980 | 819 | 405 | 412 | 2 |

The post-fix level distribution is Win32: L1 207, L2 235, L3 376; x64: L1
207, L2 221, L3 391. The counts are not a severity claim: PVS level also
includes informational and legacy-pattern diagnostics.

The fixed `WinDjView.cpp` V576, `Global.cpp` V576, `UnicodeByteStream.cpp`
V593, `Scaling.cpp` V769, and `MyScrollView.cpp` V614 diagnostics are absent
from the post-fix reports. The remaining V614 reports in `ByteStream.cpp` and
`GPixmap.cpp` concern reference-initialized `GPBuffer` paths, and the large
remaining V547/V730/V1051 groups are legacy DjVuLibre patterns; neither class
was mass-edited merely to lower the count.

## JPEG arithmetic hardening re-scan (2026-08-20)

Release Win32 and Release x64 were re-run through `run-monitoring.ps1` with
NASM SIMD after the `GPixmap` allocation and JPEG source-dimension guards.

| Configuration | Total | New L1 | New warnings in `GPixmap.cpp` | New warnings in `JPEGDecoder.cpp` |
| --- | ---: | ---: | ---: | ---: |
| Release Win32 | 818 | 0 | 0 | 0 |
| Release x64 | 819 | 0 | 0 | 0 |

The existing `GPixmap.cpp` V614/V730/V1042 and `JPEGDecoder.cpp` V522/V1042
records are unrelated legacy or informational diagnostics; no new arithmetic,
allocation, signed/unsigned, buffer-size, or null-dereference warning was
introduced by this hardening.

## Core arithmetic hardening re-scan (2026-08-24)

Release Win32 and Release x64 were run again through `run-monitoring.ps1`
with Release SIMD and NASM. The reports contain 821 and 973 diagnostics,
respectively. The new size checks were reviewed specifically: no confirmed new
L1 arithmetic, conversion, allocation, buffer-size, pointer-arithmetic, or
null-dereference defect was found in `GBitmap.cpp`, `GPixmap.cpp`,
`GSmartPointer.cpp`, `GException.cpp`, `DjVuFile.cpp`, `DjVuFileCache.cpp`, or
`JPEGDecoder.cpp`.

The scan did flag an intermediate `GBitmap` saturation expression as a
provably false unsigned-overflow pattern. It was simplified while retaining the
same `UINT_MAX` saturation behavior, then rebuilt and covered by the core
regression. No unrelated legacy warning cleanup was performed.

### Why the documented x64 count changed from 819 to 973

The saved 819-report baseline is no longer available as a `.plog`, so a
diagnostic-code and severity-level diff is **NOT VERIFIED**. Its documented
repository breakdown was application 405, libdjvu 412, and third-party 2.
The current 973-report contains application 405, libdjvu 419, third-party 2,
and 147 diagnostics under the unrelated external path
`D:\Download\FBeditor`.

| Category | Added | Removed |
| --- | ---: | ---: |
| Application | 0 | 0 |
| libdjvu | 7 | 0 |
| third_party | 0 | 0 |
| build/CMake scratch | 0 observed | 0 observed |
| external `D:\Download\FBeditor` | 147 | 0 |
| V1042/license and per-level breakdown | NOT VERIFIED | NOT VERIFIED |

Thus the whole +154 consists of 147 out-of-scope external diagnostics and
seven additional libdjvu diagnostics. Without the original `.plog`, those
seven cannot be attributed by code or severity with evidence; no such cause is
claimed here. The current report has 107 V1042 entries, but that is not a
baseline delta.

A clean final Release x64 re-scan contains 825 diagnostics: application 405,
libdjvu 418, and third-party 2; it contains no external FBeditor or CMake
scratch paths. It confirms that the 973 total was contaminated by monitoring
scope, not a 154-warning change in WinDjView sources. Compared with the
documented repository baseline, its remaining +6 libdjvu records still lack a
code/level diff and remain **NOT VERIFIED**.

## Remaining warnings

The reports still contain legacy, style, performance, deprecation, and
analyzer-limited-control-flow diagnostics. They were deliberately not
mass-fixed.
