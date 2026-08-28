# Long-path support (4.3 foundation)

The document-manager path and every `CMyFileDialog` Open/Save call now use
Unicode `CString` values rather than a `_MAX_PATH` result buffer.  On Windows
Vista and later the dialog is implemented with `IFileOpenDialog` or
`IFileSaveDialog`; Windows 7, 10, and 11 therefore use the Shell dialog.
The old `OPENFILENAME` implementation remains as a defensive fallback when
creating the Shell dialog fails.  It has a 32,768-character dynamically
allocated buffer, but is not the normal supported path on these systems.

`PathUtil::GetFullPath` and `PathUtil::GetExecutablePath` grow their buffers
until the respective Windows API reports success. `FindOpenDocument()` and
`CMyDocManager::OpenDocumentFile()` normalize through this helper before
asking MFC to match an already-open document. `DjVuSource::FromFile()` keeps
the normalized `CString` rather than a fixed buffer.

The document loader preserves the normal user-visible `C:\...` path in the
document, MRU, and session layers. At the filesystem boundary, libdjvu turns
an absolute path of at least `MAX_PATH` characters into the Win32
extended-length form for `_wfopen` (`\\?\C:\...` or `\\?\UNC\server\share\...`).
Already-prefixed paths are left unchanged; relative paths and URLs are not
prefixed. This is independent of the Windows 10/11 `longPathAware` manifest
mechanism and therefore keeps the Windows 7 compatibility model.

`tools/tests/run-long-path-djvu-regression.cmd` copies a real DjVu fixture to
a temporary Unicode path longer than `MAX_PATH`, passes the ordinary
non-prefixed path to libdjvu, and verifies document initialization plus page
zero dimensions. It runs in every legacy and native CI configuration.

## Deliberate limits

This is not a claim that every historical file operation supports arbitrary
extended-length paths. The following areas remain legacy work for later 4.3
increments: dictionary discovery, temporary-file creation, several export
filename transformations, registry enumeration, and recycle-bin deletion.
They retain `MAX_PATH` buffers because they are not on the primary document
open/dialog/MRU/session path and changing them without end-to-end tests would
be a larger compatibility risk.

Shell Link's `IShellLink::GetPath` API itself has a `MAX_PATH` output contract.
Shortcut resolution is isolated in `PathUtil`; when a shortcut target cannot
be represented, the original `.lnk` path is retained rather than truncating
it. No `\\?\` prefix is added automatically: APIs receive their normal
Windows path form and callers retain their existing relative-path semantics.

## Manual smoke checklist

- Open a DjVu document from an ordinary, Unicode, and >260-character path.
- Save/export to a >260-character path; verify filter, default extension,
  cancellation, and overwrite confirmation.
- Reopen the document from MRU and after session restoration.
- Check the same document is not opened twice through an absolute versus
  relative spelling, casing difference, or supported `.lnk`.
- Repeat Open and Save on Win32 and x64 on Windows 7, 10, and 11 where
  available.
