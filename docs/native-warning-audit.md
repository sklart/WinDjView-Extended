# Native `/W4` warning audit

This Foundation pass classifies warnings that can indicate undefined runtime
behaviour. Cosmetic warnings (`C4100`, `C4189`, `C4456`-`C4458`, and similar)
remain outside its scope.

## C4700/C4701

| Location | Variables | Classification | Reason |
| --- | --- | --- | --- |
| `MyColorPicker.cpp` | `clrFrame`, `clr3dTopLeft`, `clr3dBottomRight` | False positive | All are read only under `bSelected`; every branch that sets it also initializes the colors. |
| `ThumbnailsView.cpp` | `nAnchorPage`, `ptAnchorOffset` | False positive | They are read only in the `updateType == TOP` branch that initializes both. |
| `DjVuView.cpp` | `nWidth`, `nLeft` | Legacy invariant | The function returns unless layout is `Continuous` or `ContinuousFacing`; those two enum cases initialize both values. |
| `DjVuView.cpp` | `nStartPage`, `nStartPos`, `nSelEnd`, `ky` | Guarded control flow | Search/navigation paths initialize them before each use or return on parse failure. |
| `WinDjView.cpp` | `nIndexEng` | False positive | It is read only when `bFoundEng` is true, which is assigned together with `nIndexEng`. |

No C4700/C4701 true positive was found in this targeted set.

## C4840

The native Debug Win32 `/W4` build was used as the source of truth. `Global.cpp`,
`DjVuSource.cpp`, and `WinDjView.cpp` C4840 call sites now pass explicit
`LPCTSTR` values to variadic `FormatString`, `CString::Format`, and `TRACE`.
The Foundation-targeted files `DjVuDoc.cpp`, `DjVuView.cpp`, `MainFrm.cpp`,
`MyEdit.cpp`, `PrintDlg.cpp`, the settings pages, and `UpdateDlg.cpp` emitted
no C4840 diagnostics. Production C4840 remaining: **0**.

`/WX` remains intentionally disabled; legacy warnings are retained and audited
separately rather than suppressed.
