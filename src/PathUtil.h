// Small Unicode-only path helpers for application-owned paths.
#pragma once

namespace PathUtil
{
	bool GetExecutablePath(CString& path);
	bool GetExecutableDirectory(CString& path);
	bool GetFullPath(const CString& input, CString& output);
	bool FileExists(const CString& path);
	bool DirectoryExists(const CString& path);
	CString Combine(const CString& left, const CString& right);
	CString ReplaceExtension(const CString& path, const CString& extension);

	// Resolves .lnk files when Shell Link can provide a target.  A non-link is
	// returned unchanged.  Failure to resolve a link is deliberately non-fatal.
	bool ResolveShortcut(HWND owner, const CString& input, CString& output);
}
