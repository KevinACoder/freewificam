#!/usr/bin/env python3
"""One board round for the RTOS bin platform: stage, boot, capture, judge.

The entry is one command per boot.  It holds one oslab MCP session open for
the whole round (the lab's one-shot client loses the serial cursor between
calls), stages the image under the deploy name the lab's `rtos` profile
expects, boots it with a power cycle, captures the full serial log, optionally
drives the peer (the Raspberry Pi) at the moment the image's network case says
it is ready, and ends by judging the log and leaving the board powered off.

Evidence layout, one directory per boot:

    <out>/<boot>/
        command.txt        the exact invocation
        fingerprint.txt    size + sha256 + crc32 (host, staged), build tag
        serial.log         the full capture, nothing elided
        boot.json          boot_id and the staging/boot answers
        peer-*.log         the peer side, when it ran
        judge.json         the verdict
        RESULT.md          the same verdict, readable

Usage:
    tools/rtos/board-round.py --backend freertos \
        --out runs/L3-rtos/2026-10-24-rtos-bin --boot boot1 --peer

    tools/rtos/judge.py runs/.../boot1/serial.log      # re-judge a capture
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# The lab's MCP entry is environment, not repository: OSLAB_SESSION names the
# line-protocol client (the lab ships one), OSLAB_PYTHON the interpreter that
# has the `mcp` package.  Keeping them out of the tree is deliberate - this
# repository carries no local lab paths.
SESSION = Path(os.environ["OSLAB_SESSION"]).expanduser() if os.environ.get("OSLAB_SESSION") else None
LAB_PYTHON = Path(os.environ["OSLAB_PYTHON"]).expanduser() if os.environ.get("OSLAB_PYTHON") else None
IMAGES = REPO / "rtos" / "build" / "build"

PEER = "pi@192.168.0.17"
BOARD_IP = "192.168.0.200"
FINAL_ANCHOR = "rtos: result"
READY_ANCHOR = "net: iperf ready"


class Session:
    """One persistent oslab MCP session over the lab's line protocol."""

    def __init__(self) -> None:
        if SESSION is None or not SESSION.exists():
            raise SystemExit(
                "board-round: set OSLAB_SESSION to the lab's oslab-session.py"
            )
        if LAB_PYTHON is None or not LAB_PYTHON.exists():
            raise SystemExit("board-round: set OSLAB_PYTHON to the lab interpreter")
        self.proc = subprocess.Popen(
            [str(LAB_PYTHON), str(SESSION)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        ready = self.proc.stdout.readline().strip()
        if ready != "READY":
            self.proc.kill()
            raise SystemExit(f"board-round: session did not start (got {ready!r})")

    def call(self, tool: str, **args):
        request = json.dumps({"tool": tool, "args": args}) + "\n"
        self.proc.stdin.write(request)
        self.proc.stdin.flush()
        line = self.proc.stdout.readline()
        if not line:
            raise SystemExit(f"board-round: session died during {tool}")
        reply = json.loads(line)
        if reply.get("error"):
            raise SystemExit(f"board-round: {tool}: {reply['error']}")
        if reply.get("isError"):
            raise SystemExit(f"board-round: {tool} failed: {reply.get('result')}")
        return reply.get("result", {})

    def close(self) -> None:
        try:
            self.proc.stdin.write(json.dumps({"tool": "exit"}) + "\n")
            self.proc.stdin.flush()
        except (BrokenPipeError, ValueError):
            pass
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()


def crc32_file(path: Path) -> int:
    crc = 0
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            crc = zlib.crc32(chunk, crc)
    return crc & 0xFFFFFFFF


def ssh(command: str, timeout: int = 60) -> str:
    result = subprocess.run(
        ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", PEER, command],
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    return (result.stdout or "") + (result.stderr or "")


def peer_start_server() -> str:
    # `pkill -f 'iperf -s'` would match this very command line and kill the
    # shell running it; the exact-name form does not.  The start is confirmed
    # by the listening socket, not by the process existing: iperf2 exits on a
    # bind failure (a port still in use by the previous round), and a process
    # that died after `pgrep` would make the board's client see a reset.
    return ssh(
        "pkill -x iperf >/dev/null 2>&1 || true; "
        "i=0; while [ $i -lt 20 ] && ss -ltn 2>/dev/null | grep -q ':5001 '; "
        "do i=$((i+1)); sleep 0.25; done; "
        "(setsid iperf -s -p 5001 > /tmp/iperf2-server.log 2>&1 </dev/null &); "
        "i=0; while [ $i -lt 20 ]; do "
        "if ss -ltn 2>/dev/null | grep -q ':5001 '; then echo server-listening; exit 0; fi; "
        "i=$((i+1)); sleep 0.25; done; "
        "echo server-missing; tail -3 /tmp/iperf2-server.log"
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--backend", required=True, choices=["freertos", "threadx"])
    ap.add_argument("--out", required=True, type=Path, help="run directory")
    ap.add_argument("--boot", required=True, help="boot name, e.g. boot1")
    ap.add_argument("--peer", action="store_true", help="drive the Raspberry Pi peer")
    ap.add_argument("--timeout", type=int, default=300, help="capture window, seconds")
    ap.add_argument("--no-power-off", action="store_true")
    args = ap.parse_args()

    image = IMAGES / f"rtos-{args.backend}.bin"
    if not image.exists():
        raise SystemExit(f"board-round: no image at {image} (run make -C rtos/build)")

    out = args.out.resolve() / args.boot
    out.mkdir(parents=True, exist_ok=True)
    log_path = out / "serial.log"

    (out / "command.txt").write_text(
        " ".join([sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]]) + "\n"
    )

    host_crc = crc32_file(image)
    host_sha = subprocess.run(
        ["sha256sum", str(image)], capture_output=True, text=True
    ).stdout.split()[0]
    fingerprint = [
        f"image {image.name}",
        f"size {image.stat().st_size}",
        f"sha256 {host_sha}",
        f"crc32 {host_crc:08x}",
    ]

    stage_dir = out / "stage"
    stage_dir.mkdir(exist_ok=True)
    staged = stage_dir / "rtos.bin"
    shutil.copyfile(image, staged)

    session = Session()
    boot = {}
    try:
        status = session.call("service_status")
        (out / "service-status.json").write_text(json.dumps(status, indent=2) + "\n")

        if args.peer:
            (out / "peer-server.log").write_text(peer_start_server() + "\n")

        staged_reply = session.call("tftp_stage", paths=[str(staged)])
        (out / "staged.json").write_text(json.dumps(staged_reply, indent=2) + "\n")
        staged_crc = None
        for item in staged_reply.get("files", []):
            if str(item.get("name", "")).endswith("rtos.bin"):
                staged_crc = item.get("crc32")
        fingerprint.append(f"staged_crc32 {staged_crc}")
        if staged_crc and staged_crc.lower() != f"{host_crc:08x}":
            raise SystemExit("board-round: staged crc32 differs from the host image")

        cursor = session.call("serial_read", tail=1).get("cursor", 0)
        boot = session.call(
            "boot_os",
            os="rtos",
            power_cycle=True,
            success_pattern="rtos: banner",
            timeout_s=120,
        )
        (out / "boot.json").write_text(json.dumps(boot, indent=2) + "\n")
        fingerprint.append(f"boot_id {boot.get('boot_id')}")
        (out / "fingerprint.txt").write_text("\n".join(fingerprint) + "\n")

        started = time.time()
        final_seen = None
        peer_done = False
        lines: list[str] = []
        with log_path.open("w") as log:
            while True:
                chunk = session.call("serial_read", since_cursor=cursor)
                cursor = chunk.get("cursor", cursor)
                new = chunk.get("lines", [])
                for line in new:
                    log.write(line.get("text", "") + "\n")
                log.flush()
                lines.extend(line.get("text", "") for line in new)

                text = "\n".join(lines)
                if args.peer and not peer_done and READY_ANCHOR in text:
                    peer_done = True
                    (out / "peer-ping.log").write_text(
                        ssh(
                            "rm -f /tmp/icmp.txt; "
                            "(sudo -n timeout 8 tcpdump -i eth0 -nn icmp > /tmp/icmp.txt 2>&1 &); "
                            "sleep 0.4; "
                            f"ping -c 10 -i 0.2 -W 1 {BOARD_IP}; "
                            "sleep 0.6; "
                            "echo '--- icmp on the wire ---'; cat /tmp/icmp.txt",
                            timeout=40,
                        )
                    )
                    (out / "peer-iperf-down.log").write_text(
                        ssh(f"iperf -c {BOARD_IP} -p 5001 -t 10", timeout=60)
                    )
                    (out / "peer.log").write_text("peer steps done\n")

                if final_seen is None and FINAL_ANCHOR in text:
                    final_seen = time.time()

                if final_seen is not None and time.time() - final_seen > 3:
                    break
                if time.time() - started > args.timeout:
                    (out / "TIMEOUT").write_text(
                        f"no final anchor within {args.timeout}s\n"
                    )
                    break
                time.sleep(0.5)
    finally:
        session.close()
        if not args.no_power_off:
            end = Session()
            try:
                end.call("power_off", confirm=True)
                (out / "power_off.json").write_text(
                    json.dumps(end.call("power_status"), indent=2) + "\n"
                )
            finally:
                end.close()

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import judge as judge_mod

    expect = {"backend": args.backend}
    text = log_path.read_text(errors="replace")
    verdict = judge_mod.judge(text, expect)
    (out / "judge.json").write_text(json.dumps(verdict, indent=2) + "\n")
    (out / "RESULT.md").write_text(judge_mod.render(verdict, f"{args.boot} {args.backend}"))
    print(judge_mod.render(verdict, f"{args.boot} {args.backend}"), end="")
    print(f"board-round: evidence in {out}")
    return 0 if verdict["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())