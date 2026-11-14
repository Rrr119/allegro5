#include <stdio.h>
#include "allegro5/allegro.h"
#include "allegro5/allegro_font.h"
#include "allegro5/allegro_image.h"

#include "common.c"

static const char *default_textures[] = {
   "data/test_bc1.dds",
   "data/test_bc3.dds",
   "data/test_bc7.dds",
   "data/test_astc_4x4.ktx"
};

static void draw_label(ALLEGRO_FONT *font, const char *label,
   ALLEGRO_BITMAP *bitmap, int x, int y, int w, int h)
{
   al_draw_textf(font, al_map_rgb_f(1, 1, 1), x + 4, y + 4, 0,
      "%s: %dx%d", label, al_get_bitmap_width(bitmap),
      al_get_bitmap_height(bitmap));
   al_draw_scaled_bitmap(bitmap, 0, 0,
      al_get_bitmap_width(bitmap), al_get_bitmap_height(bitmap),
      x, y + 20, w, h, 0);
}

int main(int argc, char **argv)
{
   ALLEGRO_DISPLAY *display;
   ALLEGRO_FONT *font;
   ALLEGRO_EVENT_QUEUE *queue;
   ALLEGRO_BITMAP *textures[4];
   int count, i;

   if (!al_init()) {
      abort_example("Could not init Allegro.\n");
   }

   open_log();
   al_init_image_addon();
   al_init_font_addon();
   al_install_keyboard();

   display = al_create_display(800, 600);
   if (!display) {
      abort_example("Error creating display.\n");
   }

   al_set_new_bitmap_flags(ALLEGRO_VIDEO_BITMAP |
      ALLEGRO_MIN_LINEAR | ALLEGRO_MAG_LINEAR);

   count = argc > 1 ? argc - 1 : 4;
   if (count > 4)
      count = 4;
   for (i = 0; i < count; i++) {
      const char *filename = argc > 1 ? argv[i + 1] : default_textures[i];
      textures[i] = al_load_bitmap(filename);
      if (!textures[i]) {
         log_printf("Could not load %s\n", filename);
         abort_example("Could not load compressed texture %s.\n", filename);
      }
      log_printf("Loaded %s as format %d, flags 0x%x\n", filename,
         al_get_bitmap_format(textures[i]), al_get_bitmap_flags(textures[i]));
   }

   font = al_create_builtin_font();
   queue = al_create_event_queue();
   al_register_event_source(queue, al_get_display_event_source(display));
   al_register_event_source(queue, al_get_keyboard_event_source());

   while (true) {
      ALLEGRO_EVENT event;
      al_clear_to_color(al_map_rgb_f(0.08, 0.08, 0.08));
      for (i = 0; i < count; i++) {
         int x = (i % 2) * 400;
         int y = (i / 2) * 300;
         draw_label(font, argc > 1 ? argv[i + 1] : i == 0 ? "BC1/DDS" :
            i == 1 ? "BC3/DDS" : i == 2 ? "BC7/DDS" : "ASTC/KTX",
            textures[i], x, y, 400, 280);
      }
      al_draw_text(font, al_map_rgb_f(0.7, 0.7, 0.7), 400, 580,
         ALLEGRO_ALIGN_CENTER,
         "Imported mip chain, uploaded directly without CPU decompression");
      al_flip_display();

      al_wait_for_event(queue, &event);
      if (event.type == ALLEGRO_EVENT_DISPLAY_CLOSE ||
            (event.type == ALLEGRO_EVENT_KEY_DOWN &&
             event.keyboard.keycode == ALLEGRO_KEY_ESCAPE)) {
         break;
      }
   }

   for (i = 0; i < count; i++)
      al_destroy_bitmap(textures[i]);
   al_destroy_event_queue(queue);
   al_destroy_font(font);
   al_destroy_display(display);
   close_log(false);
   return 0;
}

/* vim: set sts=4 sw=4 et: */
