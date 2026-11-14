/*         ______   ___    ___
 *        /\  _  \ /\_ \  /\_ \
 *        \ \ \L\ \\//\ \ \//\ \      __     __   _ __   ___
 *         \ \  __ \ \ \ \  \ \ \   /'__`\ /'_ `\/\`'__\/ __`\
 *          \ \ \/\ \ \_\ \_ \_\ \_/\  __//\ \L\ \ \ \//\ \L\ \
 *           \ \_\ \_\/\____\/\____\ \____\ \____ \ \_\\ \____/
 *            \/_/\/_/\/____/\/____/\/____/\/___L\ \/_/ \/___/
 *                                           /\____/
 *                                           \_/__/
 *
 *      KTX 1 and KTX 2 reader for BC1, BC3, BC7 and ASTC textures.
 *
 *      See LICENSE.txt for copyright information.
 */

#include "allegro5/allegro.h"
#include "allegro5/allegro_image.h"
#include "allegro5/internal/aintern_image.h"

#include "compressed.h"
#include "iio.h"

ALLEGRO_DEBUG_CHANNEL("image")

#define KTX1_HEADER_SIZE 52
#define KTX2_HEADER_SIZE 80
#define KTX2_LEVEL_INDEX_SIZE 24

#define GL_RGB_S3TC_DXT1_EXT 0x83F0
#define GL_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_RGBA_S3TC_DXT5_EXT 0x83F3
#define GL_RGBA_BPTC_UNORM_ARB 0x8E8C
#define GL_SRGB_ALPHA_BPTC_UNORM_ARB 0x8E8D
#define GL_COMPRESSED_SRGB_S3TC_DXT1_EXT 0x8C4C
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT 0x8C4D
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT 0x8C4E
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F

#define VK_FORMAT_BC1_RGB_UNORM_BLOCK 131
#define VK_FORMAT_BC1_RGBA_UNORM_BLOCK 132
#define VK_FORMAT_BC1_RGB_SRGB_BLOCK 133
#define VK_FORMAT_BC1_RGBA_SRGB_BLOCK 134
#define VK_FORMAT_BC3_UNORM_BLOCK 137
#define VK_FORMAT_BC3_SRGB_BLOCK 138
#define VK_FORMAT_BC7_UNORM_BLOCK 145
#define VK_FORMAT_BC7_SRGB_BLOCK 146
#define VK_FORMAT_ASTC_4x4_UNORM_BLOCK 157
#define VK_FORMAT_ASTC_4x4_SRGB_BLOCK 158

static uint32_t read_u32(const uint8_t *data, int offset)
{
   return (uint32_t)data[offset]
      | ((uint32_t)data[offset + 1] << 8)
      | ((uint32_t)data[offset + 2] << 16)
      | ((uint32_t)data[offset + 3] << 24);
}

static uint64_t read_u64(const uint8_t *data, int offset)
{
   return (uint64_t)read_u32(data, offset)
      | ((uint64_t)read_u32(data, offset + 4) << 32);
}

static uint32_t swap_u32(uint32_t value)
{
   return ((value & 0x000000FF) << 24)
      | ((value & 0x0000FF00) << 8)
      | ((value & 0x00FF0000) >> 8)
      | ((value & 0xFF000000) >> 24);
}

static int astc_format_from_index(int index)
{
   static const int formats[] = {
      ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_4x4,
      ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_5x4,
      ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_5x5,
      ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_6x5,
      ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_6x6,
      ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_8x5,
      ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_8x6,
      ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_8x8,
      ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x5,
      ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x6,
      ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x8,
      ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x10,
      ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_12x10,
      ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_12x12
   };

   if (index < 0 || index >= (int)(sizeof(formats) / sizeof(formats[0])))
      return -1;
   return formats[index];
}

static int format_from_gl(uint32_t internal_format)
{
   if (internal_format >= 0x93B0 && internal_format <= 0x93BD)
      return astc_format_from_index((int)(internal_format - 0x93B0));
   if (internal_format >= 0x93D0 && internal_format <= 0x93DD)
      return astc_format_from_index((int)(internal_format - 0x93D0));

   switch (internal_format) {
      case GL_RGB_S3TC_DXT1_EXT:
      case GL_RGBA_S3TC_DXT1_EXT:
      case GL_COMPRESSED_SRGB_S3TC_DXT1_EXT:
      case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
         return ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC1;
      case GL_RGBA_S3TC_DXT3_EXT:
      case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
         return ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_DXT3;
      case GL_RGBA_S3TC_DXT5_EXT:
      case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
         return ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC3;
      case GL_RGBA_BPTC_UNORM_ARB:
      case GL_SRGB_ALPHA_BPTC_UNORM_ARB:
         return ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC7;
      default:
         return -1;
   }
}

static int format_from_vk(uint32_t vk_format)
{
   switch (vk_format) {
      case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
      case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
      case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
      case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
         return ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC1;
      case VK_FORMAT_BC3_UNORM_BLOCK:
      case VK_FORMAT_BC3_SRGB_BLOCK:
         return ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC3;
      case VK_FORMAT_BC7_UNORM_BLOCK:
      case VK_FORMAT_BC7_SRGB_BLOCK:
         return ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC7;
   }

   if (vk_format >= VK_FORMAT_ASTC_4x4_UNORM_BLOCK &&
         vk_format <= VK_FORMAT_ASTC_4x4_UNORM_BLOCK + 27) {
      return astc_format_from_index((int)((vk_format -
         VK_FORMAT_ASTC_4x4_UNORM_BLOCK) / 2));
   }

   return -1;
}

static int max_mipmap_levels(int w, int h)
{
   int levels = 1;
   while (w > 1 || h > 1) {
      w >>= 1;
      h >>= 1;
      levels++;
   }
   return levels;
}

static size_t level_size(int format, int w, int h)
{
   int bw = al_get_pixel_block_width(format);
   int bh = al_get_pixel_block_height(format);
   int bs = al_get_pixel_block_size(format);
   return (size_t)((w + bw - 1) / bw) * (size_t)((h + bh - 1) / bh) * bs;
}

static bool allocate_levels(int width, int height, int format, int levels,
   size_t **level_sizes_ret, uint8_t **data_ret, size_t *total_size_ret)
{
   size_t *sizes;
   size_t total = 0;
   int i;

   sizes = al_calloc(levels, sizeof(*sizes));
   if (!sizes) {
      ALLEGRO_ERROR("Out of memory reading KTX mipmap levels.\n");
      return false;
   }

   for (i = 0; i < levels; i++) {
      int level_w = width >> i;
      int level_h = height >> i;
      if (level_w < 1)
         level_w = 1;
      if (level_h < 1)
         level_h = 1;
      sizes[i] = level_size(format, level_w, level_h);
      if (sizes[i] > SIZE_MAX - total) {
         ALLEGRO_ERROR("KTX compressed data is too large.\n");
         al_free(sizes);
         return false;
      }
      total += sizes[i];
   }

   *data_ret = al_malloc(total);
   if (!*data_ret) {
      ALLEGRO_ERROR("Out of memory reading KTX data.\n");
      al_free(sizes);
      return false;
   }

   *level_sizes_ret = sizes;
   *total_size_ret = total;
   return true;
}

static ALLEGRO_BITMAP *load_ktx1(ALLEGRO_FILE *f)
{
   uint8_t header[KTX1_HEADER_SIZE];
   uint32_t endianness, gl_type, width, height, depth, array_elements;
   uint32_t faces, mipmap_levels, key_value_size;
   int swap, format, levels, i;
   size_t *level_sizes = NULL;
   uint8_t *data = NULL;
   size_t total_size;
   size_t offset = 0;
   ALLEGRO_BITMAP *bitmap = NULL;

   if (al_fread(f, header, sizeof(header)) != sizeof(header)) {
      ALLEGRO_ERROR("KTX header is truncated.\n");
      return NULL;
   }

   endianness = read_u32(header, 0);
   swap = endianness != 0x04030201;
   if (swap && endianness != 0x01020304) {
      ALLEGRO_ERROR("Invalid KTX byte order marker.\n");
      return NULL;
   }
#define KTX1_U32(off) (swap ? swap_u32(read_u32(header, (off))) : \
   read_u32(header, (off)))
   gl_type = KTX1_U32(4);
   width = KTX1_U32(24);
   height = KTX1_U32(28);
   depth = KTX1_U32(32);
   array_elements = KTX1_U32(36);
   faces = KTX1_U32(40);
   mipmap_levels = KTX1_U32(44);
   key_value_size = KTX1_U32(48);

   format = format_from_gl(KTX1_U32(16));
#undef KTX1_U32

   if (gl_type != 0) {
      ALLEGRO_ERROR("KTX file is not block-compressed.\n");
      return NULL;
   }
   if (width == 0 || height == 0 || width > INT_MAX || height > INT_MAX ||
         depth > 1 || array_elements > 1 || faces != 1) {
      ALLEGRO_ERROR("Unsupported KTX texture dimensions or layout.\n");
      return NULL;
   }
   if (format < 0) {
      ALLEGRO_ERROR("Unsupported KTX compressed pixel format.\n");
      return NULL;
   }

   if (mipmap_levels > INT_MAX) {
      ALLEGRO_ERROR("KTX declares too many mipmap levels.\n");
      return NULL;
   }
   levels = mipmap_levels ? (int)mipmap_levels : 1;
   if (levels > max_mipmap_levels((int)width, (int)height)) {
      ALLEGRO_ERROR("KTX declares too many mipmap levels.\n");
      return NULL;
   }

   if (key_value_size &&
         !al_fseek(f, key_value_size, ALLEGRO_SEEK_CUR)) {
      ALLEGRO_ERROR("Could not skip KTX key/value data.\n");
      return NULL;
   }

   if (!allocate_levels((int)width, (int)height, format, levels,
         &level_sizes, &data, &total_size)) {
      return NULL;
   }

   for (i = 0; i < levels; i++) {
      uint8_t size_bytes[4];
      uint32_t image_size;
      size_t padding;

      if (al_fread(f, size_bytes, 4) != 4) {
         ALLEGRO_ERROR("KTX mipmap level %d is truncated.\n", i);
         goto FAIL;
      }
      image_size = read_u32(size_bytes, 0);
      if (swap)
         image_size = swap_u32(image_size);
      if (image_size < level_sizes[i]) {
         ALLEGRO_ERROR("KTX mipmap level %d is too small.\n", i);
         goto FAIL;
      }
      if (al_fread(f, data + offset, level_sizes[i]) != level_sizes[i]) {
         ALLEGRO_ERROR("KTX mipmap level %d data is truncated.\n", i);
         goto FAIL;
      }
      offset += level_sizes[i];
      padding = image_size - level_sizes[i];
      padding += (4 - (image_size % 4)) % 4;
      if (padding && !al_fseek(f, (int64_t)padding, ALLEGRO_SEEK_CUR)) {
         ALLEGRO_ERROR("KTX mipmap level %d padding is truncated.\n", i);
         goto FAIL;
      }
   }

   bitmap = _al_create_compressed_bitmap((int)width, (int)height, format,
      levels, data, level_sizes);

FAIL:
   al_free(level_sizes);
   al_free(data);
   return bitmap;
}

static ALLEGRO_BITMAP *load_ktx2(ALLEGRO_FILE *f)
{
   uint8_t header[KTX2_HEADER_SIZE];
   uint32_t vk_format, width, height, depth, layers, faces;
   uint32_t supercompression, level_count;
   int format, levels, i;
   size_t *level_sizes = NULL;
   uint8_t *data = NULL;
   size_t total_size;
   size_t offset = 0;
   ALLEGRO_BITMAP *bitmap = NULL;

   if (al_fread(f, header, sizeof(header)) != sizeof(header)) {
      ALLEGRO_ERROR("KTX 2 header is truncated.\n");
      return NULL;
   }

   vk_format = read_u32(header, 12);
   width = read_u32(header, 20);
   height = read_u32(header, 24);
   depth = read_u32(header, 28);
   layers = read_u32(header, 32);
   faces = read_u32(header, 36);
   level_count = read_u32(header, 40);
   supercompression = read_u32(header, 44);

   format = format_from_vk(vk_format);
   if (format < 0) {
      ALLEGRO_ERROR("Unsupported KTX 2 compressed pixel format %u.\n",
         vk_format);
      return NULL;
   }
   if (width == 0 || height == 0 || width > INT_MAX || height > INT_MAX ||
         depth > 1 || layers > 1 || faces != 1) {
      ALLEGRO_ERROR("Unsupported KTX 2 texture dimensions or layout.\n");
      return NULL;
   }
   if (supercompression != 0) {
      ALLEGRO_ERROR("Supercompressed KTX 2 files are not supported.\n");
      return NULL;
   }

   if (level_count > INT_MAX) {
      ALLEGRO_ERROR("KTX 2 declares too many mipmap levels.\n");
      return NULL;
   }
   levels = level_count ? (int)level_count : 1;
   if (levels > max_mipmap_levels((int)width, (int)height)) {
      ALLEGRO_ERROR("KTX 2 declares too many mipmap levels.\n");
      return NULL;
   }

   if (!allocate_levels((int)width, (int)height, format, levels,
         &level_sizes, &data, &total_size)) {
      return NULL;
   }

   for (i = 0; i < levels; i++) {
      uint8_t index[KTX2_LEVEL_INDEX_SIZE];
      uint64_t level_offset, level_length;

      if (!al_fseek(f, KTX2_HEADER_SIZE + i * KTX2_LEVEL_INDEX_SIZE,
            ALLEGRO_SEEK_SET)) {
         ALLEGRO_ERROR("Could not seek to KTX 2 level index %d.\n", i);
         goto FAIL;
      }
      if (al_fread(f, index, sizeof(index)) != sizeof(index)) {
         ALLEGRO_ERROR("KTX 2 level index is truncated.\n");
         goto FAIL;
      }
      level_offset = read_u64(index, 0);
      level_length = read_u64(index, 8);
      if (level_offset > INT64_MAX || level_length < level_sizes[i]) {
         ALLEGRO_ERROR("KTX 2 mipmap level %d has invalid data.\n", i);
         goto FAIL;
      }
      if (!al_fseek(f, (int64_t)level_offset, ALLEGRO_SEEK_SET)) {
         ALLEGRO_ERROR("Could not seek to KTX 2 mipmap level %d.\n", i);
         goto FAIL;
      }
      if (al_fread(f, data + offset, level_sizes[i]) != level_sizes[i]) {
         ALLEGRO_ERROR("KTX 2 mipmap level %d data is truncated.\n", i);
         goto FAIL;
      }
      offset += level_sizes[i];
   }

   bitmap = _al_create_compressed_bitmap((int)width, (int)height, format,
      levels, data, level_sizes);

FAIL:
   al_free(level_sizes);
   al_free(data);
   return bitmap;
}

ALLEGRO_BITMAP *_al_load_ktx_f(ALLEGRO_FILE *f, int flags)
{
   uint8_t identifier[12];
   int64_t start;
   (void)flags;

   start = al_ftell(f);
   if (al_fread(f, identifier, sizeof(identifier)) != sizeof(identifier)) {
      ALLEGRO_ERROR("KTX identifier is truncated.\n");
      return NULL;
   }

   if (memcmp(identifier, "\xABKTX 11\xBB\r\n\x1A\n", 12) == 0) {
      return load_ktx1(f);
   }
   if (memcmp(identifier, "\xABKTX 20\xBB\r\n\x1A\n", 12) == 0) {
      if (!al_fseek(f, start, ALLEGRO_SEEK_SET))
         return NULL;
      return load_ktx2(f);
   }

   ALLEGRO_ERROR("Invalid KTX identifier.\n");
   return NULL;
}

ALLEGRO_BITMAP *_al_load_ktx(const char *filename, int flags)
{
   ALLEGRO_FILE *f;
   ALLEGRO_BITMAP *bitmap;

   ASSERT(filename);

   f = al_fopen(filename, "rb");
   if (!f) {
      ALLEGRO_ERROR("Unable to open %s for reading.\n", filename);
      return NULL;
   }

   bitmap = _al_load_ktx_f(f, flags);
   al_fclose(f);
   return bitmap;
}

bool _al_identify_ktx(ALLEGRO_FILE *f)
{
   uint8_t identifier[12];

   if (al_fread(f, identifier, sizeof(identifier)) != sizeof(identifier))
      return false;
   return memcmp(identifier, "\xABKTX 11\xBB\r\n\x1A\n", 12) == 0 ||
      memcmp(identifier, "\xABKTX 20\xBB\r\n\x1A\n", 12) == 0;
}

/* vim: set sts=3 sw=3 et: */
