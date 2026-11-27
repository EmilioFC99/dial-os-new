#ifndef DIALOS_RENDER_HOME_H
#define DIALOS_RENDER_HOME_H

#include "home_screen.h"
#include "library_controller.h"
#include "render.h"

void render_draw_home_frame(const struct framebuffer_surface *surface,
                            const struct home_render_snapshot *snapshot);
void render_draw_home_with_library_frame(const struct framebuffer_surface *surface,
                                         const struct home_render_snapshot *snapshot,
                                         const struct library_controller *library);

#endif
