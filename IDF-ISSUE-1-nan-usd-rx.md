# NAN-USD: received Service Discovery Frames never reach the discovery engine

**Status: draft, not filed.** One measurement is still outstanding — see
[What would settle it](#what-would-settle-it).

## Summary

With `CONFIG_ESP_WIFI_NAN_USD_ENABLE=y`, a USD publisher transmits valid NAN
Service Discovery Frames and a USD subscriber's own radio receives them, but
`nan_de_rx_sdf()` is never called, so discovery never completes. Two ESP32s
configured as publisher and subscriber never discover each other, in either
role assignment.

The subscriber's NIC counted **407 NAN SDFs** during a 60 s window in which
`nan_de_rx_sdf()` was entered zero times.

## Environment

| | |
|---|---|
| ESP-IDF | `v6.1-dev-6096` (also reproduced on `v6.1-dev-6107`, `cb73317`) |
| Targets | ESP32-D0WDQ6 **rev v0.0 (ECO0)** and ESP32-S3 |
| Config | `CONFIG_IDF_EXPERIMENTAL_FEATURES=y`, `CONFIG_ESP_WIFI_NAN_USD_ENABLE=y` |
| Logging | `CONFIG_WPA_DEBUG_PRINT=y` plus `esp_log_level_set("wpa", ESP_LOG_DEBUG)` |
| Regulatory | `cc=01`, resolved to `GB` MANUAL |

> **Chip revision.** The ESP32 under test is a SparkFun ESP32 Thing carrying
> an **ESP32-D0WDQ6 revision v0.0 (ECO0)**, 26 MHz crystal — first silicon.
> This is still supported by this IDF (`CONFIG_ESP32_REV_MIN` defaults to
> `ESP32_REV_MIN_0`), and `esp_wifi`, `esp_phy` and `esp_coex` contain **no
> revision-conditional code at all** — the only rev-0 workaround anywhere in
> IDF is the brownout interrupt. So a software-side cause would not be
> revision-specific. An analogue or RF difference in first silicon cannot be
> excluded from here, and reproduction on current silicon (v3.x) is a
> reasonable thing to ask for before treating timing or receive anomalies as
> firmware defects.

Both boards publish/subscribe the service name `ESP-USD-TEST` with
`ESP_USD_PUBLISH_DEFAULT_PARAMS()` / `ESP_USD_SUBSCRIBE_DEFAULT_PARAMS()`,
pinned to channel 6, with no STA association.

## What was measured

**The publisher transmits correctly.** Captured off the air with an
independent ESP32 in promiscuous mode: Public Action (category `0x04`),
Vendor Specific (`0x09`), WFA OUI `50:6f:9a`, OUI type `0x13`, carrying SDA
(`0x03`) and SDEA (`0x0e`) attributes, at −58 dBm, roughly every 150 ms.
These are well-formed NAN SDFs.

**The subscriber's radio receives them.** Running promiscuous mode alongside
the USD subscribe on the same chip, the subscriber counted 407 NAN SDFs in
60 s.

**The discovery engine is never invoked.** `nan_de_rx_sdf()`
(`components/wpa_supplicant/src/common/nan_de.c:1271`) logs `NAN: RX SDF from`
at `MSG_DEBUG`. That line never appears. The logging is definitely live: the
positive control

```
D (785) wpa: NAN: Assigned new subscribe handle 1 for ESP-USD-TEST
```

is emitted from the same module at the same level in the same run.

## Ruled out by measurement

- **Channel.** Both ends pinned to channel 6; `esp_nan_chan_to_freq(6)`
  returns 2437, which is correct.
- **Receiver off-channel.** `WIFI_EVENT_ROC_DONE` fires for channel 6 about
  once a second throughout.
- **Transmit failure.** `WIFI_EVENT_ACTION_TX_STATUS` never reports `FAILED`.
- **Start-time skew.** Publisher started first, subscriber first, and both
  simultaneously.
- **Regulatory domain.** Channel 6 is permitted under the active domain.
- **Power save.** Disabled.
- **Chip and direction.** Roles swapped between the ESP32 and the ESP32-S3;
  zero discoveries in every combination.
- **`.type` / active-vs-passive.** Not a factor, and worth stating because it
  is the obvious thing to try: the USD path ignores it.
  `ESP_USD_SUBSCRIBE_DEFAULT_PARAMS()` hardcodes `.active = false` and
  `ESP_USD_PUBLISH_DEFAULT_PARAMS()` hardcodes `.unsolicited = true,
  .solicited = true`, so setting it changes nothing.

## The open-source path looks correct

`esp_nan_de_rx_action()`
(`components/wpa_supplicant/esp_supplicant/src/esp_nan_usd.c:142`) is
registered as `rx_cb` on both the action-TX request and the remain-on-channel
request. Reviewed against the captured frames, every gate in it should pass:

- `len < 6` — frames are far longer.
- Category `WLAN_ACTION_PUBLIC`, action `WLAN_PA_VENDOR_SPECIFIC`, OUI
  `OUI_WFA`, OUI type `NAN_OUI_TYPE` — all four match the capture exactly.
- `esp_nan_chan_to_freq(6)` returns 2437, not `-1`.
- `nan_usd_try_lock_active()` fails only if the mutex is absent or `g_nan_de`
  is `NULL`; the subscribe succeeded, so `g_nan_de` is set.

The listen path at `esp_nan_usd.c:240` also sets `req->allow_broadcast = true`
on the `wifi_roc_req_t`, which is documented as what lets broadcast/multicast
action frames from any network reach the callback.

So nothing in the supplicant explains the loss.

## Leading hypothesis

NAN SDFs carry the NAN Network ID `51:6f:9a:01:00:00` in both A1 and A3. The
first octet is `0x51`, whose I/G bit is set, so this is a **multicast address
but not the broadcast address**. The hardware receive filter on a STA
interface accepts frames addressed to the station's own MAC, to broadcast, and
to multicast groups it has joined.

The hypothesis is that the NAN Network ID is not installed in the receive
filter while USD is running, so these frames are dropped in hardware before
`rx_cb` is ever reached. The `allow_broadcast` documentation explicitly
distinguishes the two cases — "frames whose Address3/BSSID is already
broadcast are always delivered" — and `51:6f:9a:01:00:00` is not broadcast.

This fits the central observation. Promiscuous mode counts the frames because
promiscuous delivery bypasses the MAC address filter and is a different
delivery path from the management-frame callback; it does not re-enable
`rx_cb` for them.

**This is a hypothesis, not a measurement.** It is consistent with everything
observed, but the receive filter is inside `libnet80211.a` and cannot be
inspected from source.

## What would settle it

One line, in open source, at the top of `esp_nan_de_rx_action()`:

```c
int esp_nan_de_rx_action(uint8_t *hdr, uint8_t *payload, size_t len, uint8_t channel)
{
+   wpa_printf(MSG_ERROR, "NAN-USD: rx_action entered, len=%u chan=%u", (unsigned)len, channel);
    struct ieee80211_hdr *rx_hdr = (struct ieee80211_hdr *)hdr;
```

- **Never printed** → the frames are dropped below the supplicant, and the
  receive-filter hypothesis above is the place to look.
- **Printed, but `nan_de_rx_sdf()` still not reached** → the loss is in the
  open-source gate chain after all, and the specific gate is then one more log
  line away. That case is fixable in this repository.

`MSG_ERROR` rather than `MSG_DEBUG` deliberately, so the result does not
depend on the reporter's log level.

This is already applied on a branch, based on `cb73317` so it matches the
pin currently under test:

```
https://github.com/tyeth/esp-idf/tree/nan-usd-rx-localise
```

One commit, one file, +14/−0.

## Reproducer

No NAN application code is required. Two ESP32s, one calling
`esp_wifi_nan_usd_start()` with a publish, the other with a subscribe, both
pinned to channel 6, with `esp_log_level_set("wpa", ESP_LOG_DEBUG)`. The
subscriber never reports a discovery.

## Impact

NAN-USD is non-functional for discovery on ESP-IDF today. Since
`ESP_WIFI_NAN_USD_ENABLE` depends only on `IDF_EXPERIMENTAL_FEATURES` and not
on `SOC_WIFI_NAN_SUPPORT`, USD is the only Wi-Fi Aware mode available on parts
such as the ESP32-S3, and it is the only one that can run alongside a STA
association. Both properties make it the natural foundation for a portable
Wi-Fi Aware binding, and neither is usable while discovery does not complete.

## Asks

1. Is the NAN Network ID `51:6f:9a:01:00:00` installed in the receive filter
   when USD is active on the STA interface?
2. If not, should `allow_broadcast` cover it, or is a separate multicast
   subscription needed?
3. Would you take the instrumentation line above, or is there an existing way
   to trace `rx_cb` delivery that we have missed?
