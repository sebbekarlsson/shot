#include <shot/shot.h>
#include <errno.h>
#include <stdio.h>
/* jpeglib.h needs FILE declared first. */
#include <jpeglib.h>
#include <png.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SHOT_JPEG_QUALITY 90

typedef struct {
  uint8_t* data;
  size_t size;
  size_t cap;
} MemBuf;

static void mem_write(png_structp png, png_bytep data, png_size_t len) {
  MemBuf* buf = png_get_io_ptr(png);
  if (buf->size + len > buf->cap) {
    size_t cap = buf->cap ? buf->cap : 64 * 1024;
    while (cap < buf->size + len) cap *= 2;
    uint8_t* grown = realloc(buf->data, cap);
    if (!grown) png_error(png, "out of memory");
    buf->data = grown;
    buf->cap = cap;
  }
  memcpy(buf->data + buf->size, data, len);
  buf->size += len;
}

static void mem_flush(png_structp png) {
  (void)png;
}

/* Writes to `fp` if given, otherwise into `mem`. */
static int encode_png(const ShotImage* img, FILE* fp, MemBuf* mem) {
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png) return -1;
  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_write_struct(&png, NULL);
    return -1;
  }
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    return -1;
  }

  if (fp)
    png_init_io(png, fp);
  else
    png_set_write_fn(png, mem, mem_write, mem_flush);

  png_set_IHDR(png, info, img->width, img->height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);
  for (int y = 0; y < img->height; y++)
    png_write_row(png, img->pixels + (size_t)y * img->width * 3);
  png_write_end(png, NULL);
  png_destroy_write_struct(&png, &info);
  return 0;
}

int shot_image_encode_png(const ShotImage* img, uint8_t** data_out, size_t* size_out) {
  MemBuf buf = {0};
  if (encode_png(img, NULL, &buf) != 0) {
    free(buf.data);
    fprintf(stderr, "shot: failed to encode PNG\n");
    return -1;
  }
  *data_out = buf.data;
  *size_out = buf.size;
  return 0;
}

static void encode_jpeg(const ShotImage* img, FILE* fp) {
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  jpeg_stdio_dest(&cinfo, fp);

  cinfo.image_width = img->width;
  cinfo.image_height = img->height;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, SHOT_JPEG_QUALITY, TRUE);

  jpeg_start_compress(&cinfo, TRUE);
  while (cinfo.next_scanline < cinfo.image_height) {
    JSAMPROW row = img->pixels + (size_t)cinfo.next_scanline * img->width * 3;
    jpeg_write_scanlines(&cinfo, &row, 1);
  }
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
}

int shot_image_save(const ShotImage* img, const char* path) {
  FILE* fp = fopen(path, "wb");
  if (!fp) {
    fprintf(stderr, "shot: cannot open '%s': %s\n", path, strerror(errno));
    return -1;
  }

  const char* ext = strrchr(path, '.');
  int rc = 0;
  if (ext && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0))
    encode_jpeg(img, fp);
  else
    rc = encode_png(img, fp, NULL);

  if (fclose(fp) != 0 || rc != 0) {
    fprintf(stderr, "shot: failed to write '%s'\n", path);
    return -1;
  }
  return 0;
}
