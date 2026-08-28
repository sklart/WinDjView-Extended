// Exercises the same file-open path used by WinDjView, not just libdjvu.
#include "../../src/stdafx.h"
#include <stdio.h>

extern "C" int RunDjVuSourceLongPathBridge(LPCTSTR path);

int _tmain(int argc, TCHAR** argv)
{
	if (!AfxWinInit(::GetModuleHandle(NULL), NULL, ::GetCommandLine(), 0))
	{
		fputs("could not initialize MFC for DjVuSource regression\n", stderr);
		return 1;
	}

	if (argc != 2)
	{
		fputs("usage: djvusource_long_path_regression <path-to-djvu>\n", stderr);
		return 2;
	}

	CString path(argv[1]);
	if (path.Left(4) == _T("\\\\?\\"))
	{
		fputs("the regression must receive a normal Windows path\n", stderr);
		return 2;
	}

	fputs("DjVuSource regression: FromFile\n", stderr);
	if (RunDjVuSourceLongPathBridge(path) != 0)
	{
		fputs("DjVuSource long-path regression failed\n", stderr);
		return 1;
	}
	puts("DjVuSource long-path regression passed");
	return 0;
}
