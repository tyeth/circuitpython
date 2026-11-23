# NAN: NDP Response straddles the Discovery Window end, and a spurious availability error

**Status: draft, two asks ready, one question still open.** The data path
*does* work — run 7 established an IPv6 data path between an Android phone and
an ESP32 — so this is no longer "NAN is broken with Android". It is two
specific defects plus an unexplained off-AP discovery failure.

- **Ask 1 (timing) is ready.** The ESP takes ~16 ms to answer an NDP Request
  and the Discovery Window is 16.384 ms, so the response lands on the edge.
  Measured with direct ACK capture: +15 ms acknowledged, +16/+17 ms not.
- **Ask 2 (spurious error) is ready, and is now demonstrably cosmetic.**
  `Avail: missing a conditional or committed entry` is logged on runs where
  the NDP **succeeds**, so it never was a negotiation failure.
- **Off-AP discovery** never completes and is **not explained**. Nothing
  about it should be filed.
- **A ~20 s data-path teardown** appeared in runs 7 and 8. It now has a
  mechanism and its own write-up: the ESP never answers the peer's NAF
  Schedule Request. See `IDF-ISSUE-3-nan-schedule-request.md`.
- **Off-AP discovery works with a *solicited* (unicast) publish** — run 8
  discovered in 1.25 s. The runs 5/6 failure was specific to the
  **unsolicited, group-addressed** publish, which supports the
  unicast-versus-group-addressed split recorded below.

Four earlier explanations were proposed and discarded; that history is kept in
[What is NOT established](#what-is-not-established) because each was dropped
on evidence that constrains what remains.

## Summary

An ESP32 running NAN-Sync as a publisher discovers an Android phone, exchanges
follow-up messages, and responds to its NDP Request with an **ACCEPTED** NDP
Response. That response is never acknowledged, across ten transmit attempts in
two Discovery Windows, and the phone times out after 20 s.

The ESP also logs `Avail: missing a conditional or committed entry` while
parsing a request that demonstrably contains a conditional entry.

## Environment

| | |
|---|---|
| ESP-IDF | `v6.1-dev-6107` (`cb73317`), `esp_wifi/lib` at `7cd4d7b2` |
| Target | ESP32 (classic, rev0), `examples/wifi/wifi_aware/nan_publisher` |
| Config | `CONFIG_ESP_WIFI_NAN_SYNC_ENABLE=y`, NAN security off (open NDP) |
| Peer | Samsung Galaxy Note 9 (SM-N960F), Android 10 / SDK 29 |
| Capture | ESP32-S2 in monitor mode on channel 6 |

ESP-to-ESP NAN-Sync on the same build works end to end: cluster 3.35 s, match
4.40 s, IPv6 data path 4.93 s, ICMPv6 at 2 ms RTT, no loss.

**Confound, see below: during every run the phone was also associated as a STA
to an AP on channel 64 (5320 MHz).**

## Measured facts

### The phone's NDP Request is well formed

NAF subtype 5, `fa:b7:34:46:4c:36` → ESP NMI `26:0a:c4:5f:d3:ca`, t = 13.342 s,
−36 dBm. DevCap; `NDP{dialog=5, type/status=0x00}`; two Availability
attributes:

- **`attr_ctrl=0x0010`, map ID 0** — one entry, **CONDITIONAL**, pref 3, time
  bitmap 16 TU slots / 512 TU period / start 0 / `ffffffff` (all 32 slots),
  channel entry op class 81, **channel 6**.
- **`attr_ctrl=0x0011`, map ID 1** — two **POTENTIAL** entries: op class 128
  (5 GHz, 80 MHz, chan bitmap `0x0020`, prim `0x01`), slots `000f0000`; and a
  band entry "5 GHz", slots `00f000ff`.

Then NDC (15), NDL (4), NDL-QoS (3), attr `0x1d` (81).

The request is neither Potential-only nor 5 GHz-only. Two earlier theories
about it are dead.

### The ESP responded ACCEPTED, and was not acknowledged

M2 carries status `0x11` (ACCEPTED), a committed channel-6 bitmap `feffffff`,
and NDC + NDL attributes. Transmit attempts, measured from the start of the
Discovery Window (DW = 16 TU = 16.384 ms, recurring every 524 ms):

| ESP frame | offset into DW | acknowledged? |
|---|---|---|
| SDF | +14 ms | n/a — group-addressed, never acked |
| Follow-up "Welcome" (run 3) | +9 ms | yes |
| M2, 5 tries, 13.358–13.362 | +16 to +20 ms | no |
| M2 retry, 5 tries, 13.883–13.886 | +16 to +19 ms | no |

Phone sent NDP-Termination at 33.266 s, the 20 s timeout.

### The spurious parse error

```
E (12422) wifi:Avail: missing a conditional or committed entry
```

Disassembly of `nan_dp_parse_availability` (`ieee80211_nan_datapath.o`,
esp32):

```
19e:  l32i   a8, a4, 0x140
1a1:  bnez.n a8, 1e1              ; usable entry found -> continue
1a5:  movi   a2, 0x106            ; return 262
1a8:  l32r   a13, c               ; "missing a conditional or committed entry"
1b1:  callx8 a8                   ; log
1b4:  j      41f                  ; -> two instructions before retw.n at 422
```

The success path sets `a2 = 0` at `0x417`. `nan_parse_ndp_req` calls this
parser from inside an attribute walk (backward branch `0x442 → 0x135`, two
references to the parser), which is consistent with the error coming from a
second call on map 1 — where Potential-only entries are exactly what is
expected — rather than from map 0.

Per-map storage and lookup exist in the blob: `nan_get_avail_by_map_id` walks
92-byte records keyed on map ID. It is called from
`nan_sched_alloc_conditional`, `nan_sched_allocate_crbs` and
`nan_dp_indicate_faw_start`, but **not** from the parser.

## What is NOT established

Three things, each of which a previous draft of this document got wrong.

**1. That the ESP honoured the phone's conditional entry.** An earlier draft
argued the committed ch6 bitmap in M2 proved the ESP had parsed and used
map 0. It proves nothing of the kind. In NAN-Sync the ESP is a single-channel
device that always advertises its own committed ch6 schedule: its SDFs in the
same capture carry a committed ch6 bitmap (`fcffffff`) before any NDP exists,
and it logs its own NDC as `NDP Resp NDCA: ndc=0x2 committed=0xfffffffe`.
M2's committed entry can come wholly from the ESP's own state. The ESP may
have accepted without honouring the phone's entry at all. Both readings stay
open.

*(One small open detail: the SDF bitmap is `fcffffff` and M2's is `feffffff`.
If M2's committed schedule were a straight copy of the ESP's own state, one
might expect them identical. That difference may be ordinary DW-slot
bookkeeping and is not offered as evidence either way, but a future capture
could check whether M2's bitmap ever varies with the peer's offer.)*

**2. That the Discovery Window boundary is the mechanism.** The out-of-window
side is solid — ten unacknowledged M2s across two DWs. The in-window side is
**n = 1**. SDFs are addressed to the group address `51:6f:9a:01:00:00` and are
never acknowledged, so they carry no delivery information. The only in-window
unicast is the run-3 follow-up at +9 ms. One success against ten failures is
suggestive, not proof. Run 2 additionally logged `Failed to send Follow-up
message!` twice with no sniffer timing; if those attempts were in-window, the
boundary story weakens further.

**3. That any ESP scheduling defect is involved at all.** Throughout every run
the phone was associated as a STA to an AP on **channel 64 (5320 MHz)** — its
map-1 Potential 5 GHz entries, op class 128, are consistent with exactly that.
Android time-shares the radio back to its AP channel between Discovery
Windows, and a *conditional* availability entry does not oblige the peer to
remain on channel 6 until the NDL is confirmed. So outside the DW the phone
may simply not have been listening on channel 6, which would account for every
unacknowledged M2 with no ESP defect whatsoever.

## The off-AP rerun: the timing theory does not survive either

Phone Wi-Fi on, not associated, verified over adb.

**Run 4, roles reversed** (phone publishes, ESP subscribes and initiates).
The ESP discovered the phone in **0.24 s**. Its unicast follow-up went out 22
times at +13 to +22 ms and was never acknowledged. The ESP proceeded to the
NDP regardless (`Peer datapath_reqd not set, proceeding anyway`), sent M1
dialog 1 ×5 then dialog 2, and the phone answered **NDP Response status
`0x21` = REJECTED, reason 2 (resource limitation)**. The ESP's M1 carried
Availability map 0 with a conditional ch6 entry (`fcff0100`) plus a potential
entry on ch 1–11.

**Run 5, original direction** (ESP publishes, phone subscribes). No discovery
across roughly 60 Discovery Windows. In every one of them the ESP's publish
SDF landed **+2 to +14 ms** after the phone's SDF — inside the 16 TU window,
and often *earlier* than a third-party NAN device at +1 to +14 ms that was
transmitting in the same windows. The phone still never matched.

### What this does to the timing theory

Run 5 is decisive against the *simple* version of it: the ESP transmitted
inside the 16.384 ms Discovery Window, repeatedly, and was still not heard.
"The ESP transmits too late relative to the end of the DW" cannot carry runs
5 and 6.

It survives in a narrower form for runs 3 and 4, against a shorter effective
deadline — see [Transmit offsets, all runs](#transmit-offsets-all-runs) — and
ask 1 is written to that narrower claim only.

### But "the phone hears nothing from the ESP" is too strong

In run 4 the phone **answered the ESP's M1** with a properly formed NDP
Response carrying a specific reason code. It cannot have done that without
receiving M1. So the phone's receive path for ESP frames is not globally
broken, and any explanation resting on PHY rate or frame format has to
account for M1 getting through while 22 follow-ups did not.

The reason-2 rejection itself is explained and is not an ESP fault: the test
app only called `requestNetwork()` from `onMessageReceived`, so with no
follow-up delivered Android had no pending responder request to match M1
against. The app now arms an any-peer responder at publish time
(`WifiAwareNetworkSpecifier.Builder(publishSession)`), which removes that
dependency, and run 4 should be repeated with it.

### Transmit offsets, measured from the captures

Re-derived directly from the pcaps rather than from frame counts, because
counting frames overstates the evidence: retries within a burst are not
independent trials.

**Method note.** There are **no ACK frames at all in any of the four pcaps**.
That is a property of the capture, not of the medium: the sniffer runs with
`WIFI_PROMIS_FILTER_MASK_MGMT` and keeps only NAN management frames, so
control frames are never recorded. No conclusion here may rest on "the sniffer
saw no ACK".

Delivery is instead read from two independent signals that agree: the 802.11
**retry bit** — a burst that stops before the retry limit was acknowledged,
one that runs to the limit was not — and the ESP's own TX-status logging. A
future capture can add `WIFI_PROMIS_CTRL_FILTER_MASK_ACK` to record ACKs by
RA directly, which would turn this inference into a measurement.

Every unicast transmission *event* from the ESP across runs 3 and 4, with the
offset measured from the preceding group-addressed SDF from the phone:

| run | AP | first tx | last tx | frames | outcome |
|---|---|---|---|---|---|
| 3 | on | **+9.22 ms** | **+10.22 ms** | 3 | **delivered** on the 3rd |
| 3 | on | +15.93 ms | +19.88 ms | 5 | failed |
| 3 | on | +15.93 ms | +19.61 ms | 5 | failed |
| 4 | off | +13.12 ms | +22.08 ms | 21 | failed |
| 4 | off | +17.37 ms | +20.36 ms | 5 | failed |
| 4 | off | **+4.16 ms** | — | **1** | **acked first attempt** |

Six events: two delivered, four failed. Corroboration for both successes is
independent of the capture — the phone logged
`onMessageReceived … utf8="Welcome"` for the run-3 event, and the ESP logged
`M1 TX success: ndp_id=1` for the run-4 one, which is also the only event that
needed no retry at all.

Two features of this table matter more than the raw counts.

**The effect is graded.** +4.16 ms was acknowledged on the first attempt.
+9.22 ms needed three attempts and only got through at +10.22 ms. Everything
first transmitted at +13.12 ms or later failed outright, including one event
that tried 21 times. That is the shape of a receiver that stops listening part
way through the window, not of random loss.

**The boundary is in the same place with and without the AP.** On-AP: success
at +9–10 ms, failures at +15.93 ms twice. Off-AP: success at +4.16 ms,
failures at +13.12 and +17.37 ms. If AP time-sharing on 5 GHz accounted for
the run-3 failures, the boundary would not be expected to land in the same
place off-AP. A single mechanism spanning both conditions fits better than two
different ones.

It remains a small sample — two successes — and the retry limit is inferred
from bursts stopping at 5. A deliberate sweep of follow-up delivery against DW
offset would settle it properly.

### What the offsets do NOT explain

Run 4 was **off-AP**, and the phone answered a unicast at +4 ms. So off-AP the
phone is demonstrably awake and receiving early in the window.

That rules out the simplest reading of runs 5 and 6. In those the ESP's
publish SDF landed at +2 to +14 ms after the phone's SDF, across roughly 60
Discovery Windows. Many of those were inside the +4 to +9 band that
demonstrably works. The phone still never matched, and its HAL shows
subscribe OK (`subscribeId=1`) with **no match event from firmware at all**.

So early termination cannot account for runs 5 and 6, and neither can the
frames: the ESP's publish SDF carries SDA service id `6f08e42b8c44` =
`SHA-256("esp-nan-test")[:6]`, exactly matching the phone's own subscribe SDF,
with publish type `0x00`, in cluster `50:6f:9a:01:db:dd` — the same cluster
all three devices are in — at 6 Mbps non-HT, the most robust rate available.

### The remaining asymmetry: unicast works, group-addressed does not

Putting runs 4, 5 and 6 together, off-AP:

- A **unicast** frame to the phone's NMI at +4 ms is received and answered.
- A **group-addressed** publish SDF produces no firmware match, with the
  correct service ID and cluster, **88 times out of 123 at or before +10 ms**
  in run 5 alone (23 of 37 in run 6b).

On-AP, in runs 2–3, the same group-addressed publish SDF *was* matched, in
1–23 s.

The open question this raises is whether the phone processes group-addressed
SDFs differently when it has no AP association — a receive-filter or
power-save difference rather than anything about the ESP's transmissions.

**A caution on that hypothesis.** It has the same shape as the one proposed
for the USD defect in issue 1, where group-addressed NAN frames also appear to
go missing. That symmetry is suggestive but it is also exactly the kind of
pattern-match that has produced four discarded theories in this
investigation. It is written down as a question, not a finding.

### Is the map-0/map-1 structure an artefact of the AP?

Ask 2 rests on the two-map structure of an NDP Request that was only ever
captured while the phone was associated to an AP. Decoding the phone's
Availability attributes straight out of the captures answers most of that
worry.

The phone's **map-0 entry in its SDFs is byte-identical on-AP and off-AP**:

```
0e00 1a12 1800 04 03000000 11 51 2000 00    potential, op class 81, ch 6
0b00 1a12 1800 04 fcffffff 10 02            potential, band 2.4 GHz
```

The map-1 entries are present in both conditions too, and the op class is the
decisive detail: **op class 128, channel bitmap `0x0020`**. For class 128 the
channel set is 42, 58, 106, 122, 138, 155, so bit 5 is the 80 MHz block
centred on 155 — channels 149–161, containing **149, the NAN 5 GHz discovery
channel**. The phone's AP was on channel 64, which sits in the block centred
on 58 and would be bit 1. So map 1 is the phone's own NAN 5 GHz availability
and has nothing to do with its AP association. An earlier reading of these
entries as "matching the AP channel" was wrong.

The two-map structure is therefore a property of the phone's NAN
configuration, not of the AP, and **ask 2 does not rest on an AP artefact**.

Two caveats, one of which matters.

The AP does change the *content*: the 5 GHz potential time bitmap is
`00f000ff` on-AP and `fff0ffff` off-AP, which is what one would expect from a
radio that has to serve an association.

The caveat that matters: all of the above is from **SDFs**, and the SDF map-0
entry is **Potential** with slots `03000000`, whereas the on-AP **NDP
Request** map-0 was **Conditional** with `ffffffff`. The phone upgrades its
offer when it actually asks for a data path. So SDFs are weak evidence for
what an off-AP NDP Request would contain, and no off-AP NDP Request has ever
been captured — off-AP discovery has never completed, so the exchange never
gets that far.

**The availability parse failure has been observed twice, in runs 2 and 3,
both on-AP, both with the ESP as NDP responder. There are zero off-AP
observations.** Reproducing it needs either the AP back, or discovery working
off-AP so the phone initiates an NDP there.

### The test that would settle it

The phone subscribes **ACTIVE**, so it transmits subscribe SDFs that the ESP
can see. A NAN publisher is expected to answer a matching active subscribe
with a **solicited publish, unicast to the subscriber's NMI**.

Two things to check in the run 5/6 captures:

1. **Does the ESP ever send a unicast solicited publish to the phone's NMI?**
   If it does not, discovery depends entirely on the phone catching a
   group-addressed broadcast, and the ESP's failure to answer an active
   subscribe is itself a defect worth reporting separately.
2. **If it does, at what DW offset, and is it matched?** A unicast solicited
   publish landing at +4 ms and still producing no match would clear the
   unicast/broadcast hypothesis and point back at the phone's firmware.

### Still outstanding

The on-AP/off-AP inversion is unexplained and reproduces (runs 5 and 6). Until
it is understood, no claim about the ESP's behaviour in either direction is
safe. The alternating on/off-AP pair is the next experiment and requires the
phone to be rejoined to its AP.

### Run 7: the data path works

Phone back on-AP, screen awake, ESP running a SOLICITED publisher. Captured
with ACK frames recorded, so delivery is direct evidence rather than inferred
from the retry bit.

```
4.779  DW+1       ESP solicited publish (unicast) -> phone     ACKED
5.303  DW+0       phone follow-up "Hello" -> ESP               ACKED
5.306  DW+3       ESP publish + "Welcome" -> phone             ACKED
5.311  DW+8
5.826  DW+0       phone NDP Request -> ESP                     ACKED
5.842  DW+16..17  ESP M2 burst x5                              NOT acked
6.365  DW+15      ESP M2 retry burst x3                        ACKED -> confirm
6.382             phone broadcast NAF Schedule Update
```

The app reported `TCP connect to [fe80::240a:c4ff:fecb:7ddb%aware_data0]:7
REFUSED => PASS` and `isReachable=true`; the ESP logged `NDP confirmed …
Peer IPv6 FE80::281B:56FF:FE14:B87C`, NDL ch6, `ndc=[1,1]`, `crb1=[2,31]`.

**This settles ask 2.** `Avail: missing a conditional or committed entry` was
logged in this run too, and the NDP confirmed anyway. The error never
prevented anything. It is a misleading log line, and the earlier worry that it
might also corrupt the ESP's schedule state is answered: it does not prevent
setup.

**It also sharpens ask 1, though the mechanism is still open.** An earlier
revision of this document argued that because run 3's two M2 bursts started
at the identical offset (+15.927 ms, 0.9721 slots, one DW period apart to
within 0.0001 of a period), the transmission must be anchored to the slot grid
rather than to processing latency. **That argument is withdrawn.** Run 3's
coincidence does not generalise: the successful retries in later runs started
at 0.85–0.90 slots (run 7) and 0.569 slots (run 8), nowhere near the first
attempt's position.

What *is* consistent is the **first** attempt, across every run:

| run | first M2 after M1 |
|---|---|
| 3 | 0.98 slots |
| 7 | 0.93 slots |
| 8 | 1.03 slots |

Because the phone always sends its NDP Request at DW+0, "about one slot after
M1" and "the slot-0/slot-1 boundary" are the same instant, so first attempts
alone **cannot** separate ~16 ms of processing latency from deliberate
slot-grid scheduling. Off-by-one remains a hypothesis.

The defensible statement is the one that does not depend on the mechanism: the
first NDP Response consistently goes out about 16 ms after the request, which
is at or past the end of the Discovery Window, and setup only succeeds when a
retry happens to land earlier in a later window. That is a coin flip, and it
is what ask 1 asks to fix.

**The control did not happen.** IDF refused the second publish —
`Service name esp-nan-test already used!`, publish #2 id=0 — so this was
solicited-only and there is no within-run unsolicited comparison. Two
instances need different service names. The off-AP discovery question is
therefore untouched by run 7, which was on-AP and discovered via a *unicast*
solicited publish ACKed at DW+1.

### The 20 s teardown

About 20 s after confirmation the link dropped. The phone reported
`eventDataPathTerminated` for ndp 4 and the app logged `onLost` at 26.4 s; the
ESP logged nothing.

Two candidates, and they are cheaply separable:

1. **Idle teardown.** The app connected once and then sent nothing. If an
   unused NAN data path is reclaimed, this is not a defect at all.
2. **An unsatisfiable NDL.** The ESP committed `crb1=[2,31]` — effectively
   every slot on channel 6 — which a phone concurrently associated to an AP
   on channel 64 cannot honour. If so this links back to the availability
   handling: the ESP takes the peer's *conditional* offer as licence to commit
   to everything, survives setup, and loses the link later.

The test app now sends a keepalive probe every 2 s once the data path is up
and reports how long the link survived, which distinguishes the two in one
run.

## Asks

1. **Answer an NDP Request early in slot 0 of the Discovery Window, well
   before the slot 0/slot 1 boundary, and keep retries there too.**

   The first NDP Response goes out about 16 ms after the request in every run
   measured — 0.98, 0.93 and 1.03 slots — which places it at or past the end
   of the 16.384 ms window. Setup then succeeds only when a retry happens to
   land earlier in a later window, which it did in runs 7 and 8 (0.85–0.90 and
   0.569 slots) and never did in run 3 (0.97 twice). Measured starts
   acknowledged: 0.25, 0.56, 0.569, 0.85–0.90. Not acknowledged: 0.93–1.05,
   0.97, 0.97.

   Whether the 16 ms is processing latency or scheduling aimed at the slot
   boundary cannot be told from outside the Wi-Fi libraries — the phone sends
   its request at DW+0, so the two coincide. The remedy is the same either
   way. Note also that the ESP's own NDL places its NDC in slot 1 while the
   peer's slot 1 is still only conditional before confirmation, so a response
   arriving there has no guaranteed listener.

   This ask covers data-path setup only. It does not explain the off-AP
   discovery failure in runs 5 and 6.

2. **Do not log "missing a conditional or committed entry" for a map that
   legitimately carries only Potential entries.** Run 7 confirms this error is
   **cosmetic** — it is logged on runs where the NDP goes on to confirm
   successfully. It is an error-level message for a non-error, and it cost two
   independent investigations several days. Per-map storage and lookup already
   exist — `nan_get_avail_by_map_id` walks 92-byte records keyed on map ID —
   but it is called from `nan_sched_alloc_conditional`,
   `nan_sched_allocate_crbs` and `nan_dp_indicate_faw_start`, never from the
   parser. The message sent two independent investigations down the wrong path
   for a day.

3. **Do not report ACCEPTED after a parse error**, or do not return an error
   for a condition that does not prevent acceptance. At present the log and
   the transmitted frame disagree. Also independent of the root cause.

## Why this matters

Phone interoperability is the main reason to use Wi-Fi Aware on these parts,
and essentially every phone will be STA-associated while doing it. Today
NAN-Sync works ESP-to-ESP and does not establish a data path with Android.

## Credit

Frame capture, decode and DW timing by the bench session running the ESP32-S2
monitor, which also identified the STA-concurrency confound and the n = 1
weakness in the in-window evidence.
