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


#include "shared-bindings/mdns/Server.h"

#include <stdint.h>
#include <string.h>

#include "py/objtuple.h"
#include "py/runtime.h"

#include "shared-bindings/mdns/RemoteService.h"
#include "shared-module/mdns/Browser.h"

#if CIRCUITPY_WIFI
#include "shared-bindings/wifi/__init__.h"
#endif

// The allocating find() collects into a bounded scratch array rather than an
// unbounded list, so a chatty network cannot exhaust the heap mid-browse.
#define MDNS_MAX_RESULTS (16)


void mdns_server_construct(mdns_server_obj_t *self, bool workflow) {
    self->hostname[0] = '\0';
    self->instance_name[0] = '\0';
    // The web workflow calls this directly on a static object, so default the
    // interface here rather than only in common_hal_mdns_server_construct().
    #if CIRCUITPY_WIFI
    self->network_interface = MP_OBJ_FROM_PTR(&common_hal_wifi_radio_obj);
    #else
    self->network_interface = mp_const_none;
    #endif
    self->workflow = workflow;
    self->inited = true;
}

void common_hal_mdns_server_construct(mdns_server_obj_t *self, mp_obj_t network_interface) {
    #if CIRCUITPY_WIFI
    if (network_interface != MP_OBJ_FROM_PTR(&common_hal_wifi_radio_obj)) {
        mp_raise_ValueError(MP_ERROR_TEXT("mDNS only works with built-in WiFi"));
    }
    #endif
    mdns_server_construct(self, false);
    self->network_interface = network_interface;
}

void common_hal_mdns_server_deinit(mdns_server_obj_t *self) {
    self->inited = false;
}

bool common_hal_mdns_server_deinited(mdns_server_obj_t *self) {
    return !self->inited;
}

const char *common_hal_mdns_server_get_hostname(mdns_server_obj_t *self) {
    return self->hostname;
}

void common_hal_mdns_server_set_hostname(mdns_server_obj_t *self, const char *hostname) {
    strlcpy(self->hostname, hostname, sizeof(self->hostname));
}

const char *common_hal_mdns_server_get_instance_name(mdns_server_obj_t *self) {
    if (self->instance_name[0] == '\0') {
        return self->hostname;
    }
    return self->instance_name;
}

void common_hal_mdns_server_set_instance_name(mdns_server_obj_t *self, const char *instance_name) {
    strlcpy(self->instance_name, instance_name, sizeof(self->instance_name));
}

void common_hal_mdns_server_advertise_service(mdns_server_obj_t *self, const char *service_type,
    const char *protocol, mp_int_t port, const char *txt_records[], size_t num_txt_records) {
    // This implementation is a browser only: answering queries needs a
    // responder holding port 5353, which belongs in the port's own common-hal.
    //
    // The supervisor advertises the web workflow through this same entry point
    // (supervisor/shared/web_workflow/web_workflow.c), with no nlr handler in
    // place, so raising there would be fatal. Report the limitation to user code
    // and let the supervisor carry on unadvertised - its HTTP server still
    // serves, it simply cannot be discovered by name.
    if (self->workflow) {
        return;
    }
    mp_raise_NotImplementedError(MP_ERROR_TEXT("Advertising services is not supported on this board"));
}

// Non-allocating entry point, also used by the supervisor's web workflow
// (supervisor/shared/web_workflow/web_workflow.c). That caller has no nlr
// handler, so nothing may escape from here: a failed browse is zero results.
size_t mdns_server_find(mdns_server_obj_t *self, const char *service_type, const char *protocol,
    mp_float_t timeout, mdns_remoteservice_obj_t *out, size_t out_len) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        size_t count = shared_module_mdns_find(self->network_interface, service_type, protocol,
            timeout, out, out_len);
        nlr_pop();
        return count;
    }
    // Swallowed on purpose; see the comment above. Raising here would longjmp
    // into a handler that does not exist.
    return 0;
}

mp_obj_t common_hal_mdns_server_find(mdns_server_obj_t *self, const char *service_type,
    const char *protocol, mp_float_t timeout) {
    // Browse into a scratch array, then hand out one GC object per result so
    // each is independently tracked, matching the native backends.
    mdns_remoteservice_obj_t *scratch = m_new(mdns_remoteservice_obj_t, MDNS_MAX_RESULTS);
    size_t count = shared_module_mdns_find(self->network_interface, service_type, protocol,
        timeout, scratch, MDNS_MAX_RESULTS);

    mp_obj_tuple_t *tuple = MP_OBJ_TO_PTR(mp_obj_new_tuple(count, NULL));
    for (size_t i = 0; i < count; i++) {
        mdns_remoteservice_obj_t *service = m_new_obj(mdns_remoteservice_obj_t);
        *service = scratch[i];
        service->base.type = &mdns_remoteservice_type;
        service->next = NULL;
        tuple->items[i] = MP_OBJ_FROM_PTR(service);
    }
    return MP_OBJ_FROM_PTR(tuple);
}
