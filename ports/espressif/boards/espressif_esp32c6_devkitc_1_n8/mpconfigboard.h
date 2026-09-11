// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#pragma once

// Micropython setup

#define MICROPY_HW_BOARD_NAME       "ESP32-C6-DevKitC-1-N8"
#define MICROPY_HW_MCU_NAME         "ESP32C6"

#define MICROPY_HW_NEOPIXEL         (&pin_GPIO8)

#define DEFAULT_UART_BUS_RX         (&pin_GPIO17)
#define DEFAULT_UART_BUS_TX         (&pin_GPIO16)

// Move the console onto UART0, which reaches the on-board CH343 bridge. The
// bridge stays enumerated across a reset, so a fault is still recorded after
// USB-Serial-JTAG has dropped off the bus. Requires
// CIRCUITPY_ESP_USB_SERIAL_JTAG = 0 in mpconfigboard.mk: the two consoles are
// mutually exclusive (ports/espressif/supervisor/serial.c).
#define CIRCUITPY_CONSOLE_UART_RX   DEFAULT_UART_BUS_RX
#define CIRCUITPY_CONSOLE_UART_TX   DEFAULT_UART_BUS_TX
#define CIRCUITPY_CONSOLE_UART_TIMESTAMP (1)
