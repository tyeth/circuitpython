// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2024 Cooper Dalrymple
//
// SPDX-License-Identifier: MIT

#pragma once

#include "py/obj.h"
#include "shared-module/synthio/Biquad.h"

void audiofilters_assign_filters(mp_obj_t filter_in, mp_obj_t *filter_out, mp_obj_t **filter_objs, size_t *filter_objs_len, biquad_filter_state **filter_states, uint8_t channel_count);
void audiofilters_reset_filters(biquad_filter_state *filter_states, size_t filter_objs_len, uint8_t channel_count);
void audiofilters_tick_filters(mp_obj_t *filter_objs, size_t filter_objs_len);
int32_t audiofilters_process_filters(mp_obj_t *filter_objs, size_t filter_objs_len, biquad_filter_state *filter_states, uint8_t channel_count, uint8_t channel, int32_t word);
