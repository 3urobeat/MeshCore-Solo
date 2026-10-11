#pragma once
// Included into UITask.cpp only.

#if defined(UI_PERF_L1) && defined(NRF52_PLATFORM)
// -D UI_PERF_L1: walks the main screens by itself (keys fed into the queue) and
// prints, per step over USB serial: frames drawn, frames the display skipped
// as unchanged, render/flush time (avg/max us), heap (used/free/largest
// block) and every FreeRTOS task's stack headroom. Measurement only.
#include "FreeRTOS.h"
#include "task.h"
#include <malloc.h>
extern unsigned char __HeapBase[];
extern unsigned char __HeapLimit[];
extern uint32_t g_disp_flushes;
namespace perfl1 {
  // Each step opens a screen directly (so the walk doesn't depend on which
  // Home pages are enabled), then types keys: R/L/U/D/E/X, H = hold Enter.
  enum Open : uint8_t { NONE, HOME, SETTINGS, MESSAGES, TOOLS, NEARBY, TRAIL, MAP, COMPASS, STATUS,
                        LIVE, LOCATOR, ADVERT, REPEATER, BOT, CLOCKTOOLS, CHANNEL0 };
  struct Step { const char* name; Open open; const char* keys; uint16_t dwell_ms; };
  static const Step STEPS[] = {
    {"boot-idle", NONE, "", 3000},
    {"home", HOME, "", 2000},
    {"home-R1", NONE, "R", 1500}, {"home-R2", NONE, "R", 1500}, {"home-R3", NONE, "R", 1500},
    {"home-R4", NONE, "R", 1500}, {"home-R5", NONE, "R", 1500}, {"home-R6", NONE, "R", 1500},
    {"home-R7", NONE, "R", 1500}, {"home-R8", NONE, "R", 1500},
    {"home-R9", NONE, "R", 1500}, {"home-R10", NONE, "R", 1500}, {"home-R11", NONE, "R", 1500},
    {"home-R12", NONE, "R", 1500}, {"home-R13", NONE, "R", 1500},
    {"settings", SETTINGS, "", 2000}, {"set-display", NONE, "E", 2000},
    {"set-scroll", NONE, "DDDDDDDDDDDD", 2000},
    {"messages", MESSAGES, "", 2000}, {"dm-list", NONE, "E", 2000},
    {"dm-scroll", NONE, "DDDDDDDDDDDDDDDDDDDD", 2000},
    {"dm-thread", NONE, "E", 3000}, {"dm-thread-up", NONE, "UUUU", 2000},
    {"channel", CHANNEL0, "", 3000}, {"channel-up", NONE, "UUUUUU", 2000},
    {"compose", NONE, "EE", 2000}, {"kbd-type", NONE, "RRDRRDRRE", 2000},
    {"kbd-close", NONE, "XXX", 1500},
    {"nearby", NEARBY, "", 3000}, {"nearby-scroll", NONE, "DDDDDDDDDDDDDDDDDDDD", 2000},
    {"tools", TOOLS, "", 1500}, {"trail", TRAIL, "", 2000}, {"map", MAP, "", 3000},
    {"compass", COMPASS, "", 3000},
    {"st-radio", STATUS, "", 3000}, {"st-radio-dn", NONE, "DD", 2000}, {"st-gps", NONE, "UUR", 3000},
    {"st-gps-dn", NONE, "DDD", 2000}, {"st-sky", NONE, "UUUR", 4000}, {"st-signal", NONE, "E", 3000},
    {"st-power", NONE, "R", 2000}, {"st-mesh", NONE, "R", 2000}, {"st-mesh-dn", NONE, "DDD", 2000},
    {"st-system", NONE, "UUUR", 2000}, {"st-system-dn", NONE, "DDDDDD", 2000},
    {"liveshare", LIVE, "", 1500}, {"locator", LOCATOR, "", 1500}, {"autoadvert", ADVERT, "", 1500},
    {"repeater", REPEATER, "", 1500}, {"bot", BOT, "", 1500}, {"clocktools", CLOCKTOOLS, "", 1500},
    {"home-end", HOME, "", 3000},
    {"done", NONE, "", 500},
  };
  static int step = -1;
  static uint32_t step_at = 0, frames = 0, r_sum = 0, r_max = 0, f_sum = 0, f_max = 0, fl0 = 0;
  static uint32_t heap_min_free = 0xFFFFFFFF;

  static uint32_t largestBlock() {   // bisect the biggest malloc that succeeds
    uint32_t lo = 0, hi = (uint32_t)(__HeapLimit - __HeapBase);
    while (hi - lo > 64) {
      uint32_t mid = (lo + hi) / 2;
      void* q = malloc(mid);
      if (q) { free(q); lo = mid; } else hi = mid;
    }
    return lo;
  }
  static void heap(uint32_t& used, uint32_t& freeb) {
    uint32_t total = (uint32_t)(__HeapLimit - __HeapBase);
    used = (uint32_t)mallinfo().uordblks;
    freeb = used < total ? total - used : 0;
  }
  void frame(uint32_t render_us, uint32_t flush_us) {
    frames++; r_sum += render_us; f_sum += flush_us;
    if (render_us > r_max) r_max = render_us;
    if (flush_us > f_max) f_max = flush_us;
  }
  static void report(const char* name) {
    uint32_t used, freeb; heap(used, freeb);
    if (freeb < heap_min_free) heap_min_free = freeb;
    uint32_t fl = g_disp_flushes - fl0;
    Serial.printf("PERF %-12s frames %3lu sent %3lu render avg %5lu max %6lu  flush avg %5lu max %6lu us | heap used %lu free %lu largest %lu min %lu\n",
      name, frames, fl, frames ? r_sum / frames : 0, r_max, frames ? f_sum / frames : 0, f_max,
      used, freeb, largestBlock(), heap_min_free);
  }
  static void tasks() {
    TaskStatus_t st[16];
    UBaseType_t n = uxTaskGetSystemState(st, 16, NULL);
    for (UBaseType_t i = 0; i < n; i++)
      Serial.printf("TASK %-10s stack free %5lu B\n", st[i].pcTaskName, (unsigned long)st[i].usStackHighWaterMark * sizeof(StackType_t));
  }
  static DisplayDriver* disp = nullptr;
  static void shot(const char* name) {   // the 1 KB page buffer as hex, for a PNG on the host
    if (!disp || !disp->getBuffer()) return;
    const uint8_t* b = disp->getBuffer();
    Serial.printf("SHOT %s ", name);
    for (uint16_t i = 0; i < disp->getBufferSize(); i++) Serial.printf("%02x", b[i]);
    Serial.println();
  }
  static char keyOf(char c) {
    switch (c) { case 'R': return KEY_RIGHT; case 'L': return KEY_LEFT; case 'U': return KEY_UP;
      case 'D': return KEY_DOWN; case 'E': return KEY_ENTER; case 'X': return KEY_CANCEL; case 'H': return KEY_CONTEXT_MENU; }
    return 0;
  }
  static const char* pending = nullptr;
  static uint32_t next_key_at = 0;
  // Returns a key to enqueue (0 = none).
  Open open_now = NONE;
  char tick() {
    uint32_t now = millis();
    if (step >= (int)(sizeof(STEPS) / sizeof(STEPS[0]))) return 0;
    if (pending && *pending) {
      if ((int32_t)(now - next_key_at) < 0) return 0;
      next_key_at = now + 300;
      char k = keyOf(*pending++);
      if (!*pending) { step_at = now; frames = r_sum = r_max = f_sum = f_max = 0; fl0 = g_disp_flushes; }
      return k;
    }
    if (step >= 0 && (int32_t)(now - step_at) < (int32_t)STEPS[step].dwell_ms) return 0;
    if (step >= 0) { report(STEPS[step].name); shot(STEPS[step].name); }
    step++;
    if (step >= (int)(sizeof(STEPS) / sizeof(STEPS[0]))) { tasks(); Serial.println("PERF end"); return 0; }
    if (step == 0) tasks();
    pending = STEPS[step].keys;
    open_now = STEPS[step].open;
    step_at = now; frames = r_sum = r_max = f_sum = f_max = 0; fl0 = g_disp_flushes;
    next_key_at = now;
    return 0;
  }
}
// micros() ticks in ~1 ms steps here (RTC-based), so time with the CPU cycle counter.
static inline uint32_t perfUs() {
  static bool on = false;
  if (!on) { CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk; on = true; }
  return DWT->CYCCNT;   // cycles; differences stay right across the 67 s wrap
}
#define PERF_T0() uint32_t _pf0 = perfUs()
#define PERF_T1() uint32_t _pf1 = perfUs()
#define PERF_T2() perfl1::frame((_pf1 - _pf0) / (SystemCoreClock / 1000000), (perfUs() - _pf1) / (SystemCoreClock / 1000000))
#else
#define PERF_T0()
#define PERF_T1()
#define PERF_T2()
#endif
