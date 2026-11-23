// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2026 Adafruit Industries LLC
//
// SPDX-License-Identifier: MIT

// A portable DNS-SD browser built on socketpool, so that mdns.Server.find()
// works on any port that terminates an IP stack on the host rather than only on
// those with a native mDNS library.
//
// This is a "one-shot multicast DNS query" in the sense of RFC 6762 Section 5.1:
// the query goes out from an ephemeral port with the unicast-response (QU) bit
// set, so responders reply directly to us and we never have to join the
// 224.0.0.251 multicast group. That matters because socketpool exposes
// IP_MULTICAST_TTL but has no IP_ADD_MEMBERSHIP, so a group-joining browser is
// not portable today. The cost is that responses a responder chooses to
// multicast instead (RFC 6762 Section 5.4 permits this when it has not answered
// recently) are missed; a repeat find() generally picks them up.


#pragma once

#include "py/obj.h"

#include "shared-bindings/mdns/RemoteService.h"

// Run one DNS-SD browse over plain sockets and return how many services were
// written into `out` (at most `out_len`).
//
// Needs only a SocketPool, so it works on any backend that terminates an IP
// stack on the host. `network_interface` is what a SocketPool is constructed
// from. May raise; callers without an nlr handler must wrap it.
//
// Not reentrant: it uses a single static receive buffer so that it can run in
// supervisor context, where there may be no VM heap.
size_t shared_module_mdns_find(mp_obj_t network_interface, const char *service_type,
    const char *protocol, mp_float_t timeout, mdns_remoteservice_obj_t *out, size_t out_len);
