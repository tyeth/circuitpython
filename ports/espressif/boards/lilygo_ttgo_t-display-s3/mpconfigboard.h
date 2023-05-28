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
// USB_PID = 0x813F VendorID 0x303a ProductID 0x1001
#define MICROPY_HW_BOARD_NAME       "LILYGO TTGO T-Display-S3"
#define MICROPY_HW_MCU_NAME         "ESP32S3"

// #define CIRCUITPY_PARALLELDISPLAY  // seems defined in mpconfigboard.mk

// #define MICROPY_HW_LED_STATUS (&pin_GPIO13)
// #define MICROPY_HW_NEOPIXEL (&pin_GPIO33)
// #define CIRCUITPY_STATUS_LED_POWER (&pin_GPIO21)

// #define DEFAULT_UART_BUS_RX         (&pin_GPIO20)
// #define DEFAULT_UART_BUS_TX         (&pin_GPIO21)

// #define CIRCUITPY_CONSOLE_UART_RX     DEFAULT_UART_BUS_RX
// #define CIRCUITPY_CONSOLE_UART_TX     DEFAULT_UART_BUS_TX

// #define CIRCUITPY_BOOT_BUTTON       (&pin_GPIO0)

#define DEFAULT_UART_BUS_RX         (&pin_GPIO44)
#define DEFAULT_UART_BUS_TX         (&pin_GPIO43)

// Second I2C bus(Wire1) in arduino, the Qwiic connector / StemmaQT port
#define DEFAULT_I2C_BUS_SCL (&pin_GPIO43)
#define DEFAULT_I2C_BUS_SDA (&pin_GPIO44)

#define DEFAULT_SPI_BUS_SS (&pin_GPIO10)
#define DEFAULT_SPI_BUS_SCK (&pin_GPIO12)
#define DEFAULT_SPI_BUS_MOSI (&pin_GPIO11)
#define DEFAULT_SPI_BUS_MISO (&pin_GPIO13)

// #define AUTORESET_DELAY_MS          500
