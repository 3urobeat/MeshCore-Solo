// Boot splash -- included into UITask.cpp only.
//
// The MeshCore wordmark (ui-new's 128x13 bitmap, drawn 2x in amber), "solo",
// the Solo and upstream versions and the build date -- all in the wordmark's
// lettering (its own letters, plus l, v, d, digits, '.', '-' drawn to match;
// a line with anything else falls back to Noto) -- with three dots rising
// in turn while the rest of the device starts. Covers the whole display
// (status bar too) from lv_layer_top, fades out after SPLASH_MS; a tap skips
// it. Home -- and the PIN lock, if set -- are already built underneath.

namespace splash {
static const uint32_t SPLASH_MS = 2500;
static const int LOGO_W = 128, LOGO_H = 13, LOGO_SCALE = 2;

// 'meshcore', 128x13 px, MSB first (ui-new/icons.h meshcore_logo)
static const uint8_t LOGO[] = {
  0x3c, 0x01, 0xe3, 0xff, 0xc7, 0xff, 0x8f, 0x03, 0x87, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe,
  0x3c, 0x03, 0xe3, 0xff, 0xc7, 0xff, 0x8e, 0x03, 0x8f, 0xfe, 0x3f, 0xfe, 0x1f, 0xff, 0x1f, 0xfe,
  0x3e, 0x03, 0xc3, 0xff, 0x8f, 0xff, 0x0e, 0x07, 0x8f, 0xfe, 0x7f, 0xfe, 0x1f, 0xff, 0x1f, 0xfc,
  0x3e, 0x07, 0xc7, 0x80, 0x0e, 0x00, 0x0e, 0x07, 0x9e, 0x00, 0x78, 0x0e, 0x3c, 0x0f, 0x1c, 0x00,
  0x3e, 0x0f, 0xc7, 0x80, 0x1e, 0x00, 0x0e, 0x07, 0x1e, 0x00, 0x70, 0x0e, 0x38, 0x0f, 0x3c, 0x00,
  0x7f, 0x0f, 0xc7, 0xfe, 0x1f, 0xfc, 0x1f, 0xff, 0x1c, 0x00, 0x70, 0x0e, 0x38, 0x0e, 0x3f, 0xf8,
  0x7f, 0x1f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x0e, 0x38, 0x0e, 0x3f, 0xf8,
  0x7f, 0x3f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x1e, 0x3f, 0xfe, 0x3f, 0xf0,
  0x77, 0x3b, 0x87, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xfc, 0x38, 0x00,
  0x77, 0xfb, 0x8f, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xf8, 0x38, 0x00,
  0x73, 0xf3, 0x8f, 0xff, 0x0f, 0xff, 0x1c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x78, 0x7f, 0xf8,
  0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfe, 0x3c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x3c, 0x7f, 0xf8,
  0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfc, 0x3c, 0x0e, 0x1f, 0xf8, 0xff, 0xf8, 0x70, 0x3c, 0x7f, 0xf8,
};

// The wordmark's own letters: columns of LOGO.
struct LogoLetter { char c; uint8_t x0, w; };
static const LogoLetter LOGO_LETTERS[] = {
  { 'm', 0, 18 }, { 'e', 19, 15 }, { 's', 35, 15 }, { 'h', 50, 16 }, { 'c', 66, 14 }, { 'o', 80, 16 }, { 'r', 96, 16 },
};
// The rest, drawn upright in the same strokes (3 px bars at the top, middle
// and bottom); slanted like the wordmark when drawn (SLANT).
struct PixGlyph { char c; const char* rows[LOGO_H]; };
#define F10 "##########"
#define L10 "###......."
#define R10 ".......###"
#define B10 "###....###"
static const PixGlyph GLYPHS[] = {
  { '0', { F10, F10, F10, B10, B10, B10, B10, B10, B10, B10, F10, F10, F10 } },
  { '1', { "....####..", "...#####..", "..######..", "....####..", "....####..", "....####..", "....####..",
           "....####..", "....####..", "....####..", "....####..", "....####..", "....####.." } },
  { '2', { F10, F10, F10, R10, R10, F10, F10, F10, L10, L10, F10, F10, F10 } },
  { '3', { F10, F10, F10, R10, R10, F10, F10, F10, R10, R10, F10, F10, F10 } },
  { '4', { B10, B10, B10, B10, B10, F10, F10, F10, R10, R10, R10, R10, R10 } },
  { '5', { F10, F10, F10, L10, L10, F10, F10, F10, R10, R10, F10, F10, F10 } },
  { '6', { F10, F10, F10, L10, L10, F10, F10, F10, B10, B10, F10, F10, F10 } },
  { '7', { F10, F10, F10, R10, R10, R10, R10, R10, R10, R10, R10, R10, R10 } },
  { '8', { F10, F10, F10, B10, B10, F10, F10, F10, B10, B10, F10, F10, F10 } },
  { '9', { F10, F10, F10, B10, B10, F10, F10, F10, R10, R10, F10, F10, F10 } },
  { '.', { "....", "....", "....", "....", "....", "....", "....", "....", "....", "....", "###.", "###.", "###." } },
  { '-', { ".......", ".......", ".......", ".......", ".......", "#######", "#######", "#######",
           ".......", ".......", ".......", ".......", "......." } },
  { 'v', { "###.....###", "###.....###", "###.....###", "###.....###", "###.....###", "###.....###", "###.....###",
           "###.....###", ".###...###.", ".###...###.", "..###.###..", "...#####...", "....###...." } },
  { 'l', { "###......", "###......", "###......", "###......", "###......", "###......", "###......",
           "###......", "###......", "###......", "#########", "#########", "#########" } },
  { 'd', { "#########.", F10, F10, B10, B10, B10, B10, B10, B10, B10, F10, F10, "#########." } },
  { ' ', { ".....", ".....", ".....", ".....", ".....", ".....", ".....", ".....", ".....", ".....", ".....", ".....", "....." } },
};
#undef F10
#undef L10
#undef R10
#undef B10
static const uint8_t SLANT[LOGO_H] = { 2, 2, 2, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 };   // px right, per row

static lv_obj_t*      s_root = nullptr;
static const int      MAX_IMGS = 6;
static uint32_t*      s_px[MAX_IMGS];     // ARGB8888 pixels of each drawn line
static lv_image_dsc_t s_img[MAX_IMGS];
static int            s_imgs = 0;
static lv_timer_t*    s_timer = nullptr;

static void freeAll(lv_anim_t* a) {
  (void)a;
  if (s_root) lv_obj_delete(s_root);
  s_root = nullptr;
  for (int i = 0; i < s_imgs; i++) lv_free(s_px[i]);
  s_imgs = 0;
}

static void dismiss() {
  if (s_timer) { lv_timer_delete(s_timer); s_timer = nullptr; }
  if (!s_root) return;
  lv_obj_remove_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
  lv_anim_delete(s_root, NULL);
  anim::run(s_root, anim::setOpa, LV_OPA_COVER, LV_OPA_TRANSP, 300, freeAll);
}
static void onTimer(lv_timer_t* t) { (void)t; dismiss(); }
static bool up() { return s_root != nullptr; }   // until its fade-out has finished
static void onTap(lv_event_t* e) { (void)e; dismiss(); }

// One character of the lettering: its width, and whether (x, y) is inked.
static const LogoLetter* logoLetter(char c) {
  for (const LogoLetter& l : LOGO_LETTERS) if (l.c == c) return &l;
  return nullptr;
}
static const PixGlyph* pixGlyph(char c) {
  for (const PixGlyph& g : GLYPHS) if (g.c == c) return &g;
  return nullptr;
}
static int charW(char c) {
  if (const LogoLetter* l = logoLetter(c)) return l->w;
  if (const PixGlyph* g = pixGlyph(c)) return (int)strlen(g->rows[0]) + 2;   // + the slant
  return -1;
}
static bool inked(char c, int x, int y) {
  if (const LogoLetter* l = logoLetter(c)) {
    int sx = l->x0 + x;
    return LOGO[y * (LOGO_W / 8) + sx / 8] & (0x80 >> (sx % 8));
  }
  const PixGlyph* g = pixGlyph(c);
  int gx = x - SLANT[y];
  return g && gx >= 0 && gx < (int)strlen(g->rows[y]) && g->rows[y][gx] == '#';
}

// `text` in the lettering (lower case), -1 if it has a character that isn't drawn.
static int textW(const char* text, int gap) {
  int w = 0;
  for (const char* p = text; *p; p++) {
    int cw = charW((char)tolower((unsigned char)*p));
    if (cw < 0) return -1;
    w += cw + (p[1] ? gap : 0);
  }
  return w;
}

// A line in the lettering, `scale` x, in `col`: an image, or a Noto label when
// the text has a character the lettering lacks. `text` null = the wordmark.
static lv_obj_t* letters(lv_obj_t* parent, const char* text, int scale, int gap, uint32_t col, const lv_font_t* fallback) {
  int w = text ? textW(text, gap) : LOGO_W;
  if (w <= 0 || s_imgs >= MAX_IMGS) return label(parent, text ? text : "MeshCore", fallback, col);
  const int W = w * scale, H = LOGO_H * scale;
  uint32_t* px = (uint32_t*)lv_malloc((size_t)W * H * 4);
  if (!px) return label(parent, text ? text : "MeshCore", fallback, col);
  memset(px, 0, (size_t)W * H * 4);
  uint32_t on = 0xFF000000u | (col & 0xFFFFFFu);
  auto plot = [&](int x, int y) {
    for (int dy = 0; dy < scale; dy++)
      for (int dx = 0; dx < scale; dx++) px[(y * scale + dy) * W + x * scale + dx] = on;
  };
  if (!text) {
    for (int y = 0; y < LOGO_H; y++)
      for (int x = 0; x < LOGO_W; x++)
        if (LOGO[y * (LOGO_W / 8) + x / 8] & (0x80 >> (x % 8))) plot(x, y);
  } else {
    int ox = 0;
    for (const char* p = text; *p; p++) {
      char c = (char)tolower((unsigned char)*p);
      int cw = charW(c);
      for (int y = 0; y < LOGO_H; y++)
        for (int x = 0; x < cw; x++)
          if (inked(c, x, y)) plot(ox + x, y);
      ox += cw + gap;
    }
  }
  lv_image_dsc_t& d = s_img[s_imgs];
  s_px[s_imgs++] = px;
  memset(&d, 0, sizeof(d));
  d.header.magic = LV_IMAGE_HEADER_MAGIC;
  d.header.cf = LV_COLOR_FORMAT_ARGB8888;
  d.header.w = W;
  d.header.h = H;
  d.header.stride = W * 4;
  d.data_size = (uint32_t)W * H * 4;
  d.data = (const uint8_t*)px;
  lv_obj_t* img = lv_image_create(parent);
  lv_image_set_src(img, &d);
  return img;
}

// One of the loading dots: rises 6 px and back, each a beat after the last.
static void dotY(void* o, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t*)o, v, 0); }
static void wave(lv_obj_t* dot, int i) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, dot);
  lv_anim_set_exec_cb(&a, dotY);
  lv_anim_set_values(&a, 0, -6);
  lv_anim_set_duration(&a, 260);
  lv_anim_set_playback_duration(&a, 260);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_repeat_delay(&a, 420);
  lv_anim_set_delay(&a, 140 * i);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

// "v1.28-solo.1-abcdef" -> "v1.28-solo.1": build.sh appends the commit as
// the last dash segment (as ui-new's splash).
static void soloVersion(char* out, size_t n) {
  const char* ver = FIRMWARE_VERSION;
  const char* dash = strrchr(ver, '-');
  size_t len = dash ? (size_t)(dash - ver) : strlen(ver);
  if (len >= n) len = n - 1;
  memcpy(out, ver, len);
  out[len] = '\0';
}

// The build date as digits, "2026-09-26": build.sh gives "26-Sep-2026",
// __DATE__ "Sep 26 2026". Anything else is passed through.
static void buildDate(char* out, size_t n) {
  static const char MON[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char* d = FIRMWARE_BUILD_DATE;
  int day = 0, year = 0, mon = 0;
  char m[4] = "";
  if (sscanf(d, "%d-%3s-%d", &day, m, &year) != 3 && sscanf(d, "%3s %d %d", m, &day, &year) != 3) {
    snprintf(out, n, "%s", d);
    return;
  }
  const char* f = strstr(MON, m);
  mon = f && m[0] ? (int)(f - MON) / 3 + 1 : 0;
  if (mon) snprintf(out, n, "%04d-%02d-%02d", year, mon, day);
  else snprintf(out, n, "%s", d);
}
}  // namespace splash

void UITask::showSplash() {
  using namespace splash;
  if (s_root) return;
  s_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_root);
  lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_root, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_root, onTap, LV_EVENT_CLICKED, NULL);

  lv_obj_t* logo = letters(s_root, nullptr, LOGO_SCALE, 0, theme::ACCENT, THEME_FONT_LARGE);
  lv_obj_align(logo, LV_ALIGN_CENTER, 0, -50);
  anim::rise(logo, 10, 400);
  lv_obj_t* solo = letters(s_root, "solo", LOGO_SCALE, 3, theme::TEXT, THEME_FONT_LARGE);
  lv_obj_align(solo, LV_ALIGN_CENTER, 0, -14);

  // Ours, then upstream's, then the build date.
  char ver[24], line[48], date[24];
  soloVersion(ver, sizeof(ver));
  lv_obj_t* v = letters(s_root, ver[0] ? ver : "dev", 1, 2, theme::ACCENT, THEME_FONT_BODY);
  lv_obj_align(v, LV_ALIGN_CENTER, 0, 18);
  int y = 38;
#ifdef MESHCORE_VERSION
  snprintf(line, sizeof(line), "meshcore %s", MESHCORE_VERSION);
  lv_obj_align(letters(s_root, line, 1, 2, theme::TEXT_MUTED, THEME_FONT_SMALL), LV_ALIGN_CENTER, 0, y);
  y += 20;
#endif
  buildDate(date, sizeof(date));
  lv_obj_align(letters(s_root, date, 1, 2, theme::TEXT_MUTED, THEME_FONT_SMALL), LV_ALIGN_CENTER, 0, y);

  lv_obj_t* dots = lv_obj_create(s_root);
  lv_obj_remove_style_all(dots);
  lv_obj_set_size(dots, 60, 20);
  lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -18);
  for (int i = 0; i < 3; i++) {
    lv_obj_t* d = lv_obj_create(dots);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, 7, 7);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(theme::ACCENT), 0);
    lv_obj_align(d, LV_ALIGN_BOTTOM_MID, (i - 1) * 16, -2);
    wave(d, i);
  }
  s_timer = lv_timer_create(onTimer, SPLASH_MS, NULL);   // deleted by dismiss()
}
