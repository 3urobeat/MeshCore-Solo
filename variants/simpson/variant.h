/*
 * Simpson variant for the Pro Micro nRF52840 carrier used by this device.
 * Arduino pin indices map to physical nRF52840 GPIOs in variant.cpp.
 */
#pragma once

#include "WVariant.h"

#define VARIANT_MCK        (64000000ul)
#define USE_LFRC

#define PIN_EXT_VCC        (21)
#define EXT_VCC            PIN_EXT_VCC

#define BATTERY_PIN        (17)
#define ADC_RESOLUTION     12

#define PINS_COUNT         (23)
#define NUM_DIGITAL_PINS   (23)
#define NUM_ANALOG_INPUTS  (1)
#define NUM_ANALOG_OUTPUTS (0)

#define PIN_SERIAL1_TX     (4)
#define PIN_SERIAL1_RX     (3)

#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA       (8)
#define PIN_WIRE_SCL       (7)

#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_SCK        (12)
#define PIN_SPI_MISO       (15)
#define PIN_SPI_MOSI       (14)
#define PIN_SPI_NSS        (13)

#define PIN_LED            (22)
#define LED_PIN            PIN_LED
#define LED_BLUE           PIN_LED
#define LED_BUILTIN        PIN_LED
#define LED_STATE_ON       1

#define PIN_BUTTON1        (6)
#define BUTTON_PIN         PIN_BUTTON1

#ifndef PIN_BUZZER
  #define PIN_BUZZER       (0)
#endif
#ifndef PIN_VIBRATION
  #define PIN_VIBRATION    (1)
#endif
#define PIN_GPS_EN         (5)

#define P_LORA_NSS         (13)
#define P_LORA_DIO_1       (11)
#define P_LORA_RESET       (10)
#define P_LORA_BUSY        (16)
#define P_LORA_MISO        (15)
#define P_LORA_SCLK        (12)
#define P_LORA_MOSI        (14)
#define SX126X_POWER_EN    (21)
#define SX126X_RXEN        (2)
#define SX126X_TXEN        (-1)
#define SX126X_DIO2_AS_RF_SWITCH true
#define SX126X_DIO3_TCXO_VOLTAGE (1.8f)
