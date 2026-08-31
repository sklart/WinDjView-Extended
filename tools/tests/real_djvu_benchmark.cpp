// Performance baseline helper for the real DjVu corpus.  It deliberately
// shares the decoder path with the regression helper but has no correctness
// assertions beyond ensuring that a timed decode completed successfully.
#include "../../src/stdafx.h"
#include "../../src/DjVuSource.h"
#include "../../src/RenderThread.h"
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

class BenchmarkApplication : public IApplication
{
public:
  virtual bool LoadDocSettings(const CString &, DocSettings *) { return false; }
  virtual bool GetCropPages() { return false; }
  virtual DictionaryInfo *GetDictionaryInfo(const CString &, bool) { return NULL; }
  virtual void ReportFatalError() { }
};

class BenchmarkObserver : public Observer
{
public:
  virtual void OnUpdate(const Observable *, const Message *) { }
};

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

  if (!AfxWinInit(::GetModuleHandle(NULL), NULL, ::GetCommandLine(), 0))
    return fail("mfc_initialization");

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
      std::vector<const char *> labels;
      pages.push_back(0);
      labels.push_back("first");
      if (page_count > 1)
      {
        // Keep the three metric series distinct for a two-page document too:
        // its middle and last page have the same index, but are independent
        // measurements required by the baseline contract.
        pages.push_back(page_count / 2);
        labels.push_back("middle");
        pages.push_back(page_count - 1);
        labels.push_back("last");
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
               run, labels[index], pages[index], decode_ms,
               image->get_width(), image->get_height(),
               static_cast<unsigned long long>(peak_working_set()));
      }

	  // A small deterministic reading sequence is reported separately from the
	  // individual-page timings. It gives prefetch work a stable before/after
	  // baseline without turning this informational runner into a threshold.
	  const int transitions = page_count > 1 ? min(page_count - 1, 3) : 0;
	  const double sequence_begin = now_ms();
	  for (int transition = 0; transition < transitions; ++transition)
	  {
		GP<DjVuImage> image = document->get_page(transition + 1, true);
		if (!image || !image->wait_for_complete_decode() ||
			image->get_width() <= 0 || image->get_height() <= 0)
			return fail("sequential_page_decode_failed");
	  }
      printf("BENCHMARK_SEQUENCE run=%d transitions=%d ms=%.3f peak_ws=%llu\n", run,
		 transitions, now_ms() - sequence_begin,
		 static_cast<unsigned long long>(peak_working_set()));

      BenchmarkApplication application;
      BenchmarkObserver observer;
      DjVuSource::SetApplication(&application);
      DjVuSource *source = DjVuSource::FromFile(argv[1]);
      if (source == NULL)
        return fail("prefetch_source_open");
      CRenderThread *render_thread = new CRenderThread(source, &observer);
      const int miss_page = page_count > 1 ? 1 : 0;
      const int hit_page = page_count > 2 ? 2 : 0;
      const double miss_begin = now_ms();
      GP<DjVuImage> miss_image = source->GetPage(miss_page, NULL);
      const double miss_ms = now_ms() - miss_begin;
      if (!miss_image || miss_image->get_width() <= 0 || miss_image->get_height() <= 0)
        return fail("prefetch_miss_decode");

      render_thread->AddPrefetchJob(hit_page);
      const double wait_begin = now_ms();
      while (!source->IsPrefetchReady(hit_page) && now_ms() - wait_begin < 30000.0)
        Sleep(1);
      if (!source->IsPrefetchReady(hit_page))
        return fail("prefetch_timeout");
      const double hit_begin = now_ms();
      GP<DjVuImage> hit_image = source->GetPage(hit_page, NULL);
      const double hit_ms = now_ms() - hit_begin;
      if (!hit_image || hit_image->get_width() <= 0 || hit_image->get_height() <= 0)
        return fail("prefetch_hit_decode");
      printf("BENCHMARK_PREFETCH run=%d miss_ms=%.3f hit_ms=%.3f peak_ws=%llu\n", run,
             miss_ms, hit_ms, static_cast<unsigned long long>(peak_working_set()));
      render_thread->Stop();
      source->Release();
      DjVuSource::SetApplication(NULL);
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
