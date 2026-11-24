# NAN: the esp32 NDP Response strategy has ~1.8 ms of margin and fails on slower supported hardware

**Status: draft, not filed.** Asks 1–3 are evidenced and ready. Two further
observations are recorded but deliberately not asked about, because they rest
on too little.

## Summary

The esp32 Wi-Fi library answers an NDP Request **inside the Discovery Window
the request arrived in**. On current silicon that works: an ESP32-U4WDH
rev v3.1 transmits M2 at 0.884–0.892 slots, about **1.8 ms before the window
closes**, acknowledged first try, and establishes the data path in **6 of 6**
runs.

The same binary on an ESP32-D0WDQ6 **rev v0.0** board transmits anywhere from
0.92 to 2.12 slots — a spread **150 times wider** — overshoots the window,
is never acknowledged on the first attempt, and establishes the data path in
**9 of 16** runs. Rev v0.0 is a supported target
(`CONFIG_ESP32_REV_MIN` defaults to `ESP32_REV_MIN_0`).

The **esp32s2** library uses a different strategy — defer M2 to the *next*
Discovery Window and send it early in slot 0 — which does not depend on
processing latency at all. It is **6 of 6**.

So this is not "esp32 does it wrong". It is a strategy with very little
margin, which holds on fast hardware and fails on slow hardware that the
build still supports, next to a sibling library whose strategy has no such
dependency.

Two smaller defects accompany it on all three targets: an error-level log
that fires on successful runs, and an M2 that reports ACCEPTED after the
parse that built it returned an error.

## Environment

| | |
|---|---|
| ESP-IDF | `v6.1-dev-6107` (`cb73317`), `esp_wifi/lib` at `7cd4d7b2` |
| Targets | ESP32-D0WDQ6 **rev v0.0**, ESP32-U4WDH **rev v3.1**, ESP32-S2 (Lolin S2 Mini) |
| Example | `wifi_aware/nan_publisher`, SOLICITED, identical config on all three |
| Peer | Samsung Galaxy Note 9 (SM-N960F), Android 10 / SDK 29 |
| Capture | ESP32-S2 monitor on channel 6 with ACK frames recorded |

> **Chip revision.** Rev v0.0 is a supported target — `CONFIG_ESP32_REV_MIN`
> defaults to `ESP32_REV_MIN_0` — and `esp_wifi`, `esp_phy` and `esp_coex`
> contain **no revision-conditional code at all**; the only rev-0 workaround
> anywhere in IDF is the brownout interrupt. So the library is byte-identical
> across revisions, which the v3.1 run confirms directly: the *same binary*
> succeeds 6 of 6 there and manages 9 of 16 on the rev-0 board. The
> difference is in the hardware or its timing, not in branching code.

ESP-to-ESP NAN-Sync on the same build works end to end — cluster 3.35 s,
match 4.40 s, IPv6 data path 4.93 s, ICMPv6 at 2 ms RTT. Only the Android
data path is unreliable.

### Method

Delivery is read from **ACK-by-RA**, with each ACK matched to the frame it
*immediately* follows. A wider match window credits one ACK to two frames of
a retry burst and produces false positives.

PHY rates quoted here come from the sniffer's own text output. For runs 3–28
the pcap radiotap headers carry only Channel and signal strength and **no
Rate field**, so those captures and their `*-sniff.txt` must travel together.
The sniffer has since been changed to emit radiotap Rate for non-HT frames
and MCS for HT frames, so later captures are self-contained.

Runs 17–22 used a sniffer with a PSRAM queue and ACKs filtered to known NAN
NMIs, and recorded **zero drops**. Runs 3–6 predate ACK capture and are also
drop-free. Runs 7–16 overflowed a 24-entry queue (up to 1761 drops), so for
those, burst sizes are lower bounds and a missing frame proves nothing.

---

## Ask 1 — adopt the esp32s2 defer-to-next-window strategy on esp32

### Three targets, two strategies

| | ESP32 rev v0.0 | ESP32 rev v3.1 | ESP32-S2 |
|---|---|---|---|
| Wi-Fi library | esp32 | esp32 (**identical binary**) | esp32s2 |
| M2 constructions logged | two | one | one |
| M2 in the request's own DW | yes, 0.92–2.12 slots | yes, 0.884–0.892 slots | **none** |
| M2 in the next DW | retry, position varies | — | one, 0.47–0.57 slots |
| first attempt acknowledged | **never** | **always** | **always** |
| phone M1 retransmissions | up to 7 | none | none |
| data path established | **9 of 16** | **6 of 6** | **6 of 6** |

### The margin is the problem

A Discovery Window slot is 16.384 ms. The v3.1 board transmits M2 at
**14.48–14.61 ms** into it, leaving **1.77 ms**. Its spread across six runs
is 0.008 slots — **0.13 ms**.

The rev-0 board, running the *same binary*, spreads over 1.200 slots —
**19.7 ms**, 150 times wider — and its *best* case (0.92) is already later
than the v3.1 board's *worst* case (0.892).

That it builds M2 twice where v3.1 builds it once is a symptom of the same
binary taking the retry path, not a code difference.

**This is not a sharp position cutoff.** A rev-0 retry at 0.984 slots was
acknowledged in run 21, later than first attempts at 0.92 that were not. The
variability matters more than the mean.

### The ask

Adopt the esp32s2 strategy on esp32: construct the NDP Response once and
transmit it early in the *next* Discovery Window, rather than late in the
current one. The esp32s2 library already demonstrates it against the same
peer, and unlike the same-window approach it does not depend on how quickly
the chip can turn a request around.

Failing that, the same-window strategy needs enough margin to survive the
slowest silicon the build supports, which today is rev v0.0.

### The rev-0 distribution has two parts, and they have different causes

Splitting the rev-0 first-M2 delays rather than treating them as one
population changes the picture. Measured from M1:

```
v3.1 (40 MHz XTAL)   14.485 14.616 14.490 14.541 14.606 14.522
                     mean 14.543, sd 0.051   guard = 16.384 - 14.543 = 1.841 ms = 1.80 TU

rev 0 (26 MHz XTAL)  cluster: 15.089 15.184 15.249 15.257 15.305   sd ~0.08
                     guard = 1.167 ms
                     tail:    15.77 16.08 16.83 16.93 19.22 19.92 26.09 33.06
```

The v3.1 guard is **1.80 TU**, within 2 µs of 1843.2 µs — a constant guard
before the end of slot 0, not an accident of latency. The rev-0 *cluster* is
just as tight (sd 0.08 vs 0.051), so it is not jitter either; it is the same
mechanism with a shorter guard.

**The guard ratio is 1.167 / 1.841 = 0.634.** Against 26/40 = 0.650 that
predicts a delay of 15.188 ms where 15.217 was measured — a 29 µs miss. 25/40
predicts 15.234, a 17 µs miss; the two cannot be separated. A factor of ½
predicts 15.464, about 5 sd out, and 40/26 is nowhere near.

So the rev-0 *floor* is consistent with a crystal-derived scaling error in
the guard, which moves the first M2 from 0.887 to 0.927 slots — into the
region where this phone does not acknowledge. The *tail* above 15.8 ms
remains execution latency, or rescheduling after the shortened deadline has
already been missed.

**Direction of the error.** A guard specified in ticks for 40 MHz but
counted at 26 MHz would run *longer* (2.83 ms) and transmit *earlier*, which
is the opposite of what is seen. The fit requires the reverse: a guard
converted to ticks using the real 26 MHz crystal and then consumed against a
40 MHz timebase, giving 1.196 ms. Anyone looking for this in code should
expect a multiply by the measured crystal frequency feeding a fixed-40 MHz
consumer, not a hard-coded 40 being used where the real value belonged.

**What the blob does not show.** Searched for it and came up empty, which is
worth recording:

- Neither `libnet80211.a` nor `libpp.a` references any crystal or clock
  frequency helper — no `rtc_clk_xtal_freq_get`, no `esp_clk_xtal_freq`,
  nothing matching xtal/clk_freq/apb_freq among 1453 and 447 undefined
  symbols respectively.
- No literal `40000000`, `26000000`, `73728` or `1843200` appears in any of
  them.
- `ieee80211_nan_datapath.o`'s literal pool holds nothing of plausible
  guard-time magnitude; the few values in range are source line numbers
  passed to log calls. `hal_tsf.o` has no literals at all.
- The only clock-dependent external the blobs call is ROM's
  `ets_delay_us`.

So the guard is not a visible constant in these objects. If the scaling error
is real it lives in MAC register programming, in ROM, or in a computation the
literal scan cannot see — and it cannot be confirmed or refuted from the
blob.

**The decisive test** is a **rev v3.x board with a 26 MHz crystal**. If the
crystal is the cause, its first M2 should land at about 15.19 ms (0.927
slots) despite current silicon. That separates crystal from revision cleanly,
which no board we have can do.

Suspect ordering, by phenomenon:

| phenomenon | leading suspect |
|---|---|
| the 15.2 ms floor | crystal-derived scaling of the guard |
| the tail to 33 ms | execution latency, cache and flash path |
| 48 Mbps receive misses | RF or silicon behaviour |

**Caveats.** The cluster is n=5 and was chosen post hoc, though there is a
natural gap from 15.31 to 15.77. 26/40 and 25/40 are indistinguishable at
this precision. The M1 anchor is common to both boards, so it cannot
contribute the difference.

## Ask 2 — do not log an error for a map carrying only Potential entries

The ESP logs

```
E (12422) wifi:Avail: missing a conditional or committed entry
```

on runs where the NDP **confirms successfully**, so it never indicated a
negotiation failure. It is an error-level message for a non-error, and it
appears on **all three targets** — the esp32s2 and the ESP32 rev v3.1 each
log it in all 6 of their 6 successful runs.

The peer's NDP Request carries two Availability attributes: map 0 with a
**conditional** channel-6 entry covering all 32 slots, and map 1 with
**potential** 5 GHz entries (op class 128, bitmap `0x0020` — the block
centred on 155, containing the NAN 5 GHz discovery channel 149).
Potential-only is exactly what a map-1 entry is for.

`nan_parse_ndp_req` calls `nan_dp_parse_availability` from an attribute walk
(backward branch `0x442 -> 0x135`), consistent with the error coming from the
map-1 pass. Per-map storage exists — `nan_get_avail_by_map_id` walks 92-byte
records keyed on map ID — but it is called from `nan_sched_alloc_conditional`,
`nan_sched_allocate_crbs` and `nan_dp_indicate_faw_start`, never from the
parser.

This cost two independent investigations several days.

## Ask 3 — do not report ACCEPTED after the parse returned an error

`nan_dp_parse_availability` logs and returns 262 at `0x1a8`, jumping to
`0x41f`, two instructions before `retw.n` at `0x422`; the success path returns
0 at `0x417`. The M2 the ESP then transmits carries status `0x11`, ACCEPTED.
The log and the frame disagree.

---

## Further observations, not asked about

**The esp32 misses M1 repeatedly — possibly a high-rate OFDM reception
weakness.** In run 18's first window the phone sent M1 seven times and the
ESP acknowledged none; it took the third copy in the next window. Run 20's
first M1 went unacknowledged, run 22's first three did.

There is now a candidate explanation that is not NAN-specific. The two ends
transmit at very different PHY rates, confirmed from the capture metadata:

```
phone  NDP-Request, SchedReq   bg:48M   -37 to -38 dBm
ESP    NDP-Response            bg:6M    -10 to -11 dBm
```

When an ESP32 rev v0.0 was used as the *sniffer*, it heard the 6 Mbps
reference transmitter at 93–97 % of the rate an ESP32-S2 sniffer recorded,
but the phone's 48 Mbps frames at only about 5/s against 7.6/s, and in three
runs it missed the phone's M1 entirely. So that part is a weak receiver for
high-rate OFDM, and the frames the ESP32-as-publisher misses are exactly the
48 Mbps ones.

The v3.1 board supports this: running the identical binary it acknowledged
every M1 first try, with **no phone retransmissions at all** in six runs.
Whatever causes the misses travels with the rev-0 board, not with the code.

**Caveat:** promiscuous-mode reception is not necessarily representative of
normal station reception, so this is a hypothesis rather than a measurement
of the publisher's receive path. It would also be a silicon or
target-library property rather than a NAN defect.

**Run 19: M2 constructed and never transmitted.** With zero capture drops
there is no M2 on the air at all, although the ESP acknowledged the phone's
M1 — so it received the request — and its log records building M2 twice,
then `TX Confirm failed`, then rejecting. In its place it sent 14 unicast
Service Discovery Frames to the phone at roughly M1 + 529-548 ms, none
acknowledged, and 3 more at about +1050 ms. One observation; needs repeats.

**Off-AP discovery fails with an unsolicited publish.** Runs 5 and 6 saw 88
of 123 group-addressed publish SDFs land at or before +10 ms with the correct
service ID and cluster, and the phone's firmware reported no match at all. A
*solicited* (unicast) publish discovered in 1.25-1.6 s in the same
conditions. Unexplained.

**A ~20 s teardown of idle data paths** has its own write-up in
`IDF-ISSUE-3-nan-schedule-request.md`.

## No workaround exists in the public API

All 23 public `esp_wifi_nan_*` functions, both `wifi_nan_sync_config_t` and
`wifi_nan_datapath_resp_t`, and the four NAN Kconfig options were checked.
None exposes availability, scheduling or transmit timing. `nan_app.c:1634`
only reports the TX failure after the fact. This needs a change inside the
Wi-Fi libraries.

## Appendix: explanations considered and discarded

Kept because each was dropped on evidence that constrains what remains.

1. **Android offers Potential-only availability.** No — the decoded NDP
   Request carries a conditional all-slot channel-6 entry in map 0.
2. **A 5 GHz-only offer, excluded because the "no 2.4GHz channel overlap"
   string was absent.** Invalid — that check sits at `0x2bd`, downstream of
   the early return at `0x1a8`, and is unreachable once the first check
   fails.
3. **The negotiation succeeded, proved by the committed ch6 bitmap in M2.**
   No — the ESP always advertises its own committed ch6 schedule; its SDFs
   carry one before any NDP exists.
4. **A Discovery Window deadline, early termination at ~10-13 ms.** No —
   acknowledged and unacknowledged attempts overlap across the whole range.
5. **Slot-anchored scheduling, from run 3's two bursts starting at the
   identical offset one period apart.** Run 3's coincidence does not
   generalise; later retries started at 0.43-0.90 slots.
6. **Burst length as an outcome oracle — five means exhausted.** No — run 22
   succeeded on the fifth frame of a five-frame burst, and run 18's
   two-frame burst was never acknowledged.
7. **The phone fails to receive the ESP's ACKs.** Rests on one frame in run
   18 sent 180 us after an ACK, better explained as an already-queued retry;
   runs 20 and 22 show the phone stopping immediately on acknowledgement.

## Credit

Hardware, captures and decode by the bench session running the ESP32-S2
monitor, which also supplied corrections to items 3, 5, 6 and the capture
reliability analysis.
