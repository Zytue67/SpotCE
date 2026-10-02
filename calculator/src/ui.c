#include "ui.h"

#include <graphx.h>
#include <stdio.h>
#include <string.h>

#define C_BG 0
#define C_PANEL 1
#define C_PANEL_HI 2
#define C_WHITE 3
#define C_MUTED 4
#define C_GREEN 5
#define C_TRACK 6
#define C_BORDER 7
#define C_RED 8
#define C_YELLOW 9
#define C_GRAD_START 10
#define UI_PALETTE_SIZE 32

static uint16_t applied_art_revision = 0xFFFF;
static uint8_t text_scale = 1;

static const uint8_t font_upper[26][7] = {
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
    {7,2,2,2,2,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};

static const uint8_t font_lower[26][7] = {
    {0,14,1,15,17,19,13},{16,16,30,17,17,17,30},{0,14,17,16,16,17,14},
    {1,1,15,17,17,19,13},{0,14,17,31,16,17,14},{6,9,8,28,8,8,8},
    {0,15,17,17,15,1,14},{16,16,30,17,17,17,17},{4,0,12,4,4,4,14},
    {2,0,6,2,2,18,12},{16,16,18,20,24,20,18},{12,4,4,4,4,4,14},
    {0,26,21,21,21,21,21},{0,30,17,17,17,17,17},{0,14,17,17,17,17,14},
    {0,30,17,17,30,16,16},{0,15,17,17,15,1,1},{0,22,25,16,16,16,16},
    {0,15,16,14,1,17,14},{8,8,28,8,8,9,6},{0,17,17,17,17,19,13},
    {0,17,17,17,17,10,4},{0,17,17,21,21,21,10},{0,17,10,4,10,17,17},
    {0,17,17,17,15,1,14},{0,31,2,4,8,16,31}
};

static const uint8_t font_digits[10][7] = {
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14}
};

static const uint8_t *font_glyph(char c) {
    static const uint8_t blank[7] = {0,0,0,0,0,0,0};
    static const uint8_t dash[7] = {0,0,0,31,0,0,0};
    static const uint8_t underscore[7] = {0,0,0,0,0,0,31};
    static const uint8_t period[7] = {0,0,0,0,0,12,12};
    static const uint8_t comma[7] = {0,0,0,0,0,12,8};
    static const uint8_t colon[7] = {0,12,12,0,12,12,0};
    static const uint8_t apostrophe[7] = {12,12,8,0,0,0,0};
    static const uint8_t exclamation[7] = {4,4,4,4,4,0,4};
    static const uint8_t question[7] = {14,17,1,2,4,0,4};
    static const uint8_t percent[7] = {17,2,4,8,17,0,0};
    static const uint8_t ampersand[7] = {12,18,20,8,21,18,13};
    static const uint8_t slash[7] = {1,2,2,4,8,8,16};
    static const uint8_t plus[7] = {0,4,4,31,4,4,0};
    static const uint8_t left_paren[7] = {2,4,8,8,8,4,2};
    static const uint8_t right_paren[7] = {8,4,2,2,2,4,8};
    if (c >= 'A' && c <= 'Z') return font_upper[c - 'A'];
    if (c >= 'a' && c <= 'z') return font_lower[c - 'a'];
    if (c >= '0' && c <= '9') return font_digits[c - '0'];
    switch (c) {
        case ' ': return blank;
        case '-': return dash;
        case '_': return underscore;
        case '.': return period;
        case ',': return comma;
        case ':': return colon;
        case '\'': return apostrophe;
        case '!': return exclamation;
        case '?': return question;
        case '%': return percent;
        case '&': return ampersand;
        case '/': return slash;
        case '+': return plus;
        case '(': return left_paren;
        case ')': return right_paren;
        default: return blank;
    }
}

static int text_width(const char *text) {
    return text ? (int)strlen(text) * 6 * text_scale : 0;
}

static void print_font_xy(const char *text, int x, int y) {
    if (!text) return;
    while (*text) {
        const uint8_t *glyph = font_glyph(*text++);
        unsigned row, col;
        for (row = 0; row < 7; ++row) {
            for (col = 0; col < 5; ++col) {
                if (glyph[row] & (1u << (4 - col)))
                    gfx_FillRectangle(x + (int)col * text_scale,
                                      y + (int)row * text_scale,
                                      text_scale, text_scale);
            }
        }
        x += 6 * text_scale;
    }
}

static void fill_round_rect(int x, int y, int width, int height, int radius,
                            uint8_t color) {
    if (width <= 0 || height <= 0) return;
    if (radius > (width - 1) / 2) radius = (width - 1) / 2;
    if (radius > (height - 1) / 2) radius = (height - 1) / 2;
    gfx_SetColor(color);
    if (radius == 0) {
        gfx_FillRectangle(x, y, width, height);
        return;
    }
    if (width > radius * 2)
        gfx_FillRectangle(x + radius, y, width - radius * 2, height);
    if (height > radius * 2) {
        gfx_FillRectangle(x, y + radius, radius, height - radius * 2);
        gfx_FillRectangle(x + width - radius, y + radius,
                          radius, height - radius * 2);
    }
    gfx_FillCircle(x + radius, y + radius, radius);
    gfx_FillCircle(x + width - radius - 1, y + radius, radius);
    gfx_FillCircle(x + radius, y + height - radius - 1, radius);
    gfx_FillCircle(x + width - radius - 1,
                   y + height - radius - 1, radius);
}

static void setup_palette(void) {
    uint16_t palette[UI_PALETTE_SIZE] = {
        gfx_RGBTo1555(10, 10, 10),       /* background */
        gfx_RGBTo1555(24, 24, 24),       /* Spotify card */
        gfx_RGBTo1555(40, 40, 40),       /* raised card */
        gfx_RGBTo1555(255, 255, 255),
        gfx_RGBTo1555(179, 179, 179),
        gfx_RGBTo1555(30, 215, 96),      /* Spotify green */
        gfx_RGBTo1555(83, 83, 83),
        gfx_RGBTo1555(56, 56, 56),
        gfx_RGBTo1555(235, 77, 75),
        gfx_RGBTo1555(238, 190, 72),
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    unsigned i;
    for (i = 0; i < UI_PALETTE_SIZE - C_GRAD_START; ++i) {
        unsigned steps = UI_PALETTE_SIZE - C_GRAD_START - 1;
        uint8_t r = (uint8_t)(28 - (18 * i) / steps);
        uint8_t g = (uint8_t)(74 - (64 * i) / steps);
        uint8_t b = (uint8_t)(49 - (39 * i) / steps);
        palette[C_GRAD_START + i] = gfx_RGBTo1555(r, g, b);
    }
    gfx_SetPalette(palette, sizeof(palette), 0);
}

static void set_text(uint8_t fg, uint8_t bg) {
    (void)bg;
    gfx_SetColor(fg);
}

static void print_ellipsized(const char *text, int x, int y, int max_width) {
    char buf[64];
    size_t len;

    if (!text || !*text) return;
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (text_width(buf) <= max_width) {
        print_font_xy(buf, x, y);
        return;
    }

    len = strlen(buf);
    while (len > 3) {
        --len;
        buf[len] = '\0';
        if (len >= 3) {
            buf[len - 3] = '.';
            buf[len - 2] = '.';
            buf[len - 1] = '.';
        }
        if (text_width(buf) <= max_width) {
            print_font_xy(buf, x, y);
            return;
        }
    }
}

static void print_title_two_lines(const char *text, int x, int y, int max_width) {
    char first[64];
    char second[64];
    size_t len, split, limit;

    if (!text || !*text) {
        print_ellipsized("Waiting for Spotify", x, y, max_width);
        return;
    }
    if (text_width(text) <= max_width) {
        print_font_xy(text, x, y);
        return;
    }

    len = strlen(text);
    limit = (size_t)(max_width / (6 * text_scale));
    split = limit < len ? limit : len;
    while (split > 4 && text[split] != ' ') --split;
    if (split <= 4) split = limit < len ? limit : len;
    if (split >= sizeof(first)) split = sizeof(first) - 1;
    memcpy(first, text, split);
    first[split] = '\0';
    while (text[split] == ' ') ++split;
    strncpy(second, text + split, sizeof(second) - 1);
    second[sizeof(second) - 1] = '\0';

    print_ellipsized(first, x, y, max_width);
    print_ellipsized(second, x, y + 8 * text_scale + 1, max_width);
}

static void format_time(uint32_t ms, char *buf, size_t len) {
    uint32_t total = ms / 1000;
    snprintf(buf, len, "%lu:%02lu", (unsigned long)(total / 60),
             (unsigned long)(total % 60));
}

static void draw_brand(void) {
    /* Small Spotify-green disc with the three-bar mark, drawn natively. */
    gfx_SetColor(C_GREEN);
    gfx_FillCircle(17, 15, 9);
    gfx_SetColor(C_BG);
    gfx_HorizLine(12, 12, 10);
    gfx_HorizLine(13, 15, 8);
    gfx_HorizLine(14, 18, 6);

    set_text(C_WHITE, C_PANEL);
    print_font_xy("SpotCE", 32, 11);
    set_text(C_MUTED, C_PANEL);
    print_font_xy("Spotify Player", 84, 11);
}

static void draw_status(const spotce_state_t *state) {
    const char *label;
    uint8_t color;

    if (state->usb_init_failed) {
        label = "USB ERROR";
        color = C_RED;
    } else if (state->bridge_ready) {
        label = "CONNECTED";
        color = C_GREEN;
    } else if (state->usb_connected) {
        label = "WAITING";
        color = C_YELLOW;
    } else {
        label = "NO USB";
        color = C_RED;
    }

    gfx_SetColor(color);
    gfx_FillCircle(235, 15, 3);
    set_text(color, C_PANEL);
    print_font_xy(label, 244, 11);
}

static void draw_cover(const spotce_state_t *state) {
    const int x = 18;
    const int y = 53;

    /* Layered edge and drop shadow give the artwork a Spotify-style card. */
    gfx_SetColor(C_BG);
    gfx_FillRectangle(x + 3, y + 4, SPOTCE_ART_W + 4, SPOTCE_ART_H + 4);
    gfx_SetColor(C_BORDER);
    gfx_FillRectangle(x - 2, y - 2, SPOTCE_ART_W + 4, SPOTCE_ART_H + 4);

    if (state->art_valid) {
        if (applied_art_revision != state->art_revision) {
            gfx_SetPalette(state->art_palette, SPOTCE_ART_COLORS * 2,
                           SPOTCE_ART_PALETTE_OFFSET);
            applied_art_revision = state->art_revision;
        }
        gfx_Sprite((gfx_sprite_t *)state->art_sprite, x, y);
    } else {
        gfx_SetColor(C_PANEL_HI);
        gfx_FillRectangle(x, y, SPOTCE_ART_W, SPOTCE_ART_H);
        gfx_SetColor(C_GREEN);
        gfx_FillCircle(x + 40, y + 34, 15);
        gfx_SetColor(C_BG);
        gfx_FillTriangle(x + 36, y + 25, x + 36, y + 43, x + 48, y + 34);
        set_text(C_MUTED, C_PANEL_HI);
        print_font_xy("No cover", x + 18, y + 61);
    }
}

static void draw_playback_badge(const spotce_state_t *state) {
    const char *label = "BRIDGE OFF";
    uint8_t color = C_MUTED;

    if (state->usb_init_failed) {
        label = "USB ERROR";
        color = C_RED;
    } else if (!state->bridge_ready) {
        label = state->usb_connected ? "START BRIDGE ON MAC" : "CONNECT USB + START BRIDGE";
        color = state->usb_connected ? C_YELLOW : C_MUTED;
    } else if (!state->spotify_running) {
        label = "SPOTIFY NOT RUNNING";
        color = C_RED;
    } else if (state->player_state == PLAYER_ERROR) {
        label = "SPOTIFY CHECK MAC";
        color = C_YELLOW;
    } else if (state->player_state == PLAYER_PLAYING) {
        label = "PLAYING ON THIS MAC";
        color = C_GREEN;
    } else if (state->player_state == PLAYER_PAUSED) {
        label = "PAUSED";
        color = C_YELLOW;
    } else {
        label = "SPOTIFY READY";
        color = C_MUTED;
    }

    fill_round_rect(112, 122, 194, 18, 9, C_PANEL_HI);
    gfx_SetColor(color);
    gfx_FillCircle(121, 131, 3);
    set_text(color, C_PANEL_HI);
    print_ellipsized(label, 130, 127, 168);
}

static void draw_progress(const spotce_state_t *state) {
    const int x = 18;
    const int y = 154;
    const int w = 284;
    uint32_t filled = 0;
    char left[16], right[16];

    if (state->duration_ms) {
        filled = (state->position_ms * (uint32_t)w) / state->duration_ms;
        if (filled > (uint32_t)w) filled = (uint32_t)w;
    }
    fill_round_rect(x, y, w, 6, 3, C_TRACK);
    if (filled) fill_round_rect(x, y, (int)filled, 6, 3, C_GREEN);
    if (state->duration_ms) {
        gfx_SetColor(C_WHITE);
        gfx_FillCircle(x + (int)filled, y + 3, 4);
    }

    format_time(state->position_ms, left, sizeof(left));
    format_time(state->duration_ms, right, sizeof(right));
    set_text(C_MUTED, C_BG);
    print_font_xy(left, x, 164);
    print_font_xy(right, x + w - text_width(right), 164);
}

static void draw_previous_icon(int cx, int cy, bool pressed) {
    gfx_SetColor(pressed ? C_BG : C_WHITE);
    gfx_VertLine(cx - 7, cy - 4, 8);
    gfx_FillTriangle(cx + 1, cy - 4, cx - 5, cy, cx + 1, cy + 4);
    gfx_FillTriangle(cx + 7, cy - 4, cx + 1, cy, cx + 7, cy + 4);
}

static void draw_next_icon(int cx, int cy, bool pressed) {
    gfx_SetColor(pressed ? C_BG : C_WHITE);
    gfx_FillTriangle(cx - 7, cy - 4, cx - 1, cy, cx - 7, cy + 4);
    gfx_FillTriangle(cx - 1, cy - 4, cx + 5, cy, cx - 1, cy + 4);
    gfx_VertLine(cx + 7, cy - 4, 8);
}

static void draw_play_icon(const spotce_state_t *state, bool pressed) {
    gfx_SetColor(pressed ? C_WHITE : C_GREEN);
    gfx_FillCircle(160, 196, 14);
    gfx_SetColor(pressed ? C_GREEN : C_BG);
    if (state->player_state == PLAYER_PLAYING) {
        gfx_FillRectangle(155, 190, 3, 12);
        gfx_FillRectangle(162, 190, 3, 12);
    } else {
        gfx_FillTriangle(157, 190, 157, 202, 166, 196);
    }
}

static void draw_controls(const spotce_state_t *state, uint8_t pressed_control) {
    fill_round_rect(18, 181, 284, 34, 7, C_BORDER);
    fill_round_rect(19, 182, 282, 32, 6, C_PANEL);

    fill_round_rect(86, 186, 38, 20, 10,
                    pressed_control == SPOTCE_CONTROL_PREVIOUS ? C_GREEN : C_BORDER);
    fill_round_rect(87, 187, 36, 18, 9,
                    pressed_control == SPOTCE_CONTROL_PREVIOUS ? C_GREEN : C_PANEL_HI);
    fill_round_rect(196, 186, 38, 20, 10,
                    pressed_control == SPOTCE_CONTROL_NEXT ? C_GREEN : C_BORDER);
    fill_round_rect(197, 187, 36, 18, 9,
                    pressed_control == SPOTCE_CONTROL_NEXT ? C_GREEN : C_PANEL_HI);

    draw_previous_icon(105, 196, pressed_control == SPOTCE_CONTROL_PREVIOUS);
    draw_play_icon(state, pressed_control == SPOTCE_CONTROL_PLAY_PAUSE);
    draw_next_icon(215, 196, pressed_control == SPOTCE_CONTROL_NEXT);
}

static void draw_volume(const spotce_state_t *state) {
    const int x = 18;
    const int bar_x = 60;
    const int bar_w = 204;
    int fill = ((int)state->volume * bar_w) / 100;
    char vol[8];

    set_text(C_MUTED, C_BG);
    print_font_xy("Vol", x, 226);
    fill_round_rect(bar_x, 228, bar_w, 6, 3, C_TRACK);
    if (fill) fill_round_rect(bar_x, 228, fill, 6, 3, C_GREEN);
    snprintf(vol, sizeof(vol), "%u%%", state->volume);
    set_text(C_WHITE, C_BG);
    print_font_xy(vol, 274, 226);
}

void spotce_ui_init(void) {
    gfx_Begin();
    gfx_SetDrawBuffer();
    setup_palette();
    text_scale = 1;
    applied_art_revision = 0xFFFF;
}

void spotce_ui_draw(const spotce_state_t *state, uint8_t pressed_control) {
    int y;

    gfx_FillScreen(C_BG);

    /* Smooth vertical green-to-charcoal fade behind the player card. */
    for (y = 31; y < 148; ++y) {
        uint8_t shade = (uint8_t)(C_GRAD_START + ((y - 31) * 21) / 116);
        gfx_SetColor(shade);
        gfx_HorizLine(0, y, 320);
    }

    gfx_SetColor(C_PANEL);
    gfx_FillRectangle(0, 0, 320, 31);
    gfx_SetColor(C_BORDER);
    gfx_HorizLine(0, 30, 320);
    draw_brand();
    draw_status(state);

    set_text(C_MUTED, C_BG);
    print_font_xy("Now playing", 112, 43);
    draw_cover(state);

    set_text(C_WHITE, C_BG);
    text_scale = 2;
    print_title_two_lines(state->title, 112, 62, 190);
    text_scale = 1;
    set_text(C_MUTED, C_BG);
    print_ellipsized(state->artist, 112, 99, 190);
    draw_playback_badge(state);

    draw_progress(state);
    draw_controls(state, pressed_control);
    draw_volume(state);

    gfx_SwapDraw();
}

void spotce_ui_cleanup(void) {
    gfx_End();
}
