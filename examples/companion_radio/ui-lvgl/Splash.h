// Boot splash -- included into UITask.cpp only.
//
// The MeshCore wordmark (ui-new's 128x13 bitmap, drawn 2x in amber), "SOLO",
// the Solo and upstream versions and the build date, with three dots rising
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

static lv_obj_t*      s_root = nullptr;
static uint32_t*      s_px = nullptr;     // ARGB8888 pixels of the scaled logo
static lv_image_dsc_t s_img;
static lv_timer_t*    s_timer = nullptr;

static void freeAll(lv_anim_t* a) {
  (void)a;
  if (s_root) lv_obj_delete(s_root);
  s_root = nullptr;
  if (s_px) lv_free(s_px);
  s_px = nullptr;
}

static void dismiss() {
  if (s_timer) { lv_timer_delete(s_timer); s_timer = nullptr; }
  if (!s_root) return;
  lv_obj_remove_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
  lv_anim_delete(s_root, NULL);
  anim::run(s_root, anim::setOpa, LV_OPA_COVER, LV_OPA_TRANSP, 300, freeAll);
}
static void onTimer(lv_timer_t* t) { (void)t; dismiss(); }
static void onTap(lv_event_t* e) { (void)e; dismiss(); }

// The logo bitmap scaled up into an ARGB8888 image in `col`.
static bool buildLogo(uint32_t col) {
  const int W = LOGO_W * LOGO_SCALE, H = LOGO_H * LOGO_SCALE;
  s_px = (uint32_t*)lv_malloc((size_t)W * H * 4);
  if (!s_px) return false;
  uint32_t on = 0xFF000000u | (col & 0xFFFFFFu);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      int sx = x / LOGO_SCALE, sy = y / LOGO_SCALE;
      bool bit = LOGO[sy * (LOGO_W / 8) + sx / 8] & (0x80 >> (sx % 8));
      s_px[y * W + x] = bit ? on : 0;
    }
  memset(&s_img, 0, sizeof(s_img));
  s_img.header.magic = LV_IMAGE_HEADER_MAGIC;
  s_img.header.cf = LV_COLOR_FORMAT_ARGB8888;
  s_img.header.w = W;
  s_img.header.h = H;
  s_img.header.stride = W * 4;
  s_img.data_size = (uint32_t)W * H * 4;
  s_img.data = (const uint8_t*)s_px;
  return true;
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

  if (buildLogo(theme::ACCENT)) {
    lv_obj_t* logo = lv_image_create(s_root);
    lv_image_set_src(logo, &s_img);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -42);
    anim::rise(logo, 10, 400);
  }
  lv_obj_t* solo = label(s_root, "S  O  L  O", THEME_FONT_TITLE, theme::TEXT);
  lv_obj_align(solo, LV_ALIGN_CENTER, 0, -8);

  char ver[24], line[64];
  soloVersion(ver, sizeof(ver));
  lv_obj_t* v = label(s_root, ver[0] ? ver : "dev", THEME_FONT_BODY, theme::ACCENT);
  lv_obj_align(v, LV_ALIGN_CENTER, 0, 22);
#ifdef MESHCORE_VERSION
  snprintf(line, sizeof(line), "MeshCore %s  -  built %s", MESHCORE_VERSION, FIRMWARE_BUILD_DATE);
#else
  snprintf(line, sizeof(line), "Built %s", FIRMWARE_BUILD_DATE);
#endif
  lv_obj_t* up = label(s_root, line, THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_align(up, LV_ALIGN_CENTER, 0, 42);

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
