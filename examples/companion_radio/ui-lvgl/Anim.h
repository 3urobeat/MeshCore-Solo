#pragma once
// Light motion for ui-lvgl: screens emerge from the middle, popups and toasts
// rise and fade in, Home pages slide after a swipe (buttons giving under the
// finger is a Theme.h style). Short (under 200 ms) and eased, so nothing waits
// on them.

namespace anim {
  static const uint32_t SCREEN_MS = 180;   // screen change
  static const uint32_t POP_MS    = 160;   // popup / toast in
  static const uint32_t OUT_MS    = 120;   // toast out
  static const uint32_t PAGE_MS   = 160;   // Home page after a swipe
  static const uint32_t UNLOCK_MS = 260;   // lock screen away
  static const uint32_t SPRING_MS = 200;   // a let-go slider knob back home

  static void setTy(void* o, int32_t v)  { lv_obj_set_style_translate_y((lv_obj_t*)o, v, 0); }
  static void setTx(void* o, int32_t v)  { lv_obj_set_style_translate_x((lv_obj_t*)o, v, 0); }
  static void setOpa(void* o, int32_t v) { lv_obj_set_style_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }
  static void setBgOpa(void* o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }
  static void setSlider(void* o, int32_t v) { lv_slider_set_value((lv_obj_t*)o, v, LV_ANIM_OFF); }

  static void run(lv_obj_t* o, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms,
                  lv_anim_completed_cb_t done = nullptr) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    if (done) lv_anim_set_completed_cb(&a, done);
    lv_anim_start(&a);
    cb(o, from);   // no frame of the end state before the first tick
  }

  // Rise by dy and fade in (a popup's panel, a toast).
  static void rise(lv_obj_t* o, int32_t dy = 16, uint32_t ms = POP_MS) {
    run(o, setTy, dy, 0, ms);
    run(o, setOpa, LV_OPA_TRANSP, LV_OPA_COVER, ms);
  }

  // A popup: the dimmed backdrop fades up to `dim`, its first child (the
  // panel) rises.
  static void popup(lv_obj_t* overlay, lv_opa_t dim = LV_OPA_60) {
    run(overlay, setBgOpa, LV_OPA_TRANSP, dim, POP_MS);
    lv_obj_t* panel = lv_obj_get_child(overlay, 0);
    if (panel) rise(panel);
  }

  // A new screen emerges: a cover in the background colour fades away over
  // it while its content drifts a few pixels into place (up going deeper,
  // down coming back). A plain fill blended on top -- far cheaper than
  // fading the screen itself (a full-screen layer) or sliding two screens.
  static void coverDone(lv_anim_t* a) { lv_obj_delete((lv_obj_t*)a->var); }
  static void screenIn(lv_obj_t* scr, lv_obj_t* body, bool back) {
    lv_obj_t* cover = lv_obj_create(scr);
    lv_obj_remove_style_all(cover);
    lv_obj_remove_flag(cover, LV_OBJ_FLAG_CLICKABLE);   // taps go through to the screen
    lv_obj_add_flag(cover, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(cover, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(cover, lv_obj_get_style_bg_color(scr, 0), 0);
    run(cover, setBgOpa, LV_OPA_COVER, LV_OPA_TRANSP, SCREEN_MS, coverDone);
    if (body) run(body, setTy, back ? -6 : 6, 0, SCREEN_MS);
  }

  // Content that replaced other content in place: slides in from dx.
  static void slideIn(lv_obj_t* o, int32_t dx, uint32_t ms = PAGE_MS) {
    run(o, setTx, dx, 0, ms);
    run(o, setOpa, LV_OPA_40, LV_OPA_COVER, ms);
  }

  static void hideDone(lv_anim_t* a) {
    lv_obj_t* o = (lv_obj_t*)a->var;
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(o, LV_OPA_COVER, 0);
  }
  // Fade out, then hide (the object stays for reuse).
  static void fadeHide(lv_obj_t* o) {
    lv_anim_delete(o, setOpa);
    run(o, setOpa, lv_obj_get_style_opa(o, 0), LV_OPA_TRANSP, OUT_MS, hideDone);
  }
}
