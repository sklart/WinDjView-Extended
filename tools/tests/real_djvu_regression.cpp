// Real-world DjVu corpus fixture decoder. The PowerShell driver runs one
// fixture per process so it can enforce a timeout without a GUI dependency.
#include "ByteStream.h"
#include "DjVuDocument.h"
#include "DjVuImage.h"
#include "GException.h"
#include "GSmartPointer.h"
#include "GURL.h"

#include <stdio.h>
#include <string.h>
#include <vector>
#include <windows.h>

#ifdef HAVE_NAMESPACES
using namespace DJVU;
#endif

namespace {

bool has_feature(const char *features, const char *feature)
{
  const size_t wanted = strlen(feature);
  const char *cursor = features;
  while (cursor && *cursor)
  {
    const char *end = strchr(cursor, ',');
    const size_t length = end ? static_cast<size_t>(end - cursor) : strlen(cursor);
    if (length == wanted && strncmp(cursor, feature, wanted) == 0)
      return true;
    cursor = end ? end + 1 : 0;
  }
  return false;
}

bool append_page(std::vector<int> &pages, int page)
{
  for (size_t index = 0; index < pages.size(); ++index)
    if (pages[index] == page)
      return false;
  pages.push_back(page);
  return true;
}

bool decode_page(const GP<DjVuDocument> &document, int page_number)
{
  GP<DjVuImage> image = document->get_page(page_number, true);
  if (!image || !image->wait_for_complete_decode())
  {
    fprintf(stderr, "page %d did not complete decoding\n", page_number + 1);
    return false;
  }
  if (image->get_width() <= 0 || image->get_height() <= 0)
  {
    fprintf(stderr, "page %d has invalid dimensions %d x %d\n", page_number + 1,
            image->get_width(), image->get_height());
    return false;
  }
  printf("decoded page %d: %d x %d\n", page_number + 1,
         image->get_width(), image->get_height());
  return true;
}

bool check_supported_features(const GP<DjVuDocument> &document,
                              const char *features)
{
  if (!has_feature(features, "text_layer") && !has_feature(features, "annotations"))
    return true;

  GP<DjVuImage> first_page = document->get_page(0, true);
  if (!first_page || !first_page->wait_for_complete_decode())
  {
    fputs("could not decode the first page for feature checks\n", stderr);
    return false;
  }
  if (has_feature(features, "text_layer"))
  {
    GP<ByteStream> text = first_page->get_text();
    if (!text || text->size() <= 0)
    {
      fputs("expected text layer is unavailable\n", stderr);
      return false;
    }
    puts("text layer: available");
  }
  if (has_feature(features, "annotations"))
  {
    GP<ByteStream> annotations = first_page->get_anno();
    if (!annotations || annotations->size() <= 0)
    {
      fputs("expected annotations are unavailable\n", stderr);
      return false;
    }
    puts("annotations: available");
  }
  return true;
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

} // namespace

int wmain(int argc, wchar_t **argv)
{
  if (argc != 4)
  {
    fputs("usage: real_djvu_regression <fixture> <expected-pages-or--1> <expected-features>\n", stderr);
    return 2;
  }

  const int expected_pages = _wtoi(argv[2]);
  char features[1024];
  const int feature_bytes = WideCharToMultiByte(CP_UTF8, 0, argv[3], -1,
                                                features, sizeof(features), 0, 0);
  if (feature_bytes <= 0)
  {
    fputs("could not convert expected features to UTF-8\n", stderr);
    return 1;
  }

  try
  {
    GUTF8String path;
    if (!wide_path_to_utf8(argv[1], path))
    {
      fputs("could not convert fixture path to UTF-8\n", stderr);
      return 1;
    }
    const GURL url = GURL::Filename::UTF8(path);
    if (!url.is_file())
    {
      fputs("fixture could not be opened as a local file\n", stderr);
      return 1;
    }

    GP<DjVuDocument> document = DjVuDocument::create_wait(url);
    const int pages = document->get_pages_num();
    if (pages <= 0)
    {
      fputs("document has no pages\n", stderr);
      return 1;
    }
    printf("page count: %d\n", pages);
    if (expected_pages >= 0 && pages != expected_pages)
    {
      fprintf(stderr, "page count mismatch: expected %d, got %d\n", expected_pages, pages);
      return 1;
    }

    std::vector<int> pages_to_decode;
    append_page(pages_to_decode, 0);
    if (pages > 1)
    {
      append_page(pages_to_decode, pages / 2);
      append_page(pages_to_decode, pages - 1);
    }
    for (size_t index = 0; index < pages_to_decode.size(); ++index)
      if (!decode_page(document, pages_to_decode[index]))
        return 1;
    if (!check_supported_features(document, features))
      return 1;
  }
  catch (const GException &exception)
  {
    fprintf(stderr, "DjVu exception: %s\n", exception.get_cause());
    return 1;
  }
  catch (...)
  {
    fputs("unknown exception while decoding fixture\n", stderr);
    return 1;
  }

  puts("fixture decode: PASS");
  return 0;
}
