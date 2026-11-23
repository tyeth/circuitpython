using System.Text;
using Android.App;
using Android.Content;
using Android.Locations;
using Android.Content.PM;
using Android.Net;
using Android.Net.Wifi.Aware;
using Android.OS;
using Android.Runtime;
using Android.Util;
using Android.Widget;
using Java.Net;

namespace NanTest;

[Activity(Name = "com.example.nantest.MainActivity", Label = "NanTest", MainLauncher = true, Exported = true)]
public class MainActivity : Activity
{
    const string Tag = "NanTest";

    readonly System.Diagnostics.Stopwatch _clock = new();
    TextView? _view;
    ScrollView? _scroll;

    WifiAwareManager? _aware;
    ConnectivityManager? _cm;
    WifiAwareSession? _session;
    DiscoverySession? _discovery;   // subscribe or publish session
    bool _ndpRequested;
    PeerHandle? _peer;
    ConnectivityManager.NetworkCallback? _netCb;

    string _role = "subscribe";
    string _service = "esp-nan-test";
    string _message = "Hello";
    string _reply = "Welcome";
    bool _wantNdp = true;
    bool _armNow = true;
    int _keepaliveMs = 2000;
    volatile bool _stop;
    int _probePort = 7;
    int _msgId = 1;
    long _dpUpAt = -1;

    // ---------- logging ----------

    void L(string msg)
    {
        var line = $"[{_clock.ElapsedMilliseconds,7} ms] {msg}";
        Log.Info(Tag, line);
        RunOnUiThread(() =>
        {
            _view?.Append(line + "\n");
            _scroll?.FullScroll(Android.Views.FocusSearchDirection.Down);
        });
    }

    static string Hex(byte[]? b) =>
        b is null || b.Length == 0 ? "(empty)" : BitConverter.ToString(b).Replace('-', ':').ToLowerInvariant();

    static string Utf8(byte[]? b)
    {
        if (b is null || b.Length == 0)
        {
            return "(empty)";
        }
        try
        {
            var s = Encoding.UTF8.GetString(b);
            foreach (var c in s)
            {
                if (char.IsControl(c) && c != '\n' && c != '\t')
                {
                    return "(not printable)";
                }
            }
            return $"\"{s}\"";
        }
        catch
        {
            return "(not utf-8)";
        }
    }

    // ---------- lifecycle ----------

    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        _clock.Start();

        AndroidEnvironment.UnhandledExceptionRaiser += (_, e) =>
        {
            Log.Error(Tag, $"UNHANDLED {e.Exception.GetType().Name}: {e.Exception}");
            e.Handled = true;
        };

        _view = new TextView(this) { Text = "" };
        _view.SetTextIsSelectable(true);
        _view.SetPadding(16, 16, 16, 16);
        _scroll = new ScrollView(this);
        _scroll.AddView(_view);
        SetContentView(_scroll);

        _role = (Intent?.GetStringExtra("role") ?? _role).Trim().ToLowerInvariant();
        if (_role != "subscribe" && _role != "publish")
        {
            L($"unknown role \"{_role}\"; falling back to subscribe.");
            _role = "subscribe";
        }
        _service = Intent?.GetStringExtra("service") ?? _service;
        _message = Intent?.GetStringExtra("msg") ?? _message;
        _reply = Intent?.GetStringExtra("reply") ?? _reply;
        _wantNdp = Intent?.GetBooleanExtra("ndp", true) ?? true;
        _armNow = Intent?.GetBooleanExtra("armnow", true) ?? true;
        _keepaliveMs = Intent?.GetIntExtra("keepalive", 2000) ?? 2000;
        _probePort = Intent?.GetIntExtra("port", 7) ?? 7;

        L($"start: role={_role} service=\"{_service}\" msg=\"{_message}\" reply=\"{_reply}\" " +
          $"ndp={_wantNdp} probe_port={_probePort} keepalive={_keepaliveMs}ms");
        L(_role == "publish"
            ? $"  publish mode: we are the NDP RESPONDER (armnow={_armNow})."
            : "  subscribe mode: we are the NDP INITIATOR.");
        L($"device: {Build.Manufacturer} {Build.Model}, Android {Build.VERSION.Release} (SDK {(int)Build.VERSION.SdkInt})");

        if (CheckSelfPermission(Android.Manifest.Permission.AccessFineLocation) != Permission.Granted)
        {
            L("FAIL: ACCESS_FINE_LOCATION not granted. Aware discovery will silently find nothing.");
            L("  grant with: adb shell pm grant com.example.nantest android.permission.ACCESS_FINE_LOCATION");
            RequestPermissions(new[] { Android.Manifest.Permission.AccessFineLocation }, 1);
            return;
        }
        L("ACCESS_FINE_LOCATION granted");

        if (GetSystemService(LocationService) is LocationManager lm)
        {
            var on = (int)Build.VERSION.SdkInt >= 28
                ? lm.IsLocationEnabled
                : lm.IsProviderEnabled(LocationManager.GpsProvider) ||
                  lm.IsProviderEnabled(LocationManager.NetworkProvider);
            L($"location services enabled = {on}");
            if (!on)
            {
                L("  WARNING: with location services off, Aware attaches and subscribes but");
                L("  never discovers anything, and reports no error. Turn them on.");
            }
        }

        Begin();
    }

    public override void OnRequestPermissionsResult(int requestCode, string[] permissions, Permission[] grantResults)
    {
        base.OnRequestPermissionsResult(requestCode, permissions, grantResults);
        if (grantResults.Length > 0 && grantResults[0] == Permission.Granted)
        {
            L("ACCESS_FINE_LOCATION granted at runtime");
            Begin();
        }
        else
        {
            L("FAIL: location permission refused; stopping.");
        }
    }

    void Begin()
    {
        _cm = (ConnectivityManager?)GetSystemService(ConnectivityService);
        _aware = (WifiAwareManager?)GetSystemService(WifiAwareService);

        if (_aware is null)
        {
            L("FAIL: no WifiAwareManager — this device has no Wi-Fi Aware service.");
            return;
        }

        L($"WifiAwareManager.isAvailable = {_aware.IsAvailable}");
        if (!_aware.IsAvailable)
        {
            L("FAIL: Aware reports unavailable. Usual causes: Wi-Fi off, location services off,");
            L("  or an active Wi-Fi Direct / hotspot session holding the radio.");
            return;
        }

        var ch = _aware.Characteristics;
        if (ch is not null)
        {
            L($"characteristics: maxServiceNameLength={ch.MaxServiceNameLength} " +
              $"maxServiceSpecificInfoLength={ch.MaxServiceSpecificInfoLength} " +
              $"maxMatchFilterLength={ch.MaxMatchFilterLength}");
        }

        L("attach()...");
        try
        {
            _aware.Attach(new AttachCb(this), null);
        }
        catch (Java.Lang.SecurityException e)
        {
            L($"FAIL: attach() threw SecurityException: {e.Message}");
            L("  The framework disagrees with checkSelfPermission. If you just ran");
            L("  `adb shell pm grant`, force-stop the app and start it again.");
        }
        catch (Java.Lang.Exception e)
        {
            L($"FAIL: attach() threw {e.GetType().Name}: {e.Message}");
        }
    }

    protected override void OnDestroy()
    {
        _stop = true;
        L("onDestroy: releasing");
        try
        {
            if (_netCb is not null)
            {
                _cm?.UnregisterNetworkCallback(_netCb);
            }
        }
        catch (Java.Lang.Exception e)
        {
            L($"  unregisterNetworkCallback: {e.Message}");
        }
        _discovery?.Close();
        _session?.Close();
        base.OnDestroy();
    }

    // ---------- attach ----------

    sealed class AttachCb(MainActivity a) : AttachCallback
    {
        public override void OnAttached(WifiAwareSession? session)
        {
            a.L("onAttached");
            if (session is null)
            {
                a.L("FAIL: onAttached handed a null session.");
                return;
            }
            a._session = session;
            a.StartDiscovery(session);
        }

        public override void OnAttachFailed()
        {
            a.L("FAIL: onAttachFailed");
        }
    }

    void StartDiscovery(WifiAwareSession session)
    {
        if (_role == "publish")
        {
            StartPublish(session);
        }
        else
        {
            StartSubscribe(session);
        }
    }

    void StartPublish(WifiAwareSession session)
    {
        var cfg = new PublishConfig.Builder()
            .SetServiceName(_service)!
            .SetPublishType(PublishType.Unsolicited)!
            .Build();

        if (cfg is null)
        {
            L("FAIL: PublishConfig.Builder().build() returned null.");
            return;
        }

        L($"publish(serviceName=\"{_service}\", PUBLISH_TYPE_UNSOLICITED)...");
        try
        {
            session.Publish(cfg, new DiscoveryCb(this), null);
        }
        catch (Java.Lang.SecurityException e)
        {
            L($"FAIL: publish() threw SecurityException: {e.Message}");
            L("  Grant ACCESS_FINE_LOCATION, switch location services on, then force-stop");
            L("  and relaunch.");
        }
        catch (Java.Lang.Exception e)
        {
            L($"FAIL: publish() threw {e.GetType().Name}: {e.Message}");
        }
    }

    void StartSubscribe(WifiAwareSession session)
    {
        var cfg = new SubscribeConfig.Builder()
            .SetServiceName(_service)!
            .SetSubscribeType(SubscribeType.Active)!
            .Build();

        if (cfg is null)
        {
            L("FAIL: SubscribeConfig.Builder().build() returned null.");
            return;
        }

        L($"subscribe(serviceName=\"{_service}\", SUBSCRIBE_TYPE_ACTIVE)...");
        try
        {
            session.Subscribe(cfg, new DiscoveryCb(this), null);
        }
        catch (Java.Lang.SecurityException e)
        {
            L($"FAIL: subscribe() threw SecurityException: {e.Message}");
            L("  Aware discovery is location-gated. Grant ACCESS_FINE_LOCATION, switch");
            L("  location services on, then force-stop and relaunch:");
            L("    adb shell pm grant com.example.nantest android.permission.ACCESS_FINE_LOCATION");
            L("    adb shell am force-stop com.example.nantest");
        }
        catch (Java.Lang.Exception e)
        {
            L($"FAIL: subscribe() threw {e.GetType().Name}: {e.Message}");
        }
    }

    // ---------- discovery ----------

    sealed class DiscoveryCb(MainActivity a) : DiscoverySessionCallback
    {
        public override void OnSubscribeStarted(SubscribeDiscoverySession? session)
        {
            a.L("onSubscribeStarted");
            a._discovery = session;
        }

        public override void OnPublishStarted(PublishDiscoverySession? session)
        {
            a.L("onPublishStarted");
            a._discovery = session;

            // Arm an ANY-PEER responder straight away rather than waiting for a
            // follow-up. An initiator whose follow-up never arrives still gets
            // its NDP answered; otherwise Android has no pending responder
            // request and rejects M1 with reason 2 (resource limitation),
            // which looks like a datapath fault but is just an unarmed app.
            if (session is not null && a._wantNdp && a._armNow)
            {
                a.ArmAnyPeerResponder(session);
            }
            else
            {
                a.L("  waiting for a follow-up before arming the responder.");
            }
        }

        public override void OnSessionConfigFailed()
        {
            a.L("FAIL: onSessionConfigFailed");
        }

        public override void OnSessionTerminated()
        {
            a.L("onSessionTerminated");
        }

        public override void OnServiceDiscovered(PeerHandle? peerHandle, byte[]? serviceSpecificInfo, IList<byte[]>? matchFilter)
        {
            if (peerHandle is null)
            {
                a.L("onServiceDiscovered with a null peer handle; ignoring.");
                return;
            }
            a.L($"onServiceDiscovered: peer={peerHandle}");
            a.L($"  serviceSpecificInfo: hex={Hex(serviceSpecificInfo)} utf8={Utf8(serviceSpecificInfo)}");
            if (matchFilter is null || matchFilter.Count == 0)
            {
                a.L("  matchFilter: (none)");
            }
            else
            {
                for (var i = 0; i < matchFilter.Count; i++)
                {
                    a.L($"  matchFilter[{i}]: hex={Hex(matchFilter[i])} utf8={Utf8(matchFilter[i])}");
                }
            }

            if (a._peer is not null)
            {
                a.L("  (already have a peer; ignoring this one)");
                return;
            }
            a._peer = peerHandle;
            a.SendFollowUp(peerHandle);
        }

        public override void OnMessageSendSucceeded(int messageId)
        {
            a.L($"onMessageSendSucceeded: id={messageId}");
            // In publish mode the datapath request is already in flight from
            // OnMessageReceived; only the initiator chains off its own send.
            if (a._role != "subscribe")
            {
                return;
            }
            if (a._wantNdp)
            {
                a.RequestDatapath();
            }
            else
            {
                a.L("ndp=false; stopping after the follow-up exchange.");
            }
        }

        public override void OnMessageSendFailed(int messageId)
        {
            a.L($"FAIL: onMessageSendFailed id={messageId}");
            if (a._role != "subscribe")
            {
                return;
            }
            // The datapath is worth trying even if the follow-up was dropped.
            if (a._wantNdp)
            {
                a.RequestDatapath();
            }
        }

        public override void OnMessageReceived(PeerHandle? peerHandle, byte[]? message)
        {
            a.L($"onMessageReceived from {peerHandle}: hex={Hex(message)} utf8={Utf8(message)}");

            if (a._role != "publish" || peerHandle is null)
            {
                return;
            }

            // Responder path: the ESP has found us and sent its follow-up. Reply,
            // then stand up the network request so we are listening when it calls
            // esp_wifi_nan_datapath_req() a few seconds later.
            a._peer = peerHandle;
            a.SendFollowUp(peerHandle, a._reply);
            if (a._wantNdp)
            {
                a.RequestDatapath();
            }
            else
            {
                a.L("ndp=false; replying only.");
            }
        }
    }

    void SendFollowUp(PeerHandle peer) => SendFollowUp(peer, _message);

    void SendFollowUp(PeerHandle peer, string text)
    {
        var id = _msgId++;
        var bytes = Encoding.UTF8.GetBytes(text);
        L($"sendMessage(id={id}, \"{text}\")...");
        _discovery?.SendMessage(peer, id, bytes);
    }

    // ---------- datapath ----------

    void ArmAnyPeerResponder(PublishDiscoverySession session)
    {
        if (_ndpRequested)
        {
            return;
        }

        NetworkSpecifier? spec = null;
        if ((int)Build.VERSION.SdkInt >= 29)
        {
            // The single-argument Builder takes a publish session and no peer
            // handle: an open responder that accepts an NDP from any peer.
            spec = new WifiAwareNetworkSpecifier.Builder(session).Build();
        }

        if (spec is null)
        {
            L("  any-peer responder needs API 29+; falling back to arming on the follow-up.");
            return;
        }

        var req = new NetworkRequest.Builder()
            .AddTransportType(TransportType.WifiAware)!
            .SetNetworkSpecifier(spec)!
            .Build();

        if (req is null)
        {
            L("FAIL: NetworkRequest.Builder().build() returned null.");
            return;
        }

        _ndpRequested = true;
        _netCb = new NetCb(this);
        try
        {
            _cm?.RequestNetwork(req, _netCb);
            L("requestNetwork as RESPONDER (ANY PEER, open) — armed before any follow-up.");
        }
        catch (Java.Lang.Exception e)
        {
            L($"FAIL: requestNetwork() threw {e.GetType().Name}: {e.Message}");
            _netCb = null;
            _ndpRequested = false;
        }
    }

    void RequestDatapath()
    {
        if (_discovery is null || _peer is null)
        {
            L("FAIL: no discovery session or peer; cannot request a datapath.");
            return;
        }

        if (_ndpRequested)
        {
            L("  (datapath already requested; not requesting again)");
            return;
        }
        _ndpRequested = true;

        NetworkSpecifier spec;
        if ((int)Build.VERSION.SdkInt >= 29)
        {
            // No passphrase and no PMK => open/unsecured NDP, matching the ESP
            // publisher with NAN security off.
            spec = new WifiAwareNetworkSpecifier.Builder(_discovery, _peer).Build();
            L($"requestNetwork as {(_role == "publish" ? "RESPONDER" : "INITIATOR")}: " +
              "WifiAwareNetworkSpecifier.Builder(...).build() — OPEN (no passphrase, no PMK)");
            if (_role == "publish")
            {
                L("  holding the request open; nothing happens until the ESP initiates.");
            }
        }
        else
        {
#pragma warning disable CA1422, CS0618
            spec = _discovery.CreateNetworkSpecifierOpen(_peer)!;
#pragma warning restore CA1422, CS0618
            L("requestNetwork: createNetworkSpecifierOpen() (pre-29 path)");
        }

        var req = new NetworkRequest.Builder()
            .AddTransportType(TransportType.WifiAware)!
            .SetNetworkSpecifier(spec)!
            .Build();

        if (req is null)
        {
            L("FAIL: NetworkRequest.Builder().build() returned null.");
            return;
        }

        _netCb = new NetCb(this);
        try
        {
            _cm?.RequestNetwork(req, _netCb);
        }
        catch (Java.Lang.Exception e)
        {
            L($"FAIL: requestNetwork() threw {e.GetType().Name}: {e.Message}");
            _netCb = null;
        }
    }

    sealed class NetCb(MainActivity a) : ConnectivityManager.NetworkCallback
    {
        public override void OnAvailable(Network network)
        {
            a._dpUpAt = a._clock.ElapsedMilliseconds;
            a.L($"NDP onAvailable: network={network}");
        }

        public override void OnCapabilitiesChanged(Network network, NetworkCapabilities caps)
        {
            a.L("NDP onCapabilitiesChanged");
            if (caps.TransportInfo is WifiAwareNetworkInfo info)
            {
                var addr = info.PeerIpv6Addr;
                a.L($"  peerIpv6Addr   = {addr?.HostAddress ?? "(null)"}");
                a.L($"  port           = {info.Port}");
                a.L($"  transportProto = {info.TransportProtocol}");
                if (addr is not null)
                {
                    a.Probe(network, addr, info.Port);
                }
            }
            else
            {
                a.L($"  transportInfo is {caps.TransportInfo?.GetType().Name ?? "null"}, not WifiAwareNetworkInfo");
            }
        }

        public override void OnLinkPropertiesChanged(Network network, LinkProperties props)
        {
            a.L($"NDP onLinkPropertiesChanged: iface={props.InterfaceName}");
            foreach (var la in props.LinkAddresses)
            {
                a.L($"  local address: {la}");
            }
        }

        public override void OnUnavailable()
        {
            a.L("FAIL: NDP onUnavailable — the datapath was never set up.");
            a.L("  If the ESP published with security on, Android's open request will be rejected here.");
        }

        public override void OnLost(Network network)
        {
            a._stop = true;
            var up = a._dpUpAt > 0 ? (a._clock.ElapsedMilliseconds - a._dpUpAt) : -1;
            a.L($"NDP onLost — the data path lasted {up} ms after it came up.");
        }
    }

    // ---------- reachability ----------

    void Probe(Network network, Inet6Address peer, int advertisedPort)
    {
        var port = advertisedPort > 0 ? advertisedPort : _probePort;

        if (_role == "publish")
        {
            L("probe: in publish mode a refusal or timeout is expected — the ESP runs no");
            L("  TCP listener. Its own pings over the datapath are the real proof.");
        }

        Task.Run(() =>
        {
            // isReachable() falls back to a TCP connect on port 7 when the app
            // may not send ICMP, so a false here is NOT proof the link is dead.
            // Logged for completeness, but the TCP result below is the oracle.
            try
            {
                var sw = System.Diagnostics.Stopwatch.StartNew();
                var ok = peer.IsReachable(2000);
                sw.Stop();
                L($"probe: isReachable(2000) = {ok} after {sw.ElapsedMilliseconds} ms (inconclusive if false)");
            }
            catch (Java.Lang.Exception e)
            {
                L($"probe: isReachable threw {e.GetType().Name}: {e.Message}");
            }

            // The real test. A refused connection is a SUCCESS for our purposes:
            // an RST means the peer's IP stack received the SYN and answered, so
            // the datapath carries traffic. Only a timeout means no link.
            try
            {
                var sw = System.Diagnostics.Stopwatch.StartNew();
                var sock = network.SocketFactory!.CreateSocket()!;
                sock.Connect(new InetSocketAddress(peer, port), 3000);
                sw.Stop();
                L($"probe: TCP connect to [{peer.HostAddress}]:{port} SUCCEEDED in {sw.ElapsedMilliseconds} ms");
                L("  => datapath carries traffic, and something is listening on that port.");
                sock.Close();
            }
            catch (Java.Lang.Exception e)
            {
                var m = e.Message ?? "";
                var refused = m.Contains("ECONNREFUSED", StringComparison.OrdinalIgnoreCase)
                              || m.Contains("refused", StringComparison.OrdinalIgnoreCase);
                if (refused)
                {
                    L($"probe: TCP connect to [{peer.HostAddress}]:{port} REFUSED — {m}");
                    L("  => PASS. The peer answered with a reset, so the IPv6 datapath is up.");
                }
                else
                {
                    L($"probe: TCP connect to [{peer.HostAddress}]:{port} failed — {e.GetType().Name}: {m}");
                    L("  => no answer. Either the datapath is not carrying traffic, or the peer is not up yet.");
                }
            }

            if (_keepaliveMs <= 0)
            {
                L("probe finished, keepalive disabled. Session stays open until destroyed.");
                return;
            }

            // Does the data path survive only while carrying traffic? A refused
            // TCP connect is still a SYN out and an RST back, so it exercises
            // the link in both directions without needing a listener on the peer.
            L($"keepalive: probing every {_keepaliveMs} ms to see whether traffic keeps the link up.");
            var n = 0;
            while (!_stop)
            {
                Java.Lang.Thread.Sleep(_keepaliveMs);
                if (_stop)
                {
                    break;
                }
                n++;
                var up = _dpUpAt > 0 ? _clock.ElapsedMilliseconds - _dpUpAt : -1;
                try
                {
                    var sk = network.SocketFactory!.CreateSocket()!;
                    sk.Connect(new InetSocketAddress(peer, port), 1500);
                    sk.Close();
                    L($"keepalive #{n} at t+{up} ms: connected");
                }
                catch (Java.Lang.Exception e)
                {
                    var m = e.Message ?? "";
                    var ok = m.Contains("ECONNREFUSED", StringComparison.OrdinalIgnoreCase)
                             || m.Contains("refused", StringComparison.OrdinalIgnoreCase);
                    L($"keepalive #{n} at t+{up} ms: {(ok ? "refused — link alive" : "FAILED — " + e.GetType().Name + ": " + m)}");
                }
            }
            L("keepalive stopped.");
        });
    }
}
