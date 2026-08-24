// Regression coverage for arithmetic and parser hardening in libdjvu.
#include "ByteStream.h"
#include "GBitmap.h"
#include "GPixmap.h"
#include "GException.h"
#include "GSmartPointer.h"
#include "DjVuFileCache.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_NAMESPACES
namespace DJVU {
#endif

class DjVuFileCacheTestAccess
{
public:
  static void add(DjVuFileCache &cache, const GP<DjVuFile> &file,
                  time_t time)
  {
    DjVuFileCache::Item *item = new DjVuFileCache::Item(file);
    item->time = time;
    cache.list.append(item);
    cache.cur_size = cache.calculate_size();
  }

  static int reported_size(const DjVuFileCache &cache)
  {
    return cache.cur_size;
  }

  static int item_count(const DjVuFileCache &cache)
  {
    return cache.list.size();
  }

  static void set_reported_size(DjVuFileCache &cache, int size)
  {
    cache.cur_size = size;
  }
};

#ifdef HAVE_NAMESPACES
}
#endif

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

bool accepts_pnm(const char *contents, bool bitmap)
{
  GP<ByteStream> stream = ByteStream::create(contents, strlen(contents));
  bool accepted = true;
  G_TRY
  {
    if (bitmap)
      GBitmap::create(*stream);
    else
      GPixmap::create(*stream);
  }
  G_CATCH_ALL
  {
    accepted = false;
  }
  G_ENDCATCH;
  return accepted;
}

bool parser_dimension_limits()
{
  return accepts_pnm("P2\n65535 0\n1\n", true) &&
    accepts_pnm("P3\n65535 0\n1\n", false) &&
    rejects_pnm("P2\n65536 0\n1\n", true) &&
    rejects_pnm("P3\n65536 0\n1\n", false) &&
    rejects_pnm("P2\n1 65536\n1\n", true) &&
    rejects_pnm("P3\n1 65536\n1\n", false) &&
    rejects_pnm("P2\n2147483647 0\n1\n", true) &&
    rejects_pnm("P3\n2147483647 0\n1\n", false) &&
    rejects_pnm("P2\n2147483648 0\n1\n", true) &&
    rejects_pnm("P3\n2147483648 0\n1\n", false) &&
    rejects_pnm("P2\n4294967295 0\n1\n", true) &&
    rejects_pnm("P3\n4294967295 0\n1\n", false) &&
    rejects_pnm("P2\n4294967296 0\n1\n", true) &&
    rejects_pnm("P3\n4294967296 0\n1\n", false) &&
    rejects_pnm("P2\n1 1\n0\n", true) &&
    rejects_pnm("P3\n1 1\n65536\n", false);
}

bool image_memory_usage_cases()
{
  GP<GBitmap> bitmap = GBitmap::create();
  GP<GPixmap> pixmap = GPixmap::create();
  bitmap->init(2, 3, 1);
  pixmap->init(2, 3);
  return bitmap->get_memory_usage() == sizeof(GBitmap) + 9 &&
    pixmap->get_memory_usage() == sizeof(GPixmap) +
      6 * sizeof(GPixel);
}

class TestDjVuFile : public DjVuFile
{
public:
  static GP<TestDjVuFile> create(unsigned int payload)
  {
    TestDjVuFile *file = new TestDjVuFile();
    GP<TestDjVuFile> result = file;
    file->init(ByteStream::create("", 0));
    file->set_payload(payload);
    return result;
  }
  void set_payload(unsigned int payload)
  {
    static char contents[700];
    meta = ByteStream::create(contents, payload);
  }
private:
  TestDjVuFile(void) : DjVuFile() {}
};

class TestDjVuFileCache : public DjVuFileCache
{
public:
  TestDjVuFileCache(int maximum) : DjVuFileCache(maximum), next_time(1) {}
  void add(TestDjVuFile *file)
  {
    DjVuFileCacheTestAccess::add(*this, file, next_time++);
  }
  int reported_size() const { return DjVuFileCacheTestAccess::reported_size(*this); }
  int item_count() const { return DjVuFileCacheTestAccess::item_count(*this); }
  void saturate_accounting()
  {
    DjVuFileCacheTestAccess::set_reported_size(*this, INT_MAX);
  }
private:
  time_t next_time;
};

bool cache_accounting_cases()
{
  TestDjVuFileCache ordinary(-1);
  GP<TestDjVuFile> ordinary_a = TestDjVuFile::create(100);
  GP<TestDjVuFile> ordinary_b = TestDjVuFile::create(200);
  GP<TestDjVuFile> ordinary_c = TestDjVuFile::create(300);
  ordinary.add(ordinary_a.operator->());
  ordinary.add(ordinary_b.operator->());
  ordinary.add(ordinary_c.operator->());
  if (static_cast<unsigned int>(ordinary.reported_size()) != ordinary_a->get_memory_usage() +
      ordinary_b->get_memory_usage() + ordinary_c->get_memory_usage())
    return false;

  TestDjVuFileCache grows(100000);
  GP<TestDjVuFile> growing_file = TestDjVuFile::create(100);
  grows.add(growing_file.operator->());
  growing_file->set_payload(700);
  grows.set_max_size(-1);
  if (static_cast<unsigned int>(grows.reported_size()) != growing_file->get_memory_usage())
    return false;

  TestDjVuFileCache shrinks(-1);
  GP<TestDjVuFile> shrinking_file = TestDjVuFile::create(700);
  shrinks.add(shrinking_file.operator->());
  shrinking_file->set_payload(100);
  shrinks.set_max_size(-1);
  if (static_cast<unsigned int>(shrinks.reported_size()) != shrinking_file->get_memory_usage())
    return false;

  TestDjVuFileCache grow_evict(-1);
  GP<TestDjVuFile> old_file = TestDjVuFile::create(100);
  GP<TestDjVuFile> new_file = TestDjVuFile::create(100);
  grow_evict.add(old_file.operator->());
  grow_evict.add(new_file.operator->());
  old_file->set_payload(700);
  grow_evict.set_max_size(new_file->get_memory_usage() + 200);
  if (static_cast<unsigned int>(grow_evict.reported_size()) != new_file->get_memory_usage() ||
      grow_evict.item_count() != 1)
    return false;

  TestDjVuFileCache refreshed(-1);
  GP<TestDjVuFile> refreshed_file = TestDjVuFile::create(100);
  refreshed.add(refreshed_file.operator->());
  refreshed.set_max_size(refreshed_file->get_memory_usage() + 200);
  refreshed_file->set_payload(700);
  GP<DjVuFile> refreshed_base = refreshed_file.operator->();
  refreshed.add_file(refreshed_base);
  if (refreshed.item_count() != 0 || refreshed.reported_size() != 0)
    return false;

  TestDjVuFileCache saturated(-1);
  GP<TestDjVuFile> saturated_file = TestDjVuFile::create(100);
  saturated.add(saturated_file.operator->());
  saturated.saturate_accounting();
  saturated_file->set_payload(700);
  saturated.set_max_size(-1);
  if (static_cast<unsigned int>(saturated.reported_size()) != saturated_file->get_memory_usage())
    return false;

  TestDjVuFileCache large(-1);
  GP<TestDjVuFile> large_file;
  for (int i=0; i<21; ++i)
  {
    large_file = TestDjVuFile::create(10);
    large.add(large_file.operator->());
  }
  const int item_size = large_file->get_memory_usage();
  large.set_max_size(item_size * 9);
  return large.reported_size() == item_size * 9 && large.item_count() == 9;
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
      !parser_overflow_rejected() || !parser_dimension_limits() ||
      !image_memory_usage_cases() || !cache_accounting_cases() ||
      !exception_cause_cases())
  {
    fputs("libdjvu core regression failed\n", stderr);
    return 1;
  }
  puts("libdjvu core regression passed");
  return 0;
}
