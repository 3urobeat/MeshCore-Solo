#pragma once
// Settings > Diagnostics -- ui-new's Tools > Diagnostics. Tabs Live / System /
// Font; the rows come from ui-core/Diagnostics.h. Live refreshes every second
// and its header button resets the counters (after a confirm).
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after DeviceScreen.h.

#if defined(SEEED_WIO_TRACKER_L2)
extern RADIO_CLASS radio;   // variants/wio-tracker-l2/target.cpp (the noise sweep retunes it)
#endif

namespace diagview {

enum : uint8_t { TAB_LIVE, TAB_SYSTEM, TAB_FONT, TAB_NOISE, TAB_COUNT };
static uint8_t s_tab = TAB_LIVE;   // kept across visits
static lv_obj_t* s_list = nullptr;
static const int EXTRA = 4;   // GPS, power, last reset, last crash (L2 only)
// A Live row's value labels: `in` / `out` for a packet count ("12/3" from
// ui-core), else `val` alone.
struct LiveVal { lv_obj_t* val; lv_obj_t* in; lv_obj_t* out; };
static LiveVal s_vals[diag::MAX_ROWS + EXTRA];
static int s_rows = 0;

static void extraRow(diag::Row* rows, int& n, const char* lbl, const char* fmt, ...) {
  rows[n].label = lbl;
  va_list ap; va_start(ap, fmt);
  vsnprintf(rows[n].value, sizeof(rows[n].value), fmt, ap);
  va_end(ap);
  n++;
}

// ui-core's live rows plus the receiver's state and why the device last
// started -- what to look at when GPS gets no fix or the device restarted.
static int allRows(diag::Row* rows, bool gps_on) {
  int n = diag::liveRows(rows);
#if defined(SEEED_WIO_TRACKER_L2)
  static uint32_t s_chars = 0, s_moved_ms = 0;
  uint32_t c = gps.rxChars();
  if (c != s_chars) { s_chars = c; s_moved_ms = millis(); }
  bool data = c > 0 && millis() - s_moved_ms < 3000;
  if (!gps_on) extraRow(rows, n, "GPS", "off");
  else if (!data) extraRow(rows, n, "GPS", "no data (%lu B)", (unsigned long)c);
  else extraRow(rows, n, "GPS", "%s, %ld sats", gps.isValid() ? "fix" : "no fix", gps.satellitesCount());
  extraRow(rows, n, "Power", "%s", board.isExternalPowered() ? "USB" : "battery");
#else
  (void)gps_on;
#endif
  extraRow(rows, n, "Last start", "%s", lvport::resetReason());
  char crash[32];
  if (lvport::crashSummary(crash, sizeof(crash))) extraRow(rows, n, "Last crash", "%s", crash);
  return n;
}

// ui-core's labels are cut for a 128 px OLED; here each gets a section and a
// full name. Packet rows are "received/sent" pairs, shown in two columns.
enum : uint8_t { SEC_DEVICE, SEC_PACKETS, SEC_RADIO, SEC_MEMORY, SEC_COUNT };
static const char* const SEC_TITLE[SEC_COUNT] = { "DEVICE", "PACKETS", "RADIO", "MEMORY" };
struct Name { const char* core; uint8_t sec; const char* text; bool pair; };
static const Name NAMES[] = {
  { "Uptime", SEC_DEVICE, "Uptime", false },          { "Last start", SEC_DEVICE, "Last start", false },
  { "Last crash", SEC_DEVICE, "Last crash", false },  { "Power", SEC_DEVICE, "Power", false },
  { "GPS", SEC_DEVICE, "GPS", false },
  { "Total rx/tx", SEC_PACKETS, "All", true },         { "Msg", SEC_PACKETS, "Messages", true },
  { "Advert", SEC_PACKETS, "Adverts", true },          { "Ack/Path", SEC_PACKETS, "Acks & paths", true },
  { "Other", SEC_PACKETS, "Other", true },             { "Forwarded", SEC_PACKETS, "Forwarded", false },
  { "Noise floor", SEC_RADIO, "Noise floor", false },  { "RSSI/SNR", SEC_RADIO, "Last packet", false },
  { "Queue", SEC_RADIO, "Send queue", false },         { "Errors", SEC_RADIO, "Errors", false },
  { "RXPS wd s/h", SEC_RADIO, "RX watchdog soft / hard", false },
  { "Heap free", SEC_MEMORY, "Heap free", false },     { "Stack free", SEC_MEMORY, "UI stack free (lowest)", false },
  { "Pool free", SEC_MEMORY, "Packet pool free", false },
};
static const Name* nameOf(const char* core) {
  for (const Name& n : NAMES) if (!strcmp(n.core, core)) return &n;
  return nullptr;
}

// A value as shown: "-60/40.0" -> "-60 dBm, SNR 40.0"; "12/456KB" -> "12 of 456 KB".
static void showValue(const char* core, const char* v, char* out, size_t n) {
  const char* slash = strchr(v, '/');
  if (!strcmp(core, "RSSI/SNR") && slash) snprintf(out, n, "%.*s dBm, SNR %s", (int)(slash - v), v, slash + 1);
  else if (!strcmp(core, "Heap free") && slash) snprintf(out, n, "%.*s of %s", (int)(slash - v), v, slash + 1);
  else snprintf(out, n, "%s", v);
  size_t l = strlen(out);   // "KB" / "B" units get their space
  if (l > 2 && !strcmp(out + l - 2, "KB") && out[l - 3] != ' ') snprintf(out + l - 2, n - (l - 2), " KB");
  else if (l > 1 && out[l - 1] == 'B' && out[l - 2] >= '0' && out[l - 2] <= '9') snprintf(out + l - 1, n - (l - 1), " B");
}

static void setLive(int i, const diag::Row& r) {
  LiveVal& lv = s_vals[i];
  if (lv.in) {
    const char* slash = strchr(r.value, '/');
    char a[12];
    snprintf(a, sizeof(a), "%.*s", slash ? (int)(slash - r.value) : (int)strlen(r.value), r.value);
    lv_label_set_text(lv.in, a);
    lv_label_set_text(lv.out, slash ? slash + 1 : "");
    return;
  }
  char v[40];
  showValue(r.label, r.value, v, sizeof(v));
  lv_label_set_text(lv.val, v);
  if (!strcmp(r.label, "Errors")) lv_obj_set_style_text_color(lv.val, lv_color_hex(strcmp(r.value, "OK") ? theme::FAIL : theme::OK), 0);
}

// A packets row: the name, then received and sent in fixed columns.
static const int COL_W = 56;
static lv_obj_t* pairLine(lv_obj_t* card, const char* name, lv_obj_t** in, lv_obj_t** out, bool head) {
  lv_obj_t* r = infoLine(card);
  lv_obj_t* k = label(r, name, THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_align(k, LV_ALIGN_LEFT_MID, 0, 0);
  const lv_font_t* f = head ? THEME_FONT_SMALL : THEME_FONT_BODY;
  uint32_t col = head ? theme::TEXT_MUTED : theme::TEXT;
  *out = label(r, head ? "Sent" : "", f, col);
  lv_obj_set_width(*out, COL_W);
  lv_obj_set_style_text_align(*out, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(*out, LV_ALIGN_RIGHT_MID, 0, 0);
  *in = label(r, head ? "Received" : "", f, col);
  lv_obj_set_width(*in, COL_W + 16);
  lv_obj_set_style_text_align(*in, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(*in, LV_ALIGN_RIGHT_MID, -COL_W, 0);
  return r;
}

// Noise tab: the LoRa noise floor measured with the board's parts turned off
// one at a time -- which of them is the one raising it (the L2 read ~25 dB
// above the L1). Blocking, ~90 s: the main loop doesn't run meanwhile, so the
// screen isn't redrawn and only what runs by itself is measured.
enum : uint8_t { N_BL = 1, N_TOUCH = 2, N_GNSS = 4, N_GROVE = 8, N_CPU = 16, N_LCD = 32, N_SD = 64, N_SLEEP = 128 };
struct NoiseStep { const char* name; uint8_t off; };
static const NoiseStep NOISE_STEPS[] = {
  { "Everything on", 0 },   { "Backlight off", N_BL }, { "Touch asleep", N_TOUCH },
  { "Panel asleep", N_LCD }, { "GPS off", N_GNSS },    { "Grove port off", N_GROVE },
  { "CPU at 80 MHz", N_CPU },
  { "All of these off", N_BL | N_TOUCH | N_GNSS | N_GROVE | N_CPU | N_LCD },
};
// Then two sweeps: 850-930 MHz 1 MHz apart, and the mesh's frequency +-1.1
// MHz 25 kHz apart. Flat is broadband noise (a power supply, the front end);
// a spike is some clock's harmonic landing there.
struct Sweep { float f0, step; int n; int16_t v[96]; };
static Sweep s_wide = { 850.0f, 1.0f, 81, {} };
static Sweep s_near = { 0, 0.025f, 91, {} };
// Then +-125 kHz around the loudest of those, 5 kHz apart with the receiver
// narrowed to 7.8 kHz: where exactly the spike is, and how wide.
static Sweep s_zoom = { 0, 0.005f, 51, {} };
static const int NOISE_N = sizeof(NOISE_STEPS) / sizeof(NOISE_STEPS[0]);
// For a second radio next to this one (its noise floor read by eye): each
// state held 15 s. The L2 radiates the noise (an L1 beside it read -96 dBm
// instead of -108 with the L2 off), and is quiet in its bootloader.
static const NoiseStep DETECT_STEPS[] = {
  { "Everything on", 0 },    { "Backlight off", N_BL },    { "Panel asleep", N_LCD },
  { "Touch asleep", N_TOUCH }, { "GPS off", N_GNSS },      { "SD card off", N_SD },
  { "Grove port off", N_GROVE }, { "CPU at 80 MHz", N_CPU }, { "CPU light sleep", N_SLEEP },
  { "All but sleep off", N_BL | N_LCD | N_TOUCH | N_GNSS | N_SD | N_GROVE | N_CPU },
  { "Everything on again", 0 },
};
static const int DETECT_N = sizeof(DETECT_STEPS) / sizeof(DETECT_STEPS[0]);
static const uint32_t DETECT_HOLD_MS = 15000;
static void onDetectRun(lv_event_t* e) { (void)e; s_ui->diagNoiseDetect(); }
// Spike hunt: one clean line (869.633 MHz, 10 kHz wide) sits in the mesh's
// channel, from the ESP32 while awake. One frequency doesn't name the clock;
// the spacing of its neighbours does (a clock's harmonics are a comb). So
// 864-876 MHz 5 kHz apart through a 7.8 kHz receiver, and the lines standing
// out of the floor listed.
static const float HUNT_F0 = 864.0f, HUNT_STEP = 0.005f;
static const int HUNT_N = 2401;
static const int HUNT_MAX = 16;
struct Spike { float f; int16_t dbm; };
static Spike s_spikes[HUNT_MAX];
static int s_spike_n = -1;          // -1: not run
static int16_t s_hunt_floor = 0;
static void onSpikeHunt(lv_event_t* e) { (void)e; s_ui->diagSpikeHunt(); }
static int16_t s_noise_med[NOISE_N], s_noise_lo[NOISE_N];
static bool s_noise_have = false;
static bool s_noise_usb = false;
static lv_obj_t* s_noise_status = nullptr;

static void onNoiseRun(lv_event_t* e) { (void)e; s_ui->diagNoiseRun(); }

// A sweep as bars from -130 dBm up (1.5 px a dB), the step nearest the mesh's
// frequency in the accent colour, then its quietest / loudest / mesh values.
static void plotSweep(lv_obj_t* list, const char* title, const Sweep& sw, float mesh_f) {
  sectionTitle(list, title);
  int lo = 0, hi = 0;
  for (int i = 1; i < sw.n; i++) {
    if (sw.v[i] < sw.v[lo]) lo = i;
    if (sw.v[i] > sw.v[hi]) hi = i;
  }
  lv_obj_t* plot = lv_obj_create(list);
  lv_obj_remove_style_all(plot);
  lv_obj_set_size(plot, sw.n * 3, 100);
  lv_obj_set_style_bg_color(plot, lv_color_hex(theme::SURFACE), 0);
  lv_obj_set_style_bg_opa(plot, LV_OPA_COVER, 0);
  lv_obj_remove_flag(plot, LV_OBJ_FLAG_SCROLLABLE);
  int mesh_i = (int)lroundf((mesh_f - sw.f0) / sw.step);
  for (int i = 0; i < sw.n; i++) {
    int h = (sw.v[i] + 130) * 3 / 2;
    h = h < 1 ? 1 : (h > 100 ? 100 : h);
    lv_obj_t* bar = lv_obj_create(plot);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 2, h);
    lv_obj_set_pos(bar, i * 3, 100 - h);
    lv_obj_set_style_bg_color(bar, lv_color_hex(i == mesh_i ? theme::ACCENT : theme::TEXT_MUTED), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  }
  lv_obj_t* card = infoCard(list);
  char v[40];
  const int prec = sw.step < 0.01f ? 4 : (sw.step < 1 ? 3 : 0);
  snprintf(v, sizeof(v), "%.*f MHz: %d dBm", prec, sw.f0 + sw.step * lo, sw.v[lo]);
  infoRow(card, "Quietest", v);
  snprintf(v, sizeof(v), "%.*f MHz: %d dBm", prec, sw.f0 + sw.step * hi, sw.v[hi]);
  infoRow(card, "Loudest", v);
  if (mesh_i >= 0 && mesh_i < sw.n) {
    snprintf(v, sizeof(v), "%.*f MHz: %d dBm", prec, sw.f0 + sw.step * mesh_i, sw.v[mesh_i]);
    infoRow(card, "Mesh frequency", v);
  }
}

#if defined(SEEED_WIO_TRACKER_L2)
// Only what changes from `prev` is touched: GPS power back on means its
// reset, touch wake a pulse on its line.
static void noiseApply(uint8_t off, uint8_t prev, bool gnss_was_on, uint32_t cpu_mhz, uint8_t bright) {
  uint8_t ch = off ^ prev;
  if (ch & N_LCD) {   // the controller's sleep: its oscillator and charge pumps stop
    if (off & N_LCD) display.lgfxDevice()->sleep();
    else { display.lgfxDevice()->wakeup(); delay(120); display.setBrightness(bright); }
  }
  if (ch & N_BL) { if (off & N_BL) display.lgfxDevice()->setBrightness(0); else display.setBrightness(bright); }
  if (ch & N_TOUCH) { if (off & N_TOUCH) board.touchSleep(); else board.touchWake(); }
  if ((ch & N_GNSS) && gnss_was_on) board.setGnssPower(!(off & N_GNSS));
  if (ch & N_GROVE) board.setGrovePower(!(off & N_GROVE));
  if (ch & N_CPU) setCpuFrequencyMhz((off & N_CPU) ? 80 : cpu_mhz);
  if (ch & N_SD) {   // unmounted and unpowered; back on, mounted again
    if (off & N_SD) { SD_MMC.end(); board.setSdPower(false); }
    else { board.setSdPower(true); delay(50); SD_MMC.setPins(2, 3, 1); SD_MMC.begin("/sdcard", true); }
  }
}

// 250 instantaneous RSSI readings over 5 s: the median, and the 10th
// percentile (a packet on air only lifts the top).
static void noiseSample(int16_t& med, int16_t& lo) {
  static const int N = 250;
  float v[N];
  for (int i = 0; i < N; i++) { v[i] = radio_driver.getCurrentRSSI(); delay(20); }
  std::sort(v, v + N);
  med = (int16_t)lroundf(v[N / 2]);
  lo = (int16_t)lroundf(v[N / 10]);
}

// The radio retuned step by step (50 readings each, the median), then put
// back on the mesh's settings and receiving again.
static void noiseSweep(Sweep& sw, const NodePrefs* p, float bw_khz = 0) {
  static const int K = 50;
  float v[K];
  if (bw_khz > 0) { radio.standby(); radio.setBandwidth(bw_khz); }
  for (int i = 0; i < sw.n; i++) {
    radio.standby();
    radio.setFrequency(sw.f0 + sw.step * i);
    radio.startReceive();
    delay(10);
    for (int k = 0; k < K; k++) { v[k] = radio.getRSSI(false); delay(3); }
    std::sort(v, v + K);
    sw.v[i] = (int16_t)lroundf(v[K / 2]);
  }
  radio.standby();
  if (p) radio_driver.setParams(p->freq, p->bw, p->sf, p->cr);
  radio.startReceive();
}
#endif

}  // namespace diagview

void UITask::diagNoiseRun() {
  using namespace diagview;
#if defined(SEEED_WIO_TRACKER_L2)
  bool gnss_on = board.gnssPowered();
  uint32_t cpu = getCpuFrequencyMhz();
  uint8_t bright = _prefs ? _prefs->display_brightness : 3;
  s_noise_usb = board.isExternalPowered();
  for (int i = 0; i < NOISE_N; i++) {
    if (s_noise_status) {
      char t[48];
      snprintf(t, sizeof(t), "Measuring %d of %d: %s", i + 1, NOISE_N, NOISE_STEPS[i].name);
      lv_label_set_text(s_noise_status, t);
      lv_refr_now(NULL);
    }
    uint8_t off = NOISE_STEPS[i].off;
    noiseApply(off, 0, gnss_on, cpu, bright);
    delay(1500);   // rails, clocks and the receiver's AGC settle
    noiseSample(s_noise_med[i], s_noise_lo[i]);
    noiseApply(0, off, gnss_on, cpu, bright);
  }
  if (s_noise_status) {
    lv_label_set_text(s_noise_status, "Sweeping 850-930 MHz");
    lv_refr_now(NULL);
  }
  noiseSweep(s_wide, _prefs);
  if (_prefs) {
    if (s_noise_status) {
      lv_label_set_text(s_noise_status, "Sweeping around the mesh frequency");
      lv_refr_now(NULL);
    }
    s_near.f0 = _prefs->freq - s_near.step * (s_near.n / 2);
    noiseSweep(s_near, _prefs);
    int hi = 0;
    for (int i = 1; i < s_near.n; i++) if (s_near.v[i] > s_near.v[hi]) hi = i;
    if (s_noise_status) {
      lv_label_set_text(s_noise_status, "Zooming in on the loudest");
      lv_refr_now(NULL);
    }
    s_zoom.f0 = s_near.f0 + s_near.step * hi - s_zoom.step * (s_zoom.n / 2);
    noiseSweep(s_zoom, _prefs, 7.8f);
  }
  s_noise_have = true;
  buildDiag();
#else
  showToast("Only on the device");
#endif
}

void UITask::diagNoiseDetect() {
  using namespace diagview;
#if defined(SEEED_WIO_TRACKER_L2)
  auto show = [&](int i, int n, const char* name) {   // shown before the state (dark ones hide it)
    if (!s_noise_status) return;
    char t[48];
    snprintf(t, sizeof(t), "%d/%d  %s", i + 1, n, name);
    lv_label_set_text(s_noise_status, t);
    lv_refr_now(NULL);
  };
  bool gnss_on = board.gnssPowered();
  uint32_t cpu = getCpuFrequencyMhz();
  uint8_t bright = _prefs ? _prefs->display_brightness : 3;
  for (int i = 0; i < DETECT_N; i++) {
    uint8_t off = DETECT_STEPS[i].off;
    show(i, DETECT_N, DETECT_STEPS[i].name);
    if (off & N_SLEEP) {
      esp_sleep_enable_timer_wakeup((uint64_t)DETECT_HOLD_MS * 1000);
      if (esp_light_sleep_start() != ESP_OK) delay(DETECT_HOLD_MS);   // refused (a radio busy): hold awake
      continue;
    }
    noiseApply(off, 0, gnss_on, cpu, bright);
    delay(DETECT_HOLD_MS);
    noiseApply(0, off, gnss_on, cpu, bright);
  }
  buildDiag();
  showToast("Cycle done");
#else
  showToast("Only on the device");
#endif
}

void UITask::diagSpikeHunt() {
  using namespace diagview;
#if defined(SEEED_WIO_TRACKER_L2)
  int16_t* v = psramBuf<int16_t>(HUNT_N);
  if (!v) { showToast("Out of memory"); return; }
  if (s_noise_status) {
    lv_label_set_text(s_noise_status, "Hunting spikes 864-876 MHz");
    lv_refr_now(NULL);
  }
  radio.standby();
  radio.setBandwidth(7.8f);
  float r[7];
  for (int i = 0; i < HUNT_N; i++) {
    radio.standby();
    radio.setFrequency(HUNT_F0 + HUNT_STEP * i);
    radio.startReceive();
    delay(12);   // 4 ms read the register's floor (-127.5): no reading yet
    for (int k = 0; k < 7; k++) { r[k] = radio.getRSSI(false); delay(3); }
    std::sort(r, r + 7);
    v[i] = (int16_t)lroundf(r[3]);
  }
  radio.standby();
  if (_prefs) radio_driver.setParams(_prefs->freq, _prefs->bw, _prefs->sf, _prefs->cr);
  radio.startReceive();

  // The floor: the median. A line: 8 dB over it, the top within +-3 steps.
  int16_t* tmp = psramBuf<int16_t>(HUNT_N);
  if (tmp) { memcpy(tmp, v, HUNT_N * sizeof(int16_t)); std::sort(tmp, tmp + HUNT_N); s_hunt_floor = tmp[HUNT_N / 2]; free(tmp); }
  s_spike_n = 0;
  for (int i = 3; i < HUNT_N - 3; i++) {
    if (v[i] < s_hunt_floor + 8) continue;
    bool top = true;
    for (int d = -3; d <= 3 && top; d++) if (d && (v[i + d] > v[i] || (d < 0 && v[i + d] == v[i]))) top = false;
    if (!top) continue;
    Spike sp = { HUNT_F0 + HUNT_STEP * i, v[i] };
    if (s_spike_n < HUNT_MAX) s_spikes[s_spike_n++] = sp;
    else {   // full: replace the weakest if this one is stronger
      int w = 0;
      for (int k = 1; k < HUNT_MAX; k++) if (s_spikes[k].dbm < s_spikes[w].dbm) w = k;
      if (sp.dbm > s_spikes[w].dbm) s_spikes[w] = sp;
    }
  }
  std::sort(s_spikes, s_spikes + s_spike_n, [](const Spike& a, const Spike& b) { return a.f < b.f; });
  free(v);
  buildDiag();
#else
  showToast("Only on the device");
#endif
}

static void onOpenDiag(lv_event_t* e) { (void)e; s_ui->showDiag(); }
static void onDiagTab(lv_event_t* e) {
  s_ui->diagTab((int)lv_buttonmatrix_get_selected_button((lv_obj_t*)lv_event_get_target(e)));
}
static void onDiagReset(lv_event_t* e)   { (void)e; s_ui->diagResetPopup(); }
static void onDiagResetGo(lv_event_t* e) { (void)e; s_ui->diagReset(); }

void UITask::showDiag() {
  _screen = SCR_DIAG;
  buildDiag();
}

void UITask::diagTab(int tab) {
  if (tab < 0 || tab >= diagview::TAB_COUNT) return;
  diagview::s_tab = (uint8_t)tab;
  buildDiag();
}

void UITask::buildDiag() {
  using namespace diagview;
  lv_obj_t* body = newScreen("Diagnostics", true);
  lv_obj_set_style_pad_row(body, 4, 0);
  if (s_tab == TAB_LIVE && _header) headerButton(_header, LV_SYMBOL_REFRESH " Reset", onDiagReset, 4, NULL);

  static const char* TABS[] = { "Live", "System", "Font", "Noise", "" };
  lv_obj_t* tabs = segmented(body, TABS, s_tab, lv_pct(100), 34);
  lv_obj_set_style_bg_color(tabs, lv_color_hex(theme::SURFACE), LV_PART_ITEMS);
  lv_obj_add_event_cb(tabs, onDiagTab, LV_EVENT_VALUE_CHANGED, NULL);

  s_list = scrollList(body);
  lv_obj_set_style_pad_row(s_list, 4, 0);
  s_rows = 0;

  if (s_tab == TAB_LIVE) {
    // Section by section, in ui-core's order within each.
    diag::Row rows[diag::MAX_ROWS + EXTRA];
    s_rows = allRows(rows, _core->gpsEnabled());
    for (uint8_t sec = 0; sec < SEC_COUNT; sec++) {
      lv_obj_t* card = nullptr;
      for (int i = 0; i < s_rows; i++) {
        const Name* nm = nameOf(rows[i].label);
        if ((nm ? nm->sec : SEC_DEVICE) != sec) continue;
        if (!card) {
          sectionTitle(s_list, SEC_TITLE[sec]);
          card = infoCard(s_list);
          if (sec == SEC_PACKETS) { lv_obj_t *a, *b; pairLine(card, "", &a, &b, true); }
        }
        s_vals[i] = { nullptr, nullptr, nullptr };
        if (nm && nm->pair) pairLine(card, nm->text, &s_vals[i].in, &s_vals[i].out, false);
        else s_vals[i].val = infoRow(card, nm ? nm->text : rows[i].label, "");
        setLive(i, rows[i]);
      }
    }
    return;
  }

  s_noise_status = nullptr;
  if (s_tab == TAB_NOISE) {
    lv_obj_t* t = label(s_list, "LoRa noise floor with parts of the board off, one at a time (about 90 s; "
                                "the screen goes dark for a moment). Lower is better; ~-115 dBm is a quiet receiver.",
                        THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(t, LV_PCT(100));
    lv_obj_t* b = lv_button_create(s_list);
    lv_obj_set_size(b, LV_PCT(100), 40);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, theme::RADIUS, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(theme::ACCENT_DIM), 0);
    lv_obj_add_event_cb(b, onNoiseRun, LV_EVENT_CLICKED, NULL);
    s_noise_status = label(b, LV_SYMBOL_PLAY "  Run test", THEME_FONT_BODY, theme::TEXT);
    lv_obj_center(s_noise_status);
    lv_obj_t* t2 = label(s_list, "Second radio: an L1 beside this one shows the noise this device sends out. "
                                 "Each state is held 15 s (under 3 min), its name shown first; read the L1's noise floor.",
                         THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_label_set_long_mode(t2, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(t2, LV_PCT(100));
    lv_obj_t* b2 = lv_button_create(s_list);
    lv_obj_set_size(b2, LV_PCT(100), 40);
    lv_obj_set_style_shadow_width(b2, 0, 0);
    lv_obj_set_style_radius(b2, theme::RADIUS, 0);
    lv_obj_set_style_bg_color(b2, lv_color_hex(theme::SURFACE), 0);
    lv_obj_add_event_cb(b2, onDetectRun, LV_EVENT_CLICKED, NULL);
    lv_obj_t* l2 = label(b2, LV_SYMBOL_LOOP "  Slow cycle for a second radio", THEME_FONT_BODY, theme::TEXT);
    lv_obj_center(l2);
    lv_obj_t* b3 = lv_button_create(s_list);
    lv_obj_set_size(b3, LV_PCT(100), 40);
    lv_obj_set_style_shadow_width(b3, 0, 0);
    lv_obj_set_style_radius(b3, theme::RADIUS, 0);
    lv_obj_set_style_bg_color(b3, lv_color_hex(theme::SURFACE), 0);
    lv_obj_add_event_cb(b3, onSpikeHunt, LV_EVENT_CLICKED, NULL);
    lv_obj_t* l3 = label(b3, LV_SYMBOL_EYE_OPEN "  Spike hunt 864-876 MHz (~2.5 min)", THEME_FONT_BODY, theme::TEXT);
    lv_obj_center(l3);
    if (s_spike_n >= 0) {
      char t[48];
      snprintf(t, sizeof(t), "SPIKES (floor %d dBm)", s_hunt_floor);
      sectionTitle(s_list, t);
      lv_obj_t* card = infoCard(s_list);
      if (s_hunt_floor <= -127) infoRow(card, "Invalid", "readings at the register's floor");
      else if (!s_spike_n) infoRow(card, "None", "nothing 8 dB over the floor");
      for (int i = 0; i < s_spike_n; i++) {
        char k[24], v[32];
        snprintf(k, sizeof(k), "%.3f MHz", s_spikes[i].f);
        if (i) snprintf(v, sizeof(v), "%d dBm  +%.0f kHz", s_spikes[i].dbm, (s_spikes[i].f - s_spikes[i - 1].f) * 1000);
        else snprintf(v, sizeof(v), "%d dBm", s_spikes[i].dbm);
        infoRow(card, k, v);
      }
    }
    if (s_noise_have) {
      lv_obj_t* card = infoCard(s_list);
      char v[40];
      for (int i = 0; i < NOISE_N; i++) {
        int d = s_noise_med[i] - s_noise_med[0];
        if (i == 0) snprintf(v, sizeof(v), "%d dBm (low %d)", s_noise_med[i], s_noise_lo[i]);
        else snprintf(v, sizeof(v), "%d dBm (%+d)", s_noise_med[i], d);
        infoRow(card, NOISE_STEPS[i].name, v, i && d <= -3 ? theme::OK : theme::TEXT);
      }
      infoRow(card, "Powered from", s_noise_usb ? "USB" : "battery");
      float mesh_f = _prefs ? _prefs->freq : 0;
      plotSweep(s_list, "850 - 930 MHz", s_wide, mesh_f);
      if (s_near.f0 > 0) {
        char t[40];
        snprintf(t, sizeof(t), "%.3f MHz +- 1.1", mesh_f);
        plotSweep(s_list, t, s_near, mesh_f);
      }
      if (s_zoom.f0 > 0) {
        char t[48];
        float c = s_zoom.f0 + s_zoom.step * (s_zoom.n / 2);
        snprintf(t, sizeof(t), "%.3f MHz +- 125 kHz (7.8 kHz wide)", c);
        plotSweep(s_list, t, s_zoom, mesh_f);
        int hi = 0, wide = 0;
        for (int i = 1; i < s_zoom.n; i++) if (s_zoom.v[i] > s_zoom.v[hi]) hi = i;
        for (int i = 0; i < s_zoom.n; i++) if (s_zoom.v[i] >= s_zoom.v[hi] - 6) wide++;
        lv_obj_t* card = infoCard(s_list);
        char v2[40];
        snprintf(v2, sizeof(v2), "%d kHz (within 6 dB of the top)", wide * 5);
        infoRow(card, "Spike width", v2);
      }
    }
    return;
  }
  lv_obj_t* card = infoCard(s_list);
  if (s_tab == TAB_FONT) {   // "Latin ABCabc": the script, then its sample
    diag::Line lines[diag::MAX_LINES];
    int n = diag::fontLines(lines);
    for (int i = 0; i < n; i++) {
      char* sp = strchr(lines[i], ' ');
      if (sp) *sp = '\0';
      infoRow(card, lines[i], sp ? sp + 1 : "", theme::TEXT, THEME_FONT_TITLE);
    }
    return;
  }
  // System: this build and the radio settings.
  char v[48];
  diag::shortVersion(v, sizeof(v));
  infoRow(card, "Firmware", v);
  infoRow(card, "Built", FIRMWARE_BUILD_DATE);
#ifdef MESHCORE_VERSION
  infoRow(card, "MeshCore", MESHCORE_VERSION);
#endif
  infoRow(card, "Device", board.getManufacturerName());
  infoRow(card, "Node name", the_mesh.getNodeName());
  if (NodePrefs* p = the_mesh.getNodePrefs()) {
    sectionTitle(s_list, "RADIO");
    card = infoCard(s_list);
    snprintf(v, sizeof(v), "%.3f MHz", p->freq);
    infoRow(card, "Frequency", v);
    snprintf(v, sizeof(v), "%.1f kHz", p->bw);
    if (strstr(v, ".0 ")) snprintf(v, sizeof(v), "%.0f kHz", p->bw);
    infoRow(card, "Bandwidth", v);
    snprintf(v, sizeof(v), "%u", (unsigned)p->sf);
    infoRow(card, "Spreading factor", v);
    snprintf(v, sizeof(v), "4/%u", (unsigned)p->cr);
    infoRow(card, "Coding rate", v);
    snprintf(v, sizeof(v), "%d dBm", (int)p->tx_power_dbm);
    infoRow(card, "TX power", v);
  }
}

// From loop(), once a second: the Live values in place.
void UITask::refreshDiag() {
  using namespace diagview;
  if (_screen != SCR_DIAG || s_tab != TAB_LIVE || _nav_overlay) return;
  diag::Row rows[diag::MAX_ROWS + EXTRA];
  int n = allRows(rows, _core->gpsEnabled());
  if (n != s_rows) { buildDiag(); return; }
  for (int i = 0; i < n; i++) setLive(i, rows[i]);   // unchanged text isn't redrawn (UITask.cpp)
}

void UITask::diagResetPopup() {
  lv_obj_t* panel = navPopupPanel("Reset counters?", false);
  lv_obj_t* t = label(panel, "Zeroes packet and error counts.", THEME_FONT_SMALL,
                      theme::TEXT_MUTED);
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(t, LV_PCT(100));
  lv_obj_t* b = lv_button_create(panel);
  lv_obj_set_size(b, LV_PCT(100), 40);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::FAIL), 0);
  lv_obj_add_event_cb(b, onDiagResetGo, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(b, LV_SYMBOL_REFRESH "  Reset", THEME_FONT_BODY, theme::TEXT));
}

void UITask::diagReset() {
  diag::resetCounters();
  navClosePopup();
  refreshDiag();
  showToast("Counters reset");
}
