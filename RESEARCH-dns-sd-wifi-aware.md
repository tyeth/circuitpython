# DNS-SD + Wi-Fi Aware (NAN) support in CircuitPython — research

Tracking: tyeth/deepsleep_espnow_wifi_and_ble_env_collector#18
CircuitPython @ upstream `762ab15ca0` · ESP-IDF pin `v6.1-dev-6096-g29d36039e56` (adafruit `circuitpython-v6.1`)
**v6** — two reversals from new hardware work. **§13.1a: USD does *not* inherently break an associated STA** — the earlier result was my own channel pinning. **§13.2a: the USD defect is localised** — 407 SDFs reach the NIC and the discovery engine is never called. Also reframes "needs NAN silicon" as a firmware gate (§14.2a), and revises §6/§7.1/§9/§10/§12 which v4 had left stale. v5 fixed three errors a review caught; v1's central conclusion was wrong too (§11).

## 0. TL;DR

* **There are two Wi-Fi Aware protocols, not one**, and nearly every conclusion depends on which you mean:
  * **NAN-Sync** — clustered, synchronised. Gives pairing and an **IPv6 datapath** (real sockets). Gated by `SOC_WIFI_NAN_SUPPORT`: **esp32, esp32s2, esp32c5, esp32c61, esp32s31** — *not* S3/C3/C6. **Exclusive radio mode in this IDF** (`wifi:mode : NAN`, confirmed on hardware). See §14.2a — "needs NAN silicon" is *not* a justified reading of that gate.
  * **NAN-USD** — unsynchronised. Publish / subscribe / follow-up messages only, **no datapath, no pairing**. Runs on the **STA interface**, and needs **no special hardware at all**: `ESP_WIFI_NAN_USD_ENABLE` depends only on `IDF_EXPERIMENTAL_FEATURES` (`esp_wifi/Kconfig:620`), never on `SOC_WIFI_NAN_SUPPORT` (which gates only `ESP_WIFI_NAN_SYNC_ENABLE`, `:595-598`). Confirmed on hardware: the ESP32-S3, with zero `SOC_WIFI_NAN_SUPPORT`, ran the USD stack and put well-formed NAN frames on the air. **A USD publish is an ordinary Public Action vendor-specific management frame** — anything that can transmit one and do remain-on-channel could in principle speak USD.
* **USD looked like the right answer for issue #18 — hardware says not yet.** Two measured results (§13):
  * **USD *does* coexist with STA — on the AP's channel** (§13.1a). An earlier version of this document claimed USD is inherently radio-owning; that was an artefact of my own harness pinning USD to channel 6 while the AP was on channel 1. Re-run with USD following the AP's channel, **both boards stayed associated for 100 s and the home channel never left the AP**. Off-channel, though, the link dies **silently** — no `STA_DISCONNECTED`, stale IP kept. **API rule that falls out of this: default USD's channel to the associated AP's, and only allow another when no STA is up.**
* **USD discovery is broken in the pinned IDF, and the defect is now localised** (§13.2a). The subscriber's own radio receives **407 NAN SDFs** while its USD subscribe is active, yet `nan_de_rx_sdf()` is **never entered** (proved with a positive control on the same log tag). Frames arrive at the NIC; the closed Wi-Fi blob never delivers them to the engine's remain-on-channel callback. Not a misconfiguration — a fileable upstream bug, with a reproducer that needs no NAN code.
* **NAN-Sync, by contrast, works** (§14): cluster → service match → **open (unsecured)** IPv6 datapath in **4.9 s from boot**, then ICMPv6 at **2 ms RTT, 0 % loss**, between a classic ESP32 and an ESP32-S31. Scope this honestly: **one run, ESP-to-ESP, both boards always-on, no security**. Issue #18's two distinguishing needs — **phone interop and deep sleep — are untested**, and phone interop requires `ESP_WIFI_NAN_SECURITY` / `ESP_WIFI_NAN_PAIRING`, which *are* gated on `IDF_EXPERIMENTAL_FEATURES`, the same label held against USD. For ESP-to-ESP without phones, ESP-NOW already does AP-less transport at lower wake cost; Sync's unique value lies precisely in the untested part.
* **Sync owns the radio — but honestly** (§14.4). Starting it on a live STA fires `STA_DISCONNECTED` immediately, clears the IP, refuses `esp_wifi_connect()` with `ESP_ERR_WIFI_MODE`, and gives STA back in **1.5 s** on stop. Every transition is observable, so `wifi.radio` can model it. USD, by contrast, keeps STA alive on the AP's channel (§13.1a) but fails *silently* when taken off it — so a USD binding's job is to pick the channel for you, while a Sync binding's job is to own the radio.
* **No common working denominator.** espressif has working Sync and broken USD; zephyr-cp has USD only — **and I never tested it**, so that column is "unknown", not "broken". Note too that Zephyr's hostap USD needs a driver with `remain_on_channel`/`send_action`, which the CYW43/AIROC driver lacks, so zephyr-cp USD reaches at most `frdm_rw612`. Either way Wi-Fi Aware is espressif-only and Sync-based for now — a port-specific, radio-owning module (§7.2), not the portable one §7.1 imagined.
* **Consequence: start with Feature A (DNS-SD).** It is the only part of issue #18 that is both portable and working.
* **ESP32-S3**: v1 said it was excluded outright, which was wrong — it runs the USD stack and transmits valid NAN frames (confirmed on hardware). Since the recommendation is now Sync-based, the S3 is excluded *in practice* — but by Espressif's `SOC_WIFI_NAN_SUPPORT` declaration, which is **not demonstrably a silicon limit** (§14.2a). Worth raising with Espressif rather than treating as physics.
* **CircuitPython already ships DNS-SD** as `mdns` (espressif + raspberrypi). The gap is zephyr-cp.
* **No prior art anywhere in our ecosystem**: no NAN in MicroPython, none in arduino-esp32.
* **zephyr-cp's pinned Zephyr is USD-only** — which looked like the convergence point for a portable design, until USD turned out to be the broken half (§14.3).

---

## 1. What the issue asks for

An AP-less, DHCP-less, internet-less transport for a deep-sleeping environmental sensor collector, with a DNS-SD-shaped API and iOS/Android interop. Two separable features:

| | Feature | Status |
|---|---|---|
| **A** | **DNS-SD** over mDNS on a normal IP network | **exists** as `mdns`; missing on zephyr-cp |
| **B** | **Wi-Fi Aware (NAN)** — AP-less discovery + optional datapath | **does not exist** in any port |

Feature A is independently shippable and should go **first**.

---

## 2. CircuitPython today

`shared-bindings/mdns/` — `Server` (`hostname`, `instance_name`, `find()`, `advertise_service()`) and `RemoteService`. Backends: espressif (wraps the IDF `mdns` component) and raspberrypi. Gated by `CIRCUITPY_MDNS ?= $(CIRCUITPY_WIFI)`. **zephyr-cp has no `mdns`.** No NAN anywhere.

---

## 3. ESP-IDF — the Sync/USD split

### 3.1 The decisive facts, from the pinned tree

| Fact | Evidence |
|---|---|
| Sync start **forces** `WIFI_MODE_NAN` | `nan_app.c:2003` |
| Sync stop takes the whole radio down | `nan_app.c:2057` (`esp_wifi_stop()`) |
| `wifi_mode_t` has no combined NAN+STA | `esp_wifi_types_generic.h:25-33` |
| **USD runs on `WIFI_MODE_STA`** | `nan_app.c:2986` |
| USD gated only on `IDF_EXPERIMENTAL_FEATURES`, **not** on NAN silicon | `esp_wifi/Kconfig:620-623` |
| USD example targets incl. **S3, C3, C6, C2** | `examples/wifi/wifi_aware/usd_publisher/README.md:1` |
| Sync and USD **cannot coexist** | `nan_app.c:1943` |
| Hard limits: **2 services, 2 datapath peers, 2 stored credentials** | `esp_wifi_types_generic.h:875,876,884` |
| SSI payloads: follow-up ≤2048 B, publish ≤512 B | `esp_wifi_types_generic.h:890-891` |

CP already sets `IDF_EXPERIMENTAL_FEATURES` (`ports/espressif/esp-idf-config/sdkconfig.defaults:158`), so USD, pairing and secured NDP are all reachable — but note that is Espressif-labelled **experimental** code.

### 3.2 Chip support

`SOC_WIFI_NAN_SUPPORT` is defined only for **esp32, esp32s2, esp32c5, esp32c61, esp32s31**. That gates **NAN-Sync and the datapath only**.

CP board counts: esp32 **52**, esp32s2 **56**, esp32c5 **2**, esp32c61 **1**.

**USD, and therefore discovery + messaging, works on every ESP Wi-Fi chip including the whole S3 fleet.**

### 3.3 The `wifi_aware` component is Sync-only

Espressif's `wifi_aware` component (`esp-wifi-apps`, Apache-2.0, v0.1.0) is the DNS-SD-shaped wrapper issue #18 links to. It calls `esp_wifi_nan_sync_start()` directly (`wa_init.c:113`) and its USD path is `#if 0`'d (`wa_init.c:115-122`).

So **the component implies the exclusive radio mode and the datapath**. It also:
* has **no messaging API** — init / advertise / discover / pair / connect / disconnect only (`wifi_aware.h:453-642`); follow-ups are consumed internally for its SRV/TXT exchange;
* selects peer platform at **compile time** via a Kconfig *choice* — Default / iOS / Android, **mutually exclusive** (component `Kconfig:3-31`), because Android peers support no group ciphers. **One firmware cannot serve both iOS and Android.**
* includes a **private IDF header** (`apps_private/wifi_apps_private.h`) and calls `esp_nan_set_compatibility_mode_internal` — a v0.1.0 component bound to private API on a `-dev` IDF pin.

IDF ≥6.1 is what unlocks pairing / secured NDP / iOS, and we are on 6.1-dev. That part of v1 stands.

---

## 4. Zephyr

### 4.1 DNS-SD: advertise yes, browse no

`zephyr/net/dns_sd.h` + `mdns_responder_set_ext_records()` cover advertising. **Browsing is absent** — `CONFIG_MDNS_RESOLVER` does `.local` A/AAAA only, with no PTR→SRV→TXT enumeration. `mdns.Server.find()` has nothing to wrap.

### 4.2 NAN: USD-only at our pin

zephyr-cp pins `adafruit/zephyr@ab22ad4b` (`ports/zephyr-cp/zephyr-config/west.yml`). At that revision:
* `enum wifi_nan_operation` has six ops: PUBLISH, CANCEL_PUBLISH, UPDATE_PUBLISH, SUBSCRIBE, CANCEL_SUBSCRIBE, TRANSMIT;
* hostap builds only `nan_usd.c`, `nan_usd_ap.c`, `nan_de.c`;
* **no cluster sync and no NDP anywhere in the stack.**

Upstream Zephyr main has since added `nan_ndp.c`, `nan_sec.c`, `nan_bootstrap.c` and `NAN_NDP_REQUEST`/`NAN_PAIR` control commands — only the `wifi_mgmt.h` glue is missing. So "Zephyr has no datapath" is true for us but has a **short shelf life**.

`CONFIG_WIFI_NAN_MAX_SSI_LEN` defaults to **128 bytes** — the tightest payload ceiling in the system.

### 4.3 Two zephyr-cp blockers

* **No IPv6 at all.** `CONFIG_NET_IPV6` appears nowhere in zephyr-cp. `frdm_rw612/board.conf` is IPv4 + DHCPv4 only. Any IPv6 datapath story is moot there.
* **`frdm_rw612/board.conf` has no hostap/NAN config**, and whether Wi-Fi even works on that board in zephyr-cp is unverified.

---

## 5. Prior art (as asked)

* **MicroPython: no NAN** (0 hits for `esp_wifi_nan`), **no core DNS-SD module**. The esp32 port uses the IDF mdns component only for `.local` lookups. Third-party: cbrand/micropython-mdns, nickovs/slimDNS (both pure Python). **PyDevices PR #108** adds a C `mdns` module **modeled on CircuitPython's API** — influence runs our way, so don't churn `mdns`'s shape.
* **arduino-esp32: no NAN** (0 hits). `ESPmDNS` only. **No Arduino path to cross-check against** — bring-up must use ESP-IDF's own examples.

### Phones
* **Android:** Wi-Fi Aware since 8.0 / API 26. USD support is much more recent (believed Android 16) with a small follow-up payload — **verify before committing to a USD-only design**.
* **iOS 26:** `WiFiAware` framework over Network framework; implements **DNS-SD naming on top of NAN**; service strings like `_ESP-Demo._udp` declared in `Info.plist`. **Pairing is mandatory** (6-digit PIN, no open datapath) — i.e. **iOS requires NAN-Sync + datapath**, which excludes S3 and excludes STA coexistence.

### Testing constraints
* **The Android emulator cannot test this.** No NAN HAL; `WifiAwareManager` reports unavailable. A physical Android 8+ handset is required.
* **Apple gates iOS behind the `com.apple.developer.wifi-aware` entitlement** — request and approval needed before the app will build, plus Xcode 26 and a physical iOS 26 device. **Apply now.**

---

## 6. Deep sleep — the finding that reframes the use case

NAN-Sync is structurally hostile to a deep-sleeping sensor:
* defaults `scan_time = 3 s` and `warm_up_sec = 5` before taking anchor-master role (`esp_nan.h:19-28`);
* discovery only in windows every **524 ms** (`nan_i.h:37`);
* every wake is a cold start: scan → join/form cluster → wait for a discovery window → (for iOS) PASN pairing-verify → NDP handshake;
* **estimated** seconds of radio-on per wake vs. tens of milliseconds for ESP-NOW — one to two orders of magnitude more energy per report. *Derived from constants, not measured.*
* MAC randomisation is on by default, so each wake is a new identity and a new peer handle unless paired with NVS caching;
* the peer must be on air at wake time — a phone app must have a live session.

**USD would avoid all of this**: no cluster, and `ttl = 0` sends a single publish (`esp_wifi_types_generic.h:1061`). One measured number now anchors the Sync side: **4.9 s from boot to a usable IPv6 datapath** (§14.1), of which 3.0 s is the default `scan_time` and so is tunable. That is a real figure to weigh against ESP-NOW's tens of milliseconds, and it confirms the order-of-magnitude estimate above rather than replacing it — the energy integral is still unmeasured.

**But USD cannot be used for this today** (§13.2a), so the deep-sleep argument for preferring USD is currently academic. If the upstream RX defect is fixed, this section becomes the strongest argument for USD over Sync; until then it is a reason to measure Sync's wake cost properly rather than to assume either.

**And the datapath may not be needed at all.** A sensor reading fits in a publish SSI (≤512 B) or one follow-up (≤2048 B) — no NDP, no IPv6, no sockets.

---

## 7. Proposed CircuitPython API — revised

v1 proposed capability tiers with `supported_features`. **That should be dropped.** There is no precedent for a capability-set API in `shared-bindings`; the house mechanism is module presence in the support matrix plus `NotImplementedError` on individual methods (e.g. `ports/raspberrypi/common-hal/wifi/Radio.c:540`). A module where half the classes raise on half the ports is exactly the "behaves differently per port" shape reviewers reject.

**Split by what is genuinely common instead.**

### 7.1 Core: a small USD-shaped module — **on hold**

> **Status after §13/§14: do not build this yet.** It was proposed as the portable core because both backends can nominally do USD. They cannot *usefully*: espressif's USD discovery is broken upstream (§13.2a) and zephyr-cp's is untested and reaches at most one board (§14.3). The shape below is still the right shape — it is what a fixed USD should expose — but there is nothing working to implement it against today. Build §7.2 instead, or wait for the upstream fix.

The intended shape, kept for when USD works:

```python
import wifiaware
pub = wifiaware.publish("_envcollect._udp", ssi=b"t=21.5", ttl=0)
sub = wifiaware.subscribe("_envcollect._udp")
for peer in sub:
    peer.send(b"ack")        # follow-up
```

Three constraints the hardware work added to this design:

* **Channel is not an implementation detail.** When a STA is associated, the binding must default USD's channel to the AP's, because off-channel USD kills the link *silently* (§13.1a). That is an API decision, not a tuning knob.
* **Payload limits differ** (ESP 2048 / Zephyr 128) — expose the ceiling as a constant rather than hiding it.
* **SSI is not interoperable as-is.** The ESP fork puts service-specific info on the wire raw; upstream hostap prepends the WFA OUI and protocol type and requires them on receive (§13.2). A portable API must decide which it speaks, or peers will discover each other and then fail to understand the payload.

### 7.2 Datapath + pairing: espressif-only, separate

Not a tier that raises — **classes that simply do not exist on other ports**, documented as espressif-only, like any other port-specific module. This is where iOS interop lives.

Because NAN-Sync is an **exclusive radio mode**, the entry point belongs **on or beside `wifi.radio`**, not a free-standing object: `wifi.radio` owns mode and tracks `started`/`sta_mode`/`ap_mode` (`ports/espressif/common-hal/wifi/Radio.c:62,81,102,107`). Anything that calls NAN start behind its back desynchronises that state, and web workflow / `settings.toml` auto-connect will fight it. It needs defined errors when STA/AP is active.

Agreed with v1: a new module, not an extension of `mdns` (`mdns.Server` requires a `network_interface`, `shared-bindings/mdns/Server.c:27-51`).

### 7.3 Scoped IPv6 — real, but smaller than v1 claimed

`connect`/`sendto`/`bind` all call `mp_obj_get_array_fixed_n(addr_in, 2, …)` (`shared-bindings/socketpool/Socket.c:91,129,299`), so a 4-tuple raises `ValueError` today. But output already carries scope (`getaddrinfo`/`recvfrom` return 4-tuples with `sin6_scope_id`), and lwIP parses the zoned string form `fe80::x%<netif>`. So `connect(("fe80::…%na1", port))` may already work untouched, or `Endpoint.address` can return the zoned string. Proper 4-tuple support is ~20 lines plus a common-hal signature change. **Not the dominant cost.** Moot on zephyr-cp, which has no IPv6.

---

## 8. Feature A — do this first

Porting `mdns` to zephyr-cp splits cleanly:
* `advertise_service()` → `dns_sd_rec` array + `mdns_responder_set_ext_records()`. Mechanical.
* `find()` → nothing to wrap. **Write a DNS-SD browser in `shared-module/`** over a plain UDP multicast socket.

The shared-module browser gives `mdns.Server.find()` to **every port with sockets**, present and future — directly serving the "easily facilitate other network platforms later" goal. Independently shippable, no hardware risk. Start IPv4-only (zephyr-cp has no IPv6).

---

## 9. Risks

| # | Risk | Status |
|---|---|---|
| 1 | **NAN-Sync cannot coexist with STA/AP** | **Confirmed on hardware** (§14.4): `WIFI_MODE_NAN` is exclusive, `wifi_mode_t` has no NAN+STA value. Transitions are clean and observable, and STA returns in 1.5 s. |
| 1b | **USD cannot coexist with STA** | **Disproved** (§13.1a). USD on the AP's channel holds the link for 100 s. Off-channel it dies *silently* — so the binding must pick the channel. |
| 2 | Datapath gated by `SOC_WIFI_NAN_SUPPORT` (no S3/C3/C6) | Confirmed as a Kconfig gate; **not demonstrably silicon** (§14.2a). USD needs no such gate at all. |
| 2b | **USD discovery is broken upstream** | **Confirmed and localised** (§13.2a): 407 SDFs reach the NIC, `nan_de_rx_sdf()` never runs. Blocks every USD plan until fixed. |
| 3 | **Per-wake energy/latency of NAN-Sync** | Partly measured: 4.9 s boot→datapath (§14.1). Energy still unmeasured. |
| 4 | **Compile-time iOS/Android exclusivity** | Confirmed; one firmware cannot serve both |
| 5 | **USD (Zephyr) vs Sync (component) mismatch** | Worse than stated: zephyr-cp USD is untested *and* reaches at most `frdm_rw612` (§14.3). Also, the ESP fork puts SSI on the wire raw where upstream hostap prepends the WFA OUI — so even a working USD may not interop with a Zephyr/Android peer (§13.2). |
| 6 | Limits of 2 services / 2 datapaths / 2 credentials | Confirmed |
| 7 | zephyr-cp has **no IPv6**, and rw612 Wi-Fi unverified | Confirmed |
| 8 | `wifi_aware` is v0.1.0 on private IDF API | Maintenance risk |
| 9 | Apple entitlement lead time | Apply now |
| 10 | No Android emulator path | Physical handset needed |
| 11 | Android USD API level + payload size | **Unverified** — but moot while USD discovery is broken |
| 12 | **Secured NDP / pairing is itself experimental** | `ESP_WIFI_NAN_SECURITY` and `ESP_WIFI_NAN_PAIRING` depend on `IDF_EXPERIMENTAL_FEATURES` — the same gate held against USD, and untested here. Phone interop needs them. |

---

## 10. Sequencing

Revised against §13/§14. Steps 2a and 2b of the v2 plan are **done**; the rest is reordered because USD is blocked upstream.

1. **Feature A — the portable DNS-SD browser.** Written and building (`shared-module/mdns/Browser.c`, selected by `CIRCUITPY_MDNS_SHARED`). Remaining: validate against a real responder on hardware, then the zephyr-cp `advertise_service()` half. No dependency on anything below.
2. **File the USD RX defect upstream** (§13.2a). Everything USD-shaped is blocked behind it, and the reproducer is small.
3. **Measure Sync's wake cost** — boot→datapath is 4.9 s (§14.1); the energy integral per report versus ESP-NOW is the number that actually decides issue #18.
4. **Test secured NDP and pairing** (risk #12). Untested, experimental, and the prerequisite for any phone interop.
5. **Apply for the Apple entitlement** — long lead time, start regardless.
6. **Then** decide the module: a `wifiaware` Sync module, espressif-only, owning `wifi.radio` (§7.2) — *or*, if step 2 lands a fix, revisit USD, which needs no `SOC_WIFI_NAN_SUPPORT` and coexists with STA on the AP's channel.

Note what is *not* on this list any more: "`wifiaware` USD module, espressif + zephyr" (v2 step 5). There is no working USD on either backend to build it on.

---

## 11. What changed from v1

| v1 claim | Correction |
|---|---|
| "Every ESP32-S3 board is out, permanently" | **Wrong.** The S3 runs USD; only the datapath is gated by `SOC_WIFI_NAN_SUPPORT` — which is itself a firmware declaration, not a proven silicon limit (§14.2a). |
| STA+NAN coexistence "blocking, verify on hardware" | **Settled from source** — Sync forces `WIFI_MODE_NAN`. No experiment needed. |
| NAN-USD | **Missed entirely.** v1 cited `usd_publisher` without noticing what it was, and missed `usd_subscriber`. |
| Capability tiers + `supported_features` | Dropped — no precedent in `shared-bindings`, likely rejected. |
| "Tier 1 messaging: ESP + Zephyr" | The `wifi_aware` component **has no messaging API**. |
| Zephyr facts | Verified against upstream main, **not** the pinned `adafruit/zephyr@ab22ad4b`. Pinned tree is USD-only. |
| Scoped IPv6 "may be the biggest hidden cost" | Overstated — ~20 lines, and possibly already works via zoned strings. |
| Deep sleep | Not considered. It is arguably the most important constraint of all. |
| `esp_wifi_nan_start` | On 6.x it is `esp_wifi_nan_sync_start`. |

---

## 12. Other CircuitPython network platforms

Scope is **not** limited to the deepsleep project, and **NAN-Sync is wanted as well as USD**. Surveying every Wi-Fi backend CircuitPython has:

| Platform | DNS-SD | NAN | Evidence |
|---|---|---|---|
| **espressif** (native) | `mdns` ✔ | **Sync + USD** ✔ | IDF 6.1; USD hardware-confirmed below |
| **raspberrypi** Pico W / Pico 2 W (CYW43) | `mdns` ✔ (lwIP) | **none** | 0 hits for NAN in `georgerobotics/cyw43-driver` |
| **zephyr-cp** | advertise only, no browse | **USD only** at our pin | §4 |
| **AirLift / WiFiNINA** (`nina-fw`) | via firmware | **none** | 0 hits for NAN in `adafruit/nina-fw` |
| **ESP-Hosted** | via host IP stack | **none exposed** | `rpc_v2.proto` has no NAN RPC — only "Nano" false positives; `esp_wifi_nan` appears solely in `esp_netif_defaults` |
| **ESP8266** | — | **none** (no NAN in the NONOS SDK; CP has no esp8266 port) | — |

Conclusions for the wider scope:

* **NAN is espressif-silicon-only in practice.** Every co-processor path (AirLift, ESP-Hosted, CYW43) would need firmware or RPC work before CircuitPython could expose NAN on it. AirLift and ESP-Hosted *run on NAN-capable ESP32 silicon*, so the capability exists — it is the host protocol that does not carry it. ESP-Hosted is the more tractable of the two, since extending `rpc_v2.proto` is additive; `nina-fw` is a frozen Arduino-era command set.
* Infineon's WHD has some NAN event plumbing under `COMPONENT_WIFI6`, but that is not the driver Pico W / Pico 2 W use — `cyw43-driver` has none.
* **DNS-SD is reachable on more platforms — but not "everywhere", as this section previously claimed.** Corrected after review:
  * **AirLift / WiFiNINA does *not* terminate its IP stack on the host.** nina-fw runs the stack on the co-processor, CircuitPython reaches it through the Python `adafruit_esp32spi` library rather than C `socketpool`, and `common_hal_mdns_server_construct` rejects any interface but the built-in radio. A `shared-module` browser over `socketpool` therefore does **not** reach AirLift.
  * Pico W already has a native `find()`, so it gains nothing.
  * **The realistic beneficiary of the shared browser is zephyr-cp** (plus any future socket-capable port). That is still worth doing — it is the only portable, working piece of issue #18 — but the "lights up four platforms at once" claim was wrong.
* `esp32s31` is a **preview target** in this IDF (`tools/idf_py_actions/constants.py:53`), so it builds only with `--preview`.

---

### 12.1 What "needs NAN hardware" actually means

Reframed after §13/§14, because the earlier wording ("NAN is espressif-silicon-only in practice") conflated two very different barriers.

| | What it needs | Who is blocked, and by what |
|---|---|---|
| **NAN-USD** | Nothing special. A publish is an ordinary **Public Action vendor-specific management frame** (`04 09 50:6f:9a 13 …`), plus remain-on-channel. `ESP_WIFI_NAN_USD_ENABLE` depends only on `IDF_EXPERIMENTAL_FEATURES`. | Nobody, at the silicon level. Every blocker is a **driver or host-protocol exposure** gap: `cyw43-driver` exposes no action-frame TX/ROC path, `nina-fw`'s command set is frozen, ESP-Hosted's `rpc_v2.proto` carries no NAN RPC. |
| **NAN-Sync** | TSF/cluster scheduling, several concurrent RX addresses (NMI + NDI + cluster BSSID), slot-based TDMA. Gated by `SOC_WIFI_NAN_SUPPORT`. | esp32, esp32s2, esp32c5, esp32c61, esp32s31 only — but see §14.2a: that gate is Espressif's declaration, not a demonstrated silicon limit. |

The practical consequence is more hopeful than v3 implied: **the platforms that cannot do Wi-Fi Aware are mostly blocked by software they do not expose, not by radios that cannot do it.** ESP-Hosted is the tractable one — extending `rpc_v2.proto` is additive, and the co-processor is NAN-capable ESP32 silicon. AirLift sits on the same silicon but a frozen Arduino-era firmware. CYW43 would need action-frame plumbing the driver does not have, though Infineon's own WHD carries NAN event definitions under `COMPONENT_WIFI6`.

None of that is work worth starting while USD discovery is broken on the one backend we *can* test (§13.2a). It does mean the ceiling is higher than "espressif only" if that defect is fixed.

---

## 13. Hardware results (rpi-hil006)

Built from CircuitPython's own pinned IDF (`v6.1-dev-6096`) and flashed to real boards. Both boards' original flash was backed up first (4 MB each, under `~/usd-test/backup/`).

**USD runs on the STA interface — confirmed on hardware, not just from source.** `usd_subscriber` on a classic ESP32:

```
I (658) wifi:mode : null
I (658) wifi:mode : sta (e8:9f:6d:32:50:04)
I (658) nan_app: NaN-USD Started
I (668) nan_app: Started Subscribing to ESP-USD-TEST [Service ID - 1]
```

This corroborates `nan_app.c:2986`. It was the basis for expecting STA coexistence — and §13.1 shows that expectation was wrong once the board is actually *associated*.

### 13.1 USD and an associated STA — the first, flawed run

The decisive experiment. Purpose-built firmware (not the stock examples): associate to a WPA2 AP, confirm an IP, *then* call `esp_wifi_nan_usd_start()`, and log association state, home channel and a real DNS lookup every 5 s. Run on both boards simultaneously, 100 s.

ESP32-S3, abridged:

```
I (2712) coex: PRE-USD: associated ssid=<ap> ch=1 rssi=-68 bssid=ec:fc:2f:83:dc:d8
I (2712) coex: GOT_IP 192.168.1.203
I (2712) coex: esp_wifi_nan_usd_start() = ESP_OK
I (2712) coex: mode after  usd_start = STA
I (2722) coex: [t=  0s] home_channel=1
I (2722) coex: publish: chan=6 ...
I (7722) coex: [t=  5s] home_channel=6          <-- radio leaves the AP's channel
I (10982) coex: wifi_event id=21                <-- STA_BEACON_TIMEOUT
W (29722) coex: [t= 20s] DNS FAIL err=202 (6999ms)
W (39722) coex: [t= 30s] mode=STA NOT-ASSOC ip=192.168.1.203 disc=0
```

**The two boards did *not* behave identically** — a claim this section wrongly made before review. Per board:

| | ESP32-S3 (publisher) | classic ESP32 (subscriber) |
|---|---|---|
| Home channel | 6 for the whole run | 6 for 13 of 16 samples, back to 1 at t=25 s and t=35 s |
| Beacon timeouts | 3 | 5, every ~10 s |
| Association | **lost at 35.8 s** (`ap_probe_send over, reset wifi status to disassoc`, `run -> init`) | **held for the full 75 s** — never once `NOT-ASSOC` |
| `STA_DISCONNECTED` | never delivered; stale IP kept | n/a |

So the honest result is **"publisher-on-S3 lost the link; subscriber-on-ESP32 was degraded but survived"**, with role and chip confounded (one run each). The likely mechanism is airtime, not chip: the publisher transmits ~5×/s on the USD channel while the subscriber only remains-on-channel ~1×/s, so the publisher spends far more time away from the AP.

### 13.1a Resolved: it was the channel, not USD

The above was run with USD pinned to **channel 6** (left over from the §13.2 discovery debugging) while the AP was on **channel 1**. Review flagged that as a confound I had introduced, so I re-ran it with the harness reading `esp_wifi_sta_get_ap_info()->primary` after association and pinning USD to **the AP's own channel**:

```
coex: PRE-USD: associated ssid=free4all ch=1 ...
coex: USD channel = 1 (FOLLOWING THE AP)
coex: [t=  0s] home_channel=1   ...   coex: [t= 40s] home_channel=1
```

| over 100 s | publisher (S3) | subscriber (ESP32) |
|---|---|---|
| Home channel | **1 throughout** — never leaves the AP | **1 throughout** |
| `NOT-ASSOC` samples | **0** | **0** |
| Beacon timeouts | 1 | 7 |

**Both boards stayed associated for the entire run.** Compare the ch6 run, where the publisher lost its association at 35.8 s.

**So "USD is a radio-owning mode" is wrong, and §0 said so on my evidence alone.** USD does not inherently take the radio; *USD pinned to a channel other than the AP's* does, because one radio cannot be in two places. Put USD on the AP's channel and STA survives.

What survives from §13.1 is narrower but still worth having:

* When USD *does* go off-channel, the link dies **silently** — no `STA_DISCONNECTED`, stale IP retained. That remains the API-relevant hazard.
* A CircuitPython USD binding should therefore **default `usd_default_channel` / `usd_chan_bitmap` to the associated AP's channel**, and only allow a different channel when no STA is up (or make the cost explicit). That is a concrete, cheap API rule that this experiment produced.
* The cost of staying on the AP's channel is that discovery only reaches peers that happen to use that channel — a real limitation for an AP-less use case, but irrelevant when the device is associated anyway.

| Observation | Consequence |
|---|---|
| Mode stays `WIFI_MODE_STA` across `usd_start` | The *mode* claim in §3.1 holds — but mode is not the whole story |
| `home_channel` moves 1 → 6 and **stays** there | The single radio is parked on the USD channel; the AP link is abandoned |
| `WIFI_EVENT_STA_BEACON_TIMEOUT` (id 21) repeatedly | Firmware knows beacons are being missed |
| `getaddrinfo` goes from failing in ~25 ms to timing out at ~7 s | Queries are now going unanswered rather than being refused — see the caveat below |
| `esp_wifi_sta_get_ap_info()` → `NOT_CONNECT` by t=30 s | Association genuinely lost |
| **`disc=0` — no `STA_DISCONNECTED` ever fires** | The application is never told. `esp_netif` keeps the stale IP (`192.168.1.203`) and the socket layer keeps answering as if connected |

**Caveat on the DNS numbers, found later (§14).** The test AP has no working resolver: on a *healthy* STA link `getaddrinfo` already fails, immediately (~25 ms, `err=202`). So DNS failure alone is not proof that USD killed traffic. What does change under USD is the *shape* of the failure — ~25 ms refusal becomes a ~7 s timeout, i.e. queries stop being answered at all. The load-bearing evidence for this section is therefore the unambiguous radio-level facts: the home channel moving to 6 and staying, repeated `STA_BEACON_TIMEOUT`, and `esp_wifi_sta_get_ap_info()` returning `NOT_CONNECT`. Those do not depend on the AP's resolver.

> ~~**This changes the design.** USD cannot be a quiet background service alongside `wifi.radio.connect()`; it is a radio-owning mode and the entry point belongs on `wifi.radio`.~~
>
> **Superseded by §13.1a.** That conclusion was drawn from this run alone, and this run had USD pinned to a channel the AP was not on. The mitigation this paragraph suggested "testing later" turned out to be the whole story: pin USD to the AP's channel and the problem disappears. Kept here because the logs remain the evidence for the *silent* failure mode, which is real and still matters.

### 13.2 Discovery never completes — publisher is correct, subscriber is deaf

Across every configuration tried, **no `WIFI_EVENT_NAN_SVC_MATCH` ever fires.** Ruled out, each by direct measurement:

| Hypothesis | Test | Result |
|---|---|---|
| Channel mismatch | Both pinned to ch 6 via `usd_chan_bitmap` | both confirm `ch=6`; no change |
| Boards started at different times / TTL expiry | Simultaneous reset (<400 ms apart), TTL 600 s | no change |
| World-mode regulatory block on TX | `set_country("GB", MANUAL)`; was `cc=01 nchan=11` | accepted; no change |
| Power save sleeping through frames | `esp_wifi_set_ps(WIFI_PS_NONE)` | no change |
| Transmission failing | Log `WIFI_EVENT_ACTION_TX_STATUS` | `status=0` then `2` (started → duration-completed), **never `FAILED`** |
| Receiver off-channel | Log `WIFI_EVENT_ROC_DONE` + `esp_wifi_get_channel()` | ROC on ch 6 every 1 s, `status=0`, home channel 6 throughout |
| Chip- or direction-specific bug | Swap roles: ESP32 publishes, S3 subscribes | **still zero matches both ways** |

**One row removed after review: setting `.type` was a no-op.** The public `.type` field is never read on the USD path — `esp_nan_usd.c` uses fixed defaults from `esp_private/esp_nan_usd.h`: `ESP_USD_SUBSCRIBE_DEFAULT_PARAMS()` hardcodes `.active = false` and `ESP_USD_PUBLISH_DEFAULT_PARAMS()` hardcodes `.unsolicited = true, .solicited = true`. That test changed nothing, so it ruled nothing out. (It does, however, mean the effective config — unsolicited publisher, passive subscriber — is a combination that *should* match, which argues against misconfiguration.)

Then the frames were captured off the air. The classic ESP32 was temporarily reflashed as a promiscuous sniffer on channel 6 while the S3 published:

```
sniff: VSA src=f4:12:fa:59:64:84 dst=51:6f:9a:01:00:00 bssid=51:6f:9a:01:00:00
       oui=50:6f:9a type=13 rssi=-58 ch=6 len=62
       04 09 50 6f 9a 13 | 03 09 00 be7d08e9ddb7 01 00 00 | 0e 0d 00 01 01 00 0800 "PUBHELLO"
```

Decoded: Public Action (`04`), Vendor Specific (`09`), WFA OUI `50:6f:9a`, **OUI type `0x13` = NAN SDF**; then a Service Descriptor Attribute (`0x03`, service-ID hash `be7d08e9ddb7`, instance 1) and a Service Descriptor Extension Attribute (`0x0e`) carrying the 8-byte payload. Repeating roughly every 150 ms at −58 dBm.

**So the publisher is fully correct and the RF link is fine** — the same board, same antenna, hears the frames perfectly in promiscuous mode. The failure is squarely in the USD **receive/match** path.

#### 13.2a The defect, localised

Two further experiments pin it down exactly.

**(1) The discovery engine is never called.** `wpa_printf()` maps to `ESP_LOG_LEVEL_LOCAL(level, "wpa", …)`, and `nan_de.c:1271` logs `NAN: RX SDF from …` at `MSG_DEBUG` immediately after the function's only early return. Raising that tag to `ESP_LOG_DEBUG` at runtime gives a clear **positive control** — the engine does log:

```
D (785) wpa: NAN: Assigned new subscribe handle 1 for ESP-USD-TEST     <-- subscriber
D (343) wpa: NAN: Publish in single channel state for 900 TU; ...      <-- publisher
D (1273) wpa: NAN: TX wait for response ended (freq=2437)
```

and **`NAN: RX SDF from` never appears, not once.** `nan_de_rx_sdf()` is never entered.

**(2) The frames do arrive at the receiver's own NIC.** Running the subscriber with promiscuous mode enabled *alongside* its USD subscribe, counting Public-Action/WFA/OUI-type-`0x13` frames:

```
coex: promisc on = ESP_OK
coex: [t= 60s] promisc NAN SDFs seen = 407
```

**407 NAN SDFs received by the subscriber's own radio, and zero delivered to the USD engine.**

That is conclusive and leaves no room for misconfiguration: the frames are transmitted correctly, reach the right board on the right channel, are demodulated by its NIC — and the closed Wi-Fi blob never hands them to the action-frame callback the USD engine registered via remain-on-channel. The open `nan_de.c` code is not at fault; it is simply never called.

It also **weakens my own A1/A3 address-filter hypothesis**: enabling promiscuous mode (which opens the hardware address filter) did not make discovery work. Promiscuous delivery is a separate path from the ROC `rx_cb`, so this does not strictly refute the theory — but the mechanism is no longer something I can determine from outside the blob, and the bug report does not need it.

**This is now fileable**, with a minimal reproducer that needs no NAN code at all: call `esp_wifi_remain_on_channel()` with an `rx_cb` and `allow_broadcast=true` while another board publishes, and show the callback never fires for frames a promiscuous sniffer on the same board counts in the hundreds.

Most likely cause, not yet proven: the frames are addressed to the NAN Network ID `51:6f:9a:01:00:00` in both **A1 and A3**. That is a multicast address that is not broadcast, so a STA-mode hardware RX filter will drop it unless the receiver's BSSID filter has been set to the NAN Network ID. In NAN-Sync the device joins the cluster and that filter is set; on the USD path over a plain STA interface it appears not to be.

**A competing explanation, equally plausible:** the Wi-Fi blob at this pin (`e12a754`) has as its HEAD commit *"fix(nan): Fix bug bounty reported and discovered vulnerabilities in NAN Rx"* (2026-07-14). A hardening regression in the RX path fits the evidence just as well as a filter that never worked.

*(A previous version of this section offered "the subscriber never transmits either" as corroboration. It is not: the subscriber is passive by construction — see the `.active = false` default above — and only transmits a follow-up after receiving a publish. Zero `ACTION_TX` is the same fact as zero matches, not independent evidence.)*

**Before filing upstream**, two cheap tests would make this decisive: (1) enable promiscuous mode with the MGMT filter on the subscriber while USD runs — if `SVC_MATCH` starts firing, the hardware filter is confirmed; (2) build with `CONFIG_WPA_DEBUG_PRINT=y` (off in my builds, which is why the verbose logging in run p2 showed nothing — the discovery engine logs via `wpa_printf`) and look for `NAN: RX SDF from`. Its absence proves the callback is never entered.

**A portability quirk worth recording:** the ESP fork puts service-specific info on the wire raw, whereas upstream hostap prepends the WFA OUI and protocol type and requires them on receive. So "the publisher is fully correct" should read **"correct for ESP peers"** — a hostap/Zephyr/Android peer would discard that SSI. §7.1's `ssi=b"t=21.5"` inherits this.

**Status: NAN-USD on ESP-IDF `v6.1-dev-6096` transmits but does not discover.** It is an experimental feature behind `CONFIG_IDF_EXPERIMENTAL_FEATURES`, and this looks like an upstream defect worth reporting. Until it is fixed, **USD cannot be the foundation of a CircuitPython `wifiaware` module** — which, combined with §13.1, is the single most important result of the hardware work.

### 13.3 What this does to the plan

* §8 (**DNS-SD browser in `shared-module/`**) is untouched by any of this and is unambiguously the right first step. No hardware risk, no upstream dependency, serves every port with sockets.
* The `wifiaware` module (§7.1) should **not** be started on USD against this IDF — not because USD is the wrong protocol, but because its receive path is broken (§13.2a).
* NAN-Sync was subsequently tested and works end to end: see §14.

### 13.4 Reproduction

All test firmware is in the session scratchpad. `coex_pub/` + `coex_sub/` is the main harness, with four build switches: `-DROLE_PUB=` (publish vs subscribe), `-DCOEX_WITH_STA=` (join an AP first), `-DUSD_PROMISC=` (run promiscuous RX alongside USD, for §13.2a) and `-DUSD_FIXED_CHAN=` (the fallback channel when no AP is associated; with one, it follows the AP). `swap_pub32/` + `swap_subs3/` is the role swap, `sniff/` the standalone promiscuous NAN SDF sniffer, `synccoex/` the four-phase Sync-vs-STA probe. Logs are on the Pi under `~/usd-test/{coex,sync,synccoex,apchan,diag}/`, and copied locally under `hil-logs/` with a manifest. Both boards' original 4 MB flash images are in `~/usd-test/backup/`.

Board notes: the QT Py ESP32-S3 needs `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, since UART0 is not wired to USB; and with native USB it re-enumerates under a different `/dev/serial/by-id` name in ROM download mode, which breaks naive by-id addressing in scripts. Serial capture needs a DTR/RTS reset toggle before any output appears.

---

## 14. NAN-Sync hardware results — it works

Tested after §13, on the two rig boards that actually have NAN silicon: **classic ESP32** (publisher) and **ESP32-S31** (subscriber). Stock IDF examples, security disabled, active subscribe, datapath + IPv6 ping enabled.

First, a build-level signal that was missed earlier: **Sync is not an experimental feature.** It needs only `CONFIG_ESP_WIFI_NAN_SYNC_ENABLE=y`. USD additionally requires `CONFIG_IDF_EXPERIMENTAL_FEATURES` (§3.1), and §13.2 shows why.

### 14.1 Full sequence, measured

Subscriber (ESP32-S31):

```
I (265)   wifi:mode : NAN (1e:29:04:81:b5:97)
I (3345)  NAN: Start own Cluster, Id 50:6f:9a:01:db:dd
I (3345)  nan_app: Started Subscribing to ESP-SYNC-TEST [Service ID - 5]
I (4395)  nan_app: Service matched with ea:9f:6d:c6:e9:16 [Peer Publish id - 5]
I (4925)  wifi:NDL for Peer ea:9f:6d:c6:e9:16 ch6: ndc=[2,2] crb1=[3,16] crb2=[0,0]
I (4925)  nan_app: NDP confirmed [NDP ID - 1, Peer IPv6 - FE80::E89F:6DFF:FEC6:E916]
I (10215) subscriber: 64 bytes from FE80::E89F:6DFF:FEC6:E916 icmp_seq=1 ttl=0 time=292 ms
I (11925) subscriber: 64 bytes from FE80::E89F:6DFF:FEC6:E916 icmp_seq=3 ttl=0 time=2 ms
--- FE80::E89F:6DFF:FEC6:E916 ping statistics ---
5 packets transmitted, 5 received, 0% packet loss, time 300ms
```

The publisher independently confirms the same NDP from its side, and logs `NAN: Join Cluster with Id ...` rather than starting its own — the two devices genuinely formed one cluster.

| Milestone | Time from boot |
|---|---|
| `WIFI_MODE_NAN` up | 0.27 s |
| Cluster formed | 3.35 s |
| **Service matched** | **4.40 s** |
| **IPv6 datapath confirmed (NDP)** | **4.93 s** |
| First ICMPv6 reply | 10.2 s (example waits 5 s deliberately) |
| Steady-state RTT | **2 ms**, 0 % loss |

**This is a working, AP-less, DHCP-less IPv6 transport with real sockets on it** — exactly what issue #18 asks for, and the thing USD can never provide even when fixed (USD has no datapath at all).

### 14.2 What it costs

* **Exclusive radio mode, confirmed on hardware.** The log reads `wifi:mode : NAN`, a mode distinct from STA/AP, with its own **NAN Management Interface MAC**. That MAC is **randomised at every start** — only the OUI is derived (`e8:9f:6d` → `ea:9f:6d`, locally-administered bit set); the low three bytes differ on every boot (`c6:e9:16`, `80:3e:81`, `83:f6:00`, `ea:1b:bc` across four runs). **Consequence: the peer's datapath link-local IPv6 changes on every wake**, since it is the EUI-64 of that MAC — a real problem for a deep-sleeping collector, and one §7 has not accounted for. `disable_random_mac` / NVS caching exist in `wifi_nan_sync_config_t`; neither was tested. This corroborates `nan_app.c:2003`/`:2057` from §3.1. Note §13.1 showed USD imposes effectively the same cost in practice while pretending not to — so exclusivity is no longer a discriminator between the two.
* **`SOC_WIFI_NAN_SUPPORT` required** — set only for **esp32, esp32s2, esp32c5, esp32c61, esp32s31**, excluding the ESP32-S3 and the C3/C6 parts a great many CircuitPython boards use. Whether that reflects silicon is a separate question; see §14.2a.
* The datapath IPv6 is a **link-local address derived from the peer's NMI as an EUI-64**, so anything using it needs scope/zone handling — see §7.3.

### 14.2a Is `SOC_WIFI_NAN_SUPPORT` a hardware limit? Probably not.

Earlier drafts wrote "needs NAN silicon" and called Sync's exclusivity a hardware cost. Neither is justified, and the distinction matters: it turns the S3/C3/C6 exclusion from a law of physics into something reasonable to ask Espressif about.

What `SOC_WIFI_NAN_SUPPORT` actually is: a macro in each chip's `soc_caps.h` — Espressif's declaration of what their Wi-Fi firmware supports on that part. It gates exactly one Kconfig symbol, `ESP_WIFI_NAN_SYNC_ENABLE`. Since the Wi-Fi MAC firmware is a closed blob, nothing outside it can distinguish "the block isn't there" from "we didn't implement it for this lineage".

Evidence that it is a product decision rather than silicon:

* **The chip list has the wrong shape.** ESP32 (2016) ✓, S2 ✓, S3 ✗, C3 ✗, C6 ✗, then C5 ✓, C61 ✓, S31 ✓. A physical block does not vanish for a generation and return.
* **The C6 is the awkward case.** It has `SOC_WIFI_HE_SUPPORT` — Wi-Fi 6, including TWT's negotiated scheduled wake windows, the same class of machinery NAN's Discovery Windows and NDL slot bitmaps need, and the IDF event enum carries `ITWT_SETUP`/`BTWT_SETUP`/`TWT_WAKEUP` to show the stack drives it. Yet no NAN.
* **Two timing domains on one radio already ship.** It is tempting to argue a station's TSF is slaved to its AP so a NAN cluster's timebase cannot coexist — but CircuitPython's own port sets `WIFI_MODE_APSTA` (`ports/espressif/common-hal/wifi/Radio.c:50`), where the softAP beacons its own TSF while the STA follows the upstream AP's. So "one counter cannot follow two sources" is not an available argument.
* **Other vendors ship NAN concurrently with STA.** Android phones run Wi-Fi Aware while associated as a matter of course; the spec permits concurrency and vendor firmware decides.

The one genuinely hardware-shaped question left is the number of concurrent VIF slots, TSF counters and RX address filters — Sync wants NMI + NDI + cluster BSSID, which on top of an active STA might exceed them. That is a real question, but it is a question, not a known.

**What survives regardless:** `nan_app.c:2003` forces `WIFI_MODE_NAN`, and `wifi_mode_t` has no combined NAN+STA value (`esp_wifi_types_generic.h:27-32`). Whether that is silicon or a product decision, CircuitPython cannot flip it — so the API conclusion (Sync owns `wifi.radio`) holds either way.

### 14.3 The architectural consequence

v2 §0 proposed building on USD because "both backends can do it" — espressif and zephyr-cp converging on USD. The hardware says that convergence is **on the one thing that does not work**:

| | espressif (pinned IDF) | zephyr-cp (pinned Zephyr) |
|---|---|---|
| NAN-Sync | **works, datapath and all** (§14.1) | not available |
| NAN-USD | transmits, never discovers (§13.2) | **untested** — and reaches at most one board (below) |

Two things weaken this table, both in the direction of "I know less than I implied":

* **The zephyr column is unknown, not broken.** I tested no Zephyr USD at all. And "same hostap `nan_de` lineage" cuts *against* transferring the ESP result: the open `nan_de` code reads correctly, and the ESP defect appears to be in the closed blob — so the ESP failure predicts nothing about Zephyr.
* **Zephyr USD is barely available anyway.** hostap USD needs a driver exposing `remain_on_channel`/`send_action`; in the Zephyr tree only `nrf_wifi`, `nxp` and `hwsim` have them — not the Infineon AIROC driver behind Pico W / Pico 2 W. With `hal_espressif` blocklisted in `west.yml`, that leaves `frdm_rw612`, which has no hostap/NAN config today.
* **One IDF snapshot.** USD is experimental, at a `-dev` pin, possibly broken by a dated hardening commit. That is a "re-test at the next IDF bump" item, not a permanent architectural fact.

So **there is no common working denominator today.** A portable `wifiaware` module cannot be built on USD (broken here) or on Sync (absent there, and silicon-limited). Any Wi-Fi Aware work in CircuitPython is therefore **espressif-only and Sync-based** for now, which is precisely the §7.2 shape — a port-specific module that owns the radio, not a portable one.

That reinforces, rather than weakens, the §8 conclusion: **DNS-SD first**. It is the only part of issue #18 that is portable *and* working.

### 14.4 Sync vs STA — measured, and it behaves *well*

A purpose-built four-phase probe on the classic ESP32: bring STA up, start Sync on top of it, exercise STA while Sync runs, then stop Sync and try to recover.

```
PHASE A  [A/after-sta]       mode=STA sta=ASSOC chan=1 ip=192.168.1.143 disc=0
PHASE B  *** STA_DISCONNECTED reason=8 (#1) ***        <-- fires immediately
         ### NAN_CLUSTER_JOIN ###  ### NAN_SYNC_STARTED ###
         nan_sync_start() = ESP_OK
         [B/after-nan-start] mode=NAN sta=ESP_ERR_WIFI_CONN chan=6 ip=0.0.0.0 disc=1
PHASE C  publish_service while NAN = id 5              <-- NAN genuinely usable
         esp_wifi_connect() while NAN (no set_mode) = ESP_ERR_WIFI_MODE
PHASE D  nan_sync_stop() = ESP_OK
         [D/after-nan-stop]  mode=NAN ...              <-- stop does NOT restore the old mode
         set_mode(STA)/start/connect -> STA recovery: RECOVERED in 1.5 s
```

`esp_wifi_nan_sync_start()` unconditionally calls `esp_wifi_set_mode(WIFI_MODE_NAN)` (`nan_app.c:2003`) with no check for a live STA, and `esp_wifi_nan_sync_stop()` calls `esp_wifi_stop()` (`:2058`). So Sync does take the radio — **but it takes it honestly**, and that is the whole difference from §13.1:

| | NAN-USD (§13.1) | NAN-Sync (§14.4) |
|---|---|---|
| Mode reported after start | stays `STA` — misleading | `NAN` — accurate |
| `STA_DISCONNECTED` delivered | **never** | **yes, `reason=8`, immediately** |
| `esp_netif` IP | **stale address retained** | **cleared to `0.0.0.0`** |
| STA API while active | appears to work, silently dead | `esp_wifi_connect()` → **`ESP_ERR_WIFI_MODE`** |
| Recovery | n/a | `nan_sync_stop()` → `set_mode` → `connect`: **1.5 s to a new IP** |

**This is a mode CircuitPython can model honestly.** Every transition is observable: the disconnect event fires, the address is dropped, misuse is refused with a real error code, and teardown is fast and complete. That is precisely what `wifi.radio` needs in order to keep `started`/`sta_mode`/`ap_mode` truthful (§7.2) — and precisely what USD does *not* give.

Two sharp edges for the binding to handle:

* **`esp_wifi_set_mode(WIFI_MODE_STA)` while Sync is running silently stops NAN.** Observed directly: `NAN_SYNC_STOPPED` fired on the `set_mode` call, and a subsequent `esp_wifi_nan_publish_service()` returned id `0`. The clobber is not symmetric — `connect()` under NAN is refused, but a mode change under NAN is not. Nothing but the binding can prevent user code from doing this.
* **`esp_wifi_nan_sync_stop()` leaves the mode at `NAN`.** The caller must restore STA explicitly; the stop alone leaves the radio in a state with no station and no NAN.

*(Method note: the first run of this probe put `set_mode(STA)` before the publish in Phase C, which silently stopped NAN and invalidated Phases C and D — `nan_sync_stop()` then returned `ESP_FAIL` simply because NAN was already stopped. Reordered and re-run; the numbers above are from the corrected run. The discarded run is what revealed the silent-clobber edge above.)*

### 14.5 Still open

* **Validate the DNS-SD browser against a real responder.** It builds and its parser is unit-tested and fuzzed, but no packet of its has met an Avahi/mDNSResponder instance. Nothing should be built on it until that happens.
* **zephyr-cp's `advertise_service()` half**, plus enabling `mdns` there at all. The browser side is ready and the build wiring now emits `CIRCUITPY_MDNS_SHARED` when the port has no `common-hal/mdns`, but that path is unverified — no zephyr board on this rig.

* **Secured NDP and pairing** (`ESP_WIFI_NAN_SECURITY`, `ESP_WIFI_NAN_PAIRING`) — untested, gated on `IDF_EXPERIMENTAL_FEATURES`, and the prerequisite for any phone interop. iOS vs Android is reportedly a compile-time choice.
* **NMI randomisation vs deep sleep** (§14.2): the peer IPv6 moves every boot unless `disable_random_mac` or NVS caching is used. Untested, and it shapes the API.
* **Flash/RAM cost** of enabling NAN-Sync on classic ESP32 and ESP32-S2 CircuitPython builds — unmeasured, and ESP32-S2 alone is ~56 boards.
* **The ESP32-S31 is a preview target** with no CircuitPython boards; the Sync result rests on a chip we do not ship.
* **An oddity to resolve before a binding exposes it:** in `t3-pub.log` the publisher prints its *own* EUI-64 as "Peer IPv6". The responder-side address in the NDP-confirm event may not be trustworthy.
* **`esp_wifi_nan_sync_start()` blocks for ~3 s** (5682 → 8692 ms in `run2.log`), of which 3.0 s is the default `scan_time`. A binding must keep background tasks running across that, and the 4.9 s figure is tunable rather than a floor.
* Secured NDP (`ESP_WIFI_NAN_SECURITY`, PMK/passphrase, ND-GTK) untested — and it is what iOS/Android interop will require.
* Power cost of cluster membership across deep sleep: unmeasured, and it is the crux of issue #18. A 4.9 s boot-to-datapath is already a meaningful number to weigh against ESP-NOW.

### 14.6 Bench notes (cost me real time)

* **`pyserial` asserts DTR and RTS on `open()`.** On an ESP32-S3/S31 native USB-JTAG CDC that reboots the part into the bootloader, so the console goes silent and looks like a dead firmware. Build the port object and set `dtr`/`rts` `False` *before* `open()`:
  ```python
  s = serial.Serial(); s.port = p; s.baudrate = 115200
  s.dtr = False; s.rts = False
  s.open()
  ```
  Every "no output" dead end in §13–§14 traced back to this.
* Resetting an S31 drops its native USB off the bus, so a capture must re-attach *after* re-enumeration.
* `esp32s31` needs `idf.py --preview set-target`, flashes with **`--no-stub`**, and its **bootloader goes at `0x2000`** (not `0x0`/`0x1000`).
* The CP2102N bridge on the S31 board is unreliable — it stops answering USB control transfers (`cp210x: failed set request 0x0 status: -110`) and only a host reboot brings it back.
