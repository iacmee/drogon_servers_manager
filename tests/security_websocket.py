"""Integration regression tests. Requires Python package websockets."""
import asyncio
import http.cookiejar
import json
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import urllib.error
import urllib.request

import websockets


async def check(binary):
    with tempfile.TemporaryDirectory(prefix="paperpilot-security-") as directory:
        root = Path(directory)
        database = root / "test.db"
        database.touch()
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        with (root / "server.log").open("w+") as log:
            process = subprocess.Popen([str(Path(binary).resolve()), str(database), str(port)],
                                       cwd=root, stdout=log, stderr=log)
            try:
                jar = http.cookiejar.CookieJar()
                client = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))

                def request(path, data=None):
                    body = None if data is None else json.dumps(data).encode()
                    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", data=body,
                                                 headers={"Content-Type": "application/json"})
                    try:
                        with client.open(req, timeout=2) as response:
                            return response.status
                    except urllib.error.HTTPError as error:
                        return error.code

                for _ in range(100):
                    if process.poll() is not None:
                        raise RuntimeError("Test server exited before becoming ready")
                    try:
                        request("/ready")
                        break
                    except (OSError, urllib.error.URLError):
                        await asyncio.sleep(.05)
                else:
                    raise RuntimeError("Test server did not become ready")
                log.flush()
                token = re.search(r"INITIAL BOOTSTRAP TOKEN\s+([A-Za-z0-9_-]+)",
                                  (root / "server.log").read_text()).group(1)
                assert request("/api/auth/register", {"username": "admin", "password": "test-password-123", "token": token}) == 201

                def connect(origin="https://panel.example", path="/ws/servers", cookie=True):
                    headers = {"Cookie": "; ".join(f"{c.name}={c.value}" for c in jar)} if cookie else {}
                    return websockets.connect(f"ws://127.0.0.1:{port}{path}", origin=origin,
                                              extra_headers=headers, open_timeout=2)

                async def rejected(status, **kwargs):
                    try:
                        async with connect(**kwargs):
                            raise AssertionError("Unexpected accepted handshake")
                    except websockets.exceptions.InvalidStatusCode as error:
                        assert error.status_code == status, error.status_code

                async def closed(ws):
                    await asyncio.wait_for(ws.wait_closed(), timeout=5)

                credentials = {"username": "admin", "password": "test-password-123"}
                await rejected(401, cookie=False)
                await rejected(401, cookie=False, path="/ws/console/1")
                assert request("/api/auth/login", credentials) == 200
                for origin in (None, "null", "https://evil.example", "https://panel.example.evil", "http://panel.example"):
                    await rejected(403, origin=origin)
                    await rejected(403, origin=origin, path="/ws/console/1")
                async with connect() as first, connect() as second, connect(path="/ws/console/1") as console:
                    assert json.loads(await first.recv())["type"] == "servers.snapshot"
                    await second.recv()
                    await console.recv()
                    assert request("/api/auth/logout", {}) == 200
                    await closed(first)
                    await closed(second)
                    await closed(console)
                await rejected(401)
                assert request("/api/servers") == 401
                assert request("/api/auth/login", credentials) == 200
                async with connect() as previous:
                    await previous.recv()
                    assert request("/api/auth/login", credentials) == 200
                    await closed(previous)
                async with connect() as expiring, connect(path="/ws/console/1") as console:
                    await expiring.recv()
                    await closed(expiring)
                    await closed(console)
                await rejected(401)
                assert request("/api/servers") == 401
                print("PASS: authentication, Origin checks on both endpoints, logout of multiple sockets, re-login revocation, absolute expiry")
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    import sys
    asyncio.run(check(sys.argv[1]))
