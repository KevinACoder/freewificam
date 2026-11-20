#!/usr/bin/env python3
"""F429 stream receiver on the Pi 4B.

Listens on the two TCP ports the F429 opens (5000 video, 5001 audio), parses
the 24-byte FWC header, checks the payload CRC, and writes the raw streams to
a per-run directory.  In FIFO mode it also feeds the publisher's named pipes;
a bounded queue per stream means a stalled ffmpeg never blocks the network
reader.

Usage:
  receiver.py --out DIR [--mode save|video|av] \
              [--video-port 5000] [--audio-port 5001] \
              [--video-fifo PATH] [--audio-fifo PATH] \
              [--stats-json FILE]
"""

from __future__ import annotations

import argparse
import json
import os
import queue
import signal
import socket
import struct
import sys
import threading
import time
import zlib

MAGIC = 0x46574331
VERSION = 1
TYPE_VIDEO = 1
TYPE_AUDIO = 2
HEADER = struct.Struct("<IBBHIIII")
MAX_PAYLOAD = 128 * 1024

stop = threading.Event()


def log(msg: str) -> None:
    print(msg, flush=True)


class FifoWriter(threading.Thread):
    """Own the write end of one FIFO; never block the network reader."""

    def __init__(self, path: str, name: str, depth: int):
        super().__init__(daemon=True)
        self.path = path
        self.name = name
        self.q: queue.Queue[bytes] = queue.Queue(maxsize=depth)
        self.dropped = 0
        self.written = 0
        self.fd = -1

    def put(self, data: bytes) -> None:
        while True:
            try:
                self.q.put_nowait(data)
                return
            except queue.Full:
                try:
                    self.q.get_nowait()
                    self.dropped += 1
                except queue.Empty:
                    pass

    def run(self) -> None:
        if not self.path:
            return
        try:
            if not os.path.exists(self.path):
                os.mkfifo(self.path)
        except OSError as exc:
            log(f"rx: fifo {self.name} mkfifo failed: {exc}")
            return
        while not stop.is_set():
            try:
                if self.fd < 0:
                    self.fd = os.open(self.path, os.O_WRONLY)
                    log(f"rx: fifo {self.name} writer open")
                data = self.q.get(timeout=0.5)
                view = memoryview(data)
                while view:
                    n = os.write(self.fd, view)
                    view = view[n:]
                self.written += len(data)
            except queue.Empty:
                continue
            except (BrokenPipeError, OSError) as exc:
                if self.fd >= 0:
                    try:
                        os.close(self.fd)
                    except OSError:
                        pass
                    self.fd = -1
                if not stop.is_set():
                    log(f"rx: fifo {self.name} writer reopen: {exc}")
                time.sleep(0.2)
        if self.fd >= 0:
            try:
                os.close(self.fd)
            except OSError:
                pass


class Stats:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.video_frames = 0
        self.video_bytes = 0
        self.audio_blocks = 0
        self.audio_bytes = 0
        self.crc_err = 0
        self.bad_header = 0
        self.short = 0
        self.jpeg_bad = 0
        self.connections = {"video": 0, "audio": 0}

    def as_dict(self) -> dict:
        with self.lock:
            return {
                "video_frames": self.video_frames,
                "video_bytes": self.video_bytes,
                "audio_blocks": self.audio_blocks,
                "audio_bytes": self.audio_bytes,
                "crc_err": self.crc_err,
                "bad_header": self.bad_header,
                "short": self.short,
                "jpeg_bad": self.jpeg_bad,
                "connections": dict(self.connections),
            }


def read_exact(sock: socket.socket, n: int) -> bytes | None:
    buf = bytearray()
    while len(buf) < n:
        try:
            chunk = sock.recv(n - len(buf))
        except (ConnectionError, OSError):
            return None
        if not chunk:
            return None
        buf += chunk
    return bytes(buf)


def serve(port: int, kind: int, out_dir: str, stats: Stats,
          fifo: FifoWriter | None) -> None:
    label = "video" if kind == TYPE_VIDEO else "audio"
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", port))
    srv.listen(2)
    srv.settimeout(0.5)
    log(f"rx: listen {label} port={port}")

    video_fp = open(os.path.join(out_dir, "video.mjpg"), "ab", buffering=0) \
        if kind == TYPE_VIDEO else None
    audio_fp = open(os.path.join(out_dir, "audio.pcm"), "ab", buffering=0) \
        if kind == TYPE_AUDIO else None

    while not stop.is_set():
        try:
            conn, addr = srv.accept()
        except socket.timeout:
            continue
        except OSError:
            break
        with stats.lock:
            stats.connections[label] += 1
        log(f"rx: conn {label} from={addr[0]}")
        conn.settimeout(5.0)
        local_frames = 0
        local_count = 0
        local_bytes = 0
        try:
            while not stop.is_set():
                hdr = read_exact(conn, HEADER.size)
                if hdr is None:
                    break
                magic, ver, typ, flags, length, ts, seq, crc = HEADER.unpack(hdr)
                if (magic != MAGIC or ver != VERSION or typ != kind or
                        length > MAX_PAYLOAD or length == 0):
                    with stats.lock:
                        stats.bad_header += 1
                    log(f"rx: bad header {label} magic={magic:08x} ver={ver} "
                        f"type={typ} len={length}")
                    break
                payload = read_exact(conn, length)
                if payload is None:
                    with stats.lock:
                        stats.short += 1
                    break
                got = zlib.crc32(payload) & 0xffffffff
                if got != crc:
                    with stats.lock:
                        stats.crc_err += 1
                    log(f"rx: crc mismatch {label} seq={seq} got={got:08x} "
                        f"want={crc:08x}")
                    break
                if kind == TYPE_VIDEO:
                    bad = not (payload[:2] == b"\xff\xd8" and
                               payload[-2:] == b"\xff\xd9")
                    if bad:
                        with stats.lock:
                            stats.jpeg_bad += 1
                    with stats.lock:
                        stats.video_frames += 1
                        stats.video_bytes += length
                    if video_fp is not None:
                        video_fp.write(payload)
                    if local_frames == 0:
                        first = os.path.join(out_dir, f"frame-{seq:06d}.jpg")
                        with open(first, "wb") as fp:
                            fp.write(payload)
                    if fifo is not None:
                        fifo.put(payload)
                    local_frames += 1
                    local_count += 1
                else:
                    with stats.lock:
                        stats.audio_blocks += 1
                        stats.audio_bytes += length
                    if audio_fp is not None:
                        audio_fp.write(payload)
                    if fifo is not None:
                        fifo.put(payload)
                    local_count += 1
                local_bytes += length
                if local_count <= 2 or (local_count % 50) == 0:
                    log(f"rx: {label} seq={seq} len={length} crc={crc:08x} "
                        f"ts={ts} frames={local_frames} bytes={local_bytes}")
        except socket.timeout:
            pass
        except (ConnectionError, OSError) as exc:
            log(f"rx: {label} connection error: {exc}")
        finally:
            try:
                conn.close()
            except OSError:
                pass
        log(f"rx: {label} connection closed frames={local_frames} "
            f"bytes={local_bytes}")

    srv.close()
    if video_fp is not None:
        video_fp.close()
    if audio_fp is not None:
        audio_fp.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--mode", choices=("save", "video", "av"), default="save")
    ap.add_argument("--video-port", type=int, default=5000)
    ap.add_argument("--audio-port", type=int, default=5001)
    ap.add_argument("--video-fifo", default="")
    ap.add_argument("--audio-fifo", default="")
    ap.add_argument("--stats-json", default="")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    stats = Stats()
    fifo_v = FifoWriter(args.video_fifo, "video", 32) if args.video_fifo else None
    fifo_a = FifoWriter(args.audio_fifo, "audio", 4096) if args.audio_fifo else None
    if fifo_v:
        fifo_v.start()
    if fifo_a:
        fifo_a.start()

    threads = [threading.Thread(target=serve, daemon=True,
                                args=(args.video_port, TYPE_VIDEO,
                                      args.out, stats, fifo_v))]
    if args.mode == "av":
        threads.append(threading.Thread(target=serve, daemon=True,
                                        args=(args.audio_port, TYPE_AUDIO,
                                              args.out, stats, fifo_a)))
    for t in threads:
        t.start()

    def on_signal(_sig, _frm):
        stop.set()

    signal.signal(signal.SIGTERM, on_signal)
    signal.signal(signal.SIGINT, on_signal)

    while not stop.is_set():
        time.sleep(0.2)

    snapshot = stats.as_dict()
    if args.stats_json:
        with open(args.stats_json, "w", encoding="utf-8") as fp:
            json.dump(snapshot, fp, indent=2)
    if fifo_v:
        snapshot["fifo_video_dropped"] = fifo_v.dropped
    if fifo_a:
        snapshot["fifo_audio_dropped"] = fifo_a.dropped
    log("rx: stats " + json.dumps(snapshot, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
