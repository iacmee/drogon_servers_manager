"""Trusted proxy resolution without installing or running a reverse proxy."""
import copy
import http.client
import json
from pathlib import Path
import sys
from auth_http import server


def check(binary):
    config = json.loads((Path(__file__).resolve().parents[1] / "config/config.json").read_text())
    config["app"]["allowed_origins"] = ["https://panel.example"]
    config["plugins"] = [p for p in config["plugins"] if p["name"] != "drogon::plugin::AccessLogger"]
    resolver = next(p for p in config["plugins"] if p["name"] == "drogon::plugin::RealIpResolver")
    assert resolver["config"]["trust_ips"] == ["192.168.1.37"]
    hodor = next(p for p in config["plugins"] if p["name"] == "drogon::plugin::Hodor")
    assert hodor["config"]["use_real_ip_resolver"] and not hodor["config"]["trust_ips"]

    def request(s, endpoint, forwarded, source):
        conn = http.client.HTTPConnection("127.0.0.1", int(s.url.rsplit(":", 1)[1]),
                                         timeout=5, source_address=(source, 0))
        try:
            conn.request("POST", "/api/auth/" + endpoint, "{}", {
                "Origin": "https://panel.example", "Content-Type": "application/json",
                "X-Forwarded-For": forwarded})
            response = conn.getresponse()
            response.read()
            return response.status
        finally:
            conn.close()

    # Actual production trust list: loopback is NOT a trusted proxy.
    with server(binary, proxy_profile=config) as s:
        for endpoint, limit in [("register", 5), ("login", 20)]:
            for i in range(limit):
                assert request(s, endpoint, f"203.0.113.{i+1}", "127.0.0.1") == 400
            assert request(s, endpoint, "198.51.100.1", "127.0.0.1") == 429
            assert request(s, endpoint, "198.51.100.1", "127.0.0.3") == 400

    # Simulate the remote trusted proxy on a dedicated loopback address, without
    # modifying host networking or contacting the actual Raspberry Pi.
    simulated = copy.deepcopy(config)
    next(p for p in simulated["plugins"] if p["name"] == "drogon::plugin::RealIpResolver")["config"]["trust_ips"] = ["127.0.0.2"]
    with server(binary, proxy_profile=simulated) as s:
        for endpoint, limit in [("register", 5), ("login", 20)]:
            for _ in range(limit):
                assert request(s, endpoint, "203.0.113.10", "127.0.0.2") == 400
            assert request(s, endpoint, "203.0.113.10", "127.0.0.2") == 429
            assert request(s, endpoint, "203.0.113.11", "127.0.0.2") == 400
    print("PASS: exact remote proxy trust, untrusted forwarded headers ignored, independent login/registration client quotas")


if __name__ == "__main__":
    check(sys.argv[1])
