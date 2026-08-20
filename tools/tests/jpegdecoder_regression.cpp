// Regression coverage for the direct JPEGDecoder -> GPixmap path.
#include "ByteStream.h"
#include "GPixmap.h"
#include "JPEGDecoder.h"

extern "C" {
#include <jpeglib.h>
}

#include <stdio.h>
#include <stdlib.h>

namespace {

const int kWidth = 8;
const int kHeight = 6;

GP<ByteStream> make_jpeg(bool grayscale, bool progressive,
                         int width = kWidth, int height = kHeight)
{
  jpeg_compress_struct cinfo;
  jpeg_error_mgr jerr;
  unsigned char *encoded = 0;
  unsigned long encoded_size = 0;
  unsigned char row[kWidth * 3];

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  jpeg_mem_dest(&cinfo, &encoded, &encoded_size);
  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = grayscale ? 1 : 3;
  cinfo.in_color_space = grayscale ? JCS_GRAYSCALE : JCS_RGB;
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, 100, TRUE);
  if (!grayscale)
  {
    cinfo.comp_info[0].h_samp_factor = 1;
    cinfo.comp_info[0].v_samp_factor = 1;
  }
  if (progressive)
    jpeg_simple_progression(&cinfo);
  jpeg_start_compress(&cinfo, TRUE);
  while (cinfo.next_scanline < cinfo.image_height)
  {
    const int y = (int)cinfo.next_scanline;
    for (int x = 0; x < width; ++x)
    {
      const int value = x * 20 + y * 9;
      if (grayscale)
        row[x] = (unsigned char)value;
      else
      {
        row[x * 3] = (unsigned char)value;
        row[x * 3 + 1] = (unsigned char)(255 - value);
        row[x * 3 + 2] = (unsigned char)(x * 13 + y * 25);
      }
    }
    JSAMPROW scanline = row;
    jpeg_write_scanlines(&cinfo, &scanline, 1);
  }
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);

  GP<ByteStream> stream = ByteStream::create(encoded, encoded_size);
  free(encoded);
  return stream;
}

bool approximately_equal(unsigned char actual, int expected)
{
  const int difference = (int)actual - expected;
  return difference >= -3 && difference <= 3;
}

bool verify_image(bool grayscale, bool progressive,
                  int width = kWidth, int height = kHeight)
{
  GP<ByteStream> stream = make_jpeg(grayscale, progressive, width, height);
  GP<GPixmap> pixmap = JPEGDecoder::decode(*stream);
  if ((int)pixmap->columns() != width || (int)pixmap->rows() != height)
    return false;

  for (int source_y = 0; source_y < height; ++source_y)
  {
    const GPixel *row = (*pixmap)[height - 1 - source_y];
    for (int x = 0; x < width; ++x)
    {
      const int value = x * 20 + source_y * 9;
      if (grayscale)
      {
        if (!approximately_equal(row[x].r, value) ||
            !approximately_equal(row[x].g, value) ||
            !approximately_equal(row[x].b, value))
          return false;
      }
      else if (!approximately_equal(row[x].r, value) ||
               !approximately_equal(row[x].g, 255 - value) ||
               !approximately_equal(row[x].b, x * 13 + source_y * 25))
      {
        return false;
      }
    }
  }
  return true;
}

bool rejects_truncated_input()
{
  GP<ByteStream> complete = make_jpeg(false, false);
  const TArray<char> bytes = complete->get_data();
  GP<ByteStream> truncated = ByteStream::create(bytes, bytes.size() / 2);
  bool rejected = false;
  G_TRY
  {
    JPEGDecoder::decode(*truncated);
  }
  G_CATCH_ALL
  {
    rejected = true;
  }
  G_ENDCATCH;
  return rejected;
}

bool rejects_input(GP<ByteStream> input)
{
  bool rejected = false;
  G_TRY
  {
    JPEGDecoder::decode(*input);
  }
  G_CATCH_ALL
  {
    rejected = true;
  }
  G_ENDCATCH;
  return rejected;
}

bool rejects_empty_and_malformed_input()
{
  static const char malformed[] = "not a JPEG";
  return rejects_input(ByteStream::create()) &&
    rejects_input(ByteStream::create(malformed, sizeof(malformed) - 1));
}

} // namespace

int main()
{
  if (!verify_image(false, false) || !verify_image(false, true) ||
      !verify_image(true, false) || !verify_image(false, false, 1, 1) ||
      !verify_image(false, false, 2, 2) || !rejects_truncated_input() ||
      !rejects_empty_and_malformed_input())
  {
    fputs("JPEGDecoder regression failed\n", stderr);
    return 1;
  }
  puts("JPEGDecoder regression passed");
  return 0;
}
