// End-to-end libdjvu regression for a supplied DjVu file on a long Unicode path.
#include "DjVuDocument.h"
#include "DjVuImage.h"
#include "GException.h"
#include "GSmartPointer.h"
#include "GURL.h"

#include <stdio.h>
#include <vector>
#include <windows.h>

#ifdef HAVE_NAMESPACES
using namespace DJVU;
#endif

int wmain(int argc, wchar_t **argv)
{
  if (argc != 2)
  {
    fputs("usage: long_path_djvu_regression <path-to-djvu>\n", stderr);
    return 2;
  }

  const int bytes = WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, 0, 0, 0, 0);
  if (bytes <= 1)
  {
    fputs("could not convert the supplied path to UTF-8\n", stderr);
    return 1;
  }

  std::vector<char> buffer(bytes);
  if (!WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, &buffer[0], bytes, 0, 0))
  {
    fputs("could not convert the supplied path to UTF-8\n", stderr);
    return 1;
  }
  const GUTF8String path(&buffer[0]);

  bool decoded = false;
  try
  {
    const GURL url = GURL::Filename::UTF8(path);
    if (!url.is_file())
    {
      fputs("long-path DjVu regression could not stat the fixture\n", stderr);
      return 1;
    }
    GP<DjVuDocument> document = DjVuDocument::create_wait(url);
    const int pages = document->get_pages_num();
    if (pages > 0)
    {
      GP<DjVuImage> page = document->get_page(0, true);
      decoded = !!page && page->get_width() > 0 && page->get_height() > 0;
    }
  }
  catch (const GException &exception)
  {
    fprintf(stderr, "long-path DjVu regression exception: %s\n", exception.get_cause());
    decoded = false;
  }
  catch (...)
  {
    fputs("long-path DjVu regression received an unknown exception\n", stderr);
    decoded = false;
  }

  if (!decoded)
  {
    fputs("long-path DjVu regression failed\n", stderr);
    return 1;
  }

  puts("long-path DjVu regression passed");
  return 0;
}
