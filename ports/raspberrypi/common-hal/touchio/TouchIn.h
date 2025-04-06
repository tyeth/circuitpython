// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2016 Scott Shawcroft
// SPDX-FileCopyrightText: Copyright (c) 2018 Nick Moore for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "shared-bindings/digitalio/DigitalInOut.h"

#include "py/obj.h"

// Native touchio only for RP2350, which cannot do shared-module touchio
#ifdef PICO_RP2350

typedef struct {
    mp_obj_base_t base;
    digitalio_digitalinout_obj_t *digitalinout;
    uint16_t threshold;
} touchio_touchin_obj_t;

void touchin_reset(void);

#endif
