/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Scott Shawcroft for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Micropython setup

// This definition is intended to cover all the T-QT and Pro models (same screen different S3 flash/ram combo)
#define MICROPY_HW_BOARD_NAME       "LILYGO TTGO T-QT"
#define MICROPY_HW_MCU_NAME         "ESP32S3"


// #define MICROPY_HW_NEOPIXEL (&pin_GPIO33)
// #define CIRCUITPY_STATUS_LED_POWER (&pin_GPIO34)

// #define MICROPY_HW_LED_STATUS (&pin_GPIO13)

#define DEFAULT_I2C_BUS_SCL (&pin_GPIO44)
#define DEFAULT_I2C_BUS_SDA (&pin_GPIO43)

#define DEFAULT_SPI_BUS_SCK (&pin_GPIO36)
#define DEFAULT_SPI_BUS_MOSI (&pin_GPIO35)
#define DEFAULT_SPI_BUS_MISO (&pin_GPIO37)

#define DEFAULT_UART_BUS_RX (&pin_GPIO17)
#define DEFAULT_UART_BUS_TX (&pin_GPIO18)
// #define DEFAULT_UART_BUS_RX         (&pin_GPIO44)
// #define DEFAULT_UART_BUS_TX         (&pin_GPIO43)

// #define DOUBLE_TAP_PIN (&pin_GPIO0)

#define CIRCUITPY_BOOT_BUTTON       (&pin_GPIO0)

#define CIRCUITPY_BOARD_I2C         (2)
#define CIRCUITPY_BOARD_I2C_PIN     {{.scl = &pin_GPIO44, .sda = &pin_GPIO43}, \
                                     {.scl = &pin_GPIO17, .sda = &pin_GPIO18}}

#define CIRCUITPY_BOARD_SPI         (1)
#define CIRCUITPY_BOARD_SPI_PIN     {{.clock = &pin_GPIO36, .mosi = &pin_GPIO35, .miso = &pin_GPIO37}}

// #define CIRCUITPY_BOARD_UART        (1)
// #define CIRCUITPY_BOARD_UART_PIN    {{.tx = &pin_GPIO32, .rx = &pin_GPIO7}}
