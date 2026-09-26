/* Colour emoji for the text fonts: ui_font_<size> falls back to ui_emoji_<size>
 * (generate.sh sets the fallback), which draws the Twemoji images of
 * ui_emoji_data.c (emoji.py). One 16 px image set; each font only centres it on
 * its own line, which is why there is one lv_font_t per text size.
 *
 * LVGL doesn't shape text, so a sequence draws as its parts: the joiners and
 * selectors between them take no space, a skin tone is dropped (the plain
 * emoji stays). Flags are swapped in before drawing (ui_emoji_flags). */
#include "lvgl.h"
#include <string.h>

extern const uint16_t ui_emoji_count;
extern const uint32_t ui_emoji_cps[];
extern const lv_image_dsc_t ui_emoji_img[];

#define PX 16
#define FLAG_BASE 0xF0000   /* flag images: FLAG_BASE + letter1 * 26 + letter2 (emoji.py) */

static bool zero_width(uint32_t u) {
  return u == 0x200D                      /* zero width joiner */
      || u == 0xFE0E || u == 0xFE0F       /* text / emoji presentation */
      || u == 0x20E3                      /* keycap */
      || (u >= 0x1F3FB && u <= 0x1F3FF)   /* skin tones */
      || (u >= 0xE0020 && u <= 0xE007F);  /* tags (subdivision flags) */
}

static int find(uint32_t u) {
  if (u < ui_emoji_cps[0] || u > ui_emoji_cps[ui_emoji_count - 1]) return -1;
  int lo = 0, hi = ui_emoji_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (ui_emoji_cps[mid] < u) lo = mid + 1;
    else if (ui_emoji_cps[mid] > u) hi = mid - 1;
    else return mid;
  }
  return -1;
}

/* 0..25 when p starts with a regional indicator letter (U+1F1E6 + 0..25,
 * UTF-8 F0 9F 87 A6..BF), else -1. */
static int ri_letter(const char* p) {
  return ((uint8_t)p[0] == 0xF0 && (uint8_t)p[1] == 0x9F && (uint8_t)p[2] == 0x87 && (uint8_t)p[3] >= 0xA6 &&
          (uint8_t)p[3] <= 0xBF) ? (uint8_t)p[3] - 0xA6 : -1;
}

/* A flag is a pair of regional indicator letters. Returns s with every pair
 * that has an image swapped for that image's codepoint -- a copy from lv_malloc,
 * for the caller to lv_free -- or NULL when s has no flag. The copy is never
 * longer: 8 bytes become 4. */
char* ui_emoji_flags(const char* s) {
  if (!strstr(s, "\xF0\x9F\x87")) return NULL;
  size_t n = strlen(s);
  char* out = lv_malloc(n + 1);
  if (!out) return NULL;
  char* o = out;
  while (*s) {
    int a = ri_letter(s), b = a >= 0 ? ri_letter(s + 4) : -1;
    if (b >= 0) {
      uint32_t cp = FLAG_BASE + a * 26 + b;
      if (find(cp) >= 0) {
        *o++ = (char)(0xF0 | (cp >> 18));
        *o++ = (char)(0x80 | ((cp >> 12) & 0x3F));
        *o++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *o++ = (char)(0x80 | (cp & 0x3F));
      } else {   /* no such flag: keep both letters, so the next pair stays paired */
        memcpy(o, s, 8);
        o += 8;
      }
      s += 8;
    } else {
      *o++ = *s++;
    }
  }
  *o = 0;
  return out;
}

static bool get_glyph_dsc(const lv_font_t* font, lv_font_glyph_dsc_t* g, uint32_t u, uint32_t next) {
  LV_UNUSED(next);
  g->is_placeholder = 0;
  g->ofs_x = 0;
  if (zero_width(u)) {
    g->adv_w = g->box_w = g->box_h = 0;
    g->ofs_y = 0;
    g->format = LV_FONT_GLYPH_FORMAT_NONE;
    return true;
  }
  int i = find(u);
  if (i < 0) return false;
  g->adv_w = PX + 2;   /* 1 px clear each side */
  g->box_w = g->box_h = PX;
  g->ofs_x = 1;
  g->ofs_y = (int16_t)(intptr_t)font->user_data;
  g->format = LV_FONT_GLYPH_FORMAT_IMAGE;
  g->gid.src = &ui_emoji_img[i];
  return true;
}

static const void* get_glyph_bitmap(lv_font_glyph_dsc_t* g, lv_draw_buf_t* buf) {
  LV_UNUSED(buf);
  return g->gid.src;
}

/* LVGL puts a glyph's top at line top + (line_height - base_line) - box_h - ofs_y
 * with the *text* font's metrics; this ofs_y centres the image on that line. */
#define EMOJI_FONT(name, lh, bl)                                 \
  const lv_font_t name = {                                       \
    .get_glyph_dsc = get_glyph_dsc,                              \
    .get_glyph_bitmap = get_glyph_bitmap,                        \
    .line_height = (lh),                                         \
    .base_line = (bl),                                           \
    .user_data = (void*)(intptr_t)(((lh) - (bl)) - PX - ((lh) - PX) / 2), \
  };

EMOJI_FONT(ui_emoji_12, 18, 4)
EMOJI_FONT(ui_emoji_14, 20, 4)
EMOJI_FONT(ui_emoji_16, 23, 5)
EMOJI_FONT(ui_emoji_20, 28, 6)
