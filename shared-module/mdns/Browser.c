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


// The portable DNS-SD browser.
//
// Deliberately its own translation unit, separate from the common_hal_* entry
// points in Server.c. A port that has a native mDNS *responder* but no browser
// (zephyr-cp, say) can keep its own common-hal/mdns and compile just this file
// to get find(); linking Server.c instead would drag in a second definition of
// every common_hal_mdns_server_* symbol.

#include "shared-module/mdns/Browser.h"

#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"

#include "shared/runtime/interrupt_char.h"
#include "shared-bindings/mdns/RemoteService.h"
#include "shared-bindings/socketpool/Socket.h"
#include "shared-bindings/socketpool/SocketPool.h"
#include "supervisor/shared/tick.h"

#define MDNS_MULTICAST_IPV4 "224.0.0.251"
#define MDNS_PORT (5353)

#define DNS_RRTYPE_A (1)
#define DNS_RRTYPE_PTR (12)
#define DNS_RRTYPE_SRV (33)

#define DNS_RRCLASS_IN (1)
// Top bit of QCLASS in a question asks for a unicast reply; in a resource
// record the same bit is the cache-flush flag. Mask it off before comparing.
#define DNS_CLASS_UNICAST_RESPONSE (0x8000)
#define DNS_CLASS_MASK (0x7fff)

#define DNS_HEADER_LEN (12)
// mDNS responses may be larger, but a single DNS-SD service rarely needs more
// and anything truncated is simply re-queried by calling find() again.
#define MDNS_PACKET_SIZE (1460)

// How long to wait between polls of the socket while a browse is running.
#define MDNS_POLL_INTERVAL_MS (10)

// Longest label plus NUL, matching RemoteService's instance_name field.
#define MDNS_LABEL_SIZE (64)
// <instance>.<service>.<protocol>.local - enough to validate any name we read.
#define MDNS_MAX_LABELS (4)

// Case-insensitive compare, since DNS labels are case-insensitive (RFC 1035
// Section 2.3.3) and responders do not preserve the case of the query.
static bool name_eq(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') {
            ca += 'a' - 'A';
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb += 'a' - 'A';
        }
        if (ca != cb) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}

// Read a DNS name at `offset`, following compression pointers (RFC 1035 Section
// 4.1.4). Writes up to `max_labels` leading labels into `labels`, and the number
// actually written into `n_labels`.
//
// Several labels rather than one: validating a record means checking the whole
// owner name, not its first label. An SRV for "Dev._ssh._tcp.local" and one for
// "Dev._http._tcp.local" share a first label, and matching on that alone made
// whichever arrived last win.
//
// Returns the offset of the byte after the name *in the record being read* -
// which is not where parsing ended if a pointer was followed - or 0 on a
// malformed name.
static size_t read_name(const uint8_t *pkt, size_t pkt_len, size_t offset,
    char labels[][MDNS_LABEL_SIZE], size_t max_labels, size_t *n_labels) {
    size_t resume = 0;
    size_t jumps = 0;
    size_t got = 0;

    for (size_t i = 0; i < max_labels; i++) {
        labels[i][0] = '\0';
    }
    if (n_labels != NULL) {
        *n_labels = 0;
    }

    while (offset < pkt_len) {
        uint8_t len = pkt[offset];
        if (len == 0) {
            // End of name.
            if (n_labels != NULL) {
                *n_labels = got;
            }
            return resume != 0 ? resume : offset + 1;
        }
        if ((len & 0xc0) == 0xc0) {
            if (offset + 1 >= pkt_len) {
                return 0;
            }
            size_t pointer = ((size_t)(len & 0x3f) << 8) | pkt[offset + 1];
            if (resume == 0) {
                resume = offset + 2;
            }
            // Bound the jumps so a self-referential packet cannot hang us.
            jumps++;
            if (jumps > 8 || pointer >= pkt_len || pointer == offset) {
                return 0;
            }
            offset = pointer;
            continue;
        }
        if ((len & 0xc0) != 0) {
            // Reserved label type.
            return 0;
        }
        offset++;
        if (offset + len > pkt_len) {
            return 0;
        }
        if (got < max_labels) {
            size_t copy = len;
            if (copy > MDNS_LABEL_SIZE - 1) {
                copy = MDNS_LABEL_SIZE - 1;
            }
            memcpy(labels[got], pkt + offset, copy);
            labels[got][copy] = '\0';
            got++;
        }
        offset += len;
    }
    return 0;
}

// Skip a name without decoding it. Same return convention as read_name().
static size_t skip_name(const uint8_t *pkt, size_t pkt_len, size_t offset) {
    return read_name(pkt, pkt_len, offset, NULL, 0, NULL);
}

// Does a name's labels[1..] name the service this browse asked about, i.e. is
// `labels` of the form <instance>.<service>.<protocol>[.local]?
static bool is_instance_of(char labels[][MDNS_LABEL_SIZE], size_t n_labels,
    const char *service_type, const char *protocol) {
    return n_labels >= 3 &&
           name_eq(labels[1], service_type) &&
           name_eq(labels[2], protocol);
}

static uint16_t read_u16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

typedef struct {
    mdns_remoteservice_obj_t *out;
    size_t count;
    size_t max;
    const char *service_type;
    const char *protocol;
} find_state_t;

// Look up an in-progress result by instance name, appending a new one if there
// is room. Returns NULL when the result array is full.
static mdns_remoteservice_obj_t *get_entry(find_state_t *state, const char *instance_name, bool create) {
    if (instance_name[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < state->count; i++) {
        if (name_eq(state->out[i].instance_name, instance_name)) {
            return &state->out[i];
        }
    }
    if (!create || state->count >= state->max) {
        return NULL;
    }
    mdns_remoteservice_obj_t *entry = &state->out[state->count];
    memset(entry, 0, sizeof(*entry));
    entry->base.type = &mdns_remoteservice_type;
    strlcpy(entry->instance_name, instance_name, sizeof(entry->instance_name));
    strlcpy(entry->service_name, state->service_type, sizeof(entry->service_name));
    strlcpy(entry->protocol, state->protocol, sizeof(entry->protocol));
    state->count++;
    return entry;
}

// Walk every resource record in a response.
//
// `want_addresses` selects the pass: the first pass takes PTR and SRV records to
// learn which instances exist and what host each one points at, the second takes
// A records and attaches addresses to the instances found. Two passes because
// the A record for an SRV target usually arrives in the additional section,
// which may be *before* or after the SRV record that needs it.
static void parse_records(find_state_t *state, const uint8_t *pkt, size_t pkt_len, bool want_addresses) {
    if (pkt_len < DNS_HEADER_LEN) {
        return;
    }
    // Only parse responses.
    if ((pkt[2] & 0x80) == 0) {
        return;
    }
    size_t questions = read_u16(pkt + 4);
    size_t records = (size_t)read_u16(pkt + 6) + read_u16(pkt + 8) + read_u16(pkt + 10);

    size_t offset = DNS_HEADER_LEN;
    for (size_t i = 0; i < questions; i++) {
        offset = skip_name(pkt, pkt_len, offset);
        if (offset == 0 || offset + 4 > pkt_len) {
            return;
        }
        offset += 4;
    }

    char owner[MDNS_MAX_LABELS][MDNS_LABEL_SIZE];
    size_t owner_n;
    for (size_t i = 0; i < records; i++) {
        offset = read_name(pkt, pkt_len, offset, owner, MDNS_MAX_LABELS, &owner_n);
        if (offset == 0 || offset + 10 > pkt_len) {
            return;
        }
        uint16_t type = read_u16(pkt + offset);
        uint16_t rrclass = read_u16(pkt + offset + 2) & DNS_CLASS_MASK;
        uint16_t rdlength = read_u16(pkt + offset + 8);
        size_t rdata = offset + 10;
        offset = rdata + rdlength;
        if (offset > pkt_len) {
            return;
        }
        if (rrclass != DNS_RRCLASS_IN) {
            continue;
        }

        if (!want_addresses && type == DNS_RRTYPE_PTR) {
            // The owner is <service>.<protocol>.local. Check BOTH labels: a PTR
            // for _http._udp must not satisfy a browse for _http._tcp.
            if (owner_n < 2 || !name_eq(owner[0], state->service_type) ||
                !name_eq(owner[1], state->protocol)) {
                continue;
            }
            // RDATA is the full instance name: <instance>.<service>.<protocol>.
            char target[MDNS_MAX_LABELS][MDNS_LABEL_SIZE];
            size_t target_n;
            if (read_name(pkt, pkt_len, rdata, target, MDNS_MAX_LABELS, &target_n) == 0) {
                continue;
            }
            if (!is_instance_of(target, target_n, state->service_type, state->protocol)) {
                continue;
            }
            get_entry(state, target[0], true);
        } else if (!want_addresses && type == DNS_RRTYPE_SRV) {
            // The owner is <instance>.<service>.<protocol>.local and RDATA is
            // priority(2) weight(2) port(2) target-name.
            if (rdlength < 7) {
                continue;
            }
            // Check the whole owner name, not just the instance label, so an SRV
            // for a different service of the same instance cannot overwrite us.
            if (!is_instance_of(owner, owner_n, state->service_type, state->protocol)) {
                continue;
            }
            // Only complete an instance the PTR pass already accepted, so an
            // unrelated SRV cannot add a result this browse did not ask for.
            mdns_remoteservice_obj_t *entry = get_entry(state, owner[0], false);
            if (entry == NULL) {
                continue;
            }
            entry->port = read_u16(pkt + rdata + 4);
            char target[1][MDNS_LABEL_SIZE];
            if (read_name(pkt, pkt_len, rdata + 6, target, 1, NULL) != 0) {
                strlcpy(entry->hostname, target[0], sizeof(entry->hostname));
            }
        } else if (want_addresses && type == DNS_RRTYPE_A) {
            if (rdlength != 4) {
                continue;
            }
            // Attach to every instance whose SRV target is this host. The
            // hostname guard matters: without it an entry that has no SRV yet
            // (empty hostname) matches an A record with a root owner name.
            for (size_t j = 0; j < state->count; j++) {
                mdns_remoteservice_obj_t *entry = &state->out[j];
                if (entry->ipv4_address == 0 && entry->hostname[0] != '\0' &&
                    owner_n >= 1 && name_eq(entry->hostname, owner[0])) {
                    // Keep the bytes in network order, as the native backends do.
                    entry->ipv4_address = (uint32_t)pkt[rdata] |
                        (uint32_t)pkt[rdata + 1] << 8 |
                                (uint32_t)pkt[rdata + 2] << 16 |
                                (uint32_t)pkt[rdata + 3] << 24;
                }
            }
        }
        // TXT records are skipped: RemoteService has nowhere to put them today.
    }
}

// Build "<service_type>.<protocol>.local" as a length-prefixed DNS QNAME,
// followed by QTYPE and QCLASS. Returns the total query length, or 0 if the
// names do not fit.
static size_t build_query(uint8_t *buf, size_t buf_len, const char *instance,
    const char *service_type, const char *protocol, uint8_t qtype) {
    if (buf_len < DNS_HEADER_LEN) {
        return 0;
    }
    memset(buf, 0, DNS_HEADER_LEN);
    // ID 0 is correct for multicast DNS (RFC 6762 Section 18.1).
    buf[5] = 1; // QDCOUNT = 1
    size_t offset = DNS_HEADER_LEN;

    // With an instance the name is <instance>.<service>.<protocol>.local (an
    // SRV follow-up for one result); without, <service>.<protocol>.local.
    const char *labels[4];
    size_t n_labels = 0;
    if (instance != NULL && instance[0] != '\0') {
        labels[n_labels++] = instance;
    }
    labels[n_labels++] = service_type;
    labels[n_labels++] = protocol;
    labels[n_labels++] = "local";
    for (size_t i = 0; i < n_labels; i++) {
        size_t len = strlen(labels[i]);
        if (len == 0 || len > 63 || offset + 1 + len + 5 > buf_len) {
            return 0;
        }
        buf[offset++] = len;
        memcpy(buf + offset, labels[i], len);
        offset += len;
    }
    buf[offset++] = 0; // Root label.

    buf[offset++] = 0;
    buf[offset++] = qtype;
    uint16_t qclass = DNS_RRCLASS_IN | DNS_CLASS_UNICAST_RESPONSE;
    buf[offset++] = qclass >> 8;
    buf[offset++] = qclass & 0xff;
    return offset;
}

// Receive one datagram, treating a timeout or would-block as "nothing arrived"
// rather than letting it escape as an OSError. A browse is best-effort.
static size_t try_recv(socketpool_socket_obj_t *sock, uint8_t *buf, size_t buf_len) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_uint_t received = common_hal_socketpool_socket_recvfrom_into(sock, buf, buf_len, NULL);
        nlr_pop();
        return received;
    }
    return 0;
}

size_t shared_module_mdns_find(mp_obj_t network_interface, const char *service_type,
    const char *protocol, mp_float_t timeout, mdns_remoteservice_obj_t *out, size_t out_len) {

    if (out_len == 0) {
        return 0;
    }

    // Zero both objects: they live on the C stack, and socketpool_socket() does
    // not set every field. On espressif it leaves ssl_socket and connected alone
    // (heap and static sockets are already zeroed), and socketpool_socket_close()
    // treats a non-NULL ssl_socket as an SSL socket to close, so stack garbage
    // there crashed the board in close().
    socketpool_socketpool_obj_t pool = { 0 };
    pool.base.type = &socketpool_socketpool_type;
    common_hal_socketpool_socketpool_construct(&pool, network_interface);

    socketpool_socket_obj_t sock = { 0 };
    if (!socketpool_socket(&pool, SOCKETPOOL_AF_INET, SOCKETPOOL_SOCK_DGRAM, 0, &sock)) {
        mp_raise_RuntimeError(MP_ERROR_TEXT("Unable to start mDNS query"));
    }

    find_state_t state;
    state.out = out;
    state.count = 0;
    state.max = out_len;
    state.service_type = service_type;
    state.protocol = protocol;

    // Everything below must not leak the socket, which socketpool_socket()
    // deliberately leaves unregistered.
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        // An ephemeral source port is what makes this a one-shot query rather
        // than a full mDNS responder sharing port 5353.
        if (common_hal_socketpool_socket_bind(&sock, "", 0, 0) != 0) {
            mp_raise_RuntimeError(MP_ERROR_TEXT("Unable to start mDNS query"));
        }
        // RFC 6762 Section 11 wants a TTL of 255. Not every backend implements
        // the option, and the default of 1 still reaches the local link, so a
        // failure here is not fatal.
        uint8_t ttl = 255;
        common_hal_socketpool_socket_setsockopt(&sock, SOCKETPOOL_IPPROTO_IP,
            SOCKETPOOL_IP_MULTICAST_TTL, &ttl, sizeof(ttl));
        common_hal_socketpool_socket_settimeout(&sock, MDNS_POLL_INTERVAL_MS);

        // Deliberately static, not m_malloc(): mdns_server_find() is called
        // from the supervisor's web workflow where there may be no VM heap to
        // allocate from. Documented as non-reentrant on that basis.
        static uint8_t buf[MDNS_PACKET_SIZE];

        size_t query_len = build_query(buf, MDNS_PACKET_SIZE, NULL, service_type, protocol,
            DNS_RRTYPE_PTR);
        if (query_len == 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("Invalid service type"));
        }
        common_hal_socketpool_socket_sendto(&sock, MDNS_MULTICAST_IPV4,
            strlen(MDNS_MULTICAST_IPV4), MDNS_PORT, buf, query_len);

        uint64_t start_ticks = supervisor_ticks_ms64();
        // A negative timeout would wrap to an effectively infinite loop.
        uint64_t timeout_ms = timeout > 0 ? (uint64_t)(timeout * 1000) : 0;
        // Run to the full timeout even once the array is full: later packets
        // still carry the SRV and A records that complete entries already found.
        // A responder MAY answer with only the PTR and leave SRV/A to a
        // follow-up (RFC 6763 Section 12.1 is a SHOULD, not a MUST). Without
        // chasing those the browse returns entries with port 0 and no address,
        // which looks like a flaky browser. Ask once, halfway through.
        bool chased = false;
        while (supervisor_ticks_ms64() - start_ticks < timeout_ms &&
               !mp_hal_is_interrupted()) {
            RUN_BACKGROUND_TASKS;
            uint64_t elapsed = supervisor_ticks_ms64() - start_ticks;
            if (!chased && elapsed > timeout_ms / 2) {
                chased = true;
                for (size_t i = 0; i < state.count; i++) {
                    if (state.out[i].port != 0) {
                        continue;
                    }
                    size_t n = build_query(buf, MDNS_PACKET_SIZE, state.out[i].instance_name,
                        service_type, protocol, DNS_RRTYPE_SRV);
                    if (n != 0) {
                        common_hal_socketpool_socket_sendto(&sock, MDNS_MULTICAST_IPV4,
                            strlen(MDNS_MULTICAST_IPV4), MDNS_PORT, buf, n);
                    }
                }
            }
            if (!common_hal_socketpool_readable(&sock)) {
                // Without a yield this loop spins at 100% CPU for the whole
                // timeout, which also starves the background tasks it runs.
                mp_hal_delay_ms(1);
                continue;
            }
            size_t received = try_recv(&sock, buf, MDNS_PACKET_SIZE);
            if (received == 0) {
                continue;
            }
            parse_records(&state, buf, received, false);
            parse_records(&state, buf, received, true);
        }
        nlr_pop();
    } else {
        socketpool_socket_close(&sock);
        nlr_jump(nlr.ret_val);
    }
    socketpool_socket_close(&sock);

    return state.count;
}
