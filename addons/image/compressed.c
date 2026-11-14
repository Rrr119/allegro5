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
 *      Helpers for loading compressed textures.
 *
 *      See LICENSE.txt for copyright information.
 */

#include "allegro5/allegro.h"

#include "compressed.h"

ALLEGRO_DEBUG_CHANNEL("image")

bool _al_upload_compressed_bitmap(ALLEGRO_BITMAP *bitmap, int levels,
   const void *data, const size_t *level_sizes);

ALLEGRO_BITMAP *_al_create_compressed_bitmap(int w, int h, int format,
   int levels, const void *data, const size_t *level_sizes)
{
   ALLEGRO_STATE state;
   ALLEGRO_BITMAP *bitmap;
   int flags;

   al_store_state(&state, ALLEGRO_STATE_NEW_BITMAP_PARAMETERS);

   flags = al_get_new_bitmap_flags();
   flags &= ~ALLEGRO_MEMORY_BITMAP;
   flags |= ALLEGRO_VIDEO_BITMAP | ALLEGRO_NO_PRESERVE_TEXTURE;
   if (levels > 1)
      flags |= ALLEGRO_MIPMAP;
   al_set_new_bitmap_flags(flags);
   al_set_new_bitmap_format(format);

   bitmap = al_create_bitmap(w, h);
   if (!bitmap) {
      ALLEGRO_ERROR("Failed to create %dx%d compressed bitmap.\n", w, h);
      goto DONE;
   }

   if (al_get_bitmap_format(bitmap) != format) {
      ALLEGRO_ERROR("Created bitmap with unexpected compressed format.\n");
      al_destroy_bitmap(bitmap);
      bitmap = NULL;
      goto DONE;
   }

   if (!_al_upload_compressed_bitmap(bitmap, levels, data, level_sizes)) {
      ALLEGRO_ERROR("Failed to upload compressed bitmap data.\n");
      al_destroy_bitmap(bitmap);
      bitmap = NULL;
   }

DONE:
   al_restore_state(&state);
   return bitmap;
}

/* vim: set sts=3 sw=3 et: */
