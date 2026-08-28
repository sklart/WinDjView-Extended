// Small Unicode-only path helpers for application-owned paths.
#pragma once

namespace PathUtil
{
	bool GetExecutablePath(CString& path);
	bool GetExecutableDirectory(CString& path);
	bool GetFullPath(const CString& input, CString& output);
	// Produces a path suitable only for a Win32 filesystem API.  The visible
	// canonical path must remain unprefixed for document identity and settings.
	bool GetExtendedFileSystemPath(const CString& input, CString& output);
	HANDLE OpenFileReadOnly(const CString& path);
	bool FileExists(const CString& path);
	bool DirectoryExists(const CString& path);
	CString Combine(const CString& left, const CString& right);
	CString ReplaceExtension(const CString& path, const CString& extension);

	// Resolves .lnk files when Shell Link can provide a target.  A non-link is
	// returned unchanged.  Failure to resolve a link is deliberately non-fatal.
	bool ResolveShortcut(HWND owner, const CString& input, CString& output);
}
