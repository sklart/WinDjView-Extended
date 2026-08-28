#include "../../src/stdafx.h"
#include "../../src/PathUtil.h"

#include <stdio.h>

namespace
{
	bool Check(bool value, const char* message)
	{
		if (!value)
			fprintf(stderr, "pathutil regression failed: %s\n", message);
		return value;
	}

	bool MakeDirectoryTree(const CString& path)
	{
		int start = path.Left(4) == _T("\\\\?\\") ? 7 : 3;
		CString current = path.Left(start);
		while (start < path.GetLength())
		{
			int slash = path.Find(_T('\\'), start);
			CString segment = path.Mid(start, slash < 0 ? path.GetLength() - start : slash - start);
			current = PathUtil::Combine(current, segment);
			if (!PathUtil::DirectoryExists(current))
			{
				if (!::CreateDirectory(current, NULL) && ::GetLastError() != ERROR_ALREADY_EXISTS)
					return false;
			}
			if (slash < 0)
				break;
			start = slash + 1;
		}
		return PathUtil::DirectoryExists(path);
	}
}

int _tmain()
{
	TCHAR temp[MAX_PATH] = { 0 };
	if (!Check(::GetTempPath(_countof(temp), temp) != 0, "GetTempPath"))
		return 1;

	CString root = CString(_T("\\\\?\\")) + temp;
	if (root.Right(1) == _T("\\"))
		root = root.Left(root.GetLength() - 1);
	root += _T("\\WinDjView PathUtil \x0416");
	for (int i = 0; i < 12; ++i)
		root = PathUtil::Combine(root, _T("long directory segment 0123456789"));

	if (!Check(root.GetLength() > MAX_PATH, "long test path"))
		return 1;
	if (!Check(MakeDirectoryTree(root), "create long Unicode directory tree"))
		return 1;

	CString filePath = PathUtil::Combine(root, _T("sample file.txt"));
	HANDLE file = ::CreateFile(filePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (!Check(file != INVALID_HANDLE_VALUE, "create file"))
		return 1;
	::CloseHandle(file);

	CString fullPath;
	if (!Check(PathUtil::GetFullPath(filePath, fullPath), "GetFullPath")) return 1;
	if (!Check(PathUtil::FileExists(fullPath), "FileExists")) return 1;
	if (!Check(PathUtil::DirectoryExists(root), "DirectoryExists")) return 1;
	if (!Check(PathUtil::Combine(root, _T("sample file.txt")) == filePath, "Combine")) return 1;
	if (!Check(PathUtil::ReplaceExtension(filePath, _T("djvu")).Right(5) == _T(".djvu"), "ReplaceExtension")) return 1;

	CString relative = PathUtil::Combine(root, _T(".\\sample file.txt"));
	if (!Check(PathUtil::GetFullPath(relative, relative) && PathUtil::FileExists(relative), "relative dot path")) return 1;

	::DeleteFile(filePath);
	puts("pathutil regression passed");
	return 0;
}
