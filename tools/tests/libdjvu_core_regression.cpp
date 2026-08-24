// Regression coverage for arithmetic and parser hardening in libdjvu.
#include "ByteStream.h"
#include "GBitmap.h"
#include "GPixmap.h"
#include "GException.h"
#include "GSmartPointer.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

namespace {

bool bitmap_init_rejected(int rows, int columns, int border)
{
  bool rejected = false;
  GP<GBitmap> bitmap = GBitmap::create();
  G_TRY { bitmap->init(rows, columns, border); }
  G_CATCH_ALL { rejected = true; }
  G_ENDCATCH;
  return rejected;
}

bool bitmap_init_cases()
{
  GP<GBitmap> bitmap = GBitmap::create();
  bitmap->init(0, 0, 0);
  bitmap->init(1, 1, 0);
  bitmap->init(1, 1, 1);
  bitmap->init(16, 32, 2);
  return bitmap_init_rejected(-1, 1, 0) &&
    bitmap_init_rejected(1, -1, 0) &&
    bitmap_init_rejected(1, 1, -1) &&
    bitmap_init_rejected(USHRT_MAX + 1, 1, 0) &&
    bitmap_init_rejected(1, USHRT_MAX + 1, 0) &&
    bitmap_init_rejected(1, 1, USHRT_MAX + 1) &&
    bitmap_init_rejected(1, USHRT_MAX, 1) &&
    bitmap_init_rejected(USHRT_MAX, USHRT_MAX, 0);
}

bool donate_cases()
{
  bool bitmap_rejected = false;
  bool rle_rejected = false;
  bool pixmap_rejected = false;
  GP<GBitmap> bitmap = GBitmap::create();
  GP<GPixmap> pixmap = GPixmap::create();
  G_TRY { bitmap->donate_data(0, -1, 1); }
  G_CATCH_ALL { bitmap_rejected = true; }
  G_ENDCATCH;
  G_TRY { bitmap->donate_rle(0, 0, USHRT_MAX + 1, 1); }
  G_CATCH_ALL { rle_rejected = true; }
  G_ENDCATCH;
  G_TRY { pixmap->donate_data(0, 1, -1); }
  G_CATCH_ALL { pixmap_rejected = true; }
  G_ENDCATCH;
  return bitmap_rejected && rle_rejected && pixmap_rejected;
}

bool buffer_overflow_rejected()
{
  unsigned int *data = 0;
  bool rejected = false;
  const size_t count = static_cast<size_t>(-1) / sizeof(unsigned int) + 1;
  G_TRY
  {
    GPBuffer<unsigned int> buffer(data, count);
  }
  G_CATCH_ALL
  {
    rejected = true;
  }
  G_ENDCATCH;
  return rejected && !data;
}

bool rejects_pnm(const char *contents, bool bitmap)
{
  GP<ByteStream> stream = ByteStream::create(contents, strlen(contents));
  bool rejected = false;
  G_TRY
  {
    if (bitmap)
      GBitmap::create(*stream);
    else
      GPixmap::create(*stream);
  }
  G_CATCH_ALL
  {
    rejected = true;
  }
  G_ENDCATCH;
  return rejected;
}

bool parser_overflow_rejected()
{
  return rejects_pnm("P2\n42949672960 1\n1\n", true) &&
    rejects_pnm("P2\n1 42949672960\n1\n", true) &&
    rejects_pnm("P2\n1 1\n42949672960\n", true) &&
    rejects_pnm("P3\n42949672960 1\n1\n", false) &&
    rejects_pnm("P3\n1 42949672960\n1\n", false) &&
    rejects_pnm("P3\n1 1\n42949672960\n", false);
}

bool exception_cause_cases()
{
  return GException::cmp_cause("EOF", "EOF") == 0 &&
    GException::cmp_cause("EOF\tfoo", "EOF") == 0 &&
    GException::cmp_cause("EOF", "EOF\tfoo") == 0 &&
    GException::cmp_cause("EOF\tfoo", "EOF\tbar") == 0 &&
    GException::cmp_cause("EOF\nfoo", "EOF") == 0 &&
    GException::cmp_cause("EOF\nfoo", "EOF\tbar") == 0 &&
    GException::cmp_cause("EOF", "STOP") != 0 &&
    GException::cmp_cause("EOF\tfoo", "STOP") != 0 &&
    GException::cmp_cause("EOF", "EOF2") != 0 &&
    GException::cmp_cause("ABC\tfoo", "ABD\tfoo") != 0 &&
    GException::cmp_cause(0, 0) == -1 &&
    GException::cmp_cause("", 0) == -1 &&
    GException::cmp_cause(0, "") == -1;
}

} // namespace

int main()
{
  if (!bitmap_init_cases() || !donate_cases() || !buffer_overflow_rejected() ||
      !parser_overflow_rejected() || !exception_cause_cases())
  {
    fputs("libdjvu core regression failed\n", stderr);
    return 1;
  }
  puts("libdjvu core regression passed");
  return 0;
}
