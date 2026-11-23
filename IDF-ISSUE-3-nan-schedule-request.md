# NAN: Schedule Request from a peer is never answered, and the peer tears the data path down

**Status: draft, strong.** The mechanism is identified in the binary and the
behaviour is reproduced in two runs under opposite AP conditions. One
confirmation is outstanding — see [One check](#one-check).

## Summary

After a NAN data path is established, an Android peer sends a unicast NAN
Action Frame **Schedule Request (subtype 10)**. The ESP acknowledges it at the
link layer and then does nothing: no Schedule Response (subtype 11) is ever
transmitted, and nothing is logged. The peer retries once, waits, and then
terminates the data path locally.

The ESP has **no handling for NAF subtypes 10–13 at all**.

## Environment

| | |
|---|---|
| ESP-IDF | `v6.1-dev-6107` (`cb73317`), `esp_wifi/lib` at `7cd4d7b2` |
| Target | ESP32 (classic, rev0), `wifi_aware/nan_publisher`, SOLICITED |
| Peer | Samsung Galaxy Note 9 (SM-N960F), Android 10 / SDK 29 |
| Capture | ESP32-S2 monitor on channel 6, with ACK frames recorded |

## Observed, in two runs with opposite AP state

Run 7 (phone associated to an AP on channel 64) and run 8 (phone not
associated at all) behave identically:

1. NDP confirms; the IPv6 data path works. A TCP probe to the peer's
   link-local address over `aware_data0` is refused, which proves the peer's
   stack answered.
2. About **12 s after confirm**, the phone sends a unicast NAF **Schedule
   Request** to the ESP. The ESP **ACKs it** — captured directly as ACK-by-RA,
   not inferred.
3. About **6 s later** the phone sends a second Schedule Request. The ESP ACKs
   that too.
4. No Schedule Response (subtype 11) appears anywhere in either capture, and
   the ESP logs nothing.
5. The phone terminates the data path locally — HAL
   `eventDataPathTerminated`, and the app's `onLost` at 26.4 s. In run 8 there
   is **no over-the-air NDP Termination from the phone at all**; it simply
   gives up.

The Schedule Request is not an unreasonable one. Its content in run 8:

- **map 0** — potential channel 6 slot 0 (`01000000`), plus **COMMITTED**
  channel 6 slots 1–31 (`feffffff`)
- **map 1** — 5 GHz potential (band `fff0ffff`, op class 128 `0x0020`, slots
  `000f0000`)
- plus NDC (15), NDL (4), NDL-QoS (3)

The phone is offering to **commit to exactly the channel-6 schedule the ESP
itself committed to** (`crb1=[2,31]`). This is the peer doing the right thing
and converting a conditional arrangement into a committed one. The ESP ignores
it.

## Mechanism, from the binary

`ieee80211_nan_datapath.o` in `libnet80211.a` (esp32). The receive dispatcher
`nan_rx_naf` references exactly **five** parse handlers:

```
nan_parse_ndp_req               subtype 5   Data Path Request
nan_parse_ndp_resp              subtype 6   Data Path Response
nan_parse_ndp_confirm           subtype 7   Data Path Confirm
nan_parse_ndp_security_install  subtype 8   Data Path Key Installment
nan_parse_ndp_terminate         subtype 9   Data Path Termination
```

The complete function list for the object confirms there is no counterpart for
any of:

```
subtype 10  Schedule Request
subtype 11  Schedule Response
subtype 12  Schedule Confirm
subtype 13  Schedule Update Notification
```

There is no `nan_parse_sched_*`, no `nan_construct_sched_*`, and no
`nan_send_sched_*`. Every `nan_sched_*` symbol present
(`nan_sched_construct_availability`, `nan_sched_allocate_crbs`,
`nan_sched_alloc_conditional` and so on) is availability construction for the
NDP exchange, not NAF schedule negotiation.

`nan_rx_naf` carries the string `Unknown NAN Action subtype - %d`, and that
string lives in `.rodata_wlog_error.144` — it is an **error-level** message.

## One check

The ESP reportedly logged nothing when the Schedule Requests arrived. If the
dispatcher were reached, an error-level `Unknown NAN Action subtype - 10`
should have appeared. So either that line was simply not captured, or the
frame is dropped before `nan_rx_naf`, even though the MAC acknowledged it.

Grepping an ESP log from a run that reaches this point for
`Unknown NAN Action subtype` distinguishes the two, and should be done before
filing. It does not change the conclusion — the handlers do not exist either
way — but it changes whether the report says "unhandled and logged" or
"silently discarded".

## Impact

A NAN data path with an Android peer is **torn down roughly 20 s after it is
established**, in both AP conditions. Schedule renegotiation after setup is
normal behaviour for a peer whose availability changes, and on Android it
appears to be routine rather than exceptional. Any long-lived use of Wi-Fi
Aware between an ESP32 and a phone is therefore not possible today, even once
the data path comes up.

This is also the most likely explanation for the teardown previously recorded
as "cause unknown" in `IDF-ISSUE-2-nan-ndp-availability.md`.

## Asks

1. **Handle NAF Schedule Request (subtype 10) and reply with a Schedule
   Response (subtype 11).** At minimum, accept a request that proposes a
   schedule compatible with the one already committed, which is what the peer
   sent here.
2. **Handle Schedule Update Notification (subtype 13)**, which the phone also
   broadcasts after confirm and which is likewise unhandled.
3. **Do not silently discard unknown NAF subtypes on an active NDL.** If the
   `Unknown NAN Action subtype` path is not being reached, something upstream
   is dropping acknowledged frames.

## Credit

Captures, decode and the observation that the Schedule Requests go unanswered
come from the bench session running the hardware; the binary analysis is from
the captures it supplied.
