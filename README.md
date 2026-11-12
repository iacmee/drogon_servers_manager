# PaperPilot

**A web panel for managing Minecraft servers, built with C++20.**

PaperPilot brings server status, lifecycle controls, and an interactive console into one interface. The backend uses Drogon for HTTP APIs and WebSockets, SQLite for users and invitations, and D-Bus to communicate with systemd. The frontend uses HTML, CSS, and JavaScript, with no separate build step.

The project explores asynchronous C++, Linux process management, and real-time communication, from coroutine-based database access to console flow control.

## Features

- **Multi-server dashboard** with search, server counts, and live status updates over WebSocket.
- **Start, stop, and restart** through HTTP APIs and systemd user units.
- **Interactive console** with streaming output, bounded history, and commands sent from the browser.
- **Invitation-based registration**, with configurable usage limits and expiration through the API.
- **Session-based authentication**, password hashing with libsodium, and invitations stored as hashes.
- **Direct HTTPS or reverse proxy deployment**, with configurable allowed origins.
- **Regression suites** for authentication, HTTP, WebSockets, the console, and proxy handling.

## Architecture

```text
Browser · HTML / CSS / JavaScript
  │
  ├── HTTP: authentication and server actions
  └── WebSocket: status and console
          │
     Backend C++20 / Drogon
          ├── SQLite: users and invitation tokens
          ├── libsodium: passwords and access secrets
          └── D-Bus / systemd --user
                    └── Java / PaperMC processes
```

Starting a server creates a transient systemd unit and connects its input and output through pipes. D-Bus signals report state changes, which are forwarded to connected browsers. Password hashing runs in a worker pool with a bounded queue to keep HTTP event loops responsive.

The console limits command size, history, and queues. Output uses a Ping/Pong acknowledgement mechanism to pace delivery to slow clients and prevent unbounded memory growth.

## Building

### Requirements

- Linux with systemd and a session D-Bus connection for server management.
- A C++20 compiler, Make, CMake, Git, `pkg-config`, and `patch`.
- Development libraries for JsonCpp, SQLite3, libsodium, libsystemd, OpenSSL, and zlib.
- A Java runtime compatible with your PaperMC version.
- Python 3 and the `websockets` package for integration tests.

On Debian/Ubuntu, install the build dependencies with:

```bash
sudo apt install build-essential cmake git pkg-config patch \
  libjsoncpp-dev libsqlite3-dev libsodium-dev libsystemd-dev \
  libssl-dev zlib1g-dev uuid-dev openssl python3 python3-venv
```

### Building Drogon and the project

The Makefile requires a local Drogon build pinned to commit
`4c5430757ea5451a7c38fbbef4b4bef7dbb47f2f`, with the patches included in this repository. These address fragmented WebSocket messages, HTTP parsing, and session rotation.

```bash
git clone https://github.com/iacmee/drogon_servers_manager.git
cd drogon_servers_manager

git clone https://github.com/drogonframework/drogon.git .deps/drogon-checkout
git -C .deps/drogon-checkout checkout 4c5430757ea5451a7c38fbbef4b4bef7dbb47f2f
git -C .deps/drogon-checkout submodule update --init --recursive

bash tools/build_drogon.sh "$PWD/.deps/drogon-checkout"
make -j2
```

The script builds and installs Drogon under `.deps/`, without changing system libraries. You can also use an existing checkout containing the required commit and its matching Trantor submodule. The project builds the `servers_manager` executable.

## Running locally

The files in `config/` contain paths and settings specific to the development environment. To try the panel, create `config/config.local.json` with this standalone configuration and an empty Minecraft server list:

```json
{
  "app": {
    "bind": "127.0.0.1",
    "port": 8443,
    "threads": 2,
    "java_path": "/usr/bin/java",
    "document_root": "static",
    "backup_root": "db/test/backups",
    "database_file": "db/test/database.db",
    "allowed_origins": ["https://localhost:8443"],
    "https": {
      "enabled": true,
      "cert": "certs/test/server.crt",
      "key": "certs/test/server.key"
    },
    "session": {"enabled": true, "timeout": 3600}
  },
  "plugins": [
    {
      "name": "drogon::plugin::Hodor",
      "dependencies": [],
      "config": {
        "algorithm": "sliding_window",
        "urls": ["^/api/auth/login$"],
        "time_unit": 60,
        "capacity": 1000,
        "ip_capacity": 20,
        "user_capacity": 15,
        "multi_threads": true,
        "use_real_ip_resolver": false,
        "trust_ips": []
      }
    }
  ],
  "servers": []
}
```

Prepare the directories and self-signed certificate, then run from the repository root:

```bash
make prepare-test-config
./servers_manager config/config.local.json
```

Open **https://localhost:8443**. The local certificate requires a browser exception. On startup, if the database has no users, the process prints a bootstrap invitation valid for **5 minutes and one use**. Enter it in the registration tab, then sign in with your new credentials.

New passwords must contain at least 12 Unicode characters and one ASCII punctuation symbol, with a maximum length of 128 bytes. Stop the panel with `Ctrl+C`.

## Configuring a Minecraft server

Add an entry to the `servers` array in your local configuration:

```json
{
  "id": 1,
  "name": "survival",
  "directory": "/absolute/path/survival",
  "service": "survival.service",
  "logPath": "/absolute/path/survival/logs/latest.log",
  "jarPath": "/absolute/path/survival/paper.jar"
}
```

Prepare the server directory and JAR, and accept the Minecraft EULA before starting the server. Set `app.java_path` to the appropriate runtime and restart the panel after changing its configuration.

IDs must be unique; use positive integers to access the WebSocket console as well. The code derives the unit name by appending `.service` to `name`: choose a valid systemd name and keep the `service` field consistent.

The backend connects to the **user bus**: run it as the user who will manage the Minecraft processes, with an available systemd user session. The console connects to the pipes of processes started by the panel; attaching to independently started processes is not implemented.

## API

| Method | Path | Purpose |
| --- | --- | --- |
| POST | `/api/auth/register` | Register with `username`, `password`, and `token` |
| POST | `/api/auth/login` | Sign in with `username` and `password` |
| POST | `/api/auth/logout` | End the session |
| POST | `/api/auth/new_token` | Create invitations with `number_of_valid_uses` and `lifetime` in minutes |
| GET | `/api/servers` | List servers and their status |
| POST | `/api/servers/{id}/start` | Start a server |
| POST | `/api/servers/{id}/stop` | Stop a server |
| POST | `/api/servers/{id}/restart` | Restart a server |
| WebSocket | `/ws/servers` | Live status updates |
| WebSocket | `/ws/console/{id}` | Console output and commands |

Login and registration are public; all other operations require an authenticated session. HTTP requests that change state must include an `Origin` header matching an allowed origin. WebSockets also validate the origin. Command-line clients must explicitly send this header and retain authentication cookies.

## Security and deployment

Protections include `Secure`, `HttpOnly`, and `SameSite=Lax` cookies, an access secret renewed at login, origin checks, a Content Security Policy, and authentication rate limits. Invitation consumption and user creation take place within a SQLite transaction.

Deploy with direct HTTPS or a reverse proxy that terminates TLS. For proxy deployments, configure the exact public origins and trust forwarded headers only from the proxy. Preserve `.deps/install/lib` alongside the executable as laid out in the repository: the binary loads libraries through a relative RPATH.

For implementation details and limitations, see:

- [Authentication and invitations](docs/auth-security.txt)
- [HTTP, cookies, and origin checks](docs/http-security.txt)
- [Console limits and WebSocket patches](docs/console-security.txt)
- [Reverse proxy configuration](docs/reverse-proxy.txt)

## Tests

After building the dependencies, prepare a Python environment for the integration suites (`websockets` has been tested with version 10.4):

```bash
python3 -m venv .deps/test-venv
source .deps/test-venv/bin/activate
python -m pip install websockets==10.4

make test-console test-auth test-security test-http-security test-proxy test-audit
```

The suites cover console limits, authentication, concurrent invitation use, sessions, HTTP/WebSocket origins, fragmented messages, trusted proxies, and HTTP parser regressions. HTTP tests use temporary fixtures and databases; integration tests require permission to open local sockets.

## Repository structure

```text
src/          Backend, systemd integration, console, and API controllers
include/      C++ interfaces, filters, and application limits
models/       ORM models for users and invitations
static/       Browser dashboard and console
config/       JSON configurations
patches/      Patches for the pinned Drogon version
tools/        Local dependency build scripts
tests/        C++ tests and Python integration suites
docs/         Technical and deployment notes
```

## Project status

PaperPilot is under development. Servers are configured through JSON files; world creation, JAR uploads, backups, and granular permission management are not currently available in the panel. The configuration includes a `backup_root` field, but backup functionality is not implemented.
