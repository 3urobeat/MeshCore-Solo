#pragma once
// Minimal Everest ES8311 mono codec setup for tone playback: DAC only, I2S
// slave, 16-bit, the host supplies MCLK = 256 x fs. Register values follow
// Espressif's esp-adf es8311 driver, with its clock coefficients for MCLK
// 4.096 MHz / fs 16 kHz folded in.

#include <Arduino.h>
#include <Wire.h>

namespace es8311 {

static bool write(TwoWire& w, uint8_t addr, uint8_t reg, uint8_t val) {
  w.beginTransmission(addr);
  w.write(reg);
  w.write(val);
  return w.endTransmission() == 0;
}

// Codec on and playing whatever arrives on I2S. False: no codec at `addr`.
static bool begin(TwoWire& w, uint8_t addr) {
  static const uint8_t SEQ[][2] = {
    { 0x45, 0x00 }, { 0x01, 0x30 }, { 0x02, 0x00 }, { 0x03, 0x10 }, { 0x16, 0x24 },
    { 0x04, 0x10 }, { 0x05, 0x00 }, { 0x0B, 0x00 }, { 0x0C, 0x00 }, { 0x10, 0x1F },
    { 0x11, 0x7F },
    { 0x00, 0x80 },   // power up the state machine, slave mode
    { 0x01, 0x3F },   // MCLK from its pin, every clock on
    // fs = MCLK / 256: pre-divider / multiplier 1, ADC / DAC dividers 1,
    // single-speed, OSR 0x10, LRCK divider 0x0FF, BCLK divider 4
    { 0x02, 0x00 }, { 0x05, 0x00 }, { 0x03, 0x10 }, { 0x04, 0x10 },
    { 0x07, 0x00 }, { 0x08, 0xFF }, { 0x06, 0x03 },
    { 0x13, 0x10 }, { 0x1B, 0x0A }, { 0x1C, 0x6A },
    { 0x09, 0x0C },   // DAC serial port: I2S, 16-bit, unmuted
    { 0x0A, 0x0C },   // ADC serial port: the same (unused)
    // start: analog up, DAC powered, output driver on
    { 0x17, 0xBF }, { 0x0E, 0x02 }, { 0x12, 0x00 }, { 0x14, 0x1A }, { 0x0D, 0x01 },
    { 0x15, 0x40 }, { 0x37, 0x08 }, { 0x45, 0x00 },
    { 0x32, 0xBF },   // DAC volume 0 dB (the player scales its samples)
    { 0x31, 0x00 },   // DAC unmuted
  };
  for (size_t i = 0; i < sizeof(SEQ) / sizeof(SEQ[0]); i++)
    if (!write(w, addr, SEQ[i][0], SEQ[i][1])) return false;
  return true;
}

// Everything but the I2C interface off (esp-adf's es8311 suspend): DAC, ADC,
// references and the output driver. begin() brings it back, with MCLK running.
static bool standby(TwoWire& w, uint8_t addr) {
  static const uint8_t SEQ[][2] = {
    { 0x32, 0x00 }, { 0x17, 0x00 }, { 0x0E, 0xFF }, { 0x12, 0x02 }, { 0x14, 0x00 },
    { 0x0D, 0xFA }, { 0x15, 0x00 }, { 0x37, 0x08 }, { 0x02, 0x10 },
    { 0x00, 0x00 }, { 0x00, 0x1F },   // state machine reset
    { 0x01, 0x30 }, { 0x01, 0x00 },   // clocks off
    { 0x45, 0x00 }, { 0x0D, 0xFC }, { 0x02, 0x00 },
  };
  for (size_t i = 0; i < sizeof(SEQ) / sizeof(SEQ[0]); i++)
    if (!write(w, addr, SEQ[i][0], SEQ[i][1])) return false;
  return true;
}

}  // namespace es8311
