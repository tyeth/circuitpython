# NanTest — Android Wi-Fi Aware (NAN) bench tool

Subscribes to a NAN service, sends a follow-up message, requests an open
(unsecured) NAN data path, and reports the peer's IPv6 address and whether
traffic actually crosses it. Built for testing a phone against an ESP32
running ESP-IDF's `wifi_aware/nan_publisher`.

Single activity, no UI beyond a scrolling log. Everything is also written to
logcat under the tag `NanTest`, each line prefixed with milliseconds since
the activity started.

## What this is built with

A plain **.NET Android** app (`net11.0-android`), not Kotlin/Gradle. That is
what this build machine has: the .NET `android` workload 37.2.29, the Android
SDK at `C:\Program Files (x86)\Android\android-sdk` (build-tools 37.0.0) and
JDK 21. There is no JDK, Android SDK or Gradle on the Linux side.

The framework makes no difference to the test: it calls the same
`android.net.wifi.aware` platform APIs, takes the same intent extras, and
logs to the same tag.

- `minSdkVersion` 26, `targetSdkVersion` 29, compiled against API 37.
- ABIs: `arm64-v8a` and `armeabi-v7a`.
- Debug-signed, self-contained, ~12.9 MB.

## Build

```
dotnet build NanTest.csproj -c Debug
```

Output: `bin/Debug/net11.0-android/com.example.nantest-Signed.apk`.

Two project settings are load-bearing; do not drop them:

- **`EmbedAssembliesIntoApk=true`.** Debug builds default to fast deployment,
  which leaves the managed assemblies out of the APK and pushes them to the
  device separately via `dotnet build -t:Install`. A fast-deployment APK
  installs fine with `adb install` and then crashes at launch. With this set,
  the APK carries `lib/<abi>/libassembly-store.so` (189 assemblies, 3.5 MB).
- **`[Activity(Name = "com.example.nantest.MainActivity", ...)]`.** Without an
  explicit `Name`, .NET Android emits a hashed class name such as
  `crc64389dd3b041192768.MainActivity`, and `am start -n
  com.example.nantest/.MainActivity` fails with
  `Activity class does not exist`.

## Install and run

```
adb install -r app-debug.apk
```

```
adb shell pm grant com.example.nantest android.permission.ACCESS_FINE_LOCATION
```

```
adb logcat -c && adb logcat -s NanTest
```

```
adb shell am start -n com.example.nantest/.MainActivity --es service esp-nan-test --es msg Hello --ez ndp true
```

Extras, all optional:

| extra | type | default | meaning |
|---|---|---|---|
| `role` | string | `subscribe` | `subscribe` (phone initiates) or `publish` (phone responds) |
| `service` | string | `esp-nan-test` | NAN service name |
| `msg` | string | `Hello` | follow-up body sent in subscribe mode |
| `reply` | string | `Welcome` | reply sent in publish mode |
| `ndp` | bool | `true` | request a data path |
| `armnow` | bool | `true` | publish mode: arm an any-peer responder at publish time instead of waiting for a follow-up |
| `port` | int | `7` | TCP port to probe when the peer advertises none |
| `keepalive` | int | `2000` | ms between keepalive probes once the data path is up; `0` disables |

### Role reversal

`role=publish` exists to test around the ESP's availability-negotiation gap
described below. The phone publishes and acts as the NDP **responder**, so the
ESP builds the committed availability itself instead of having to derive one
from Android's Potential-only offer.

```
adb shell am start -n com.example.nantest/.MainActivity --es role publish --es service esp-nan-test
```

The phone publishes `PUBLISH_TYPE_UNSOLICITED` and, by default, immediately
arms an **any-peer** open responder — `WifiAwareNetworkSpecifier.Builder` with
a publish session and no peer handle — rather than waiting for a follow-up.

That default matters. If the responder is only armed on `onMessageReceived`
and the initiator's follow-up never arrives, Android has no pending responder
request and rejects the incoming M1 with status `0x21`, reason 2 (resource
limitation). That looks like a data-path fault and is really just an unarmed
app. Pass `--ez armnow false` to get the old wait-for-follow-up behaviour,
which is worth doing deliberately if you want to reproduce that rejection.

It still replies (`reply`, default `Welcome`) to any follow-up that does
arrive. Nothing else happens until the peer initiates.

In this mode the TCP probe is expected to be refused or to time out — the ESP
runs no listener. The ESP's own pings across the data path are the proof.

The sessions stay open until the activity is destroyed. Stop with:

```
adb shell am force-stop com.example.nantest
```

## Reading the result

The sequence you want to see is: `onAttached` → `onSubscribeStarted` →
`onServiceDiscovered` → `onMessageSendSucceeded` → `NDP onAvailable` →
`NDP onCapabilitiesChanged` with a `peerIpv6Addr` → a probe verdict.

**The reachability verdict is deliberately not `isReachable()`.**
`InetAddress.isReachable()` only sends ICMP when the process may open a raw
socket; otherwise it silently falls back to a TCP connect on port 7, which the
ESP will not answer. It therefore returns `false` on a perfectly healthy link.
It is logged, but marked inconclusive.

The oracle is the TCP connect that follows, which classifies three ways:

- **connected** — the link works and something is listening.
- **refused (`ECONNREFUSED`)** — *this is a pass*. A reset means the peer's IP
  stack received the SYN and answered, so the data path carries traffic.
- **timeout / unreachable** — no link.

## Things that will cost you an hour if you miss them

- **Location.** Aware discovery needs `ACCESS_FINE_LOCATION` granted *and*
  location services switched on. Without either, `attach()` succeeds, the
  subscribe starts, and nothing is ever discovered — no error anywhere. The
  app logs the permission state at startup for this reason.
- **The ESP must publish unsecured.** This app requests an open data path (no
  passphrase, no PMK). If the publisher has NAN security on, you get
  `NDP onUnavailable` with no further detail.
- **The ESP's NMI MAC is randomised at every start.** Only the OUI derives
  from the STA MAC. Do not pin anything to the peer's MAC or to its data-path
  link-local IPv6 address between boots.
- **NAN-Sync is an exclusive radio mode on the ESP.** It cannot hold a STA
  association at the same time, so the ESP will be off your Wi-Fi while it
  publishes.
- `targetSdkVersion` 29 against a compile SDK of 37 raises warnings XA1006 and
  XA4211 at build time. Both are expected and harmless here.

## Bench results so far

**The data path works.** Run 7, phone on-AP, ESP running a SOLICITED
publisher: discovery via a unicast solicited publish ACKed at DW+1, follow-up
exchange, NDP Request, and an IPv6 data path confirmed. The app's probe
reported `TCP connect to [fe80::…%aware_data0]:7 REFUSED => PASS` — a reset
proves the peer's stack answered — and the ESP logged
`NDP confirmed … Peer IPv6 FE80::281B:56FF:FE14:B87C`.

Three things that cost a lot of time, recorded so nobody repeats them:

- `Avail: missing a conditional or committed entry` is logged by the ESP even
  on runs where the data path **succeeds**. It is misleading, not fatal.
- The NDP Response straddles the end of the Discovery Window. The ESP takes
  about 16 ms to answer an NDP Request, and the DW is 16.384 ms, so whether
  setup works is close to a coin flip. In run 7 a burst at +16/+17 ms was not
  acknowledged and a retry at +15 ms in the following window was.
- Off-AP the phone's firmware never matched the ESP's group-addressed publish
  SDFs at all, across roughly 60 windows in two runs, despite correct service
  ID, cluster and timing. Unexplained; every success so far has been on-AP.

**The link then dropped about 20 s after it came up**, with the phone
reporting `eventDataPathTerminated` and the ESP logging nothing. The
`keepalive` extra exists to test whether an idle data path is simply torn
down: it runs a refused-TCP probe every two seconds, which is a SYN out and
an RST back, and logs how long the link survived.

Full write-up in `IDF-ISSUE-2-nan-ndp-availability.md` in the CircuitPython
research worktree.

## Untested

This has never been run against a device — there is no phone attached to the
build machine. It compiles, the manifest and the component name are verified
against the built APK with `aapt2 dump badging`, and the assembly store is
verified present. Runtime behaviour is unverified.
