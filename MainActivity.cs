using System.Text;
using Android.App;
using Android.Content;
using Android.Content.PM;
using Android.Net;
using Android.Net.Wifi.Aware;
using Android.OS;
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
    SubscribeDiscoverySession? _subscribe;
    PeerHandle? _peer;
    ConnectivityManager.NetworkCallback? _netCb;

    string _service = "esp-nan-test";
    string _message = "Hello";
    bool _wantNdp = true;
    int _probePort = 7;
    int _msgId = 1;

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

        _view = new TextView(this) { Text = "" };
        _view.SetTextIsSelectable(true);
        _view.SetPadding(16, 16, 16, 16);
        _scroll = new ScrollView(this);
        _scroll.AddView(_view);
        SetContentView(_scroll);

        _service = Intent?.GetStringExtra("service") ?? _service;
        _message = Intent?.GetStringExtra("msg") ?? _message;
        _wantNdp = Intent?.GetBooleanExtra("ndp", true) ?? true;
        _probePort = Intent?.GetIntExtra("port", 7) ?? 7;

        L($"start: service=\"{_service}\" msg=\"{_message}\" ndp={_wantNdp} probe_port={_probePort}");
        L($"device: {Build.Manufacturer} {Build.Model}, Android {Build.VERSION.Release} (SDK {(int)Build.VERSION.SdkInt})");

        if (CheckSelfPermission(Android.Manifest.Permission.AccessFineLocation) != Permission.Granted)
        {
            L("FAIL: ACCESS_FINE_LOCATION not granted. Aware discovery will silently find nothing.");
            L("  grant with: adb shell pm grant com.example.nantest android.permission.ACCESS_FINE_LOCATION");
            RequestPermissions(new[] { Android.Manifest.Permission.AccessFineLocation }, 1);
            return;
        }
        L("ACCESS_FINE_LOCATION granted");

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
        _aware.Attach(new AttachCb(this), null);
    }

    protected override void OnDestroy()
    {
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
        _subscribe?.Close();
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
            a.StartSubscribe(session);
        }

        public override void OnAttachFailed()
        {
            a.L("FAIL: onAttachFailed");
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
        session.Subscribe(cfg, new DiscoveryCb(this), null);
    }

    // ---------- discovery ----------

    sealed class DiscoveryCb(MainActivity a) : DiscoverySessionCallback
    {
        public override void OnSubscribeStarted(SubscribeDiscoverySession? session)
        {
            a.L("onSubscribeStarted");
            a._subscribe = session;
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
            // The datapath is worth trying even if the follow-up was dropped.
            if (a._wantNdp)
            {
                a.RequestDatapath();
            }
        }

        public override void OnMessageReceived(PeerHandle? peerHandle, byte[]? message)
        {
            a.L($"onMessageReceived from {peerHandle}: hex={Hex(message)} utf8={Utf8(message)}");
        }
    }

    void SendFollowUp(PeerHandle peer)
    {
        var id = _msgId++;
        var bytes = Encoding.UTF8.GetBytes(_message);
        L($"sendMessage(id={id}, \"{_message}\")...");
        _subscribe?.SendMessage(peer, id, bytes);
    }

    // ---------- datapath ----------

    void RequestDatapath()
    {
        if (_subscribe is null || _peer is null)
        {
            L("FAIL: no subscribe session or peer; cannot request a datapath.");
            return;
        }

        NetworkSpecifier spec;
        if ((int)Build.VERSION.SdkInt >= 29)
        {
            // No passphrase and no PMK => open/unsecured NDP, matching the ESP
            // publisher with NAN security off.
            spec = new WifiAwareNetworkSpecifier.Builder(_subscribe, _peer).Build();
            L("requestNetwork: WifiAwareNetworkSpecifier.Builder(...).build() — OPEN (no passphrase, no PMK)");
        }
        else
        {
#pragma warning disable CA1422, CS0618
            spec = _subscribe.CreateNetworkSpecifierOpen(_peer)!;
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
        _cm?.RequestNetwork(req, _netCb);
    }

    sealed class NetCb(MainActivity a) : ConnectivityManager.NetworkCallback
    {
        public override void OnAvailable(Network network)
        {
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
            a.L("NDP onLost");
        }
    }

    // ---------- reachability ----------

    void Probe(Network network, Inet6Address peer, int advertisedPort)
    {
        var port = advertisedPort > 0 ? advertisedPort : _probePort;

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

            L("probe finished. Session stays open until the activity is destroyed.");
        });
    }
}
