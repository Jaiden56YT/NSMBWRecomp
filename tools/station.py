#!/usr/bin/env python3
"""Drive the NSMBW test station on the Switch over Wi-Fi (see tools/station/source/main.c).

  tools/station.py ping
  tools/station.py ls /switch/nsmbw2
  tools/station.py put LOCAL /switch/nsmbw2/NAME
  tools/station.py get /switch/nsmbw2/NAME LOCAL
  tools/station.py run                 # start the game as it is on the card
  tools/station.py stop                # station mode off, open the normal menu
  tools/station.py cycle --nro dist/nsmbw2/nsmbw2.nro --seconds 900 --out logs/hw/run30 [--env K=V ...]

`cycle` uploads the build and an env.txt carrying NSMBW_STATION_RUN_SECONDS, clears old logs and
profiles, starts the game, waits for it to restart back into the station, then downloads the logs
and profiles into --out. Host: --host or $NSMBW_SWITCH_HOST (default 192.168.1.193).
"""
import argparse
import os
import socket
import sys
import time

PORT = 5577
GAME_DIR = "/switch/nsmbw2"


def connect(host, timeout=10):
    s = socket.create_connection((host, PORT), timeout=timeout)
    s.settimeout(120)
    return s


def read_line(s):
    out = bytearray()
    while True:
        c = s.recv(1)
        if not c:
            raise ConnectionError("connection closed")
        if c == b"\n":
            return out.decode(errors="replace")
        out += c


def command(host, line, payload=None, timeout=10):
    with connect(host, timeout) as s:
        s.sendall(line.encode() + b"\n")
        if payload is not None:
            s.sendall(payload)
        return s, read_line(s)


def ping(host, timeout=5):
    try:
        with connect(host, timeout) as s:
            s.sendall(b"PING\n")
            return read_line(s)
    except OSError:
        return None


def put(host, data, remote):
    with connect(host) as s:
        s.sendall(f"PUT {remote} {len(data)}\n".encode())
        # Python's sendall timeout covers the whole call; send in pieces so only a real stall fails.
        view = memoryview(data)
        step = 1 << 20
        started = time.time()
        for off in range(0, len(data), step):
            s.sendall(view[off:off + step])
            if len(data) > 16 * step and (off // step) % 16 == 15:
                done = off + step
                rate = done / max(time.time() - started, 1e-3) / 1e6
                print(f"  {done >> 20}/{len(data) >> 20} MB, {rate:.1f} MB/s", flush=True)
        reply = read_line(s)
    if not reply.startswith("OK"):
        raise RuntimeError(f"PUT {remote}: {reply}")


def get(host, remote):
    with connect(host) as s:
        s.sendall(f"GET {remote}\n".encode())
        reply = read_line(s)
        if not reply.startswith("OK"):
            return None
        size = int(reply.split()[1])
        buf = bytearray()
        while len(buf) < size:
            chunk = s.recv(min(1 << 20, size - len(buf)))
            if not chunk:
                raise ConnectionError("short read")
            buf += chunk
        return bytes(buf)


def ls(host, remote):
    with connect(host) as s:
        s.sendall(f"LIST {remote}\n".encode())
        if not read_line(s).startswith("OK"):
            return []
        entries = []
        while True:
            line = read_line(s)
            if line == ".":
                return entries
            size, name = line.split(" ", 1)
            entries.append((name, int(size)))


def simple(host, line):
    with connect(host) as s:
        s.sendall(line.encode() + b"\n")
        return read_line(s)


def wait_for_station(host, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        reply = ping(host)
        if reply and reply.startswith("OK"):
            return reply
        time.sleep(10)
    return None


def cycle(host, nro, seconds, out, extra_env):
    if not wait_for_station(host, 30):
        sys.exit("station not reachable")
    env = [f"NSMBW_STATION_RUN_SECONDS={seconds}"] + list(extra_env)
    put(host, ("\n".join(env) + "\n").encode(), f"{GAME_DIR}/env.txt")
    for name, _ in ls(host, GAME_DIR):
        if name.startswith("profile_") or name in ("nsmbw.log", "nsmbw_err.log"):
            simple(host, f"DEL {GAME_DIR}/{name}")
    if nro:
        data = open(nro, "rb").read()
        put(host, data, f"{GAME_DIR}/nsmbw2.nro")
        back = get(host, f"{GAME_DIR}/nsmbw2.nro")
        if back != data:
            sys.exit("uploaded nro does not read back identically")
        print(f"uploaded {nro} ({len(data)} bytes), verified")
    print("run:", simple(host, "RUN"))
    started = time.time()
    time.sleep(seconds + 20)
    reply = wait_for_station(host, 600)
    elapsed = time.time() - started
    if not reply:
        print(f"station did not come back within {elapsed:.0f} s (crash screen or sleep?)")
        sys.exit(2)
    print(f"station back after {elapsed:.0f} s")
    os.makedirs(out, exist_ok=True)
    for name, _ in ls(host, GAME_DIR):
        if name.startswith("profile_") or name in ("nsmbw.log", "nsmbw_err.log"):
            data = get(host, f"{GAME_DIR}/{name}")
            if data is not None:
                open(os.path.join(out, name), "wb").write(data)
    print("logs saved to", out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=os.environ.get("NSMBW_SWITCH_HOST", "192.168.1.193"))
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("ping")
    sub.add_parser("run")
    sub.add_parser("stop")
    p = sub.add_parser("ls"); p.add_argument("remote")
    p = sub.add_parser("put"); p.add_argument("local"); p.add_argument("remote")
    p = sub.add_parser("get"); p.add_argument("remote"); p.add_argument("local")
    p = sub.add_parser("cycle")
    p.add_argument("--nro")
    p.add_argument("--seconds", type=int, default=900)
    p.add_argument("--out", required=True)
    p.add_argument("--env", action="append", default=[])
    a = ap.parse_args()

    if a.cmd == "ping":
        print(ping(a.host) or "no answer")
    elif a.cmd in ("run", "stop"):
        print(simple(a.host, a.cmd.upper()))
    elif a.cmd == "ls":
        for name, size in ls(a.host, a.remote):
            print(f"{size:>12} {name}")
    elif a.cmd == "put":
        put(a.host, open(a.local, "rb").read(), a.remote)
        print("OK")
    elif a.cmd == "get":
        data = get(a.host, a.remote)
        if data is None:
            sys.exit("missing")
        open(a.local, "wb").write(data)
        print(f"{len(data)} bytes")
    elif a.cmd == "cycle":
        cycle(a.host, a.nro, a.seconds, a.out, a.env)


if __name__ == "__main__":
    main()
