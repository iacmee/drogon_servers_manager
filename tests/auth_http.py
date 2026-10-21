"""Real HTTP/SQLite registration tests, with isolated data for every scenario."""
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import re
import secrets
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

PASSWORD = "a-long-test-password"


class Server:
    def request(self, path, body, headers=None):
        request = urllib.request.Request(self.url + path, data=json.dumps(body).encode(),
                                        headers={"Content-Type": "application/json", **(headers or {})})
        try:
            response = urllib.request.urlopen(request, timeout=5)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            return response.status, response.read(), response.headers

    def register(self, username, token=None, password=PASSWORD):
        return self.request("/api/auth/register", {"username": username,
                            "password": password, "token": token or self.token})[0]

    def invite(self):
        token = secrets.token_urlsafe(32)
        digest = hashlib.blake2b(token.encode(), digest_size=32).hexdigest()
        with sqlite3.connect(self.database) as db:
            db.execute("INSERT INTO invite_tokens(token_hash, uses_left, enabled) VALUES (?, 1, 1)", (digest,))
        return token, digest


@contextmanager
def server(binary):
    with tempfile.TemporaryDirectory(prefix="paperpilot-auth-") as directory:
        root = Path(directory)
        instance = Server()
        instance.database = root / "test.db"
        instance.database.touch()
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        instance.url = f"http://127.0.0.1:{port}"
        with (root / "server.log").open("w+") as log:
            process = subprocess.Popen([str(Path(binary).resolve()), str(instance.database), str(port)],
                                       cwd=root, stdout=log, stderr=log)
            try:
                for _ in range(200):
                    if process.poll() is not None:
                        raise RuntimeError(f"Auth test server exited: {process.returncode}")
                    try:
                        urllib.request.urlopen(instance.url + "/ready", timeout=.2)
                        break
                    except urllib.error.HTTPError:
                        break
                    except OSError:
                        time.sleep(.025)
                else:
                    raise RuntimeError("Auth test server not ready")
                instance.token = re.search(r"INITIAL BOOTSTRAP TOKEN\s+([A-Za-z0-9_-]+)",
                                          (root / "server.log").read_text()).group(1)
                yield instance
                assert process.poll() is None
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


def check(binary):
    with server(binary) as s:
        assert s.register("admin", password="abcdefghij!") == 400
        assert s.register("admin", password="abcdefghijkl") == 400
        assert s.register("admin", password="abcdefghijk ") == 400
        assert s.register("admin", password="éééééééééé!") == 400
        assert s.register("admin", password="abcdefghijk!") == 201
        assert s.request("/api/auth/login", {"username": "admin", "password": "abcdefghijk!"})[0] == 200

    with server(binary) as s:
        assert s.register("") == 400
        assert s.register("admin", password="short") == 400
        assert s.register("admin", token="bad") == 400
        assert s.request("/api/auth/register", {"padding": "x" * 5000})[0] == 413
        assert s.register("admin") == 201
        assert s.register("other", token="A" * 43) == 403
        status, _, headers = s.request("/api/auth/register", {}, {"X-Forwarded-For": "192.0.2.1"})
        assert status == 429 and headers["Retry-After"] == "60"
        assert s.request("/api/auth/login", {"username": "admin", "password": PASSWORD})[0] == 200
        assert s.request("/api/auth/login", {"username": "admin", "password": "wrong"})[0] == 401

    with server(binary) as s:
        with ThreadPoolExecutor(2) as pool:
            results = list(pool.map(s.register, ("first", "second")))
        assert sorted(results) == [201, 403], results
        winner = ("first", "second")[results.index(201)]
        # A second connection must see the committed row as soon as HTTP succeeds.
        with sqlite3.connect(s.database) as db:
            assert db.execute("SELECT COUNT(*) FROM users").fetchone()[0] == 1
            assert db.execute("SELECT COUNT(*) FROM invite_tokens").fetchone()[0] == 0
        token, digest = s.invite()
        assert s.register(winner, token) == 403
        with sqlite3.connect(s.database) as db:
            assert db.execute("SELECT uses_left FROM invite_tokens WHERE token_hash=?", (digest,)).fetchone()[0] == 1
        assert s.register("third", token) == 201

    with server(binary) as s:
        assert s.register("admin") == 201
        token, digest = s.invite()
        with sqlite3.connect(s.database) as db:
            db.execute("CREATE TRIGGER fail_insert BEFORE INSERT ON users BEGIN SELECT RAISE(ABORT, 'test failure'); END")
        status = s.register("failed", token)
        assert status == 500, status
        with sqlite3.connect(s.database) as db:
            assert db.execute("SELECT uses_left FROM invite_tokens WHERE token_hash=?", (digest,)).fetchone()[0] == 1
            assert db.execute("SELECT COUNT(*) FROM users WHERE username='failed'").fetchone()[0] == 0
            db.execute("DROP TRIGGER fail_insert")
        assert s.register("retry", token) == 201
    print("PASS: HTTP validation, body limits, registration throttling, concurrent single-use invites, duplicate rollback, DB failure and recovery, committed success")


if __name__ == "__main__":
    check(sys.argv[1])
