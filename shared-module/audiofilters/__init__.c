// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2024 Cooper Dalrymple
//
// SPDX-License-Identifier: MIT

#include "shared-module/audiofilters/__init__.h"

void audiofilters_assign_filters(mp_obj_t filter_in, mp_obj_t *filter_out, mp_obj_t **filter_objs, size_t *filter_objs_len, biquad_filter_state **filter_states, uint8_t channel_count) {
    size_t n_items;
    mp_obj_t *items;

    if (filter_in == mp_const_none) {
        n_items = 0;
        *filter_objs = NULL;
    } else if (mp_obj_is_type(filter_in, &mp_type_tuple)) {
        mp_obj_tuple_get(filter_in, &n_items, &items);
        for (size_t i = 0; i < n_items; i++) {
            if (!mp_obj_is_type(items[i], &synthio_biquad_type_obj)) {
                mp_raise_TypeError_varg(
                    MP_ERROR_TEXT("%q in %q must be of type %q, not %q"),
                    MP_QSTR_object,
                    MP_QSTR_filter,
                    MP_QSTR_Biquad,
                    mp_obj_get_type(items[i])->name);
            }
        }
        *filter_objs = items;
    } else {
        n_items = 1;
        if (!mp_obj_is_type(filter_in, &synthio_biquad_type_obj)) {
            mp_raise_TypeError_varg(
                MP_ERROR_TEXT("%q must be of type %q or %q, not %q"),
                MP_QSTR_filter, MP_QSTR_Biquad, MP_QSTR_iterable, mp_obj_get_type(filter_in)->name);
        }
        *filter_objs = filter_out;
    }

    // everything has been checked, so we can do the following without fear

    *filter_out = filter_in;
    *filter_states = m_renew(biquad_filter_state,
        *filter_states,
        *filter_objs_len * channel_count,
        n_items * channel_count);
    *filter_objs_len = n_items;
}

void audiofilters_reset_filters(biquad_filter_state *filter_states, size_t filter_objs_len, uint8_t channel_count) {
    if (filter_states) {
        for (uint8_t i = 0; i < filter_objs_len * channel_count; i++) {
            synthio_biquad_filter_reset(&filter_states[i]);
        }
    }
}

void audiofilters_tick_filters(mp_obj_t *filter_objs, size_t filter_objs_len) {
    for (uint8_t j = 0; j < filter_objs_len; j++) {
        common_hal_synthio_biquad_tick(filter_objs[j]);
    }
}

int32_t audiofilters_process_filters(mp_obj_t *filter_objs, size_t filter_objs_len, biquad_filter_state *filter_states, uint8_t channel_count, uint8_t channel, int32_t word) {
    // Process biquad filters
    for (uint8_t j = 0; j < filter_objs_len; j++) {
        mp_obj_t filter_obj = filter_objs[j];
        word = synthio_biquad_filter_sample(filter_obj, &filter_states[j * channel_count + channel], word);
    }
    return word;
}
