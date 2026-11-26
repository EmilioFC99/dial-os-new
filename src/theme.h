#ifndef DIALOS_THEME_H
#define DIALOS_THEME_H

#include <stdint.h>

enum appearance_mode {
    APPEARANCE_MODE_DARK = 0,
    APPEARANCE_MODE_LIGHT = 1
};

struct cube_face_palette {
    uint32_t top;
    uint32_t right;
    uint32_t left;
};

struct appearance_palette {
    uint32_t canvas;
    uint32_t surface;
    uint32_t hairline;
    uint32_t hairline_strong;
    uint32_t ink_primary;
    uint32_t ink_secondary;
    uint32_t signal_highlight;
    uint32_t signal_highlight_ink;
    uint32_t signal_alert;
    uint32_t signal_alert_ink;
    struct cube_face_palette cube_highlight_faces;
    struct cube_face_palette cube_ink_faces;
    uint32_t cube_wireframe;
    uint8_t cube_wireframe_alpha;
    uint32_t cube_edge_stroke;
};

const struct appearance_palette *theme_get_palette(enum appearance_mode mode);

#endif
