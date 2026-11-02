#!/usr/bin/env python3
"""Capture all tof_instruments GSP simulator states via --frames 0 + --api-listen + native C backend."""

from __future__ import annotations

import json
import os
import socket
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(r"C:\Users\hexinlin\Desktop\mosaico-test\esp-mosaico-vibe")
PROJ = ROOT / "projects/tof_instruments"
SIM_EXE = PROJ / "managed_components/espressif__esp-gsp/sim.exe"
BACKEND_EXE = PROJ / "build-sim/kodou_matsuri_backend.exe"
BUNDLE = PROJ / "build-sim/kodou_matsuri_backend_assets/app.gspb"
OUT_DIR = PROJ / "page-shots"
OUT_DIR.mkdir(parents=True, exist_ok=True)


class ApiClient:
    def __init__(self, endpoint: str):
        host, port_s = endpoint.replace("tcp://", "").split(":")
        self.sock = socket.create_connection((host, int(port_s)), timeout=10)
        self.buf = b""
        self.rid = 0

    def call(self, method: str, params: dict | None = None):
        self.rid += 1
        body = json.dumps({"jsonrpc": "2.0", "id": self.rid, "method": method, "params": params or {}}).encode("utf-8")
        self.sock.sendall(f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body)
        while True:
            while b"\r\n\r\n" not in self.buf:
                chunk = self.sock.recv(4096)
                if not chunk:
                    raise RuntimeError("API socket closed")
                self.buf += chunk
            header, _, self.buf = self.buf.partition(b"\r\n\r\n")
            length = int(
                next(x.split(b":", 1)[1] for x in header.split(b"\r\n") if x.lower().startswith(b"content-length:")).strip()
            )
            while len(self.buf) < length:
                chunk = self.sock.recv(4096)
                if not chunk:
                    raise RuntimeError("API socket closed during body")
                self.buf += chunk
            data, self.buf = self.buf[:length], self.buf[length:]
            msg = json.loads(data)
            if msg.get("id") == self.rid:
                if "error" in msg:
                    raise RuntimeError(msg["error"])
                return msg.get("result")

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


cases = [
    ("01-首页.png", "start", "zh", 0.90),
    ("02-吉他.png", "guitar", "zh", 0.90),
    ("03-太鼓选曲.png", "select", "zh", 0.90),
    ("04-太鼓弹奏.png", "play", "zh", 1.10),
    ("05-太鼓结算.png", "result", "zh", 1.30),
    ("06-设置-双ToF.png", "settings", "zh", 1.20),
    ("07-设置-单ToF.png", "settings_single", "zh", 1.10),
    ("08-设置-英文.png", "settings", "en", 1.20),
    ("09-首页-英文.png", "start", "en", 1.00),
    ("10-吉他-英文.png", "guitar", "en", 1.00),
    ("11-选曲-英文.png", "select", "en", 1.00),
    ("12-结算-英文.png", "result", "en", 1.30),
    ("13-演奏-英文.png", "play", "en", 1.10),
]

for idx, (out_name, view, lang, wait_s) in enumerate(cases):
    api_port = 18820 + idx
    with tempfile.TemporaryDirectory(prefix="tof-sim-cap-") as td:
        ready = Path(td) / "ready.json"
        host_cmd = [
            str(SIM_EXE),
            "--bundle", str(BUNDLE),
            "--headless",
            "--frames", "0",
            "--backend-enable",
            "--backend-required",
            "--backend-idle-timeout", "30",
            "--api-listen", f"tcp://127.0.0.1:{api_port}",
            "--ready-file", str(ready),
        ]
        host = subprocess.Popen(
            host_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        info = {}
        for _ in range(150):
            if ready.is_file() and ready.stat().st_size > 0:
                try:
                    info = json.loads(ready.read_text(encoding="utf-8"))
                    if info.get("backend"):
                        break
                except Exception:
                    pass
            time.sleep(0.02)

        endpoint = info["backend"]
        api_endpoint = info.get("api") or f"127.0.0.1:{api_port}"

        env = os.environ.copy()
        env["KODOU_SIM_VIEW"] = view
        env["KODOU_SIM_LANG"] = lang
        backend = subprocess.Popen(
            [str(BACKEND_EXE), "--endpoint", endpoint],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )

        try:
            time.sleep(wait_s)
            client = ApiClient(api_endpoint)
            client.call("wait", {"frames": 4})
            out_path = OUT_DIR / out_name
            client.call("screenshot", {"path": str(out_path.resolve()), "format": "png"})
            client.close()
            print(f"Captured {out_name}")
        finally:
            for p in (backend, host):
                try:
                    p.terminate()
                    p.wait(timeout=3)
                except Exception:
                    p.kill()
