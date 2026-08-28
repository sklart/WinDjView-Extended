// Exercises the same file-open path used by WinDjView, not just libdjvu.
#include "../../src/stdafx.h"
#include "../../src/DjVuSource.h"

#include <stdio.h>

int _tmain(int argc, TCHAR** argv)
{
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

	DjVuSource* source = DjVuSource::FromFile(path);
	if (source == NULL)
	{
		fputs("DjVuSource could not open the long-path fixture\n", stderr);
		return 1;
	}

	bool decoded = false;
	if (source->GetPageCount() > 0)
	{
		GP<DjVuImage> page = source->GetPage(0);
		decoded = !!page && page->get_width() > 0 && page->get_height() > 0;
	}
	source->Release();

	if (!decoded)
	{
		fputs("DjVuSource long-path regression failed\n", stderr);
		return 1;
	}
	puts("DjVuSource long-path regression passed");
	return 0;
}
