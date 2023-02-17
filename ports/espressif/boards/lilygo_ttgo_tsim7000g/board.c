// /*
//  * This file is part of the MicroPython project, http://micropython.org/
//  *
//  * The MIT License (MIT)
//  *
//  * Copyright (c) 2022 Fabian Affolter <fabian@affolter-engineering.ch>
//  *
//  * Permission is hereby granted, free of charge, to any person obtaining a copy
//  * of this software and associated documentation files (the "Software"), to deal
//  * in the Software without restriction, including without limitation the rights
//  * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  * copies of the Software, and to permit persons to whom the Software is
//  * furnished to do so, subject to the following conditions:
//  *
//  * The above copyright notice and this permission notice shall be included in
//  * all copies or substantial portions of the Software.
//  *
//  * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  * THE SOFTWARE.
//  */

#include "supervisor/board.h"
#include "mpconfigboard.h"
#include "shared-bindings/microcontroller/Pin.h"
// #include "driver/gpio.h"

void board_init(void) {
    // Debug UART
    #ifdef DEBUG
    common_hal_never_reset_pin(&pin_GPIO34);
    common_hal_never_reset_pin(&pin_GPIO35);
    #endif /* DEBUG */
    // common_hal_never_reset_pin(&pin_GPIO12);
    // common_hal_never_reset_pin(&pin_GPIO4);
    // common_hal_never_reset_pin(&pin_GPIO5);
    // common_hal_never_reset_pin(&pin_GPIO0);
}

bool board_requests_safe_mode(void) {
    return false;
}

// Use the MP_WEAK supervisor/shared/board.c versions of routines not defined here.
