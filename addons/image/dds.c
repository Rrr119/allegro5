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
 *      DDS reader for BC1, BC3, BC7 and ASTC textures.
 *
 *      See LICENSE.txt for copyright information.
 */

#include "allegro5/allegro.h"
#include "allegro5/allegro_image.h"
#include "allegro5/internal/aintern_image.h"

#include "compressed.h"
#include "iio.h"

ALLEGRO_DEBUG_CHANNEL("image")

#define DDS_MAGIC 0x20534444
#define DDS_HEADER_SIZE 124
#define DDS_PIXELFORMAT_SIZE 32

#define DDS_FOURCC(c0, c1, c2, c3) ((uint32_t)(c0) | \
   ((uint32_t)(c1) << 8) | ((uint32_t)(c2) << 16) | ((uint32_t)(c3) << 24))

#define DDPF_FOURCC 0x4
#define DDSCAPS2_CUBEMAP 0x200
#define DDSCAPS2_VOLUME 0x200000

#define DX10_HEADER_SIZE 20
#define D3D10_RESOURCE_DIMENSION_TEXTURE2D 3

static uint32_t read_u32(const uint8_t *data, int offset)
{
   return (uint32_t)data[offset]
      | ((uint32_t)data[offset + 1] << 8)
      | ((uint32_t)data[offset + 2] << 16)
      | ((uint32_t)data[offset + 3] << 24);
}

static int format_from_dxgi(uint32_t dxgi_format)
{
   static const int astc_formats[] = {
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
   int i;

   switch (dxgi_format) {
      case 70: /* BC1_TYPELESS */
      case 71: /* BC1_UNORM */
      case 72: /* BC1_UNORM_SRGB */
         return ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC1;
      case 76: /* BC3_TYPELESS */
      case 77: /* BC3_UNORM */
      case 78: /* BC3_UNORM_SRGB */
         return ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC3;
      case 97: /* BC7_TYPELESS */
      case 98: /* BC7_UNORM */
      case 99: /* BC7_UNORM_SRGB */
         return ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC7;
   }

   /* ASTC uses a reserved, non-standard block of DXGI values. */
   for (i = 0; i < (int)(sizeof(astc_formats) / sizeof(astc_formats[0])); i++) {
      uint32_t first = 133 + i * 4;
      if (dxgi_format == first || dxgi_format == first + 1 ||
            dxgi_format == first + 2) {
         return astc_formats[i];
      }
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

ALLEGRO_BITMAP *_al_load_dds_f(ALLEGRO_FILE *f, int flags)
{
   uint8_t header[DDS_HEADER_SIZE];
   uint8_t dx10[DX10_HEADER_SIZE];
   uint32_t width, height, mipmap_count, fourcc;
   uint32_t caps2, pf_flags;
   int format, levels, i;
   int block_width, block_height, block_size;
   size_t total_size = 0;
   size_t *level_sizes = NULL;
   uint8_t *data = NULL;
   size_t offset = 0;
   ALLEGRO_BITMAP *bitmap = NULL;
   (void)flags;

   if (al_fread32le(f) != DDS_MAGIC) {
      ALLEGRO_ERROR("Invalid DDS magic number.\n");
      return NULL;
   }

   if (al_fread(f, header, sizeof(header)) != sizeof(header)) {
      ALLEGRO_ERROR("DDS header is truncated.\n");
      return NULL;
   }
   if (read_u32(header, 0) != DDS_HEADER_SIZE ||
         read_u32(header, 72) != DDS_PIXELFORMAT_SIZE) {
      ALLEGRO_ERROR("Invalid DDS header size.\n");
      return NULL;
   }

   height = read_u32(header, 8);
   width = read_u32(header, 12);
   mipmap_count = read_u32(header, 24);
   pf_flags = read_u32(header, 76);
   fourcc = read_u32(header, 80);
   caps2 = read_u32(header, 108);

   if (width == 0 || height == 0 || width > INT_MAX || height > INT_MAX) {
      ALLEGRO_ERROR("Invalid DDS dimensions: %ux%u.\n", width, height);
      return NULL;
   }
   if (caps2 & (DDSCAPS2_CUBEMAP | DDSCAPS2_VOLUME)) {
      ALLEGRO_ERROR("DDS cube maps and volume textures are not supported.\n");
      return NULL;
   }
   if (!(pf_flags & DDPF_FOURCC)) {
      ALLEGRO_ERROR("Only compressed DDS formats are supported.\n");
      return NULL;
   }

   if (fourcc == DDS_FOURCC('D', 'X', '1', '0')) {
      uint32_t dxgi_format, resource_dimension, array_size, misc_flag;
      if (al_fread(f, dx10, sizeof(dx10)) != sizeof(dx10)) {
         ALLEGRO_ERROR("DDS DX10 header is truncated.\n");
         return NULL;
      }
      dxgi_format = read_u32(dx10, 0);
      resource_dimension = read_u32(dx10, 4);
      array_size = read_u32(dx10, 12);
      misc_flag = read_u32(dx10, 8);
      if (resource_dimension != D3D10_RESOURCE_DIMENSION_TEXTURE2D ||
            array_size > 1 || (misc_flag & 0x4)) {
         ALLEGRO_ERROR("DDS texture arrays and cube maps are not supported.\n");
         return NULL;
      }
      format = format_from_dxgi(dxgi_format);
   }
   else {
      switch (fourcc) {
         case DDS_FOURCC('D', 'X', 'T', '1'):
         case DDS_FOURCC('B', 'C', '1', ' '):
            format = ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC1;
            break;
         case DDS_FOURCC('D', 'X', 'T', '2'):
         case DDS_FOURCC('D', 'X', 'T', '3'):
            format = ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_DXT3;
            break;
         case DDS_FOURCC('D', 'X', 'T', '4'):
         case DDS_FOURCC('D', 'X', 'T', '5'):
         case DDS_FOURCC('B', 'C', '3', ' '):
            format = ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC3;
            break;
         default:
            format = -1;
      }
   }

   if (format < 0) {
      ALLEGRO_ERROR("Unsupported DDS compressed pixel format.\n");
      return NULL;
   }

   if (mipmap_count > INT_MAX) {
      ALLEGRO_ERROR("DDS declares too many mipmap levels.\n");
      return NULL;
   }
   levels = mipmap_count ? (int)mipmap_count : 1;
   if (levels > max_mipmap_levels((int)width, (int)height)) {
      ALLEGRO_ERROR("DDS declares too many mipmap levels.\n");
      return NULL;
   }

   block_width = al_get_pixel_block_width(format);
   block_height = al_get_pixel_block_height(format);
   block_size = al_get_pixel_block_size(format);

   level_sizes = al_calloc(levels, sizeof(*level_sizes));
   if (!level_sizes) {
      ALLEGRO_ERROR("Out of memory reading DDS mipmap levels.\n");
      goto FAIL;
   }

   for (i = 0; i < levels; i++) {
      int level_w = (int)width >> i;
      int level_h = (int)height >> i;
      size_t level_size;
      if (level_w < 1)
         level_w = 1;
      if (level_h < 1)
         level_h = 1;
      level_size = (size_t)((level_w + block_width - 1) / block_width)
         * (size_t)((level_h + block_height - 1) / block_height)
         * block_size;
      if (level_size > SIZE_MAX - total_size) {
         ALLEGRO_ERROR("DDS compressed data is too large.\n");
         goto FAIL;
      }
      level_sizes[i] = level_size;
      total_size += level_size;
   }

   data = al_malloc(total_size);
   if (!data) {
      ALLEGRO_ERROR("Out of memory reading DDS data.\n");
      goto FAIL;
   }

   for (i = 0; i < levels; i++) {
      if (al_fread(f, data + offset, level_sizes[i]) != level_sizes[i]) {
         ALLEGRO_ERROR("DDS file is truncated in mipmap level %d.\n", i);
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

ALLEGRO_BITMAP *_al_load_dds(const char *filename, int flags)
{
   ALLEGRO_FILE *f;
   ALLEGRO_BITMAP *bitmap;

   ASSERT(filename);

   f = al_fopen(filename, "rb");
   if (!f) {
      ALLEGRO_ERROR("Unable to open %s for reading.\n", filename);
      return NULL;
   }

   bitmap = _al_load_dds_f(f, flags);
   al_fclose(f);
   return bitmap;
}

bool _al_identify_dds(ALLEGRO_FILE *f)
{
   uint8_t x[4];
   uint32_t y;

   if (al_fread(f, x, 4) != 4)
      return false;
   if (memcmp(x, "DDS ", 4) != 0)
      return false;
   y = (uint32_t)al_fread32le(f);
   return y == 124;
}

/* vim: set sts=3 sw=3 et: */
