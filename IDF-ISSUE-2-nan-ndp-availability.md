# NAN: NDP Response is never acknowledged on its first attempt, and a spurious availability error

**Status: draft, not filed.** Asks 1–3 are evidenced and ready. Two further
observations are recorded but deliberately not asked about, because they rest
on too little.

## Summary

An ESP32 running NAN-Sync establishes a Wi-Fi Aware data path with an Android
phone in **9 of 16 runs**. The failures share a mechanism: the ESP's first
NDP Response transmission is *never* acknowledged, in any run, and setup
depends on one of its retries being taken.

Two smaller defects accompany it: an error-level log that fires on successful
runs, and an M2 that reports ACCEPTED after the parse that built it returned
an error.

## Environment

| | |
|---|---|
| ESP-IDF | `v6.1-dev-6107` (`cb73317`), `esp_wifi/lib` at `7cd4d7b2` |
| Target | ESP32-D0WDQ6 **rev v0.0 (ECO0)**, `wifi_aware/nan_publisher`, SOLICITED |
| Peer | Samsung Galaxy Note 9 (SM-N960F), Android 10 / SDK 29 |
| Capture | ESP32-S2 monitor on channel 6 with ACK frames recorded |

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

ESP-to-ESP NAN-Sync on the same build works end to end — cluster 3.35 s,
match 4.40 s, IPv6 data path 4.93 s, ICMPv6 at 2 ms RTT. Only the Android
data path is unreliable.

### Method

Delivery is read from **ACK-by-RA**, with each ACK matched to the frame it
*immediately* follows. A wider match window credits one ACK to two frames of
a retry burst and produces false positives.

Runs 17–22 used a sniffer with a PSRAM queue and ACKs filtered to known NAN
NMIs, and recorded **zero drops**. Runs 3–6 predate ACK capture and are also
drop-free. Runs 7–16 overflowed a 24-entry queue (up to 1761 drops), so for
those, burst sizes are lower bounds and a missing frame proves nothing.

---

## Ask 1 — answer the NDP Request on the first attempt

**The first M2 transmission is never acknowledged.** Across the five
zero-drop runs that put an M2 on the air, the opening frame of the first
burst has no ACK immediately following it, every time.

| run | first-window M2 | second-window M2 | outcome |
|---|---|---|---|
| 17 | 5 fr @ 1.58–1.68, none acked | 2 fr @ 0.42–0.46, 1st acked | OK |
| 18 | 2 fr @ 0.96–1.04, none acked | 3 fr @ 0.75–0.83, 3rd acked | OK |
| 19 | **no M2 on air at all** | — | FAIL |
| 20 | 5 fr @ 0.96–1.21, none acked | 1 fr @ 0.24, acked | OK |
| 21 | 2 fr @ 0.95–0.98, **2nd acked** | — | OK |
| 22 | 5 fr @ 2.05–2.15, none acked | 5 fr @ 0.22–0.33, 5th acked | OK |

Offsets are in slots (1 slot = 16 TU = 16.384 ms) from the preceding
group-addressed frame from the phone, which sits within 1.31 ms of a
Discovery Window start beacon — median 0.00 ms, sigma 0.40 ms over 593
windows, so it is a sound anchor.

The first-window M2 starts anywhere from **0.92 to 2.12 slots** after the
request, frequently past slot 1 entirely, which is where the peer is not
reliably receiving.

**It is not a position threshold.** Acknowledged and unacknowledged attempts
overlap throughout the range: run 11's second burst at 0.740 failed where
run 9's at 0.724 succeeded; run 13's at 0.548 failed where run 8's at 0.581
succeeded.

**It is not impossible to get right.** Run 21's retry at 0.984 slots was
acknowledged and the data path came up without a second window ever being
needed. The ESP *can* close the exchange inside the window the request
arrived in.

**The ask:** transmit the NDP Response early enough in the Discovery Window
that the first attempt has a chance, and leave room for retries within that
window. At present the first attempt never lands and setup is 9 in 16.

## Ask 2 — do not log an error for a map carrying only Potential entries

The ESP logs

```
E (12422) wifi:Avail: missing a conditional or committed entry
```

on runs where the NDP **confirms successfully**, so it never indicated a
negotiation failure. It is an error-level message for a non-error.

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

**The ESP misses M1 repeatedly.** In run 18's first window the phone sent M1
seven times and the ESP acknowledged none; it took the third copy in the next
window. Run 20's first M1 went unacknowledged, run 22's first three did. This
is an ESP receive-side problem and it is uncharacterised.

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
