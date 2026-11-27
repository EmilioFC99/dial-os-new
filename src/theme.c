#include "theme.h"

static const struct appearance_palette DARK_MODE_PALETTE = {
    .canvas = 0xFF060709u,
    .surface = 0xFF12151Bu,
    .hairline = 0xFF232833u,
    .hairline_strong = 0xFF485060u,
    .ink_primary = 0xFFF5F6F8u,
    .ink_secondary = 0xFF8D96A6u,
    .signal_highlight = 0xFFC0FE05u,
    .signal_highlight_ink = 0xFF060709u,
    .signal_alert = 0xFFFF2E17u,
    .signal_alert_ink = 0xFFFFFFFFu,
    .cube_highlight_faces = {
        .top = 0xFFDAFF66u,
        .right = 0xFFC0FE05u,
        .left = 0xFF8BB803u,
    },
    .cube_ink_faces = {
        .top = 0xFFFFFFFFu,
        .right = 0xFFD6DCE6u,
        .left = 0xFF8E97A6u,
    },
    .cube_wireframe = 0xFF748094u,
    .cube_wireframe_alpha = 87u,
    .cube_edge_stroke = 0xFF060709u,
};

static const struct appearance_palette LIGHT_MODE_PALETTE = {
    .canvas = 0xFFE6E8EBu,
    .surface = 0xFFD6D9DFu,
    .hairline = 0xFFC0C5CEu,
    .hairline_strong = 0xFF828A96u,
    .ink_primary = 0xFF2E3238u,
    .ink_secondary = 0xFF626A76u,
    .signal_highlight = 0xFFE85A24u,
    .signal_highlight_ink = 0xFFFFFFFFu,
    .signal_alert = 0xFFCC3124u,
    .signal_alert_ink = 0xFFFFFFFFu,
    .cube_highlight_faces = {
        .top = 0xFFF27B4Cu,
        .right = 0xFFE85A24u,
        .left = 0xFFB84316u,
    },
    .cube_ink_faces = {
        .top = 0xFF4E545Eu,
        .right = 0xFF343940u,
        .left = 0xFF22252Bu,
    },
    .cube_wireframe = 0xFF727A86u,
    .cube_wireframe_alpha = 97u,
    .cube_edge_stroke = 0xFFE6E8EBu,
};

const struct appearance_palette *theme_get_palette(enum appearance_mode mode) {
    if (mode == APPEARANCE_MODE_LIGHT) {
        return &LIGHT_MODE_PALETTE;
    }
    return &DARK_MODE_PALETTE;
}
