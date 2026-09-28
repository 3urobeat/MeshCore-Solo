#pragma once
// The browser simulator has no GPS receiver: a made-up sky (GPS, BeiDou and
// GLONASS satellites drifting slowly round), sent through the real GpsSky
// parser as NMEA once a second. The fix comes 20 s after the first ask.
// Both UIs' satellite screens read it through gpsSkySim().

#include "GpsSky.h"
#include <math.h>
#include <stdio.h>

static GpsSky s_gps_sim_sky;
static void gpsSimSentence(const char* body) {
  uint8_t cs = 0;
  for (const char* p = body; *p; p++) cs ^= (uint8_t)*p;
  char line[120];
  snprintf(line, sizeof(line), "$%s*%02X\r\n", body, cs);
  for (const char* p = line; *p; p++) s_gps_sim_sky.feed(*p);
}
static void gpsSimFeed() {
  static uint32_t s_next = 0, s_t0 = 0;
  uint32_t now = millis();
  if (!s_t0) s_t0 = now;
  if ((int32_t)(now - s_next) < 0) return;
  s_next = now + 1000;
  struct SimSat { const char* talker; int prn, elev, az, snr; };
  static const SimSat SATS[] = {
    { "GP", 2, 72, 40, 44 }, { "GP", 5, 48, 120, 41 }, { "GP", 12, 31, 205, 36 }, { "GP", 15, 18, 290, 27 },
    { "GP", 20, 63, 310, 43 }, { "GP", 25, 9, 75, 18 }, { "GP", 29, 22, 160, 31 }, { "GP", 44, 30, 190, 33 },
    { "BD", 7, 55, 250, 38 }, { "BD", 10, 40, 20, 35 }, { "BD", 23, 12, 140, -1 }, { "BD", 37, 67, 95, 42 },
    { "GL", 71, 35, 330, 29 }, { "GL", 72, 50, 260, 34 }, { "GL", 80, 6, 30, -1 },
  };
  const int N = sizeof(SATS) / sizeof(SATS[0]);
  float t = (now - s_t0) / 1000.0f;
  bool fix = t > 20;
  char b[110];
  for (const char* talker : { "GP", "BD", "GL" }) {
    const SimSat* mine[8];
    int k = 0;
    for (int i = 0; i < N; i++) if (!strcmp(SATS[i].talker, talker)) mine[k++] = &SATS[i];
    int msgs = (k + 3) / 4;
    for (int m = 0; m < msgs; m++) {
      int o = snprintf(b, sizeof(b), "%sGSV,%d,%d,%02d", talker, msgs, m + 1, k);
      for (int j = m * 4; j < k && j < m * 4 + 4; j++) {
        const SimSat& s = *mine[j];
        int az = ((int)(s.az + t / 6) % 360);
        int snr = s.snr < 0 ? -1 : s.snr - (fix ? 0 : 8) + (int)(3 * sinf(t / 3 + s.prn));
        if (snr >= 0) o += snprintf(b + o, sizeof(b) - o, ",%02d,%02d,%03d,%02d", s.prn, s.elev, az, snr);
        else o += snprintf(b + o, sizeof(b) - o, ",%02d,%02d,%03d,", s.prn, s.elev, az);
      }
      gpsSimSentence(b);
    }
  }
  if (fix) {
    gpsSimSentence("GNGSA,A,3,02,05,12,15,20,29,,,,,,,1.6,0.9,1.3,1");
    gpsSimSentence("GNGSA,A,3,07,10,37,,,,,,,,,,1.6,0.9,1.3,4");
    gpsSimSentence("GNGSA,A,3,72,,,,,,,,,,,,1.6,0.9,1.3,2");
    gpsSimSentence("GNGGA,120000.00,5213.0000,N,02100.0000,E,1,10,0.9,112.4,M,34.5,M,,");
    gpsSimSentence("GNRMC,120000.00,A,5213.0000,N,02100.0000,E,0.4,87.0,260926,,,A");
  } else {
    gpsSimSentence("GNGSA,A,1,,,,,,,,,,,,,99.9,99.9,99.9,1");
    gpsSimSentence("GNGGA,120000.00,,,,,0,00,99.9,,,,,,");
    gpsSimSentence("GNRMC,120000.00,V,,,,,,,260926,,,N");
  }
}

static GpsSky* gpsSkySim() {
  gpsSimFeed();
  return &s_gps_sim_sky;
}
