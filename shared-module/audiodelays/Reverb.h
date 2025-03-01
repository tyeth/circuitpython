// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Mark Komus
//
// SPDX-License-Identifier: MIT
#pragma once

#include "py/obj.h"

#include "shared-module/audiocore/__init__.h"
#include "shared-module/synthio/__init__.h"
#include "shared-module/synthio/block.h"

extern const mp_obj_type_t audiodelays_reverb_type;

typedef struct {
    audiosample_base_t base;

    int8_t *buffer[2];
    uint8_t last_buf_idx;
    uint32_t buffer_len; // max buffer in bytes

    uint8_t *sample_remaining_buffer;
    uint32_t sample_buffer_length;

    bool loop;
    bool more_data;

    mp_obj_t sample;
} audiodelays_reverb_obj_t;

void audiodelays_reverb_reset_buffer(audiodelays_reverb_obj_t *self,
    bool single_channel_output,
    uint8_t channel);

audioio_get_buffer_result_t audiodelays_reverb_get_buffer(audiodelays_reverb_obj_t *self,
    bool single_channel_output,
    uint8_t channel,
    uint8_t **buffer,
    uint32_t *buffer_length);  // length in bytes
