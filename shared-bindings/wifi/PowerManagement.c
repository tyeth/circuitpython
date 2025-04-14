// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Dan Halbert for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "py/enum.h"

#include "shared-bindings/wifi/PowerManagement.h"

//| class PowerManagement:
//|     """Power-saving options for wifi"""
//|
//|     MIN: PowerManagement
//|     """Minimum power management (default). The WiFi station wakes up to receive a beacon every DTIM period.
//|        The DTIM period is set by the access point."""
//|     MAX: PowerManagement
//|     """Maximum power management, at the expense of some performance. The WiFi station wakes up every 100 ms."""
//|     NONE: PowerManagement
//|     """No power management: the WiFi station does not sleep."""

// In order of the enum type.
MAKE_ENUM_VALUE(wifi_power_management_type, power_management, NONE, POWER_MANAGEMENT_NONE);
MAKE_ENUM_VALUE(wifi_power_management_type, power_management, MIN, POWER_MANAGEMENT_MIN);
MAKE_ENUM_VALUE(wifi_power_management_type, power_management, MAX, POWER_MANAGEMENT_MAX);

MAKE_ENUM_MAP(wifi_power_management) {
    MAKE_ENUM_MAP_ENTRY(power_management, NONE),
    MAKE_ENUM_MAP_ENTRY(power_management, MIN),
    MAKE_ENUM_MAP_ENTRY(power_management, MAX),
};

static MP_DEFINE_CONST_DICT(wifi_power_management_locals_dict, wifi_power_management_locals_table);

MAKE_PRINTER(wifi, wifi_power_management);

MAKE_ENUM_TYPE(wifi, PowerManagement, wifi_power_management);
