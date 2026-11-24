# NAN: a spurious error-level availability log, and ACCEPTED reported after a parse error

**Status: draft, not filed.** Two small defects, both reproduced on three
targets. A third ask about NDP Response timing was withdrawn — see
[The withdrawn timing ask](#the-withdrawn-timing-ask).

## Summary

Wi-Fi Aware data paths between an ESP32 and an Android phone work. On
current silicon they came up in **16 of 16** runs — 6 on an ESP32-S2
(solicited), 6 on an ESP32 rev v3.1 (solicited), 4 on the same v3.1
(unsolicited).

Two defects remain, neither affecting whether the data path forms:

1. An **error-level log** fires on runs that succeed.
2. The NDP Response reports **ACCEPTED** after the parse that built it
   returned an error.

## Environment

| | |
|---|---|
| ESP-IDF | `v6.1-dev-6107` (`cb73317`), `esp_wifi/lib` at `7cd4d7b2` |
| Targets | ESP32-U4WDH **rev v3.1**, ESP32-S2 (Lolin S2 Mini) |
| Example | `wifi_aware/nan_publisher`, solicited and unsolicited |
| Peer | Samsung Galaxy Note 9 (SM-N960F), Android 10 / SDK 29 |
| Capture | monitor on channel 6 with ACK frames recorded, zero drops |

## Ask 1 — do not log an error for a map carrying only Potential entries

The ESP logs

```
E (12422) wifi:Avail: missing a conditional or committed entry
```

on runs where the NDP **confirms successfully**. It appears on **all three
targets**, including every one of the 16 successful runs on current silicon.
It never indicated a negotiation failure: it is an error-level message for a
non-error.

The peer's NDP Request carries two Availability attributes — map 0 with a
**conditional** channel-6 entry covering all 32 slots, and map 1 with
**potential** 5 GHz entries (op class 128, bitmap `0x0020`, the block
centred on 155 containing NAN's 5 GHz discovery channel 149). Potential-only
is exactly what a map-1 entry is for.

`nan_parse_ndp_req` calls `nan_dp_parse_availability` from an attribute walk
(backward branch `0x442 -> 0x135`), consistent with the error coming from the
map-1 pass. Per-map storage exists — `nan_get_avail_by_map_id` walks 92-byte
records keyed on map ID — but it is called from `nan_sched_alloc_conditional`,
`nan_sched_allocate_crbs` and `nan_dp_indicate_faw_start`, never from the
parser.

This message sent two independent investigations down the wrong path for
days.

## Ask 2 — do not report ACCEPTED after the parse returned an error

In `ieee80211_nan_datapath.o`, `nan_dp_parse_availability` logs and returns
262 at `0x1a8`, jumping to `0x41f` — two instructions before `retw.n` at
`0x422`. The success path returns 0 at `0x417`. The M2 the ESP then
transmits carries status `0x11`, ACCEPTED. The log and the frame disagree.

## The withdrawn timing ask

Earlier revisions asked Espressif to change how the esp32 library schedules
the NDP Response, on the evidence that its first transmission was never
acknowledged and setup succeeded in only 9 of 16 runs. **That evidence came
entirely from one board, and that board turned out to be faulty.**

The board was a SparkFun ESP32 Thing, ESP32-D0WDQ6 **rev v0.0**, 26 MHz
crystal, external flash. Its radio link to the phone is bad in both
directions:

| | rev-0 Thing | ESP32 rev v3.1 | ESP32-S2 |
|---|---|---|---|
| transmissions per unicast frame | **9.81** | 1.00 | 1.01 |
| first transmission acked, 0–4 ms into DW | **44/388 = 11 %** | 275/278 = 99 % | — |
| data path established | 9 of 16 | 6 of 6 | 6 of 6 |

At 0–4 ms into the Discovery Window the peer is certainly listening, and the
phone still failed to receive or decode **89 %** of that board's frames. The
same board also received the phone's 48 Mbps OFDM poorly when used as a
sniffer. A bidirectional RF-level problem — carrier frequency offset from
the 26 MHz crystal, or rev-0 RF — fits both directions; it was not measurable
with the available equipment.

So the M2 failures cannot be attributed to transmit timing, and the ask is
withdrawn rather than reworded.

### What remains interesting about it

One observation survives as a curiosity rather than a defect. The esp32
library answers inside the Discovery Window the request arrived in, with a
guard before the end of slot 0 of **1.841 ms = 1.80 TU** on v3.1 (mean of
six runs, sd 0.051 ms) — a designed constant, within 2 µs of 1843.2 µs.

On the rev-0 board the equivalent guard was 1.167 ms. The ratio, 0.634, sits
between 26/40 = 0.650 and 25/40 = 0.625, which a crystal-derived scaling of
the guard would produce; 26/40 predicts a delay of 15.188 ms against 15.217
measured. The direction requires a guard converted to ticks with the real
26 MHz crystal and then consumed against a 40 MHz timebase — not a
hard-coded 40 used where the real value belonged, which would lengthen the
guard rather than shorten it.

Nothing in the blob supports or refutes it. Neither `libnet80211.a` nor
`libpp.a` references any crystal or clock frequency helper among their 1453
and 447 undefined symbols; no literal `40000000`, `26000000`, `73728` or
`1843200` appears; `ieee80211_nan_datapath.o`'s literal pool holds nothing of
guard-time magnitude, only log line numbers; `hal_tsf.o` has no literals at
all. The only clock-dependent external called is ROM's `ets_delay_us`.

A rev v3.x board with a 26 MHz crystal would settle it — predicted first M2
at about 15.19 ms. We have no such board, and with the RF fault identified
the question is academic for ESP-IDF purposes.

It is worth noting that the two anomalies on that board may share one cause.
A clock problem that scaled a guard interval by roughly 26/40 would also
produce a carrier frequency offset, and carrier offset degrades OFDM decode
in both directions — which is exactly the 11 %-versus-99 % asymmetry and the
poor 48 Mbps reception. One faulty crystal or clock path explains the timing
floor and the RF loss together, and neither needs an ESP-IDF defect to
account for it.

## Previously fenced observations, now attributed to the faulty board

- **The ESP missing M1 up to seven times running** — zero occurrences on
  v3.1 or S2.
- **Run 19's M2 constructed and never transmitted** — zero occurrences on
  v3.1 or S2.
- **Off-AP discovery failing with an unsolicited publish** — does not
  reproduce. The v3.1 board discovered in 0.55–0.76 s in 4 of 4 runs, with
  about 114 group-addressed publish SDFs per run, and the data path came up
  every time. The phone does not ignore group-addressed SDFs off-AP; runs 5
  and 6 failed because of the publisher.

## Related

`IDF-ISSUE-3-nan-schedule-request.md` — the ESP has no handling for NAF
subtypes 10–13, so a peer's Schedule Request is never answered and idle data
paths are torn down. That one reproduces on every target and is unaffected
by any of the above.

## Credit

Hardware, captures and decode by the bench session running the monitor,
which identified the faulty board and supplied the measurements that
withdrew the timing ask.
