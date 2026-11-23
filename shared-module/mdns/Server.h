// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2026 Adafruit Industries LLC
//
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>

#include "py/obj.h"

#include "shared-module/mdns/RemoteService.h"

typedef struct {
    mp_obj_base_t base;
    // The radio this server queries through. Kept so find() can build a
    // SocketPool on demand rather than holding a socket open.
    mp_obj_t network_interface;
    // Stored for the hostname/instance_name properties. This implementation has
    // no responder, so they are not advertised anywhere; see
    // common_hal_mdns_server_advertise_service().
    char hostname[64]; // RFC 6762 Appendix A - 63 bytes for label + 1 for NUL
    char instance_name[64]; // RFC 6763 Section 7.2 - 63 bytes + 1 for NUL
    // Set for the supervisor's web workflow object, which cannot handle an
    // exception. See common_hal_mdns_server_advertise_service().
    bool workflow;
    bool inited;
} mdns_server_obj_t;
