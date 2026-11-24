# NAN-USD: group-addressed SDFs are dropped before the ROC Rx callback unless A3 is broadcast, so USD discovery never completes

**Status: draft, ready.** Measured on current silicon (ESP32 rev v3.1 and
ESP32-S2) with instrumented logging in the open-source receive path. The loss
is localised to below the supplicant, and the deciding address field is
identified.

## Summary

With `CONFIG_ESP_WIFI_NAN_USD_ENABLE=y`, a USD subscriber that is listening on
the right channel never has its ROC receive callback (`rx_cb`,
`esp_nan_de_rx_action()`) entered for the publisher's Service Discovery
Frames, so discovery never completes. Two ESP boards running the unmodified
`usd_publisher` / `usd_subscriber` examples never discover each other, in
either role assignment.

The frames are dropped because their **Address 3 (BSSID) is the NAN Network ID
`51:6f:9a:01:00:00`, not broadcast**. Changing only A3 to
`ff:ff:ff:ff:ff:ff` makes them arrive and discovery complete in under a
second. Changing only A1 does not.

This contradicts the documented behaviour of `wifi_roc_req_t.allow_broadcast`,
which the USD listen path sets to `true`:

> If set to true, broadcast/multicast action frames from any network
> (Address3/BSSID=ANY) will be received in the ROC Rx callback

## Environment

| | |
|---|---|
| ESP-IDF | `v6.1-dev-6107` (`cb73317`), `esp_wifi/lib` at `7cd4d7b2` |
| Targets | ESP32-U4WDH **rev v3.1** (40 MHz crystal) and ESP32-S2FNR2 rev v1.0 |
| Projects | `examples/wifi/wifi_aware/usd_publisher` and `usd_subscriber`, unchanged apart from logging |
| Config | `CONFIG_IDF_EXPERIMENTAL_FEATURES=y`, `CONFIG_ESP_WIFI_NAN_USD_ENABLE=y`, `CONFIG_ESP_WIFI_DEBUG_PRINT=y`, `esp_log_level_set("wpa", ESP_LOG_DEBUG)` |
| Mode | `WIFI_MODE_NULL`, no association, default channel 6 |
| Capture | a third ESP32 in promiscuous mode on channel 6, written to pcap |

An earlier round on an ESP32 rev v0.0 and an ESP32-S3 showed the same
outcome. Everything below is from the rev v3.1 and S2 boards.

## Instrumentation

All in open source, in `esp_supplicant/src/esp_nan_usd.c`, at `MSG_ERROR` so
the result does not depend on log level (patch attached as
`IDF-ISSUE-1-localise.patch`):

- the first line of `esp_nan_de_rx_action()` logs every entry with A1, A2, A3
  and the first six body bytes, and each early-return gate logs which gate
  failed;
- `esp_nan_de_listen()` logs every `esp_wifi_remain_on_channel()` request,
  its `allow_broadcast` value and its return code;
- the `WIFI_EVENT_ROC_DONE` handler logs each completion.

## What was measured

**The subscriber is listening on the right channel the whole time.** In a 60 s
run it issued 56 consecutive ROC requests on channel 6, 1000 ms each,
`allow_broadcast=1`, every one returning `ESP_OK`, each followed by
`ROC_DONE`:

```
D (677) wpa: NAN: Assigned new subscribe handle 1 for ESP-USD-TEST
E (687) wpa: NAN-USD: ROC req chan=6 dur=1000 allow_bcast=1 -> 0
E (1687) wpa: NAN-USD: ROC_DONE chan=6
E (1687) wpa: NAN-USD: ROC req chan=6 dur=1000 allow_bcast=1 -> 0
...
```

**The publisher's SDFs are on the air, well-formed and strong.** In the same
60 s, the sniffer captured 134 USD publish SDFs from the publisher on channel
6 at about −10 dBm: Public Action, Vendor Specific, WFA OUI, OUI type `0x13`,
SDA and SDEA attributes, with A1 = A3 = `51:6f:9a:01:00:00`. Two other NAN
devices nearby added 164 more SDFs on channel 6, at −32 and −61 dBm.

**`rx_cb` is never entered.** Not once for any of those roughly 300 frames.
Because the entry log is the first statement in the callback, no gate in the
open-source path is involved. The frames are discarded below the supplicant.

The same holds with the roles swapped (ESP32 publishing, ESP32-S2
subscribing): 39 accepted ROC windows on channel 6, 87 publish SDFs on air,
zero callback entries, no discovery.

## The deciding experiment

The publisher was rebuilt so that `esp_nan_de_tx()` replaces the NAN Network
ID with `ff:ff:ff:ff:ff:ff` in A1, in A3, or in both, for group-addressed SDFs
only. Nothing else changed. ESP32-S2 publishing, ESP32 rev v3.1 subscribing:

| A1 (DA) | A3 (BSSID) | `rx_cb` entered | discovery |
|---|---|---|---|
| `51:6f:9a:01:00:00` | `51:6f:9a:01:00:00` | never (134 frames on air) | never |
| `ff:ff:ff:ff:ff:ff` | `51:6f:9a:01:00:00` | never (101 frames on air) | never |
| `51:6f:9a:01:00:00` | `ff:ff:ff:ff:ff:ff` | yes | 1.0 s after boot |
| `ff:ff:ff:ff:ff:ff` | `ff:ff:ff:ff:ff:ff` | yes | 0.9 s after boot |

With A3 broadcast, the first frame arrives and is processed normally:

```
E (867) wpa: NAN-USD: rx_action #1 len=24 chan=6 A1=ff:ff:ff:ff:ff:ff A2=7c:e8:b1:4a:11:14 A3=ff:ff:ff:ff:ff:ff body=04 09 506f9a 13
D (877) wpa: NAN: RX SDF from 7c:e8:b1:4a:11:14 freq=2437 len=18
I (897) wpa: NAN_USD DISCOVERY_RESULT - subscribe_id = 1 peer_publish_id = 1 peer_address = 7c:e8:b1:4a:11:14 service_protocol_type = 0
```

The reverse direction (ESP32 publishing with A3 broadcast, ESP32-S2
subscribing) also discovers at once: the subscriber matched before its USB
console attached, about a second after boot, and its unicast follow-ups to the
publisher are on air from the start of the capture. With the stock A3 it
never discovers.

**Unicast frames are not affected.** Follow-ups addressed to the receiver's
own MAC, with A3 still `51:6f:9a:01:00:00`, reach `rx_cb` normally; the
ESP32 publisher logged 20 such entries and 11 follow-up receptions. So the
filter applies to group-addressed frames whose A3 is not broadcast.

## Why ESP-to-ESP USD can never work as shipped

The supplicant itself transmits USD SDFs with A3 set to the NAN Network ID.
In `components/wpa_supplicant/src/common/nan_de.c`, `nan_de_tx_multicast()`:

```c
	if (srv->is_p2p) {
		network_id = p2p_network_id;
		bssid = wildcard_bssid;
	} else {
		network_id = nan_network_id;
		bssid = nan_network_id;
	}
```

So every non-P2P USD publish or active subscribe an ESP sends carries an A3
that another ESP's receive path discards. Discovery between two ESPs cannot
complete, in either direction, whatever the timing or channel.

## Already ruled out

- **Channel.** Both ends on channel 6; `esp_nan_chan_to_freq(6)` = 2437. The
  subscriber is on channel 6 for essentially the whole run (ROC log above).
- **The open-source gate chain.** The entry log precedes every gate and never
  prints, so none of `len < 6`, category/action/OUI/type,
  `esp_nan_chan_to_freq()` or `nan_usd_try_lock_active()` is involved.
- **Signal level.** The publisher was received by the sniffer at −10 to
  −17 dBm, the other devices at −32 and −61 dBm; none reached `rx_cb`.
- **Transmit failure.** The frames are on air, and with A3 changed the same
  frames are received.
- **Chip.** ESP32 rev v3.1 and ESP32-S2 behave identically, both directions.
- **`.type` / active vs passive.** The USD path ignores it:
  `ESP_USD_SUBSCRIBE_DEFAULT_PARAMS()` hardcodes `.active = false` and
  `ESP_USD_PUBLISH_DEFAULT_PARAMS()` hardcodes `.unsolicited = true,
  .solicited = true`.

## Impact

NAN-USD discovery does not work between ESP devices on ESP-IDF today. It also
cannot receive standard USD SDFs from other implementations, since the Wi-Fi
Aware specification puts a non-broadcast value in A3 (the NAN Cluster ID in
v4.0; the comment in `nan_de.c` near the follow-up path discusses this). The
same filter also drops NAN-Sync SDFs, whose A3 is a Cluster ID such as
`50:6f:9a:01:db:dd`: none of those reached `rx_cb` either.

Since `ESP_WIFI_NAN_USD_ENABLE` depends only on `IDF_EXPERIMENTAL_FEATURES`
and not on `SOC_WIFI_NAN_SUPPORT`, USD is the only Wi-Fi Aware mode on parts
such as the ESP32-S3, and the only one that can run alongside a STA
association.

## Asks

1. **Make `allow_broadcast` behave as documented**: when set on a ROC
   request, deliver group-addressed Public Action frames to `rx_cb` whatever
   their A3, not only when A3 is broadcast.
2. Until then, consider whether `nan_de_tx_multicast()` should use the
   wildcard BSSID for A3 on the non-P2P path, as it already does for P2P.
   That alone makes ESP-to-ESP USD work (measured above), though it does not
   fix reception from other implementations.
3. If the current filtering is intended, please correct the
   `allow_broadcast` documentation, and say how a USD subscriber is meant to
   receive SDFs whose A3 is the NAN Network ID or a Cluster ID.
