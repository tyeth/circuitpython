# NAN: Schedule Request from a peer is never answered, and idle data paths are torn down

**Status: draft, ready.** The mechanism is identified in the binary and the
behaviour is reproduced in three runs under both AP conditions. The impact is
narrower than first thought: idle data paths are torn down, data paths
carrying traffic survive.

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

## Observed, in three runs under both AP conditions

Run 7 (phone associated to an AP on channel 64), run 8 (phone not associated
at all) and run 9 (not associated, with keepalive traffic) all show the ESP
never answering. Runs 7 and 8 were idle and both ended in a teardown:

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

## No log is emitted

`nan_rx_naf` carries the string `Unknown NAN Action subtype - %d`, and that
string sits in `.rodata_wlog_error.144` — **error level**. If the dispatcher's
default case were reached, that line should appear.

It does not. Across runs 2, 3, 7, 8 and 9 there are **zero** occurrences of
`Unknown NAN Action subtype` in any ESP log. In run 9 the console was captured
continuously for about 110 s: the last line is `NDP confirmed` at 12.8 s,
followed by 100 s of silence while three Schedule Requests arrived, at least
the first of them MAC-acknowledged. Error-level `wifi:` lines are plainly not
suppressed in these logs — `Avail: missing a conditional or committed entry`
prints in the same runs.

So the frames appear to be discarded somewhere before the dispatcher's default
case, despite the MAC acknowledging them. This cannot be proved from outside
the libraries — there may be a path into `nan_rx_naf` that skips the log — so
the accurate statement is: **no log is emitted, although error-level `wifi:`
logs are visible in the same capture.**

## Impact

**An idle NAN data path is torn down about 20 s after it is established.** A
data path carrying traffic is not: in run 9, with the peer sending a probe
every 2 s, the link stayed up for the full 100 s capture with no teardown and
no `eventDataPathTerminated`.

Traffic also changes how often the peer asks. On an idle link the Schedule
Requests came about 6 s apart (runs 7 and 8); with traffic they came about
30–36 s apart (run 9). The phone tolerates unanswered requests for at least
100 s when the link is in use.

So the teardown observed in runs 7 and 8 required **both** conditions: an idle
link *and* an unanswered Schedule Request. State that as the observed
correlation, not as cause. Whether a spec-compliant Schedule Response would
by itself prevent the idle teardown cannot be known without a peer that
answers one, and no such peer was available.

What is certain either way is the first half: there is no code to answer a
Schedule Request, so the ESP cannot participate in schedule renegotiation at
all. The user-visible consequence today is that an idle data path does not
survive, and that a peer's attempts to adjust the schedule are silently
ignored however often it tries.

## Asks

1. **Handle NAF Schedule Request (subtype 10) and reply with a Schedule
   Response (subtype 11).** At minimum, accept a request that proposes a
   schedule compatible with the one already committed, which is what the peer
   sent here.
2. **Handle Schedule Update Notification (subtype 13)**, which the phone also
   broadcasts after confirm and which is likewise unhandled.
3. **Do not silently discard unknown NAF subtypes on an active NDL.** A
   MAC-acknowledged frame that produces no log at all, while error-level
   `wifi:` logging is demonstrably working, suggests something upstream of
   `nan_rx_naf` is dropping it.

## Credit

Captures, decode and the observation that the Schedule Requests go unanswered
come from the bench session running the hardware; the binary analysis is from
the captures it supplied.
