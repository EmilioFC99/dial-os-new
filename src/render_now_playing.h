#ifndef DIALOS_RENDER_NOW_PLAYING_H
#define DIALOS_RENDER_NOW_PLAYING_H

#include "library_cover_cache.h"
#include "now_playing_controller.h"
#include "render.h"
#include "theme.h"

void render_draw_now_playing_frame(const struct framebuffer_surface *surface,
                                   struct now_playing_controller *controller,
                                   struct library_cover_cache *cover_cache,
                                   enum appearance_mode appearance_mode);

#endif
