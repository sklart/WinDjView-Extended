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
  unsigned char *row = new unsigned char[fixture.width * components];

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
    JSAMPROW scanline = row;
    jpeg_write_scanlines(&cinfo, &scanline, 1);
  }
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  delete [] row;

  GP<ByteStream> stream = ByteStream::create(encoded, encoded_size);
  free(encoded);
  return stream;
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
  double times[8];
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
  const double median = times[fixture.iterations / 2];
  const double megapixels = (double)fixture.width * fixture.height / 1000000.0;
  printf("%s,%dx%d,%s,%.3f,%.3f,%.2f\n", fixture.name, fixture.width,
    fixture.height, fixture.grayscale ? "gray" : "rgb", average, median,
    megapixels * 1000.0 / average);
}

} // namespace

int main()
{
  const Fixture fixtures[] = {
    { "baseline-small", 1200, 1600, false, false, 5 },
    { "progressive-medium", 2500, 3500, false, true, 3 },
    { "grayscale-large", 4000, 6000, true, false, 3 }
  };
  puts("fixture,dimensions,type,average_ms,median_ms,megapixels_per_second");
  for (int i = 0; i < (int)(sizeof(fixtures) / sizeof(fixtures[0])); ++i)
    run_fixture(fixtures[i]);
  return 0;
}
