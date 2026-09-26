#include <Arduino.h>
#include "target.h"

WioTrackerL2Board board;

static SPIClass spi;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
L2GpsProvider gps(Serial1, &rtc_clock);
EnvironmentSensorManager sensors(gps);

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true, false);   // no multi-click: back answers at once
#endif

#ifdef BUZZER_I2S
void buzzerAmpPower(bool on) { board.setSpeakerAmp(on); }
#endif

void L2GpsProvider::begin() {
  board.setGnssPower(true);   // with the power-up reset
  if (_uart_off) {            // back from stop(): the UART again
    Serial1.setRxBufferSize(1024);
    Serial1.begin(GPS_BAUD_RATE, SERIAL_8N1, PIN_GPS_TX, PIN_GPS_RX);
    _uart_off = false;
  }
  MicroNMEALocationProvider::begin();
}

void L2GpsProvider::stop() {
  MicroNMEALocationProvider::stop();
  // The UART's TX idling high would feed the unpowered module through its pin.
  Serial1.end();
  pinMode(PIN_GPS_RX, INPUT);   // PIN_GPS_RX: the module's RX, our TX
  _uart_off = true;
  board.setGnssPower(false);
}

void L2GpsProvider::reset() { board.gnssReset(); }
bool L2GpsProvider::isEnabled() { return board.gnssPowered(); }

bool radio_init() {
  MESH_DEBUG_PRINTLN("radio_init: rtc + sx1262 init");
  fallback_clock.begin();
  rtc_clock.begin(Wire);  // Wire already running on 47/48 from board.begin()

  bool ok = radio.std_init(&spi);
  MESH_DEBUG_PRINTLN("radio_init: SX1262 %s", ok ? "OK" : "FAILED");
  return ok;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
