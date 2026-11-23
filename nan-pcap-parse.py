import struct, sys

def frames(path):
    d = open(path, 'rb').read()
    if len(d) < 24:
        return
    magic = d[:4]
    if magic == b'\xd4\xc3\xb2\xa1':
        endi, usec = '<', True
    elif magic == b'\xa1\xb2\xc3\xd4':
        endi, usec = '>', True
    elif magic == b'\x4d\x3c\xb2\xa1':
        endi, usec = '<', False
    elif magic == b'\xa1\xb2\x3c\x4d':
        endi, usec = '>', False
    else:
        print("unknown magic", magic.hex(), file=sys.stderr); return
    link = struct.unpack(endi + 'I', d[20:24])[0]
    off = 24
    while off + 16 <= len(d):
        ts, tu, incl, orig = struct.unpack(endi + 'IIII', d[off:off + 16])
        off += 16
        pkt = d[off:off + incl]
        off += incl
        if len(pkt) < incl:
            break
        t = ts + (tu / 1e6 if usec else tu / 1e9)
        body = pkt
        rate = None
        if link == 127 and len(pkt) >= 8:          # radiotap
            rtlen = struct.unpack('<H', pkt[2:4])[0]
            present = struct.unpack('<I', pkt[4:8])[0]
            p = 8
            while present & (1 << 31) and p + 4 <= len(pkt):   # ext bitmaps
                present2 = struct.unpack('<I', pkt[p:p + 4])[0]
                p += 4
                present = present2
            pres0 = struct.unpack('<I', pkt[4:8])[0]
            q = p
            if pres0 & (1 << 0):                    # TSFT, 8 align 8
                q = (q + 7) & ~7
                q += 8
            if pres0 & (1 << 1):                    # Flags
                q += 1
            if pres0 & (1 << 2) and q < len(pkt):   # Rate, 0.5 Mbps units
                rate = pkt[q] / 2.0
            body = pkt[rtlen:]
        if len(body) < 2:
            continue
        fc = struct.unpack('<H', body[0:2])[0]
        ftype = (fc >> 2) & 3
        sub = (fc >> 4) & 0xF
        a1 = a2 = a3 = None
        if ftype == 1:                              # control
            if len(body) >= 10:
                a1 = body[4:10].hex(':')
        else:
            if len(body) >= 10: a1 = body[4:10].hex(':')
            if len(body) >= 16: a2 = body[10:16].hex(':')
            if len(body) >= 22: a3 = body[16:22].hex(':')
        yield dict(t=t, ftype=ftype, sub=sub, a1=a1, a2=a2, a3=a3,
                   rate=rate, body=body)

NAN_OUI = bytes.fromhex('506f9a')

def nan_info(f):
    """Return a short NAF description, or None."""
    if f['ftype'] != 0 or f['sub'] != 13:
        return None
    b = f['body']
    if len(b) < 24 + 6:
        return None
    p = b[24:]
    if p[0] != 0x04 or p[1] != 0x09:                # public action / vendor
        return None
    if p[2:5] != NAN_OUI:
        return None
    oui_type = p[5]
    if oui_type != 0x13:
        return f"NAF oui_type=0x{oui_type:02x}"
    # NAN attributes follow
    attrs = []
    q = p[6:]
    i = 0
    while i + 3 <= len(q):
        aid = q[i]
        alen = struct.unpack('<H', q[i + 1:i + 3])[0]
        attrs.append((aid, alen, q[i + 3:i + 3 + alen]))
        i += 3 + alen
    return attrs
