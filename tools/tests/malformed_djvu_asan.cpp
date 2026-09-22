// Runs one DjVu fixture in one process.  The PowerShell driver supplies the
// timeout and checks sanitizer diagnostics, so malformed inputs cannot block
// the entire regression run.
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

namespace {

bool wide_path_to_utf8(const wchar_t *wide_path, GUTF8String &path)
{
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, 0, 0, 0, 0);
  if (bytes <= 1)
    return false;
  std::vector<char> buffer(bytes);
  if (!WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, &buffer[0], bytes, 0, 0))
    return false;
  path = GUTF8String(&buffer[0]);
  return true;
}

bool decode_first_page(const wchar_t *path)
{
  GUTF8String utf8_path;
  if (!wide_path_to_utf8(path, utf8_path))
    return false;
  const GURL url = GURL::Filename::UTF8(utf8_path);
  if (!url.is_file())
    return false;

  GP<DjVuDocument> document = DjVuDocument::create_wait(url);
  if (document->get_pages_num() <= 0)
    return false;
  GP<DjVuImage> image = document->get_page(0, true);
  return image && image->wait_for_complete_decode() &&
         image->get_width() > 0 && image->get_height() > 0;
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
  if (argc != 3 || (wcscmp(argv[2], L"positive") != 0 &&
                    wcscmp(argv[2], L"malformed") != 0))
  {
    fputs("usage: malformed_djvu_asan <fixture> <positive|malformed>\n", stderr);
    return 2;
  }

  const bool expect_success = wcscmp(argv[2], L"positive") == 0;
  try
  {
    const bool decoded = decode_first_page(argv[1]);
    if (expect_success && decoded)
    {
      puts("ASAN_RESULT: PASS");
      return 0;
    }
    if (!expect_success && !decoded)
    {
      puts("ASAN_RESULT: CONTROLLED_FAILURE");
      return 0;
    }
    fputs(expect_success ? "positive fixture did not decode\n" :
                           "malformed fixture unexpectedly decoded\n", stderr);
  }
  catch (const GException &exception)
  {
    fprintf(stderr, "DjVu exception: %s\n", exception.get_cause());
    if (!expect_success)
    {
      puts("ASAN_RESULT: CONTROLLED_FAILURE");
      return 0;
    }
  }
  catch (...)
  {
    fputs("unknown exception while decoding fixture\n", stderr);
    if (!expect_success)
    {
      puts("ASAN_RESULT: CONTROLLED_FAILURE");
      return 0;
    }
  }
  fputs("ASAN_RESULT: FAIL\n", stderr);
  return 1;
}
