// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2026 Adafruit Industries LLC
//
// SPDX-License-Identifier: MIT

#pragma once

// SparkFun ESP32 Thing (DEV-13907): ESP32-D0WDQ6, 4MB flash, no PSRAM,
// 26 MHz crystal (see sdkconfig), FT231X USB-serial on GPIO1/GPIO3.

#define MICROPY_HW_BOARD_NAME       "SparkFun ESP32 Thing"
#define MICROPY_HW_MCU_NAME         "ESP32"

#define MICROPY_HW_LED_STATUS       (&pin_GPIO5)

#define CIRCUITPY_BOARD_I2C         (1)
#define CIRCUITPY_BOARD_I2C_PIN     {{.scl = &pin_GPIO22, .sda = &pin_GPIO21}}

#define CIRCUITPY_BOARD_SPI         (1)
#define CIRCUITPY_BOARD_SPI_PIN     {{.clock = &pin_GPIO18, .mosi = &pin_GPIO23, .miso = &pin_GPIO19}}

#define CIRCUITPY_BOARD_UART        (1)
#define CIRCUITPY_BOARD_UART_PIN    {{.tx = &pin_GPIO17, .rx = &pin_GPIO16}}

// UART pins attached to the USB-serial converter chip
#define CIRCUITPY_CONSOLE_UART_TX (&pin_GPIO1)
#define CIRCUITPY_CONSOLE_UART_RX (&pin_GPIO3)
