"""Exercise HTTP protections and real Secure cookie handling over isolated TLS."""
import http.cookiejar
import json
import sys
import urllib.error
import urllib.request
from auth_http import server, PASSWORD


def check(binary):
    with server(binary, tls=True) as s:
        jar = http.cookiejar.CookieJar()
        client = urllib.request.build_opener(
            urllib.request.HTTPSHandler(context=s.context),
            urllib.request.HTTPCookieProcessor(jar))

        def request(path, body=None, origin="https://panel.example", method=None, extra=None):
            headers = {} if origin is None else {"Origin": origin}
            headers.update(extra or {})
            data = None if body is None else json.dumps(body).encode()
            if data is not None:
                headers["Content-Type"] = "application/json"
            req = urllib.request.Request(s.url + path, data=data, headers=headers, method=method)
            try:
                response = client.open(req, timeout=5)
            except urllib.error.HTTPError as error:
                response = error
            with response:
                result = response.status, response.headers, response.read()
            assert result[1]["X-Content-Type-Options"] == "nosniff"
            assert result[1]["X-Frame-Options"] == "DENY"
            assert result[1]["Referrer-Policy"] == "no-referrer"
            assert result[1]["Strict-Transport-Security"] == "max-age=31536000"
            assert "camera=()" in result[1]["Permissions-Policy"]
            policy = result[1]["Content-Security-Policy"]
            assert "frame-ancestors 'none'" in policy
            assert "script-src 'self'" in policy and "unsafe-inline" not in policy
            assert "wss://panel.example" in policy
            if path.startswith("/api/"):
                assert result[1]["Cache-Control"] == "no-store"
            for cookie in jar:
                assert cookie.secure and cookie.has_nonstandard_attr("HttpOnly")
                assert cookie.get_nonstandard_attr("SameSite") == "Lax"
            return result

        for path in ["/index.html", "/app.js", "/style.css"]:
            assert request(path, origin=None)[0] == 200
        assert request("/missing", origin=None)[0] == 404
        credentials = {"username": "admin", "password": PASSWORD}
        registration = {**credentials, "token": s.token}
        # Denied requests cannot consume invitations or registration quota.
        for origin in [None, "null", "https://evil.example", "https://panel.example.evil",
                       "http://panel.example", "https://panel.example:444", "https://panel.example/"]:
            assert request("/api/auth/register", registration, origin)[0] == 403
            assert request("/api/auth/login", credentials, origin)[0] == 403
        assert request("/api/auth/register", registration)[0] == 201
        before = next(c.value for c in jar if c.name == "JSESSIONID")
        assert request("/api/auth/login", credentials)[0] == 200
        after = next(c.value for c in jar if c.name == "JSESSIONID")
        assert before != after
        assert request("/api/servers", origin=None)[0] == 200

        for path in ["/api/auth/logout", "/api/servers/999/start", "/api/servers/999/stop",
                     "/api/servers/999/restart"]:
            for origin in [None, "null", "https://evil.example"]:
                assert request(path, {}, origin)[0] == 403
                assert request("/api/servers", origin=None)[0] == 200
        # An allowed origin reaches the real controller (unknown ID avoids D-Bus).
        for action in ["start", "stop", "restart"]:
            assert request(f"/api/servers/999/{action}", {})[0] == 404
        for method in ["POST", "PUT", "PATCH", "DELETE"]:
            assert request("/api/future-route", {}, None, method)[0] == 403
        assert request("/api/auth/logout", {}, None, extra={
            "Referer": "https://panel.example/", "X-Forwarded-Host": "panel.example",
            "X-Forwarded-Proto": "https"})[0] == 403
        assert request("/api/auth/logout", {})[0] == 200
        assert request("/api/servers", origin=None)[0] == 401
    print("PASS: strict HTTP origins, protected mutations, TLS session cookie flags/rotation, static and API security headers")


if __name__ == "__main__":
    check(sys.argv[1])
