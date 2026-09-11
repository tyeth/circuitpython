// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#pragma once

// Micropython setup

#define MICROPY_HW_BOARD_NAME       "ESP32-S3-DevKitC-1-N8"
#define MICROPY_HW_MCU_NAME         "ESP32S3"

#define MICROPY_HW_NEOPIXEL         (&pin_GPIO48)

#define DEFAULT_UART_BUS_RX         (&pin_GPIO44)
#define DEFAULT_UART_BUS_TX         (&pin_GPIO43)

// Mirror the console onto UART0, which reaches the on-board CP2102N bridge.
// The bridge stays enumerated across a reset, so a fault is still recorded
// after the native USB console has dropped off the bus.
#define CIRCUITPY_CONSOLE_UART_RX   DEFAULT_UART_BUS_RX
#define CIRCUITPY_CONSOLE_UART_TX   DEFAULT_UART_BUS_TX
#define CIRCUITPY_CONSOLE_UART_TIMESTAMP (1)
