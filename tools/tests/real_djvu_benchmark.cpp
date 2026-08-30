// Performance baseline helper for the real DjVu corpus.  It deliberately
// shares the decoder path with the regression helper but has no correctness
// assertions beyond ensuring that a timed decode completed successfully.
#include "DjVuDocument.h"
#include "DjVuImage.h"
#include "GException.h"
#include "GSmartPointer.h"
#include "GURL.h"

#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <windows.h>

#ifdef HAVE_NAMESPACES
using namespace DJVU;
#endif

namespace {

double now_ms()
{
  LARGE_INTEGER frequency, counter;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&counter);
  return 1000.0 * static_cast<double>(counter.QuadPart) /
         static_cast<double>(frequency.QuadPart);
}

SIZE_T peak_working_set()
{
  PROCESS_MEMORY_COUNTERS counters;
  counters.cb = sizeof(counters);
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
    return 0;
  return counters.PeakWorkingSetSize;
}

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

bool append_page(std::vector<int> &pages, int page)
{
  for (size_t index = 0; index < pages.size(); ++index)
    if (pages[index] == page)
      return false;
  pages.push_back(page);
  return true;
}

const char *page_label(int page, int page_count)
{
  if (page == 0)
    return "first";
  if (page == page_count - 1)
    return "last";
  return "middle";
}

int fail(const char *message)
{
  fprintf(stderr, "BENCHMARK_RESULT: FAIL %s\n", message);
  return 1;
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
  if (argc != 4)
  {
    fputs("usage: real_djvu_benchmark <fixture> <fixture-id> <runs>\n", stderr);
    return 2;
  }
  const int runs = _wtoi(argv[3]);
  if (runs < 2 || runs > 32)
    return fail("runs_must_be_between_2_and_32");

  GUTF8String path;
  if (!wide_path_to_utf8(argv[1], path))
    return fail("path_conversion");
  const GURL url = GURL::Filename::UTF8(path);
  if (!url.is_file())
    return fail("input_unavailable");

  for (int run = 0; run < runs; ++run)
  {
    try
    {
      const double open_begin = now_ms();
      GP<DjVuDocument> document = DjVuDocument::create_wait(url);
      const double open_ms = now_ms() - open_begin;
      const int page_count = document->get_pages_num();
      if (page_count <= 0)
        return fail("document_has_no_pages");
      printf("BENCHMARK_OPEN run=%d ms=%.3f pages=%d peak_ws=%llu\n", run,
             open_ms, page_count, static_cast<unsigned long long>(peak_working_set()));

      std::vector<int> pages;
      append_page(pages, 0);
      if (page_count > 1)
      {
        append_page(pages, page_count / 2);
        append_page(pages, page_count - 1);
      }
      for (size_t index = 0; index < pages.size(); ++index)
      {
        const double decode_begin = now_ms();
        GP<DjVuImage> image = document->get_page(pages[index], true);
        const bool decoded = image && image->wait_for_complete_decode();
        const double decode_ms = now_ms() - decode_begin;
        if (!decoded || image->get_width() <= 0 || image->get_height() <= 0)
          return fail("page_decode_failed");
        printf("BENCHMARK_DECODE run=%d page=%s index=%d ms=%.3f width=%d height=%d peak_ws=%llu\n",
               run, page_label(pages[index], page_count), pages[index], decode_ms,
               image->get_width(), image->get_height(),
               static_cast<unsigned long long>(peak_working_set()));
      }
    }
    catch (const GException &exception)
    {
      fprintf(stderr, "DjVu exception: %s\n", exception.get_cause());
      return fail("djvu_exception");
    }
    catch (...)
    {
      return fail("unknown_exception");
    }
  }
  puts("BENCHMARK_RESULT: PASS");
  return 0;
}
