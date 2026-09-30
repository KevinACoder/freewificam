#!/usr/bin/env python3
"""Deterministic TCP forwarder for the freewebcam gdb stub.

The oslab hub's raw port queues a NEW client's first bytes and re-delivers
them after its serial path attaches, so a client that fires its handshake
within the first millisecond (gdb does) sees every frame twice and the
reply stream desynchronizes. This forwarder takes the race hit itself:

  - at process start it opens the hub connection and plays one throwaway
    qSupported round trip through the attach window;
  - from then on it pipes gdb <-> hub BYTE-FOR-BYTE, no parsing, no
    filtering, no reconnect logic.

    python3 tools/gdb/rsp_forward.py [local_port]     (default 18000)
    gdb:  target remote 127.0.0.1:<local_port>
"""

import socket
import sys
import threading
import time

HUB = ("192.168.0.18", 18000)
LOCAL_PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18000


def csum(p):
    return sum(p.encode()) & 0xff


def pkt(p):
    return f"${p}#{csum(p):02x}".encode()


def pump(src, dst):
    try:
        while True:
            data = src.recv(4096)
            if not data:
                break
            dst.sendall(data)
    except OSError:
        pass
    try:
        src.shutdown(socket.SHUT_RD)
    except OSError:
        pass
    try:
        dst.shutdown(socket.SHUT_WR)
    except OSError:
        pass


hub = socket.create_connection(HUB, timeout=10)
hub.settimeout(2.0)

# Warm-up: one throwaway round trip through the hub's attach window. The
# stub answers it from a synthetic session; every later frame (gdb's whole
# handshake) flows through that already-attached session.
hub.sendall(pkt("qSupported:swbreak+"))
buf = b""
deadline = time.time() + 5.0
while b"PacketSize" not in buf and time.time() < deadline:
    try:
        buf += hub.recv(4096)
    except socket.timeout:
        pass
print(f"[fwd] warm-up reply: {buf[:60].decode('utf-8', 'replace')!r}", flush=True)
hub.settimeout(None)

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", LOCAL_PORT))
srv.listen(1)
print(f"[fwd] hub warm; listening on 127.0.0.1:{LOCAL_PORT}", flush=True)

while True:
    conn, _ = srv.accept()
    print("[fwd] gdb connected", flush=True)
    t = threading.Thread(target=pump, args=(conn, hub), daemon=True)
    t.start()
    pump(hub, conn)
    t.join(timeout=2)
    print("[fwd] gdb disconnected", flush=True)
