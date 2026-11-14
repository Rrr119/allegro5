#ifndef __al_included_aintern_image_compressed_h
#define __al_included_aintern_image_compressed_h

#include "allegro5/allegro.h"

#ifdef __cplusplus
extern "C" {
#endif

ALLEGRO_BITMAP *_al_create_compressed_bitmap(int w, int h, int format,
   int levels, const void *data, const size_t *level_sizes);

#ifdef __cplusplus
}
#endif

#endif
