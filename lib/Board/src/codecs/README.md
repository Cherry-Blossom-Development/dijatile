ES8311 (DAC/codec) and ES7210 (mic ADC) drivers, Apache-2.0, Espressif Systems.
Taken from Waveshare's ESP32-S3-LCD-1.54 Arduino examples, which ported
Espressif's esp-bsp component drivers to use Arduino `Wire` for register access.
Local change: include `hal/i2c_types.h` instead of the legacy `driver/i2c.h`.
