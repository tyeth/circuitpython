// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2020 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#pragma once

#include "py/obj.h"

#include "shared-bindings/wifi/ScannedNetworks.h"
#include "shared-bindings/wifi/Network.h"

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi.h>

// Stations tracked for wifi.radio.stations_ap; more than this and the oldest
// entries are simply not listed.
#define WIFI_AP_MAX_STATIONS 8

// Event bits for the Radio event group.
#define WIFI_SCAN_DONE_BIT BIT0
#define WIFI_CONNECTED_BIT BIT1
#define WIFI_DISCONNECTED_BIT BIT2

typedef struct {
    mp_obj_base_t base;
    wifi_scannednetworks_obj_t *current_scan;
    // StaticEventGroup_t event_group;
    // EventGroupHandle_t event_group_handle;
    // wifi_config_t sta_config;
    // wifi_network_obj_t ap_info;
    // esp_netif_ip_info_t ip_info;
    // esp_netif_dns_info_t dns_info;
    struct net_if *sta_netif;
    // uint32_t ping_elapsed_time;
    // wifi_config_t ap_config;
    // esp_netif_ip_info_t ap_ip_info;
    struct net_if *ap_netif;
    bool started;
    bool ap_mode;
    bool sta_mode;
    uint8_t retries_left;
    uint8_t starting_retries;
    uint8_t last_disconnect_reason;
    // Signalled from the net_mgmt event handler when a connect attempt
    // finishes, so common_hal_wifi_radio_connect() can wait on the result.
    struct k_sem connect_sem;
    // Latest wifi_conn_status from NET_EVENT_WIFI_CONNECT_RESULT.
    int last_connect_status;
    bool connected;
    // SSID of the association that `connected` refers to, so that a connect()
    // for the network we are already on can return without touching the link.
    uint8_t current_ssid[WIFI_SSID_MAX_LEN];
    size_t current_ssid_len;

    // Access point state. The AIROC (CYW43439) driver runs the AP on the same
    // net_if as the station, so this is bookkeeping beside sta_netif rather
    // than a second interface: the AP's IPv4 configuration, whether the DHCPv4
    // server is up, and the stations the driver has reported as associated.
    struct net_in_addr ap_addr;
    struct net_in_addr ap_netmask;
    struct net_in_addr ap_gw;
    bool ap_addr_configured;
    bool dhcp_server_running;
    uint8_t ap_stations[WIFI_AP_MAX_STATIONS][6];
    size_t ap_station_count;
} wifi_radio_obj_t;

// Maintained from the net_mgmt AP station events (common-hal/wifi/__init__.c).
void wifi_radio_ap_station_add(wifi_radio_obj_t *self, const uint8_t *mac);
void wifi_radio_ap_station_remove(wifi_radio_obj_t *self, const uint8_t *mac);
// Non-raising AP teardown for the supervisor's wifi_reset().
void wifi_radio_ap_reset(wifi_radio_obj_t *self);

extern void common_hal_wifi_radio_gc_collect(wifi_radio_obj_t *self);
