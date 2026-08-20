// Benchmark the current direct JPEGDecoder -> GPixmap integration.
#include "ByteStream.h"
#include "GPixmap.h"
#include "JPEGDecoder.h"

extern "C" {
#include <jpeglib.h>
}

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <vector>

namespace {

struct Fixture {
  const char *name;
  int width;
  int height;
  bool grayscale;
  bool progressive;
  int iterations;
};

GP<ByteStream> make_jpeg(const Fixture &fixture)
{
  jpeg_compress_struct cinfo;
  jpeg_error_mgr jerr;
  unsigned char *encoded = 0;
  unsigned long encoded_size = 0;
  const int components = fixture.grayscale ? 1 : 3;
  if (fixture.width <= 0 || fixture.height <= 0 ||
      fixture.width > INT_MAX / components)
  {
    fputs("invalid benchmark fixture dimensions\n", stderr);
    exit(2);
  }
  const size_t row_size = static_cast<size_t>(fixture.width) * components;
  std::vector<unsigned char> row(row_size);

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  jpeg_mem_dest(&cinfo, &encoded, &encoded_size);
  cinfo.image_width = fixture.width;
  cinfo.image_height = fixture.height;
  cinfo.input_components = components;
  cinfo.in_color_space = fixture.grayscale ? JCS_GRAYSCALE : JCS_RGB;
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, 85, TRUE);
  if (fixture.progressive)
    jpeg_simple_progression(&cinfo);
  jpeg_start_compress(&cinfo, TRUE);
  while (cinfo.next_scanline < cinfo.image_height)
  {
    const int y = (int)cinfo.next_scanline;
    for (int x = 0; x < fixture.width; ++x)
    {
      const unsigned char value = (unsigned char)((x * 13 + y * 7) & 0xff);
      if (fixture.grayscale)
        row[x] = value;
      else
      {
        row[x * 3] = value;
        row[x * 3 + 1] = (unsigned char)((x * 3 + y * 11) & 0xff);
        row[x * 3 + 2] = (unsigned char)(255 - value);
      }
    }
    JSAMPROW scanline = &row[0];
    jpeg_write_scanlines(&cinfo, &scanline, 1);
  }
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  GP<ByteStream> stream = ByteStream::create(encoded, encoded_size);
  free(encoded);
  return stream;
}

unsigned long crc32(const TArray<char> &data)
{
  unsigned long crc = 0xffffffffUL;
  for (int i = 0; i < data.size(); ++i)
  {
    crc ^= (unsigned char)data[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320UL : 0);
  }
  return crc ^ 0xffffffffUL;
}

double now_ms()
{
  LARGE_INTEGER frequency, counter;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&counter);
  return 1000.0 * (double)counter.QuadPart / (double)frequency.QuadPart;
}

void run_fixture(const Fixture &fixture)
{
  GP<ByteStream> encoded = make_jpeg(fixture);
  const TArray<char> data = encoded->get_data();
  GP<ByteStream> warmup_input = ByteStream::create(data, data.size());
  GP<GPixmap> warmup_image = JPEGDecoder::decode(*warmup_input);
  if ((int)warmup_image->columns() != fixture.width ||
      (int)warmup_image->rows() != fixture.height)
  {
    fputs("benchmark warmup dimension mismatch\n", stderr);
    exit(1);
  }

  double times[32];
  double sum = 0.0;
  for (int i = 0; i < fixture.iterations; ++i)
  {
    GP<ByteStream> input = ByteStream::create(data, data.size());
    const double begin = now_ms();
    GP<GPixmap> image = JPEGDecoder::decode(*input);
    const double elapsed = now_ms() - begin;
    if ((int)image->columns() != fixture.width || (int)image->rows() != fixture.height)
    {
      fputs("benchmark decode dimension mismatch\n", stderr);
      exit(1);
    }
    times[i] = elapsed;
    sum += elapsed;
  }
  for (int i = 0; i < fixture.iterations; ++i)
    for (int j = i + 1; j < fixture.iterations; ++j)
      if (times[j] < times[i]) { double temp = times[i]; times[i] = times[j]; times[j] = temp; }
  const double average = sum / fixture.iterations;
  const int middle = fixture.iterations / 2;
  const double median = fixture.iterations % 2 ? times[middle] :
    (times[middle - 1] + times[middle]) / 2.0;
  const double megapixels = (double)fixture.width * fixture.height / 1000000.0;
  printf("%s,%dx%d,%s,%lu,%08lX,1,%d,%.3f,%.3f,%.2f\n", fixture.name,
    fixture.width, fixture.height, fixture.grayscale ? "gray" : "rgb",
    (unsigned long)data.size(), crc32(data), fixture.iterations, average,
    median, megapixels * 1000.0 / median);
}

} // namespace

int main()
{
  const Fixture fixtures[] = {
    { "baseline-small", 1200, 1600, false, false, 25 },
    { "progressive-medium", 2500, 3500, false, true, 15 },
    { "grayscale-large", 4000, 6000, true, false, 7 }
  };
  puts("fixture,dimensions,type,encoded_size,crc32,warmups,runs,average_ms,median_ms,median_megapixels_per_second");
  for (int i = 0; i < (int)(sizeof(fixtures) / sizeof(fixtures[0])); ++i)
    run_fixture(fixtures[i]);
  return 0;
}
