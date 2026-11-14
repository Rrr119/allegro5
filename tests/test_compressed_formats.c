#include <stdio.h>
#include <string.h>

#include "allegro5/allegro.h"
#include "allegro5/allegro_image.h"
#include "allegro5/allegro_opengl.h"

typedef struct TEST_TEXTURE {
   const char *filename;
   int format;
   int block_width;
   int block_height;
   int block_size;
} TEST_TEXTURE;

static const TEST_TEXTURE textures[] = {
   { "test_bc1.dds", ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC1, 4, 4, 8 },
   { "test_bc3.dds", ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC3, 4, 4, 16 },
   { "test_bc7.dds", ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC7, 4, 4, 16 },
   { "test_astc_4x4.dds", ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_4x4, 4, 4, 16 },
   { "test_bc1.ktx", ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC1, 4, 4, 8 },
   { "test_bc3.ktx", ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC3, 4, 4, 16 },
   { "test_bc7.ktx", ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC7, 4, 4, 16 },
   { "test_astc_4x4.ktx", ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_4x4, 4, 4, 16 },
   { "test_astc_6x6.ktx", ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_6x6, 6, 6, 16 },
   { "test_bc7.ktx2", ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC7, 4, 4, 16 },
   { "test_astc_6x6.ktx2", ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_6x6, 6, 6, 16 }
};

static bool format_supported(int format)
{
   ALLEGRO_DISPLAY *display = al_get_current_display();
   bool opengl_es = display &&
      (al_get_display_flags(display) & ALLEGRO_OPENGL_ES_PROFILE);
   uint32_t version = al_get_opengl_version();

   switch (format) {
      case ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC1:
      case ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC3:
         return al_have_opengl_extension("GL_EXT_texture_compression_s3tc");
      case ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_BC7:
         return al_have_opengl_extension("GL_ARB_texture_compression_bptc") ||
            al_have_opengl_extension("GL_EXT_texture_compression_bptc") ||
            version >= 0x04020000;
      default:
         return al_have_opengl_extension("GL_KHR_texture_compression_astc_ldr") ||
            al_have_opengl_extension("GL_OES_texture_compression_astc") ||
            (opengl_es && version >= 0x03020000);
   }
}

static int level_size(const TEST_TEXTURE *texture, int w, int h)
{
   return ((w + texture->block_width - 1) / texture->block_width)
      * ((h + texture->block_height - 1) / texture->block_height)
      * texture->block_size;
}

int main(int argc, char **argv)
{
   const char *data_dir = argc > 1 ? argv[1] : "../examples/data";
   ALLEGRO_DISPLAY *display;
   char path[1024];
   int failed = 0;
   int skipped = 0;
   unsigned i;

   if (!al_init() || !al_init_image_addon()) {
      fprintf(stderr, "Could not initialize Allegro.\n");
      return 1;
   }

   display = al_create_display(64, 64);
   if (!display) {
      printf("SKIP: could not create a display.\n");
      return 0;
   }
   if (!(al_get_display_flags(display) & ALLEGRO_OPENGL)) {
      printf("SKIP: compressed texture upload test requires OpenGL.\n");
      al_destroy_display(display);
      return 0;
   }

   for (i = 0; i < sizeof(textures) / sizeof(textures[0]); i++) {
      const TEST_TEXTURE *texture = &textures[i];
      ALLEGRO_BITMAP *bitmap;
      GLuint gl_texture;
      GLint max_level = 0;
      int level;

      snprintf(path, sizeof(path), "%s/%s", data_dir, texture->filename);
      if (!format_supported(texture->format)) {
         printf("SKIP %s (format not supported)\n", texture->filename);
         skipped++;
         continue;
      }

      bitmap = al_load_bitmap(path);
      if (!bitmap) {
         printf("FAIL %s (load)\n", texture->filename);
         failed++;
         continue;
      }

      if (al_get_bitmap_format(bitmap) != texture->format ||
            al_get_bitmap_width(bitmap) != 16 ||
            al_get_bitmap_height(bitmap) != 16 ||
            !(al_get_bitmap_flags(bitmap) & ALLEGRO_MIPMAP)) {
         printf("FAIL %s (metadata)\n", texture->filename);
         al_destroy_bitmap(bitmap);
         failed++;
         continue;
      }

      gl_texture = al_get_opengl_texture(bitmap);
      glBindTexture(GL_TEXTURE_2D, gl_texture);
      glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &max_level);
      if (!gl_texture || max_level != 4) {
         printf("FAIL %s (mipmap levels: %d)\n", texture->filename, max_level);
         al_destroy_bitmap(bitmap);
         failed++;
         continue;
      }

      for (level = 0; level < 5; level++) {
         GLint size = 0;
         int w = 16 >> level;
         int h = 16 >> level;
         if (w < 1)
            w = 1;
         if (h < 1)
            h = 1;
         glGetTexLevelParameteriv(GL_TEXTURE_2D, level,
            GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &size);
         if (size != level_size(texture, w, h)) {
            printf("FAIL %s (mipmap %d size: %d)\n", texture->filename,
               level, size);
            failed++;
            break;
         }
      }

      if (level == 5)
         printf("OK   %s\n", texture->filename);
      al_destroy_bitmap(bitmap);
   }

   al_destroy_display(display);
   printf("%u passed, %d skipped, %d failed\n",
      (unsigned)(sizeof(textures) / sizeof(textures[0])) - failed - skipped,
      skipped, failed);
   return failed ? 1 : 0;
}

/* vim: set sts=3 sw=3 et: */
