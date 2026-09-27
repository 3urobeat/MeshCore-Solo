#pragma once

#include <Arduino.h>

// NRF52 (and the sim, see buzzer.cpp) use a custom non-blocking RTTTL
// player; only the remaining platforms pull in the NonBlockingRtttl library
// here.
#if !defined(NRF52_PLATFORM) && !defined(SIM_PLATFORM) && !defined(BUZZER_I2S)
  #include <NonBlockingRtttl.h>
#endif

#if defined(BUZZER_I2S)
// Board-provided (its target.cpp): powers the speaker amplifier on / off.
// Called from the thread that calls genericBuzzer's methods, never the
// audio task, so it may use the I2C bus.
void buzzerAmpPower(bool on);
#endif

/* class abstracts underlying RTTTL library

    Just a simple implementation to start.  At the moment use same
    melody for message and discovery
    Suggest enum type for different sounds
    - on message
    - on discovery

    TODO
    - make message ring tone configurable

*/

class genericBuzzer
{
    public:
        void begin();
        void play(const char *melody);
        void playForced(const char *melody);
        void loop();
        void startup();
        void shutdown();
        bool isPlaying();
        // The note sounding now (0-based, rests count), -1 when silent or
        // unknown -- for an editor to follow the playback.
        int noteIndex() const;
        void quiet(bool buzzer_state);
        bool isQuiet();
        void setVolume(uint8_t level);
        uint8_t getVolume() const { return _volume_level; }
        void stop();

    private:
        uint8_t _volume_level = 4;
        void applyVolume();
        const char *startup_song  = "Startup:d=4,o=5,b=160:16c6,16e6,8g6";
        const char *shutdown_song = "Shutdown:d=4,o=5,b=100:8g5,16e5,16c5";
        bool _is_quiet = true;

#if defined(NRF52_PLATFORM) || defined(SIM_PLATFORM) || defined(BUZZER_I2S)
        // Shared RTTTL cursor state + parser, reused by both the NRF52
        // direct-PWM player below and the sim's poll-only player (buzzer.cpp,
        // #elif defined(SIM_PLATFORM)) -- the parser itself never touches
        // hardware, only _nrfStartPwm/_nrfStopPwm/the TIMER1 IRQ do, so it's
        // free to share between the two.
        const char*   _rtttl_pos   = nullptr;
        bool          _rtttl_done  = true;
        uint8_t       _def_dur     = 4;
        uint8_t       _def_oct     = 5;
        uint16_t      _def_bpm     = 120;
        volatile int16_t _note_idx = -1;

        static uint16_t _noteFreq(char letter, bool sharp, uint8_t octave);
        static bool     _parseNext(const char*& pos, uint8_t def_dur, uint8_t def_oct,
                                   uint16_t bpm, uint16_t& freq_hz, uint32_t& dur_ms);
        static void     _parseHeader(const char* melody, uint8_t& def_dur, uint8_t& def_oct,
                                     uint16_t& bpm, const char*& notes_start);
#endif

#if defined(NRF52_PLATFORM)
        // Own RTTTL player — bypasses tone() to allow volume control from note start.
        // tone() pre-computes seq_refresh so the DMA repeats 50% duty for ~30ms before
        // re-reading its buffer; we cannot beat that timing.  By owning NRF_PWM2 directly
        // and setting REFRESH=0, DMA re-reads _duty_buf every period so duty takes effect
        // immediately at SEQSTART.
        volatile uint16_t _duty_buf = 0;   // DMA source — must stay in RAM
        bool          _pwm_on      = false;

        void    _nrfBegin(const char* melody);
        void    _nrfAdvance();
        void    _nrfStartPwm(uint16_t freq);
        void    _nrfStopPwm();
        uint8_t _dutyPct() const;

        // TIMER1-driven note advance: a hardware compare interrupt calls
        // _nrfAdvance() at the exact moment the current note's duration ends,
        // so playback timing survives a blocking display refresh (e-ink)
        // instead of waiting for the next loop() poll. Same reasoning as
        // MomentaryButton's GPIO-IRQ edge capture, just timer- rather than
        // pin-driven. TIMER0 is reserved by the SoftDevice; TIMER1 is free.
        void _armNoteTimer(uint32_t dur_ms);
        void _disarmNoteTimer();
        static genericBuzzer* _isr_instance;

    public:
        // Must be public: the extern "C" TIMER1_IRQHandler (see buzzer.cpp) is
        // a free function — the vector table requires that exact symbol — so
        // it needs access from outside the class to dispatch into it.
        static void _timer1ISR();
    private:
#elif defined(SIM_PLATFORM)
        // No real PWM/timer hardware to drive -- just track which frequency
        // (0 = silent) should be sounding right now and when the current
        // note ends, advanced by plain millis()-polling from loop() (same
        // non-blocking shape as the NonBlockingRtttl-driven platforms, not
        // the NRF52 IRQ path). A host page polls currentFreqHz()/isPlaying()
        // every frame to drive a Web Audio oscillator -- see
        // variants/sim/web/index.html.
        uint32_t _note_end_ms = 0;

        void _advance();

    public:
        uint16_t currentFreqHz() const { return _cur_freq; }
    private:
        uint16_t _cur_freq = 0;
#elif defined(BUZZER_I2S)
        // A speaker behind an I2S codec (BUZZER_CODEC_ES8311) instead of a
        // PWM pin. An audio task synthesises the melody and advances its
        // notes by samples written, so timing holds through a stalled UI
        // loop; loop() powers the amp and the codec. See buzzer.cpp.
        static const int MEL_MAX = 256;
        char          _mel[MEL_MAX];         // the melody playing (a copy: callers reuse buffers)
        volatile uint32_t _req = 0;          // bumped by play()/stop(); the task restarts on a change
        volatile bool _stop_req = false;
        volatile bool _task_playing = false;
        volatile uint16_t _settle_ms = 0;    // silence before the first note while the amp powers up
        volatile bool _amp_on = false;       // guarded by the player's lock (the task reads it)
        volatile bool _amp_pending = false;  // wanted, waiting for the codec's clocks to settle
        volatile bool _clk_running = false;  // I2S clocking the codec
        volatile uint32_t _clk_on_ms = 0;
        bool          _i2s_ok = false;
        bool          _codec_on = false;     // codec powered up (standby while the clocks are off); caller's thread only
        uint32_t      _codec_on_ms = 0;
        uint32_t      _amp_off_at = 0;       // amp stays on this long after the last sound
        void*         _task = nullptr;       // TaskHandle_t

        bool _i2sBegin();
        void _start(const char* melody);
        void _taskLoop();
        static void _taskEntry(void* self);
#endif
};
