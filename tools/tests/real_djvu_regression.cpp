// Real-world DjVu corpus fixture decoder. The PowerShell driver runs one
// fixture per process so it can enforce a timeout without a GUI dependency.
#include "ByteStream.h"
#include "DjVuDocument.h"
#include "DjVuAnno.h"
#include "DjVuFile.h"
#include "DjVuImage.h"
#include "DjVmNav.h"
#include "GMapAreas.h"
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

int fail(const char *failure_type, const char *message)
{
  if (message)
    fprintf(stderr, "%s\n", message);
  fprintf(stderr, "CORPUS_RESULT: FAIL %s\n", failure_type);
  return 1;
}

unsigned long read_be32(const unsigned char *bytes)
{
  return (static_cast<unsigned long>(bytes[0]) << 24) |
         (static_cast<unsigned long>(bytes[1]) << 16) |
         (static_cast<unsigned long>(bytes[2]) << 8) |
         static_cast<unsigned long>(bytes[3]);
}

int find_fourcc(const std::vector<unsigned char> &bytes, const char *fourcc)
{
  for (size_t offset = 0; offset + 4 <= bytes.size(); ++offset)
    if (bytes[offset] == static_cast<unsigned char>(fourcc[0]) &&
        bytes[offset + 1] == static_cast<unsigned char>(fourcc[1]) &&
        bytes[offset + 2] == static_cast<unsigned char>(fourcc[2]) &&
        bytes[offset + 3] == static_cast<unsigned char>(fourcc[3]))
      return static_cast<int>(offset);
  return -1;
}

// The negative fixtures are generated deterministically from SHA-checked
// positives.  This classifies their structural defect only after DjVuLibre has
// rejected the document or page decode; it does not turn a successful decode
// into an expected failure.
const char *classify_known_corruption(const wchar_t *path)
{
  FILE *file = _wfopen(path, L"rb");
  if (!file)
    return "input_unavailable";
  std::vector<unsigned char> bytes;
  unsigned char buffer[4096];
  size_t count = 0;
  while ((count = fread(buffer, 1, sizeof(buffer), file)) != 0)
    bytes.insert(bytes.end(), buffer, buffer + count);
  fclose(file);

  // The generated truncated fixture preserves the first 32 source bytes so
  // it still has an IFF header.  It is nevertheless an incomplete input,
  // not an intentionally oversized root FORM declaration.
  if (bytes.size() < 64)
    return "truncated_input";
  if (memcmp(&bytes[0], "AT&TFORM", 8) != 0)
    return "invalid_header";
  if (read_be32(&bytes[8]) > bytes.size() - 12)
    return "invalid_form_length";

  const int info = find_fourcc(bytes, "INFO");
  if (info >= 0 && static_cast<size_t>(info) + 8 <= bytes.size() &&
      read_be32(&bytes[info + 4]) > bytes.size() - static_cast<size_t>(info) - 8)
    return "invalid_chunk_length";
  if (find_fourcc(bytes, "JUNK") >= 0)
    return "missing_incl";
  return "decode_failure";
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
  if (!has_feature(features, "text_layer") && !has_feature(features, "annotations") &&
      !has_feature(features, "bookmarks") && !has_feature(features, "hyperlinks"))
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
  GP<ByteStream> annotations = first_page->get_anno();
  if ((!annotations || annotations->size() <= 0) && document->get_djvu_file(0))
    annotations = document->get_djvu_file(0)->get_anno();
  if (has_feature(features, "annotations"))
  {
    if (!annotations || annotations->size() <= 0)
    {
      fputs("expected annotations are unavailable\n", stderr);
      return false;
    }
    puts("annotations: available");
  }
  if (has_feature(features, "hyperlinks"))
  {
    if (!annotations || annotations->size() <= 0)
    {
      fputs("expected hyperlink annotations are unavailable\n", stderr);
      return false;
    }
    GP<DjVuAnno> decoded = first_page->get_decoded_anno();
    int hyperlinks = 0;
    if (decoded && decoded->ant)
      for (GPosition position = decoded->ant->map_areas; position; ++position)
        if (decoded->ant->map_areas[position] &&
            decoded->ant->map_areas[position]->url.length())
          ++hyperlinks;
    if (hyperlinks <= 0)
    {
      fputs("expected hyperlink map area is unavailable\n", stderr);
      return false;
    }
    printf("hyperlinks: %d map area(s)\n", hyperlinks);
  }
  if (has_feature(features, "bookmarks"))
  {
    GP<DjVmNav> bookmarks = document->get_djvm_nav();
    if (!bookmarks || bookmarks->getBookMarkCount() <= 0)
    {
      fputs("expected navigation bookmarks are unavailable or empty\n", stderr);
      return false;
    }
    printf("bookmarks: %d\n", bookmarks->getBookMarkCount());
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
    return fail("argument_error", "could not convert expected features to UTF-8");
  }

  const char *known_corruption = classify_known_corruption(argv[1]);

  try
  {
    GUTF8String path;
    if (!wide_path_to_utf8(argv[1], path))
    {
      return fail("path_conversion", "could not convert fixture path to UTF-8");
    }
    const GURL url = GURL::Filename::UTF8(path);
    if (!url.is_file())
    {
      return fail("input_unavailable", "fixture could not be opened as a local file");
    }

    GP<DjVuDocument> document = DjVuDocument::create_wait(url);
    const int pages = document->get_pages_num();
    if (pages <= 0)
    {
      return fail(known_corruption, "document has no pages");
    }
    printf("page count: %d\n", pages);
    if (expected_pages >= 0 && pages != expected_pages)
    {
      char message[160];
      sprintf(message, "page count mismatch: expected %d, got %d", expected_pages, pages);
      return fail("page_count_mismatch", message);
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
        return fail(known_corruption, 0);
    if (!check_supported_features(document, features))
      return fail("feature_unavailable", 0);
  }
  catch (const GException &exception)
  {
    char message[1024];
    sprintf(message, "DjVu exception: %s", exception.get_cause());
    return fail(known_corruption, message);
  }
  catch (...)
  {
    return fail(known_corruption, "unknown exception while decoding fixture");
  }

  puts("CORPUS_RESULT: PASS");
  return 0;
}
