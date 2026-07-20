#!/usr/bin/env python3
"""Host-side esphook daemon and CLI.

The daemon exposes a small HTTP API for callers and a length-prefixed JSON
TCP link for ESP devices.  It intentionally uses only the Python standard
library so the host does not need a broker or a third-party WebSocket module.
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import http.server
import json
import os
from pathlib import Path
import secrets
import select
import socket
import socketserver
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
import uuid
from typing import Any


MAX_FRAME = 8192
DEFAULT_DEVICE_PORT = 18765
DEFAULT_WEB_PORT = 8787
DEFAULT_CONFIG = Path.home() / "bin" / "esphook.conf"
DEFAULT_REGISTRY = Path.home() / ".config" / "esphook" / "devices.json"
_inject_warning_lock = threading.Lock()
_inject_warning_emitted = False


def log(message: str) -> None:
    print(f"esphook: {message}", file=sys.stderr)


def parse_shell_config(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError:
        return values
    for line in lines:
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip().strip("'\"")
        values[key] = value
    return values


def config_path() -> Path:
    return Path(os.environ.get("ESPHOOK_CONF", str(DEFAULT_CONFIG)))


def _first_env(*names: str) -> str:
    for name in names:
        value = os.environ.get(name, "").strip()
        if value:
            return value
    return ""


def resolve_client_id(cfg: "HostConfig", explicit: str = "") -> str:
    """Resolve the caller identity without making the daemon process stateful.

    A daemon is shared by all local agents, so client/session identity must be
    carried on every request.  Explicit CLI/environment values are useful for
    hooks and integrations; the config file remains the manual fallback.
    """
    return (
        explicit.strip()
        or _first_env("ESPHOOK_CLIENT_ID")
        or cfg.values.get("CLIENT_ID", "").strip()
        or f"{socket.gethostname()}:esphook"
    )


def resolve_session_id(cfg: "HostConfig", explicit: str = "") -> str:
    """Resolve a stable per-agent session identifier.

    CODEX_THREAD_ID is supplied by Codex hooks.  Other integrations pass their
    native session/conversation id through ESPHOOK_SESSION_ID after reading
    their hook JSON from stdin.  SESSION_ID is intentionally only a manual
    fallback for callers that do not have a native lifecycle id.
    """
    return (
        explicit.strip()
        or _first_env("ESPHOOK_SESSION_ID", "CODEX_THREAD_ID")
        or cfg.values.get("SESSION_ID", "default").strip()
        or "default"
    )


class HostConfig:
    def __init__(self) -> None:
        self.values = parse_shell_config(config_path())
        self.device_ip = self.values.get("DEVICE_IP", "")
        self.device_id = self.values.get("DEVICE_ID", "")
        self.device_port = int(os.environ.get(
            "ESPHOOK_DEVICE_PORT",
            self.values.get("DEVICE_PORT", self.values.get("DAEMON_PORT", str(DEFAULT_DEVICE_PORT))),
        ))
        self.web_bind = os.environ.get("ESPHOOK_WEB_BIND", self.values.get("WEB_BIND", "127.0.0.1"))
        self.web_port = int(os.environ.get("ESPHOOK_WEB_PORT", self.values.get("WEB_PORT", str(DEFAULT_WEB_PORT))))
        self.daemon_host = self.values.get("DAEMON_HOST", "")
        self.daemon_url = self.values.get("DAEMON_URL", f"http://127.0.0.1:{self.web_port}")
        self.callback_host = self.values.get("CALLBACK_HOST", self.daemon_host)
        self.admin_token = self.values.get("ESPHOOK_ADMIN_TOKEN", "")
        registry = self.values.get("ESPHOOK_DEVICES_FILE", "")
        self.registry_path = Path(registry).expanduser() if registry else DEFAULT_REGISTRY
        alias_path = self.values.get("ESPHOOK_ALIASES_FILE", "")
        self.alias_path = Path(alias_path).expanduser() if alias_path else self.registry_path.with_name("aliases.json")

    def preferred_host(self) -> str:
        if self.daemon_host:
            return self.daemon_host
        # This is only used during USB provisioning.  Prefer a non-loopback
        # address if the machine has one; the user can always set DAEMON_HOST.
        try:
            probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            probe.connect(("192.0.2.1", 9))
            address = probe.getsockname()[0]
            probe.close()
            if address and not address.startswith("127."):
                return address
        except OSError:
            pass
        try:
            address = socket.gethostbyname(socket.gethostname())
            if address:
                return address
        except OSError:
            pass
        return "127.0.0.1"


class DeviceRegistry:
    """Persistent device-id -> secret registry.

    A pending entry is allowed during USB provisioning.  The first
    authenticated device connection binds that secret to the device's MAC
    based id.  This avoids requiring the user to type the MAC manually.
    """

    def __init__(self, path: Path) -> None:
        self.path = path
        self.lock = threading.RLock()
        self.data: dict[str, Any] = {"devices": {}}
        self._load()

    def _load(self) -> None:
        try:
            loaded = json.loads(self.path.read_text(encoding="utf-8"))
            if isinstance(loaded, dict) and isinstance(loaded.get("devices"), dict):
                self.data = loaded
        except (OSError, ValueError):
            self.data = {"devices": {}}

    def reload(self) -> None:
        with self.lock:
            self._load()

    def _save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_suffix(self.path.suffix + ".tmp")
        temporary.write_text(json.dumps(self.data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        os.chmod(temporary, 0o600)
        os.replace(temporary, self.path)

    def add_pending(self, secret: str) -> None:
        key = "pending-" + secret[:12]
        with self.lock:
            self.data.setdefault("devices", {})[key] = {
                "device_id": "",
                "secret": secret,
                "created": int(time.time()),
            }
            self._save()

    @staticmethod
    def _proof(secret: str, device_id: str, client_nonce: str, server_nonce: str) -> str:
        try:
            raw_secret = bytes.fromhex(secret)
        except ValueError:
            return ""
        material = f"{device_id}:{client_nonce}:{server_nonce}".encode("utf-8")
        return hmac.new(raw_secret, material, hashlib.sha256).hexdigest()

    def authenticate(self, device_id: str, client_nonce: str, server_nonce: str, proof: str) -> bool:
        with self.lock:
            # `esphook pair` may add a pending secret while daemon is already
            # running.  Reload before every new handshake so no daemon restart
            # is required during provisioning.
            self._load()
            devices = self.data.setdefault("devices", {})
            candidates: list[tuple[str, dict[str, Any]]] = []
            exact = devices.get(device_id)
            if isinstance(exact, dict):
                candidates.append((device_id, exact))
            candidates.extend((key, value) for key, value in devices.items()
                              if key != device_id and isinstance(value, dict))
            for key, entry in candidates:
                secret = str(entry.get("secret", ""))
                expected = self._proof(secret, device_id, client_nonce, server_nonce)
                if expected and hmac.compare_digest(expected, proof):
                    if key != device_id:
                        # Bind the pending secret to the real device id.
                        devices[device_id] = dict(entry)
                        devices[device_id]["device_id"] = device_id
                        devices[device_id]["paired"] = int(time.time())
                        del devices[key]
                        self._save()
                    return True
        return False

    def secret_for(self, device_id: str = "") -> str:
        with self.lock:
            self._load()
            devices = self.data.get("devices", {})
            if device_id and isinstance(devices.get(device_id), dict):
                return str(devices[device_id].get("secret", ""))
            # Single-device installations can use the only registered secret.
            entries = [v for v in devices.values() if isinstance(v, dict) and v.get("secret")]
            return str(entries[0].get("secret", "")) if len(entries) == 1 else ""

    def public_devices(self, online: set[str]) -> list[dict[str, Any]]:
        with self.lock:
            self._load()
            devices = self.data.get("devices", {})
            result = []
            for key, entry in devices.items():
                if not isinstance(entry, dict):
                    continue
                device_id = str(entry.get("device_id", "") or ("" if key.startswith("pending-") else key))
                result.append({
                    "device_id": device_id,
                    "online": device_id in online if device_id else False,
                    "alias": str(entry.get("alias", "")),
                    "paired": entry.get("paired", entry.get("created", 0)),
                })
            return result


class FrameError(Exception):
    pass


def recv_exact(sock: socket.socket, size: int, timeout: float) -> bytes:
    result = bytearray()
    deadline = time.monotonic() + timeout
    while len(result) < size:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError
        ready, _, _ = select.select([sock], [], [], remaining)
        if not ready:
            raise TimeoutError
        part = sock.recv(size - len(result))
        if not part:
            raise ConnectionError("peer closed")
        result.extend(part)
    return bytes(result)


def recv_json(sock: socket.socket, timeout: float) -> dict[str, Any]:
    header = recv_exact(sock, 4, timeout)
    size = int.from_bytes(header, "big")
    if size <= 0 or size > MAX_FRAME:
        raise FrameError(f"invalid frame size {size}")
    payload = recv_exact(sock, size, timeout)
    try:
        value = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as exc:
        raise FrameError("invalid JSON") from exc
    if not isinstance(value, dict):
        raise FrameError("JSON message must be an object")
    return value


def send_json(sock: socket.socket, message: dict[str, Any], lock: threading.Lock | None = None) -> None:
    payload = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(payload) > MAX_FRAME:
        raise FrameError("outgoing message is too large")
    packet = len(payload).to_bytes(4, "big") + payload
    if lock:
        with lock:
            sock.sendall(packet)
    else:
        sock.sendall(packet)


def inject_word(word: str) -> None:
    global _inject_warning_emitted
    if not word:
        return
    for command in (("ydotool", "type", word), ("xdotool", "type", "--", word), ("wtype", "--", word)):
        if not shutil_which(command[0]):
            continue
        try:
            subprocess.run(command, check=False, timeout=2)
            return
        except (OSError, subprocess.TimeoutExpired):
            return
    with _inject_warning_lock:
        if not _inject_warning_emitted:
            log(
                "no inject tool (ydotool/xdotool/wtype); keyboard input is "
                "unavailable (X11: install xdotool; Wayland: install wtype or "
                "ydotool and run ydotoold)"
            )
            _inject_warning_emitted = True


def shutil_which(program: str) -> str | None:
    # Keep the daemon dependency-free without importing the whole shutil API
    # just for this small helper.
    for directory in os.environ.get("PATH", "").split(os.pathsep):
        candidate = Path(directory) / program
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    return None


class DeviceConnection:
    def __init__(self, core: "DaemonCore", sock: socket.socket, address: tuple[Any, ...], device_id: str) -> None:
        self.core = core
        self.sock = sock
        self.address = address
        self.device_id = device_id
        self.send_lock = threading.Lock()
        self.last_seen = time.monotonic()
        self.closed = False

    def send(self, message: dict[str, Any]) -> bool:
        if self.closed:
            return False
        try:
            send_json(self.sock, message, self.send_lock)
            return True
        except OSError:
            self.close()
            return False

    def close(self) -> None:
        if self.closed:
            return
        self.closed = True
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass

    def run(self) -> None:
        try:
            while not self.closed:
                try:
                    message = recv_json(self.sock, 1.0)
                except TimeoutError:
                    if time.monotonic() - self.last_seen > 75:
                        raise ConnectionError("heartbeat timeout")
                    continue
                self.last_seen = time.monotonic()
                self.core.handle_device_message(self, message)
        except (ConnectionError, FrameError, OSError) as exc:
            if not self.closed:
                log(f"device {self.device_id} link closed: {exc}")
        finally:
            self.close()
            self.core.unregister(self)


class DaemonCore:
    def __init__(self, cfg: HostConfig) -> None:
        self.cfg = cfg
        self.registry = DeviceRegistry(cfg.registry_path)
        self.connections: dict[str, DeviceConnection] = {}
        self.lock = threading.RLock()
        self.aliases = self._load_aliases()
        self.known_clients: set[str] = set(self.aliases["clients"])
        self.known_sessions: set[str] = set(self.aliases["sessions"])

    def _load_aliases(self) -> dict[str, dict[str, str]]:
        empty = {"clients": {}, "sessions": {}}
        try:
            data = json.loads(self.cfg.alias_path.read_text(encoding="utf-8"))
            if not isinstance(data, dict):
                return empty
            # Backward compatibility: the original format was a flat
            # client_id -> alias map.
            if "clients" not in data and "sessions" not in data:
                return {"clients": {str(k): str(v) for k, v in data.items() if str(v)},
                        "sessions": {}}
            result = {"clients": {}, "sessions": {}}
            for kind in result:
                values = data.get(kind, {})
                if isinstance(values, dict):
                    result[kind] = {str(k): str(v) for k, v in values.items() if str(v)}
            return result
        except (OSError, ValueError):
            return empty

    def _save_aliases(self) -> None:
        self.cfg.alias_path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.cfg.alias_path.with_suffix(self.cfg.alias_path.suffix + ".tmp")
        temporary.write_text(json.dumps(self.aliases, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        os.chmod(temporary, 0o600)
        os.replace(temporary, self.cfg.alias_path)

    def aliases_status(self) -> dict[str, Any]:
        with self.lock:
            return {
                "ok": True,
                "clients": dict(self.aliases["clients"]),
                "sessions": dict(self.aliases["sessions"]),
                "known_clients": sorted(self.known_clients),
                "known_sessions": sorted(self.known_sessions),
            }

    def set_alias(self, kind: str, identifier: str, alias: str) -> dict[str, Any]:
        category = "clients" if kind in ("client", "clients") else "sessions" if kind in ("session", "sessions") else ""
        identifier = identifier.strip()
        alias = alias.strip()
        if not category or not identifier:
            raise ValueError("kind must be client/session and id is required")
        limit = 23 if category == "clients" else 47
        if len(alias.encode("utf-8")) > limit:
            raise ValueError(f"alias is limited to {limit} UTF-8 bytes")
        with self.lock:
            if category == "clients":
                self.known_clients.add(identifier)
            else:
                self.known_sessions.add(identifier)
            if alias:
                self.aliases[category][identifier] = alias
            else:
                self.aliases[category].pop(identifier, None)
            self._save_aliases()
            return self.aliases_status()

    def online_ids(self) -> set[str]:
        with self.lock:
            return set(self.connections)

    def register(self, connection: DeviceConnection) -> None:
        with self.lock:
            old = self.connections.get(connection.device_id)
            self.connections[connection.device_id] = connection
        if old and old is not connection:
            old.close()
        log(f"device {connection.device_id} authenticated from {connection.address[0]}")

    def unregister(self, connection: DeviceConnection) -> None:
        with self.lock:
            if self.connections.get(connection.device_id) is connection:
                del self.connections[connection.device_id]

    def handle_socket(self, sock: socket.socket, address: tuple[Any, ...]) -> None:
        try:
            hello = recv_json(sock, 10.0)
            if hello.get("op") != "hello":
                raise FrameError("expected hello")
            device_id = str(hello.get("device_id", ""))
            client_nonce = str(hello.get("client_nonce", ""))
            if not device_id or not client_nonce:
                raise FrameError("hello missing device_id/client_nonce")
            server_nonce = secrets.token_hex(16)
            send_json(sock, {"op": "challenge", "server_nonce": server_nonce})
            auth = recv_json(sock, 10.0)
            if auth.get("op") != "auth":
                raise FrameError("expected auth")
            proof = str(auth.get("proof", ""))
            if not self.registry.authenticate(device_id, client_nonce, server_nonce, proof):
                send_json(sock, {"op": "auth_error"})
                raise PermissionError("authentication failed")
            connection = DeviceConnection(self, sock, address, device_id)
            self.register(connection)
            connection.send({"op": "auth_ok", "device_id": device_id})
            connection.run()
        except (ConnectionError, FrameError, OSError, PermissionError, TimeoutError) as exc:
            log(f"device connection from {address[0]} rejected: {exc}")
            try:
                sock.close()
            except OSError:
                pass

    def handle_device_message(self, connection: DeviceConnection, message: dict[str, Any]) -> None:
        op = message.get("op")
        if op == "heartbeat":
            connection.send({"op": "heartbeat_ack", "time": int(time.time())})
        elif op == "pong":
            return
        elif op == "key":
            word = str(message.get("word", ""))
            log(f"key from {connection.device_id}: {word!r}")
            inject_word(word)
        elif op == "ack":
            log(f"device {connection.device_id} ack request={message.get('request_id', '')} ok={message.get('ok')}")
        elif op == "state":
            # Kept for the management page; no persistent state is required yet.
            return
        else:
            log(f"device {connection.device_id}: unknown op={op!r}")

    def _choose_connection(self, target: str) -> DeviceConnection | None:
        with self.lock:
            if target and target in self.connections:
                return self.connections[target]
            if not target and len(self.connections) == 1:
                return next(iter(self.connections.values()))
            return None

    def _device_target(self, payload: dict[str, Any]) -> str:
        return str(payload.get("device_id") or self.cfg.device_id or "")

    def _direct_secret(self, target: str) -> str:
        return self.registry.secret_for(target)

    def _direct_callback(self) -> str:
        host = self.cfg.callback_host or self.cfg.preferred_host()
        return f"{host}:{self.cfg.web_port}"

    def _direct_request(self, path: str, payload: dict[str, Any] | None = None,
                        method: str = "GET", target: str = "") -> dict[str, Any]:
        if not self.cfg.device_ip:
            raise RuntimeError("DEVICE_IP is not configured")
        url = f"http://{self.cfg.device_ip}{path}"
        body = None
        headers = {"Accept": "application/json"}
        if payload is not None:
            body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            headers["Content-Type"] = "application/json"
            method = "POST"
        secret = self._direct_secret(target)
        if secret:
            headers["X-Esphook-Token"] = secret
        request = urllib.request.Request(url, data=body, headers=headers, method=method)
        with urllib.request.urlopen(request, timeout=2.0) as response:
            raw = response.read(8192)
        try:
            decoded = json.loads(raw.decode("utf-8"))
            return decoded if isinstance(decoded, dict) else {"ok": True}
        except (UnicodeDecodeError, ValueError):
            return {"ok": True, "raw": raw.decode("utf-8", "replace")}

    def _with_alias(self, payload: dict[str, Any]) -> dict[str, Any]:
        message = dict(payload)
        client_id = str(message.get("client_id", ""))
        session_id = str(message.get("session_id", ""))
        with self.lock:
            if client_id:
                self.known_clients.add(client_id)
                client_alias = self.aliases["clients"].get(client_id, "")
            else:
                client_alias = ""
            if session_id:
                self.known_sessions.add(session_id)
                session_alias = self.aliases["sessions"].get(session_id, "")
            else:
                session_alias = ""
        if client_alias:
            message["client_id"] = client_alias
        if session_alias:
            message["session_id"] = session_alias
        return message

    def route(self, op: str, payload: dict[str, Any]) -> dict[str, Any]:
        target = self._device_target(payload)
        message = self._with_alias(payload)
        message["op"] = op
        message.setdefault("request_id", uuid.uuid4().hex[:16])
        connection = self._choose_connection(target)
        if connection:
            if connection.send(message):
                return {"ok": True, "transport": "reverse", "device_id": connection.device_id,
                        "request_id": message["request_id"]}
            raise RuntimeError("device link send failed")

        # Direct HTTP is a compatibility route.  It is selected only after a
        # reverse connection was not found.
        if op == "notify":
            message["type"] = message.get("status", message.get("type", "done"))
            message.setdefault("callback", self._direct_callback())
        elif op == "dismiss":
            pass
        result = self._direct_request(f"/{op}", message, target=target)
        result.setdefault("transport", "direct")
        result.setdefault("request_id", message["request_id"])
        return result

    def status(self) -> dict[str, Any]:
        online = self.online_ids()
        result: dict[str, Any] = {
            "ok": True,
            "devices": self.registry.public_devices(online),
            "reverse_online": sorted(online),
        }
        if self.cfg.device_ip:
            try:
                result["direct"] = self._direct_request("/health")
            except (OSError, urllib.error.URLError, RuntimeError) as exc:
                result["direct"] = {"ok": False, "error": str(exc)}
        return result


class DeviceTCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, address: tuple[str, int], core: DaemonCore) -> None:
        self.core = core
        super().__init__(address, DeviceTCPHandler)


class DeviceTCPHandler(socketserver.BaseRequestHandler):
    def handle(self) -> None:
        self.server.core.handle_socket(self.request, self.client_address)  # type: ignore[attr-defined]


class WebHandler(http.server.BaseHTTPRequestHandler):
    server_version = "esphookd/0.1"

    @property
    def core(self) -> DaemonCore:
        return self.server.core  # type: ignore[attr-defined]

    def log_message(self, fmt: str, *args: Any) -> None:
        log("http " + (fmt % args))

    def authorized(self) -> bool:
        token = self.core.cfg.admin_token
        if not token:
            return True
        return self.headers.get("X-Esphook-Admin", "") == token

    def send_json(self, status: int, value: dict[str, Any]) -> None:
        body = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def read_json(self) -> dict[str, Any]:
        try:
            size = int(self.headers.get("Content-Length", "0"))
        except ValueError as exc:
            raise ValueError("bad content length") from exc
        if size <= 0 or size > MAX_FRAME:
            raise ValueError("body too large or empty")
        value = json.loads(self.rfile.read(size).decode("utf-8"))
        if not isinstance(value, dict):
            raise ValueError("body must be a JSON object")
        return value

    def do_GET(self) -> None:
        if not self.authorized():
            self.send_json(401, {"ok": False, "error": "unauthorized"})
            return
        if self.path == "/":
            self.send_page()
        elif self.path == "/api/aliases":
            self.send_json(200, self.core.aliases_status())
        elif self.path in ("/api/status", "/api/devices"):
            status = self.core.status()
            self.send_json(200, status if self.path.endswith("status") else {"ok": True, "devices": status["devices"]})
        else:
            self.send_json(404, {"ok": False, "error": "not found"})

    def do_POST(self) -> None:
        if not self.authorized():
            self.send_json(401, {"ok": False, "error": "unauthorized"})
            return
        try:
            value = self.read_json()
        except (ValueError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            self.send_json(400, {"ok": False, "error": str(exc)})
            return
        try:
            if self.path in ("/api/notify", "/notify"):
                result = self.core.route("notify", value)
            elif self.path in ("/api/dismiss", "/dismiss"):
                result = self.core.route("dismiss", value)
            elif self.path == "/api/alias":
                result = self.core.set_alias(
                    str(value.get("kind", "")),
                    str(value.get("id", "")),
                    str(value.get("alias", "")),
                )
            elif self.path in ("/api/keyevent", "/keyevent"):
                inject_word(str(value.get("word", "")))
                result = {"ok": True}
            else:
                self.send_json(404, {"ok": False, "error": "not found"})
                return
            self.send_json(200, result)
        except ValueError as exc:
            self.send_json(400, {"ok": False, "error": str(exc)})
        except (RuntimeError, OSError, urllib.error.URLError) as exc:
            self.send_json(503, {"ok": False, "error": str(exc)})

    def send_page(self) -> None:
        page = """<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>esphook</title>
<style>
:root{color-scheme:dark;--bg:#0a0e14;--panel:#141a23;--line:#273244;--txt:#d7e0eb;--dim:#7e8b9c;--green:#00d9a3;--yellow:#ffb800;--red:#ff4757}
*{box-sizing:border-box}body{background:var(--bg);color:var(--txt);font:14px/1.5 system-ui,sans-serif;max-width:640px;margin:0 auto;padding:16px}
h1{color:var(--green);font-size:20px}.card{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:14px;margin:12px 0}
label{display:flex;gap:8px;align-items:center;margin:8px 0}label span{width:76px;color:var(--dim)}input,textarea,select{flex:1;min-width:0;background:var(--bg);color:var(--txt);border:1px solid var(--line);border-radius:4px;padding:8px;font:inherit}textarea{min-height:80px;resize:vertical}
button{background:var(--green);color:#06120f;border:0;border-radius:4px;padding:9px 14px;font-weight:700;cursor:pointer;margin:4px 0}button.red{background:var(--red);color:white}button.yellow{background:var(--yellow)}pre{white-space:pre-wrap;color:var(--dim);font-size:12px}
</style></head><body><h1>esphook daemon</h1>
<div class="card"><strong>设备</strong><pre id="devices">加载中…</pre></div>
<div class="card"><strong>显示别名</strong><pre id="aliases">加载中…</pre>
<label><span>类型</span><select id="alias-kind"><option value="session">session</option><option value="client">client</option></select></label>
<label><span>ID</span><input id="alias-id" placeholder="完整 client/session ID"></label>
<label><span>别名</span><input id="alias-name" placeholder="留空可删除"></label>
<button onclick="saveAlias()">保存别名</button><div class="hint">通知和关闭操作会自动使用保存的别名；设备只接收别名后的显示字段。</div></div>
<div class="card"><label><span>device_id</span><input id="device"></label>
<label><span>status</span><select id="status"><option value="done">done</option><option value="confirm">confirm</option><option value="error">error</option></select></label>
<label><span>client</span><input id="client" value="browser"></label>
<label><span>session</span><input id="session" value="default"></label>
<label><span>title</span><input id="title"></label>
<label><span>body</span><textarea id="body"></textarea></label>
<button onclick="send('/api/notify')">发送提醒</button><button class="red" onclick="dismiss()">关闭当前</button><pre id="result"></pre></div>
<script>
async function refresh(){let r=await fetch('/api/status');let x=await r.json();document.getElementById('devices').textContent=JSON.stringify(x,null,2);let a=await fetch('/api/aliases');document.getElementById('aliases').textContent=JSON.stringify(await a.json(),null,2);let d=(x.reverse_online||[])[0];if(d&&!document.getElementById('device').value)document.getElementById('device').value=d;}
async function saveAlias(){let x={kind:v('alias-kind'),id:v('alias-id'),alias:v('alias-name')};let r=await fetch('/api/alias',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(x)});document.getElementById('result').textContent=JSON.stringify(await r.json(),null,2);refresh();}
async function send(path){let x={device_id:v('device'),client_id:v('client'),session_id:v('session'),title:v('title'),body:v('body'),status:v('status')};let r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(x)});document.getElementById('result').textContent=JSON.stringify(await r.json(),null,2);refresh();}
async function dismiss(){let x={device_id:v('device'),client_id:v('client'),session_id:v('session')};let r=await fetch('/api/dismiss',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(x)});document.getElementById('result').textContent=JSON.stringify(await r.json(),null,2);refresh();}
function v(id){return document.getElementById(id).value}refresh();setInterval(refresh,5000);
</script></body></html>"""
        body = page.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class WebServer(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], core: DaemonCore) -> None:
        self.core = core
        super().__init__(address, WebHandler)


def run_daemon(cfg: HostConfig) -> int:
    core = DaemonCore(cfg)
    device_server = DeviceTCPServer(("0.0.0.0", cfg.device_port), core)
    web_server = WebServer((cfg.web_bind, cfg.web_port), core)
    device_thread = threading.Thread(target=device_server.serve_forever, name="device-listener", daemon=True)
    device_thread.start()
    web_thread = threading.Thread(target=web_server.serve_forever, name="web-listener", daemon=True)
    web_thread.start()
    log(f"web listening on {cfg.web_bind}:{cfg.web_port}")
    log(f"device link listening on 0.0.0.0:{cfg.device_port}")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        log("stopping")
    finally:
        device_server.shutdown()
        web_server.shutdown()
        device_server.server_close()
        web_server.server_close()
    return 0


def cmd_daemon(args: list[str], cfg: HostConfig) -> int:
    parser = argparse.ArgumentParser(prog="esphook daemon")
    parser.add_argument("--host", default=cfg.web_bind,
                        help="management web bind address (default: configured value)")
    parser.add_argument("--port", type=int, default=cfg.web_port,
                        help="management web port (default: 8787)")
    parser.add_argument("--device-port", type=int, default=cfg.device_port,
                        help="ESP device-link port (default: 18765)")
    parsed = parser.parse_args(args)
    cfg.web_bind = parsed.host
    cfg.web_port = parsed.port
    cfg.device_port = parsed.device_port
    return run_daemon(cfg)


def find_serial(explicit: str = "") -> str:
    if explicit:
        return explicit
    for candidate in ("/dev/ttyACM0", "/dev/ttyACM1", "/dev/ttyUSB0", "/dev/ttyUSB1"):
        if Path(candidate).exists():
            return candidate
    raise RuntimeError("no USB serial port found (/dev/ttyACM*)")


def serial_write_lines(port: str, lines: list[str]) -> None:
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise RuntimeError("pyserial is required for provision") from exc
    with serial.Serial(port, 115200, timeout=0.2, write_timeout=1) as serial_port:
        for line in lines:
            serial_port.write((line + "\n").encode("utf-8"))
            serial_port.flush()
            time.sleep(0.2)


def cmd_provision(args: list[str], cfg: HostConfig) -> int:
    parser = argparse.ArgumentParser(prog="esphook provision")
    parser.add_argument("ssid")
    parser.add_argument("password")
    parser.add_argument("--server-host", default=cfg.daemon_host or cfg.preferred_host())
    parser.add_argument("--device-port", type=int, default=cfg.device_port)
    parser.add_argument("--serial", default="")
    parsed = parser.parse_args(args)
    secret = secrets.token_hex(32)
    cfg.registry_path.parent.mkdir(parents=True, exist_ok=True)
    registry = DeviceRegistry(cfg.registry_path)
    registry.add_pending(secret)
    port = find_serial(parsed.serial)
    log(f"provisioning {port}; daemon={parsed.server_host}:{parsed.device_port}")
    serial_write_lines(port, [
        f"ssid:{parsed.ssid}",
        f"pass:{parsed.password}",
        f"daemon_host:{parsed.server_host}",
        f"daemon_port:{parsed.device_port}",
        f"daemon_secret:{secret}",
        "connect",
    ])
    log("Wi-Fi and device-link credentials sent; start `esphook daemon` if it is not running")
    return 0


def cmd_pair(args: list[str], cfg: HostConfig) -> int:
    """Pair an already Wi-Fi-configured board without replacing its SSID/password."""
    parser = argparse.ArgumentParser(prog="esphook pair")
    parser.add_argument("--server-host", default=cfg.daemon_host or cfg.preferred_host())
    parser.add_argument("--device-port", type=int, default=cfg.device_port)
    parser.add_argument("--serial", default="")
    parsed = parser.parse_args(args)
    secret = secrets.token_hex(32)
    cfg.registry_path.parent.mkdir(parents=True, exist_ok=True)
    DeviceRegistry(cfg.registry_path).add_pending(secret)
    port = find_serial(parsed.serial)
    log(f"pairing {port}; daemon={parsed.server_host}:{parsed.device_port}")
    serial_write_lines(port, [
        f"daemon_host:{parsed.server_host}",
        f"daemon_port:{parsed.device_port}",
        f"daemon_secret:{secret}",
    ])
    log("device-link credentials sent without changing Wi-Fi settings")
    return 0


def http_call(url: str, payload: dict[str, Any] | None = None, token: str = "") -> dict[str, Any]:
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8") if payload is not None else None
    headers = {"Accept": "application/json"}
    if body is not None:
        headers["Content-Type"] = "application/json"
    if token:
        headers["X-Esphook-Token"] = token
    method = "POST" if body is not None else "GET"
    request = urllib.request.Request(url, data=body, headers=headers, method=method)
    with urllib.request.urlopen(request, timeout=2.0) as response:
        raw = response.read(8192)
    value = json.loads(raw.decode("utf-8"))
    return value if isinstance(value, dict) else {"ok": True}


def daemon_call(cfg: HostConfig, path: str, payload: dict[str, Any] | None = None) -> dict[str, Any]:
    return http_call(cfg.daemon_url.rstrip("/") + path, payload, cfg.admin_token)


def direct_call(cfg: HostConfig, path: str, payload: dict[str, Any], registry: DeviceRegistry) -> dict[str, Any]:
    if not cfg.device_ip:
        raise RuntimeError("DEVICE_IP is not configured")
    secret = registry.secret_for(cfg.device_id)
    if path == "/notify":
        payload = dict(payload)
        payload.setdefault("callback", f"{cfg.callback_host or cfg.preferred_host()}:{cfg.web_port}")
        payload.setdefault("type", payload.get("status", "done"))
    return http_call(f"http://{cfg.device_ip}{path}", payload, secret)


def cmd_status(cfg: HostConfig) -> int:
    try:
        print(json.dumps(daemon_call(cfg, "/api/status"), ensure_ascii=False, indent=2))
        return 0
    except (OSError, urllib.error.URLError, ValueError):
        try:
            registry = DeviceRegistry(cfg.registry_path)
            print(json.dumps(http_call(f"http://{cfg.device_ip}/health", token=registry.secret_for(cfg.device_id)), ensure_ascii=False))
            return 0
        except (OSError, urllib.error.URLError, ValueError, RuntimeError) as exc:
            log(f"device offline: {exc}")
            return 0


def cmd_notify(args: list[str], cfg: HostConfig) -> int:
    parser = argparse.ArgumentParser(prog="esphook notify", add_help=False)
    parser.add_argument("--session-id", default="")
    parser.add_argument("--client-id", default="")
    parser.add_argument("--device-id", default="")
    parser.add_argument("status", nargs="?", default="done")
    parser.add_argument("title", nargs="?", default="")
    parser.add_argument("body", nargs="?", default="")
    try:
        parsed = parser.parse_args(args)
    except SystemExit:
        log("usage: esphook notify [--session-id ID] [--client-id ID] <done|confirm|error> <title> [body]")
        return 0
    kind = parsed.status
    title = parsed.title
    body = parsed.body
    if not title:
        log("usage: esphook notify [--session-id ID] [--client-id ID] <done|confirm|error> <title> [body]")
        return 0
    payload = {
        "device_id": parsed.device_id or cfg.device_id,
        "client_id": resolve_client_id(cfg, parsed.client_id),
        "session_id": resolve_session_id(cfg, parsed.session_id),
        "title": title,
        "body": body,
        "status": kind,
    }
    try:
        daemon_call(cfg, "/api/notify", payload)
    except (OSError, urllib.error.URLError, ValueError):
        try:
            direct_call(cfg, "/notify", {**payload, "type": kind}, DeviceRegistry(cfg.registry_path))
        except (OSError, urllib.error.URLError, ValueError, RuntimeError):
            pass
    return 0


def cmd_dismiss(args: list[str], cfg: HostConfig) -> int:
    parser = argparse.ArgumentParser(prog="esphook dismiss", add_help=False)
    parser.add_argument("--session-id", default="")
    parser.add_argument("--client-id", default="")
    parser.add_argument("--device-id", default="")
    parser.add_argument("session", nargs="?", default="")
    try:
        parsed = parser.parse_args(args)
    except SystemExit:
        log("usage: esphook dismiss [--session-id ID] [--client-id ID] [session]")
        return 0
    payload = {
        "device_id": parsed.device_id or cfg.device_id,
        "client_id": resolve_client_id(cfg, parsed.client_id),
        "session_id": resolve_session_id(cfg, parsed.session_id or parsed.session),
    }
    try:
        daemon_call(cfg, "/api/dismiss", payload)
    except (OSError, urllib.error.URLError, ValueError):
        try:
            direct_call(cfg, "/dismiss", payload, DeviceRegistry(cfg.registry_path))
        except (OSError, urllib.error.URLError, ValueError, RuntimeError):
            pass
    return 0


def cmd_setup(args: list[str]) -> int:
    tool = args[0] if args else ""
    installer = Path(__file__).with_name("install_hooks.py")
    if tool in ("claude", "claudecode", "codex", "kimi", "kimicode", "cursor", "cursor-agent"):
        result = subprocess.run(
            [sys.executable, str(installer), "--tools", tool, *args[1:]],
            check=False,
        )
        return result.returncode
    if tool == "daemon":
        log("run `esphook daemon` from a user service or desktop autostart")
    elif tool == "hooks":
        result = subprocess.run([sys.executable, str(installer), *args[1:]], check=False)
        return result.returncode
    else:
        log("usage: esphook setup claude|codex|kimi|cursor|hooks [options]")
    return 0


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    cfg = HostConfig()
    command = argv.pop(0) if argv else ""
    if command in ("daemon", "serve"):
        return cmd_daemon(argv, cfg)
    if command == "provision":
        return cmd_provision(argv, cfg)
    if command == "pair":
        return cmd_pair(argv, cfg)
    if command == "status":
        return cmd_status(cfg)
    if command == "notify":
        return cmd_notify(argv, cfg)
    if command == "dismiss":
        return cmd_dismiss(argv, cfg)
    if command == "setup":
        return cmd_setup(argv)
    if command == "test":
        return cmd_notify(["done", "test", "esphook test notification"], cfg)
    log("usage: esphook provision|pair|status|notify|dismiss|setup|daemon [--host HOST]|test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
