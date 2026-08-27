#include "stdafx.h"
#include "PathUtil.h"

namespace
{
	bool GetModulePath(HMODULE module, CString& path)
	{
		DWORD size = 512;
		for (;;)
		{
			LPTSTR buffer = path.GetBuffer(size);
			DWORD length = ::GetModuleFileName(module, buffer, size);
			path.ReleaseBuffer();
			if (length == 0)
				return false;
			if (length < size - 1)
				return true;
			size *= 2;
		}
	}
}

bool PathUtil::GetExecutablePath(CString& path)
{
	return GetModulePath(NULL, path);
}

bool PathUtil::GetExecutableDirectory(CString& path)
{
	if (!GetExecutablePath(path))
		return false;
	int slash = path.ReverseFind(_T('\\'));
	if (slash < 0)
		return false;
	// Keep a drive root as "C:\\", not the drive-relative spelling "C:".
	path = slash == 2 && path.GetLength() > 2 && path[1] == _T(':')
		? path.Left(3) : path.Left(slash);
	return true;
}

bool PathUtil::GetFullPath(const CString& input, CString& output)
{
	if (input.IsEmpty())
		return false;
	// Callers may intentionally normalize a CString in place.
	CString source(input);

	DWORD size = 512;
	for (;;)
	{
		LPTSTR buffer = output.GetBuffer(size);
		DWORD length = ::GetFullPathName(source, size, buffer, NULL);
		output.ReleaseBuffer();
		if (length == 0)
			return false;
		if (length < size)
			return true;
		size = length + 1;
	}
}

bool PathUtil::FileExists(const CString& path)
{
	DWORD attributes = ::GetFileAttributes(path);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool PathUtil::DirectoryExists(const CString& path)
{
	DWORD attributes = ::GetFileAttributes(path);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

CString PathUtil::Combine(const CString& left, const CString& right)
{
	if (left.IsEmpty())
		return right;
	if (right.IsEmpty())
		return left;
	if (left.Right(1) == _T("\\") || left.Right(1) == _T("/"))
		return left + right;
	return left + _T("\\") + right;
}

CString PathUtil::ReplaceExtension(const CString& path, const CString& extension)
{
	int slash = max(path.ReverseFind(_T('\\')), path.ReverseFind(_T('/')));
	int dot = path.ReverseFind(_T('.'));
	CString base = dot > slash ? path.Left(dot) : path;
	if (extension.IsEmpty())
		return base;
	return base + (extension[0] == _T('.') ? extension : _T(".") + extension);
}

bool PathUtil::ResolveShortcut(HWND owner, const CString& input, CString& output)
{
	output = input;
	if (input.GetLength() < 4 || input.Right(4).CompareNoCase(_T(".lnk")) != 0)
		return true;

	HRESULT initialized = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	IShellLink* shellLink = NULL;
	HRESULT result = ::CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
		IID_IShellLink, reinterpret_cast<void**>(&shellLink));
	if (SUCCEEDED(result))
	{
		IPersistFile* persistFile = NULL;
		result = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persistFile));
		if (SUCCEEDED(result))
		{
			result = persistFile->Load(input, STGM_READ);
			persistFile->Release();
		}
		if (SUCCEEDED(result))
			result = shellLink->Resolve(owner, SLR_NO_UI);

		// IShellLink is a legacy API and exposes a MAX_PATH output buffer.  Keep
		// it isolated here; paths which cannot be represented remain the .lnk.
		TCHAR target[MAX_PATH] = { 0 };
		if (SUCCEEDED(result) && SUCCEEDED(shellLink->GetPath(target, _countof(target), NULL, SLGP_RAWPATH)) && target[0] != 0)
			output = target;
		shellLink->Release();
	}
	if (SUCCEEDED(initialized))
		::CoUninitialize();
	return true;
}
