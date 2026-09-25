#pragma once
// Outgoing message text, shared by ui-new's Messages and ui-lvgl's thread:
// the placeholders a message can carry ({loc}, {time}, {batt}, live sensor
// readings), expanding them with this device's position / clock / battery /
// sensors when it's sent, and the ten quick messages (NodePrefs::custom_msgs),
// sent with one tap and expanded the same way.
//
// Include after target.h (sensors, board, rtc_clock).

#include "../MsgExpand.h"

namespace msgtext {

// ── Quick messages ────────────────────────────────────────────────────────────
static const int QUICK_COUNT = 10;
static const int QUICK_LEN = (int)sizeof(((NodePrefs*)0)->custom_msgs[0]);   // incl. the terminator

static const char* quick(const NodePrefs* p, int i) {
  return p && i >= 0 && i < QUICK_COUNT ? p->custom_msgs[i] : "";
}
// "" clears the slot. The caller saves the prefs.
static void setQuick(NodePrefs* p, int i, const char* text) {
  if (p && i >= 0 && i < QUICK_COUNT) snprintf(p->custom_msgs[i], QUICK_LEN, "%s", text ? text : "");
}
static int quickUsed(const NodePrefs* p) {
  int n = 0;
  for (int i = 0; i < QUICK_COUNT; i++) if (quick(p, i)[0]) n++;
  return n;
}
// A fresh device starts with "OK" in the first slot.
static void seedQuick(NodePrefs* p) {
  if (p && !p->custom_msgs[0][0]) setQuick(p, 0, "OK");
}

// ── Placeholders ──────────────────────────────────────────────────────────────

// The sensor readings this device has right now, as placeholders ({temp},
// {hum}, ...; {batt} only from a voltage sensor).
static void sensorPlaceholders(SensorManager* sm, void (*add)(const char* ph, void* ctx), void* ctx) {
  if (!sm) return;
  CayenneLPP lpp(100);
  sm->querySensors(0xFF, lpp);
  uint8_t present[16];
  int pc = 0;
  LPPReader r(lpp.getBuffer(), lpp.getSize());
  uint8_t ch, type;
  while (r.readHeader(ch, type)) {
    bool seen = false;
    for (int i = 0; i < pc; i++) if (present[i] == type) { seen = true; break; }
    if (!seen && pc < 16) present[pc++] = type;
    r.skipData(type);
  }
  static const struct { uint8_t t; const char* ph; } MAP[] = {
    { LPP_TEMPERATURE, "{temp}" }, { LPP_RELATIVE_HUMIDITY, "{hum}" }, { LPP_BAROMETRIC_PRESSURE, "{pres}" },
    { LPP_VOLTAGE, "{batt}" },     { LPP_ALTITUDE, "{alt}" },          { LPP_LUMINOSITY, "{lux}" },
    { LPP_DISTANCE, "{dist}" },    { LPP_CONCENTRATION, "{co2}" },
  };
  for (const auto& m : MAP)
    for (int i = 0; i < pc; i++)
      if (present[i] == m.t) { add(m.ph, ctx); break; }
}

// Everything a message can carry here: {loc}, {time}, {batt} (the board's
// voltage), then the sensors'.
static void placeholders(SensorManager* sm, void (*add)(const char* ph, void* ctx), void* ctx) {
  add("{loc}", ctx);
  add("{time}", ctx);
  add("{batt}", ctx);
  struct Skip { void (*add)(const char*, void*); void* ctx; } skip = { add, ctx };
  sensorPlaceholders(sm, [](const char* ph, void* c) {
    Skip* s = (Skip*)c;
    if (strcmp(ph, "{batt}") != 0) s->add(ph, s->ctx);   // already there
  }, &skip);
}

// ── Expanding ─────────────────────────────────────────────────────────────────

// Placeholders in `tmpl` replaced with this device's values.
static void expand(const char* tmpl, char* out, int n, const NodePrefs* p) {
  double lat = 0, lon = 0;
  bool gps_valid = false;
#if ENV_INCLUDE_GPS == 1
  LocationProvider* loc = sensors.getLocationProvider();
  if (loc && loc->isValid()) {
    lat = loc->getLatitude() / 1000000.0;
    lon = loc->getLongitude() / 1000000.0;
    gps_valid = true;
  }
#endif
  float batt = (float)board.getBattMilliVolts() / 1000.0f;
  ::expandMsg(tmpl, out, n, lat, lon, gps_valid, rtc_clock.getCurrentTime(),
              p ? p->tz_offset_hours : 0, &sensors, batt);
}

// A typed message: a reply's leading "@[nick] " stays as it is (a nick may
// hold braces), the rest is expanded.
static void expandOutgoing(const char* text, char* out, int n, const NodePrefs* p) {
  int pre = 0;
  if (text[0] == '@' && text[1] == '[') {
    const char* e = strstr(text, "] ");
    if (e) pre = (int)(e + 2 - text);
  }
  if (pre >= n) pre = n - 1;
  memcpy(out, text, pre);
  expand(text + pre, out + pre, n - pre, p);
}

}  // namespace msgtext
