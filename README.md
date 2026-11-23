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

No APK is committed to this branch; build one first (see above), then:

```
adb install -r bin/Debug/net11.0-android/com.example.nantest-Signed.apk
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
| `service` | string | `esp-nan-test` | NAN service name to subscribe to |
| `msg` | string | `Hello` | follow-up message body |
| `ndp` | bool | `true` | request a data path after discovery |
| `port` | int | `7` | TCP port to probe when the peer advertises none |

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

## Untested

This has never been run against a device — there is no phone attached to the
build machine. It compiles, the manifest and the component name are verified
against the built APK with `aapt2 dump badging`, and the assembly store is
verified present. Runtime behaviour is unverified.
