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

## Remaining warnings

The reports still contain legacy, style, performance, deprecation, and
analyzer-limited-control-flow diagnostics. They were deliberately not
mass-fixed.
