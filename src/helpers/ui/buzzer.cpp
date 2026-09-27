#include "Arduino.h"
#ifdef PIN_BUZZER
#include "buzzer.h"

void genericBuzzer::begin() {
    // No real GPIO pin to configure in the sim -- PIN_BUZZER there is just a
    // dummy sentinel value so the #ifdef PIN_BUZZER guards elsewhere (this
    // file included) activate at all; variants/sim/arduino/Arduino.h
    // deliberately has no pinMode()/digitalWrite() shim since nothing else
    // ever needed one before this.
#if !defined(SIM_PLATFORM) && !defined(BUZZER_I2S)
    #ifdef PIN_BUZZER_EN
      pinMode(PIN_BUZZER_EN, OUTPUT);
      digitalWrite(PIN_BUZZER_EN, HIGH);
    #endif
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW); // need to pull low by default to avoid extreme power draw
#endif
#if defined(NRF52_PLATFORM)
    _isr_instance = this;
    NRF_TIMER1->TASKS_STOP  = 1;
    NRF_TIMER1->MODE        = TIMER_MODE_MODE_Timer << TIMER_MODE_MODE_Pos;
    NRF_TIMER1->BITMODE     = TIMER_BITMODE_BITMODE_32Bit << TIMER_BITMODE_BITMODE_Pos;
    NRF_TIMER1->PRESCALER   = 4;   // 16 MHz / 2^4 = 1 MHz -> 1 us/tick
    NRF_TIMER1->SHORTS      = TIMER_SHORTS_COMPARE0_CLEAR_Msk | TIMER_SHORTS_COMPARE0_STOP_Msk;
    NRF_TIMER1->INTENSET    = TIMER_INTENSET_COMPARE0_Msk;
    // Lowest application priority: this only ever reschedules a tone, it must
    // never contend with anything radio/BLE-timing-critical.
    NVIC_SetPriority(TIMER1_IRQn, 7);
    NVIC_ClearPendingIRQ(TIMER1_IRQn);
    NVIC_EnableIRQ(TIMER1_IRQn);
#endif
#if defined(BUZZER_I2S)
    if (!_i2sBegin()) return;   // no codec: stay silent
#endif
    startup();
}

void genericBuzzer::quiet(bool buzzer_state) {
    _is_quiet = buzzer_state;
#ifdef PIN_BUZZER_EN
    digitalWrite(PIN_BUZZER_EN, _is_quiet ? LOW : HIGH);
#endif
}

bool genericBuzzer::isQuiet() { return _is_quiet; }

void genericBuzzer::startup()  { play(startup_song); }
void genericBuzzer::shutdown() { play(shutdown_song); }

// ---------------------------------------------------------------------------
// Shared RTTTL parser -- pure string/arithmetic, no hardware access, so both
// the NRF52 direct-PWM player and the sim's poll-only player (below) reuse
// it verbatim instead of each carrying their own copy.
// ---------------------------------------------------------------------------
#if defined(NRF52_PLATFORM) || defined(SIM_PLATFORM) || defined(BUZZER_I2S)

// Chromatic frequencies for octave 4 (Hz): C C# D D# E F F# G G# A A# B
static const uint16_t CHROM4[12] = { 262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494 };

// Map 'a'-'g' → chromatic index within octave
static const uint8_t NOTE_IDX[7] = { 9, 11, 0, 2, 4, 5, 7 };  // a b c d e f g

uint16_t genericBuzzer::_noteFreq(char letter, bool sharp, uint8_t octave) {
    if (letter == 'p') return 0;
    if (letter < 'a' || letter > 'g') return 0;
    uint8_t idx = NOTE_IDX[letter - 'a'];
    if (sharp) { if (++idx >= 12) { idx = 0; octave++; } }
    if (octave < 4) octave = 4;
    if (octave > 8) octave = 8;   // parser accepts octaves 4-8; B8 (~7.9 kHz) is within range
    uint32_t f = (uint32_t)CHROM4[idx] << (octave - 4);
    return (uint16_t)(f > 25000 ? 25000 : f);
}

void genericBuzzer::_parseHeader(const char* melody, uint8_t& def_dur, uint8_t& def_oct,
                                  uint16_t& bpm, const char*& notes) {
    def_dur = 4; def_oct = 5; bpm = 120;
    const char* p = melody;
    while (*p && *p != ':') p++;
    if (*p == ':') p++;
    while (*p && *p != ':') {
        while (*p == ' ' || *p == ',') p++;
        if      (p[0]=='d' && p[1]=='=') { p+=2; def_dur=(uint8_t)atoi(p); while(*p&&*p!=','&&*p!=':')p++; }
        else if (p[0]=='o' && p[1]=='=') { p+=2; def_oct=(uint8_t)atoi(p); while(*p&&*p!=','&&*p!=':')p++; }
        else if (p[0]=='b' && p[1]=='=') { p+=2; bpm=(uint16_t)atoi(p);    while(*p&&*p!=','&&*p!=':')p++; }
        else { while(*p&&*p!=','&&*p!=':')p++; }
    }
    if (*p == ':') p++;
    notes = p;
}

bool genericBuzzer::_parseNext(const char*& p, uint8_t def_dur, uint8_t def_oct, uint16_t bpm,
                                uint16_t& freq, uint32_t& dur_ms) {
    while (*p == ' ' || *p == ',') p++;
    if (*p == '\0') return false;

    uint8_t dur = def_dur;
    if (*p >= '0' && *p <= '9') { dur=(uint8_t)atoi(p); while(*p>='0'&&*p<='9')p++; }
    if (dur == 0) dur = 4;

    if (*p == '\0') return false;
    char note = *p++;
    bool sharp = (*p == '#') ? (p++, true) : false;
    uint8_t oct = def_oct;
    if (*p >= '4' && *p <= '8') oct = (uint8_t)(*p++ - '0');
    bool dot = (*p == '.') ? (p++, true) : false;

    dur_ms = (60000UL * 4UL) / ((uint32_t)bpm * dur);
    if (dot) dur_ms = dur_ms * 3 / 2;

    freq = _noteFreq(note, sharp, oct);
    return true;
}

int genericBuzzer::noteIndex() const { return _note_idx; }

#endif // NRF52_PLATFORM || SIM_PLATFORM || BUZZER_I2S

// ---------------------------------------------------------------------------
// nRF52 path — direct NRF_PWM2 control, bypasses tone()
// ---------------------------------------------------------------------------
#if defined(NRF52_PLATFORM)

uint8_t genericBuzzer::_dutyPct() const {
    // Inverted polarity (0x8000 bit): duty_HIGH = 100% - PCT.
    // Values chosen for ~6-8 dB perceptual steps: -24/-16/-9/-3/0 dB.
    static const uint8_t PCT[5] = { 2, 5, 12, 25, 50 };
    return PCT[_volume_level < 5 ? _volume_level : 4];
}

void genericBuzzer::_nrfStartPwm(uint16_t freq) {
    if (freq < 20 || freq > 25000) { _nrfStopPwm(); return; }

    uint32_t nrf_pin = g_ADigitalPinMap[PIN_BUZZER];
    uint16_t top = 125000 / freq;
    uint16_t cmp = (uint16_t)(((uint32_t)top * _dutyPct()) / 100);
    if (cmp == 0) cmp = 1;  // inverted polarity: cmp=0 → 100% HIGH → no AC → silence

    // Write duty BEFORE SEQSTART so DMA reads our value on the very first period
    _duty_buf = 0x8000U | cmp;
    __DMB();

    // Only wait for STOPPED if PWM was actually running — TASKS_STOP on a disabled
    // PWM never fires EVENTS_STOPPED, so the wait would always time out at 2 ms.
    if (_pwm_on) {
      NRF_PWM2->TASKS_STOP = 1;
      uint32_t t = millis();
      while (!(NRF_PWM2->EVENTS_STOPPED) && (millis() - t) < 2) {}
      NRF_PWM2->EVENTS_STOPPED = 0;
    }

    NRF_PWM2->PSEL.OUT[0] = nrf_pin;
    NRF_PWM2->PSEL.OUT[1] = 0xFFFFFFFFUL;
    NRF_PWM2->PSEL.OUT[2] = 0xFFFFFFFFUL;
    NRF_PWM2->PSEL.OUT[3] = 0xFFFFFFFFUL;
    NRF_PWM2->ENABLE      = PWM_ENABLE_ENABLE_Enabled << PWM_ENABLE_ENABLE_Pos;
    NRF_PWM2->MODE        = PWM_MODE_UPDOWN_Up << PWM_MODE_UPDOWN_Pos;
    // DIV_128 on 16 MHz = 125 kHz — same clock as tone(), so same frequency math
    NRF_PWM2->PRESCALER   = PWM_PRESCALER_PRESCALER_DIV_128 << PWM_PRESCALER_PRESCALER_Pos;
    NRF_PWM2->COUNTERTOP  = top;
    NRF_PWM2->DECODER     = (PWM_DECODER_LOAD_Common     << PWM_DECODER_LOAD_Pos) |
                             (PWM_DECODER_MODE_RefreshCount << PWM_DECODER_MODE_Pos);
    NRF_PWM2->SHORTS      = PWM_SHORTS_LOOPSDONE_SEQSTART0_Msk;
    NRF_PWM2->LOOP        = 0xFFFFUL << PWM_LOOP_CNT_Pos;

    // Both SEQ0 and SEQ1 point to the same buffer; REFRESH=0 means DMA re-reads every period
    NRF_PWM2->SEQ[0].PTR      = (uint32_t)&_duty_buf;
    NRF_PWM2->SEQ[0].CNT      = 1;
    NRF_PWM2->SEQ[0].REFRESH  = 0;
    NRF_PWM2->SEQ[0].ENDDELAY = 0;
    NRF_PWM2->SEQ[1].PTR      = (uint32_t)&_duty_buf;
    NRF_PWM2->SEQ[1].CNT      = 1;
    NRF_PWM2->SEQ[1].REFRESH  = 0;
    NRF_PWM2->SEQ[1].ENDDELAY = 0;

    NRF_PWM2->TASKS_SEQSTART[0] = 1;
    _pwm_on = true;
}

void genericBuzzer::_nrfStopPwm() {
    NRF_PWM2->TASKS_STOP  = 1;
    NRF_PWM2->PSEL.OUT[0] = 0xFFFFFFFFUL;
    NRF_PWM2->ENABLE      = 0;
    digitalWrite(PIN_BUZZER, LOW);
    _pwm_on = false;
}

genericBuzzer* genericBuzzer::_isr_instance = nullptr;

void genericBuzzer::_armNoteTimer(uint32_t dur_ms) {
    NRF_TIMER1->TASKS_STOP  = 1;
    NRF_TIMER1->TASKS_CLEAR = 1;
    NRF_TIMER1->EVENTS_COMPARE[0] = 0;
    NRF_TIMER1->CC[0] = dur_ms * 1000UL;   // 1 us/tick (see PRESCALER in begin())
    NRF_TIMER1->TASKS_START = 1;
}

// Halt the timer AND drop any interrupt it already latched. Stopping the timer
// and clearing EVENTS_COMPARE[0] de-asserts the IRQ source, but an interrupt
// the NVIC latched just before we stopped stays pending and would fire one
// spurious _nrfAdvance() after we return — skipping the first note of a new
// melody, or sounding a blip just after an explicit stop. Order matters: clear
// the event (with a read-back to flush the write buffer, per the nRF52 event
// anomaly) before clearing the NVIC, else the still-set event re-latches it.
void genericBuzzer::_disarmNoteTimer() {
    NRF_TIMER1->TASKS_STOP = 1;
    NRF_TIMER1->EVENTS_COMPARE[0] = 0;
    (void)NRF_TIMER1->EVENTS_COMPARE[0];
    NVIC_ClearPendingIRQ(TIMER1_IRQn);
}

// Static member (not a free function) so it can reach private state without a
// friend declaration — same trick MomentaryButton's isrTrampolineN() uses.
// The real ISR (TIMER1_IRQHandler, below) is just a one-line dispatch to this.
void genericBuzzer::_timer1ISR() {
    NRF_TIMER1->EVENTS_COMPARE[0] = 0;
    (void)NRF_TIMER1->EVENTS_COMPARE[0];   // flush write buffer so the IRQ doesn't immediately re-fire (nRF52 anomaly)
    if (_isr_instance) _isr_instance->_nrfAdvance();
}

void genericBuzzer::_nrfBegin(const char* melody) {
    _disarmNoteTimer();   // drop any in-flight/pending note advance before reconfiguring
    _nrfStopPwm();
    _note_idx = -1;
    if (!melody || !*melody) { _rtttl_done = true; return; }
    const char* notes;
    _parseHeader(melody, _def_dur, _def_oct, _def_bpm, notes);
    _rtttl_pos  = notes;
    _rtttl_done = false;
    _nrfAdvance();
}

void genericBuzzer::_nrfAdvance() {
    uint16_t freq; uint32_t dur_ms;
    if (_parseNext(_rtttl_pos, _def_dur, _def_oct, _def_bpm, freq, dur_ms)) {
        _note_idx++;
        _armNoteTimer(dur_ms);
        if (freq > 0) _nrfStartPwm(freq); else _nrfStopPwm();
    } else {
        _nrfStopPwm();
        _note_idx = -1;
        _rtttl_done = true;
    }
}

void genericBuzzer::applyVolume() {
    if (!_pwm_on) return;
    uint16_t top = (uint16_t)NRF_PWM2->COUNTERTOP;
    uint16_t cmp = (uint16_t)(((uint32_t)top * _dutyPct()) / 100);
    if (cmp == 0) cmp = 1;
    _duty_buf = 0x8000U | cmp;  // DMA picks this up within one period (< 2.3 ms at A4)
}

void genericBuzzer::play(const char* melody) {
    if (_is_quiet) return;
    _nrfBegin(melody);
}

void genericBuzzer::playForced(const char* melody) {
    _nrfBegin(melody);
}

bool genericBuzzer::isPlaying() { return !_rtttl_done; }

void genericBuzzer::stop() {
    _disarmNoteTimer();   // ensure no latched note-advance fires after we stop
    _nrfStopPwm();
    _note_idx = -1;
    _rtttl_done = true;
}

// No-op: TIMER1's compare interrupt (_timer1ISR -> _nrfAdvance) now drives
// note advancement directly, so timing no longer depends on how often (or
// whether) the caller's loop() gets to run. Kept as a real method, not
// removed, since UITask polls buzzer.loop() unconditionally for both
// platforms.
void genericBuzzer::loop() {}

extern "C" void TIMER1_IRQHandler(void) {
    genericBuzzer::_timer1ISR();
}

void genericBuzzer::setVolume(uint8_t level) {
    _volume_level = level < 5 ? level : 4;
    applyVolume();
}

// ---------------------------------------------------------------------------
// Sim path — no real PWM/timer hardware; just track (freq, note-end-time)
// and let loop() poll millis() to advance, same non-blocking shape as the
// NonBlockingRtttl path below minus the library. A host page polls
// currentFreqHz()/isPlaying() every frame to drive a Web Audio oscillator
// (see variants/sim/web/index.html) instead of sounding real hardware.
// ---------------------------------------------------------------------------
#elif defined(SIM_PLATFORM)

void genericBuzzer::_advance() {
    uint16_t freq; uint32_t dur_ms;
    if (_parseNext(_rtttl_pos, _def_dur, _def_oct, _def_bpm, freq, dur_ms)) {
        _note_idx++;
        _cur_freq = freq;
        _note_end_ms = millis() + dur_ms;
    } else {
        _note_idx = -1;
        _cur_freq = 0;
        _rtttl_done = true;
    }
}

void genericBuzzer::applyVolume() {
    // No hardware duty cycle to touch -- the host page maps getVolume()
    // (0-4) to a Web Audio gain value itself.
}

void genericBuzzer::play(const char* melody) {
    if (_is_quiet) return;
    playForced(melody);
}

void genericBuzzer::playForced(const char* melody) {
    _rtttl_done = true;
    _cur_freq = 0;
    _note_idx = -1;
    if (!melody || !*melody) return;
    const char* notes;
    _parseHeader(melody, _def_dur, _def_oct, _def_bpm, notes);
    _rtttl_pos  = notes;
    _rtttl_done = false;
    _advance();
}

bool genericBuzzer::isPlaying() { return !_rtttl_done; }

void genericBuzzer::stop() {
    _rtttl_done = true;
    _cur_freq = 0;
    _note_idx = -1;
}

void genericBuzzer::loop() {
    if (_rtttl_done) return;
    if ((int32_t)(millis() - _note_end_ms) >= 0) _advance();
}

void genericBuzzer::setVolume(uint8_t level) {
    _volume_level = level < 5 ? level : 4;
}

#elif defined(BUZZER_I2S)

// ---------------------------------------------------------------------------
// I2S codec path -- a speaker behind an ES8311 (Wio Tracker L2). An audio
// task synthesises a sine per note and counts samples to end it, so timing
// holds however long the UI loop stalls (map tiles, SD). The task only
// touches I2S; the codec (I2C) and the amp (the board's IO expander, I2C
// too) are driven from the caller's thread, which owns the bus.
// ---------------------------------------------------------------------------

#include <esp_heap_caps.h>
#include "ES8311.h"
#if ESP_IDF_VERSION_MAJOR >= 5
  #include <driver/i2s_std.h>   // Arduino-ESP32 3.x (the legacy driver goes in IDF 6)
#else
  #include <driver/i2s.h>
#endif

#ifndef AUDIO_AMP_SETTLE_MS
  // Silence after amp power-up, so the first note isn't clipped. Seeed's
  // Meshtastic port waits 250 ms, PR #3381's player 3 ms: in between.
  #define AUDIO_AMP_SETTLE_MS 100
#endif

static const int      SAMPLE_RATE   = 16000;
static const uint32_t AMP_LINGER_MS = 3000;   // amp stays on between close sounds (no settle wait each time)
static const uint32_t CLK_SETTLE_MS = 30;     // codec clocked this long before the amp comes on
// Note edges follow a raised cosine (a linear 3 ms ramp still ticked): the
// attack, the release, and the fade of a note cut short by the next sound.
static const uint32_t ATTACK        = 80;     // 5 ms
static const uint32_t RELEASE       = 160;    // 10 ms
static const int      CUT_FADE      = 240;    // 15 ms
static const int      CHUNK         = 128;    // frames per i2s_write
static portMUX_TYPE   s_mux         = portMUX_INITIALIZER_UNLOCKED;
static int16_t        s_sine[256];
static int16_t        s_ease[65];     // (1 - cos(pi x)) / 2 over 0..1, Q15

// Gain `peak` eased in over `n` samples: position x of n.
static int32_t ease(int32_t peak, uint32_t x, uint32_t n) {
  if (x >= n) return peak;
  return peak * s_ease[x * 64 / n] / 32767;
}
static uint32_t       s_amp_on_ms   = 0;

// Peak sample per volume level: -24/-16/-9/-3/0 dB like the nRF52 duty
// steps, under an -8 dBFS ceiling (the class-D amp is loud near full scale;
// PR #3381 played at about -21 dBFS and called it gentle).
static int16_t peakFor(uint8_t level) {
  static const int16_t PEAK[5] = { 820, 2060, 4620, 9220, 13000 };
  return PEAK[level < 5 ? level : 4];
}

// ── The I2S channel: 16 kHz, 16-bit stereo, 6 DMA buffers of CHUNK frames,
// MCLK = 256 fs; an underrun plays silence, not the last buffer again. ──────
static const uint32_t WAIT_FOREVER = 0xFFFFFFFF;
#if ESP_IDF_VERSION_MAJOR >= 5
static i2s_chan_handle_t s_tx = nullptr;
static bool i2sInstall() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.dma_desc_num = 6;   // 48 ms queued: short, since the task keeps it topped up
  cc.dma_frame_num = CHUNK;
  cc.auto_clear_after_cb = true;
  if (i2s_new_channel(&cc, &s_tx, nullptr) != ESP_OK) return false;
  i2s_std_config_t sc = {};
  sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
  sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  sc.gpio_cfg.mclk = (gpio_num_t)PIN_I2S_MCLK;
  sc.gpio_cfg.bclk = (gpio_num_t)PIN_I2S_BCK;
  sc.gpio_cfg.ws = (gpio_num_t)PIN_I2S_WS;
  sc.gpio_cfg.dout = (gpio_num_t)PIN_I2S_DOUT;
  sc.gpio_cfg.din = I2S_GPIO_UNUSED;
  if (i2s_channel_init_std_mode(s_tx, &sc) != ESP_OK || i2s_channel_enable(s_tx) != ESP_OK) {
    i2s_del_channel(s_tx); s_tx = nullptr;
    return false;
  }
  return true;
}
static void i2sUninstall() { i2s_channel_disable(s_tx); i2s_del_channel(s_tx); s_tx = nullptr; }
static void i2sWrite(const void* src, size_t n, uint32_t ms) {
  if (!s_tx) return;
  size_t w;
  i2s_channel_write(s_tx, src, n, &w, ms);
}
// Clocks off between sounds: a disabled channel still drives MCLK, and its
// fractional divider (160 MHz / 39 1/16) makes a comb of lines every 256 kHz
// right up the LoRa band -- 869.632 MHz sat in the EU narrow channel, ~15 dB
// over the Wio Tracker L2's noise floor. So the channel goes, the pins held
// low, and a sound makes it again.
static void i2sStop() {
  if (!s_tx) return;
  i2sUninstall();
  const int pins[] = { PIN_I2S_MCLK, PIN_I2S_BCK, PIN_I2S_WS, PIN_I2S_DOUT };
  for (int p : pins) { gpio_reset_pin((gpio_num_t)p); pinMode(p, OUTPUT); digitalWrite(p, LOW); }
}
static void i2sRestart() { if (!s_tx) i2sInstall(); }
#else
static const i2s_port_t I2S_PORT = I2S_NUM_0;
static bool i2sInstall() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = CHUNK;
  cfg.tx_desc_auto_clear = true;
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  i2s_pin_config_t pins = {};
  pins.mck_io_num = PIN_I2S_MCLK;
  pins.bck_io_num = PIN_I2S_BCK;
  pins.ws_io_num = PIN_I2S_WS;
  pins.data_out_num = PIN_I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_driver_install(I2S_PORT, &cfg, 0, nullptr) != ESP_OK) return false;
  if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) { i2s_driver_uninstall(I2S_PORT); return false; }
  i2s_zero_dma_buffer(I2S_PORT);
  return true;
}
static void i2sUninstall() { i2s_driver_uninstall(I2S_PORT); }
static void i2sWrite(const void* src, size_t n, uint32_t ms) {
  size_t w;
  i2s_write(I2S_PORT, src, n, &w, ms == WAIT_FOREVER ? portMAX_DELAY : pdMS_TO_TICKS(ms));
}
static void i2sStop()    { i2s_stop(I2S_PORT); }
static void i2sRestart() { i2s_zero_dma_buffer(I2S_PORT); i2s_start(I2S_PORT); }
#endif

bool genericBuzzer::_i2sBegin() {
  // i2s_driver_install() crashes in IDF's cleanup when its DMA allocation
  // fails (PR #3381 saw a boot loop): don't try without clear headroom.
  if (heap_caps_get_free_size(MALLOC_CAP_DMA) < 32000) return false;
  for (int i = 0; i < 256; i++) s_sine[i] = (int16_t)(32767.0f * sinf(i * 2.0f * (float)M_PI / 256.0f));
  for (int i = 0; i <= 64; i++) s_ease[i] = (int16_t)(32767.0f * 0.5f * (1.0f - cosf(i * (float)M_PI / 64.0f)));

  if (!i2sInstall()) return false;

  // MCLK is running now, so the codec's clock tree comes up with it.
  if (!es8311::begin(Wire, BUZZER_CODEC_ES8311)) {
    i2sUninstall();
    return false;
  }
  _clk_on_ms = millis();
  _clk_running = true;
  delay(CLK_SETTLE_MS);   // settled before the startup sound powers the amp
  // Above the UI loop and LVGL (priority 1), so rendering never starves it.
  _i2s_ok = xTaskCreate(_taskEntry, "buzzer", 3072, this, 6, (TaskHandle_t*)&_task) == pdPASS;
  return _i2s_ok;
}

void genericBuzzer::_taskEntry(void* self) { ((genericBuzzer*)self)->_taskLoop(); }

// Waits for play()/stop(), then plays the latest melody; a newer request
// (_req changed) cuts the one playing within a chunk plus the DMA queue,
// fading the cut note out so it doesn't click.
void genericBuzzer::_taskLoop() {
  static int16_t buf[CHUNK * 2];
  static const int16_t zeros[CHUNK * 2] = {0};
  static char mel[MEL_MAX];
  uint32_t done_req = 0;
  for (;;) {
    // Between sounds, while the codec is clocked, keep the DMA queue full of
    // silence: a sound starting into a queue that had run dry could be played
    // from a half-written buffer -- a knock at slow taps, never at fast ones.
    // Idle past the amp's linger, stop the clocks: only with the amp off (and
    // not about to come on), or the codec's output step pops through it.
    uint32_t idle_since = millis();
    while (_req == done_req) {
      if (!_clk_running) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); idle_since = millis(); continue; }
      i2sWrite(zeros, sizeof(zeros), 50);
      if (millis() - idle_since < AMP_LINGER_MS + 500) continue;
      bool stop = false;
      portENTER_CRITICAL(&s_mux);
      if (!_amp_on && !_amp_pending && _req == done_req) { _clk_running = false; stop = true; }
      portEXIT_CRITICAL(&s_mux);
      if (stop) i2sStop(); else idle_since = millis();
    }
    ulTaskNotifyTake(pdTRUE, 0);   // its request is being taken now
    for (;;) {
      uint32_t req; bool stop; uint16_t settle;
      portENTER_CRITICAL(&s_mux);
      req = _req; stop = _stop_req; settle = _settle_ms; _settle_ms = 0;
      memcpy(mel, _mel, MEL_MAX);
      portEXIT_CRITICAL(&s_mux);
      if (req == done_req) break;
      done_req = req;
      _note_idx = -1;
      if (stop || !mel[0]) { _task_playing = false; break; }
      if (!_clk_running) {
        i2sRestart();
        portENTER_CRITICAL(&s_mux);
        _clk_on_ms = millis();
        _clk_running = true;   // loop() powers the amp once the codec settles
        portEXIT_CRITICAL(&s_mux);
      }

      bool cut = false;
      memset(buf, 0, sizeof(buf));
      for (uint32_t n = (uint32_t)settle * SAMPLE_RATE / 1000; n > 0 && !cut; ) {
        uint32_t k = n < CHUNK ? n : CHUNK;
        i2sWrite(buf, k * 4, WAIT_FOREVER);
        n -= k;
        cut = _req != req;
      }

      uint8_t def_dur, def_oct; uint16_t bpm; const char* pos;
      _parseHeader(mel, def_dur, def_oct, bpm, pos);
      uint16_t freq; uint32_t dur_ms;
      int16_t idx = -1;
      while (!cut && _parseNext(pos, def_dur, def_oct, bpm, freq, dur_ms)) {
        _note_idx = ++idx;
        uint32_t total = dur_ms * SAMPLE_RATE / 1000;
        uint32_t phase = 0, step = (uint32_t)(((uint64_t)freq << 32) / SAMPLE_RATE);
        int32_t peak = peakFor(_volume_level), g = 0;
        // Short notes (1/32 at 180 BPM is 41 ms) get shorter edges.
        uint32_t att = total / 4 < ATTACK ? total / 4 : ATTACK;
        uint32_t rel = total / 3 < RELEASE ? total / 3 : RELEASE;
        uint32_t i = 0;
        while (i < total && !cut) {
          uint32_t k = total - i < CHUNK ? total - i : CHUNK;
          for (uint32_t j = 0; j < k; j++, i++) {
            int16_t s = 0;
            if (freq) {
              uint32_t left = total - 1 - i;
              g = i < att ? ease(peak, i, att) : ease(peak, left, rel);
              s = (int16_t)((int32_t)s_sine[phase >> 24] * g / 32767);
              phase += step;
            }
            buf[j * 2] = buf[j * 2 + 1] = s;
          }
          i2sWrite(buf, k * 4, WAIT_FOREVER);
          cut = _req != req;
        }
        if (cut && freq && g) {   // fade out from where the note was cut
          for (int j = 0; j < CUT_FADE; j += CHUNK) {
            int k = CUT_FADE - j < CHUNK ? CUT_FADE - j : CHUNK;
            for (int m = 0; m < k; m++) {
              int32_t gj = ease(g, CUT_FADE - 1 - (j + m), CUT_FADE);
              buf[m * 2] = buf[m * 2 + 1] = (int16_t)((int32_t)s_sine[phase >> 24] * gj / 32767);
              phase += step;
            }
            i2sWrite(buf, k * 4, WAIT_FOREVER);
          }
        }
      }
      _note_idx = -1;
      if (cut) continue;   // a newer request: take it at once
      // Let the DMA queue play out before reporting the melody done.
      for (int i = 0; i < 6; i++) i2sWrite(zeros, sizeof(zeros), WAIT_FOREVER);
      if (_req == req) _task_playing = false;
    }
  }
}

void genericBuzzer::_start(const char* melody) {
  if (!_i2s_ok) return;
  if (!melody || !*melody) { stop(); return; }
  uint16_t settle = 0;
  bool power_now = false;
  portENTER_CRITICAL(&s_mux);
  if (_amp_on) {   // a replay within the settle time still waits out the rest
    uint32_t since = millis() - s_amp_on_ms;
    settle = since < AUDIO_AMP_SETTLE_MS ? (uint16_t)(AUDIO_AMP_SETTLE_MS - since) : 0;
  } else if (_clk_running && millis() - _clk_on_ms >= CLK_SETTLE_MS) {
    _amp_on = power_now = true;   // claimed here, so the task keeps the clocks
    settle = AUDIO_AMP_SETTLE_MS;
  } else {
    _amp_pending = true;          // loop() powers it once the clocks have settled
    settle = CLK_SETTLE_MS + AUDIO_AMP_SETTLE_MS;
  }
  strncpy(_mel, melody, MEL_MAX - 1);
  _mel[MEL_MAX - 1] = 0;
  _stop_req = false;
  _settle_ms = settle;
  _req++;
  _task_playing = true;
  portEXIT_CRITICAL(&s_mux);
  if (power_now) { buzzerAmpPower(true); s_amp_on_ms = millis(); }
  _amp_off_at = millis() + AMP_LINGER_MS;
  xTaskNotifyGive((TaskHandle_t)_task);
}

void genericBuzzer::applyVolume() {}   // the task reads _volume_level per note

void genericBuzzer::play(const char* melody) {
  if (_is_quiet) return;
  _start(melody);
}

void genericBuzzer::playForced(const char* melody) { _start(melody); }

bool genericBuzzer::isPlaying() { return _task_playing; }

void genericBuzzer::stop() {
  if (!_i2s_ok) return;
  portENTER_CRITICAL(&s_mux);
  _stop_req = true;
  _req++;
  _task_playing = false;
  portEXIT_CRITICAL(&s_mux);
  xTaskNotifyGive((TaskHandle_t)_task);
}

// The amp: on once the codec's clocks have settled (a pending start), off
// once nothing has played for AMP_LINGER_MS.
void genericBuzzer::loop() {
  if (!_i2s_ok) return;
  if (_amp_pending) {
    bool power = false;
    portENTER_CRITICAL(&s_mux);
    if (!_task_playing) _amp_pending = false;   // stopped before it got going
    else if (_clk_running && millis() - _clk_on_ms >= CLK_SETTLE_MS) { _amp_pending = false; _amp_on = power = true; }
    portEXIT_CRITICAL(&s_mux);
    if (power) { buzzerAmpPower(true); s_amp_on_ms = millis(); }
  }
  if (!_amp_on) return;
  if (_task_playing) _amp_off_at = millis() + AMP_LINGER_MS;
  else if ((int32_t)(millis() - _amp_off_at) >= 0) {
    buzzerAmpPower(false);
    portENTER_CRITICAL(&s_mux);
    _amp_on = false;
    portEXIT_CRITICAL(&s_mux);
  }
}

void genericBuzzer::setVolume(uint8_t level) {
  _volume_level = level < 5 ? level : 4;
}

#else  // NRF52_PLATFORM / SIM_PLATFORM / BUZZER_I2S

// ---------------------------------------------------------------------------
// Non-nRF52, non-sim path — NonBlockingRtttl + analogWrite for volume
// ---------------------------------------------------------------------------

void genericBuzzer::applyVolume() {
    // After tone() sets 50% duty, analogWrite overrides duty on the same PWM channel.
    static const uint8_t duty[5] = { 6, 20, 50, 90, 128 };
    uint8_t d = duty[_volume_level < 5 ? _volume_level : 4];
    if (d < 128) analogWrite(PIN_BUZZER, d);
}

void genericBuzzer::play(const char* melody) {
    if (isPlaying()) rtttl::stop();
    if (_is_quiet) return;
    rtttl::begin(PIN_BUZZER, melody);
}

void genericBuzzer::playForced(const char* melody) {
    if (isPlaying()) rtttl::stop();
    rtttl::begin(PIN_BUZZER, melody);
}

bool genericBuzzer::isPlaying() { return rtttl::isPlaying(); }

int genericBuzzer::noteIndex() const { return -1; }   // the library doesn't say

void genericBuzzer::stop() { rtttl::stop(); }

void genericBuzzer::loop() {
    if (!rtttl::done()) {
        rtttl::play();
        if (_volume_level < 4) applyVolume();
    }
}

void genericBuzzer::setVolume(uint8_t level) {
    _volume_level = level < 5 ? level : 4;
    if (isPlaying()) applyVolume();
}

#endif  // NRF52_PLATFORM / SIM_PLATFORM / BUZZER_I2S

#endif  // PIN_BUZZER
