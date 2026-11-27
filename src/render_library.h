#ifndef DIALOS_RENDER_LIBRARY_H
#define DIALOS_RENDER_LIBRARY_H

#include "library_controller.h"
#include "render.h"
#include "theme.h"

void render_draw_library_frame(const struct framebuffer_surface *surface,
                               const struct library_controller *controller,
                               enum appearance_mode appearance_mode, float back_swipe_progress);

void render_library_hub_transition_layer(const struct framebuffer_surface *surface,
                                         const struct library_controller *controller,
                                         enum appearance_mode appearance_mode,
                                         float reveal_progress);

#endif
