// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2026 Vladimir Smitka
//
// SPDX-License-Identifier: MIT

#pragma once

#include "py/obj.h"

// Defined in shared-bindings/picogame/__init__.c (consolidated with Bitmap/Sprite).
extern const mp_obj_type_t picogame_stripdraw_type;
extern const mp_obj_type_t picogame_triangles_type;

uint8_t picogame_kind_of(mp_obj_t o);
