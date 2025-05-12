// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Your Name
//
// SPDX-License-Identifier: MIT

#define MICROPY_HW_BOARD_NAME "Elecrow RP2350 Pico W5"
#define MICROPY_HW_MCU_NAME "rp2350a"

#define MICROPY_HW_LED_STATUS (&pin_GPIO25)

#define CIRCUITPY_BOARD_I2C         (1)
#define CIRCUITPY_BOARD_I2C_PIN     {{.scl = &pin_GPIO3, .sda = &pin_GPIO2}}
