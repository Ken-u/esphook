import json
import hashlib
import hmac
import socket
import tempfile
import threading
import time
from pathlib import Path

import esphookd
from esphookd import (
    DaemonCore,
    DeviceRegistry,
    DeviceTCPServer,
    HostConfig,
    cmd_pair,
    recv_json,
    send_json,
)


def test_auth_and_reverse_notify() -> None:
    with tempfile.TemporaryDirectory() as directory:
        cfg = HostConfig()
        cfg.registry_path = Path(directory) / "devices.json"
        cfg.alias_path = Path(directory) / "aliases.json"
        cfg.device_ip = ""
        secret = "11" * 32
        DeviceRegistry(cfg.registry_path).add_pending(secret)
        core = DaemonCore(cfg)
        server = DeviceTCPServer(("127.0.0.1", 0), core)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        client = socket.create_connection(server.server_address, timeout=2)
        try:
            device_id = "8856a657dc64"
            client_nonce = "client-nonce"
            send_json(client, {"op": "hello", "device_id": device_id, "client_nonce": client_nonce})
            challenge = recv_json(client, 2)
            server_nonce = challenge["server_nonce"]
            material = f"{device_id}:{client_nonce}:{server_nonce}".encode()
            proof = hmac.new(bytes.fromhex(secret), material, hashlib.sha256).hexdigest()
            send_json(client, {"op": "auth", "proof": proof})
            assert recv_json(client, 2)["op"] == "auth_ok"
            for _ in range(20):
                if device_id in core.online_ids():
                    break
                time.sleep(0.01)
            core.set_alias("session", "session", "构建")
            result = core.route("notify", {
                "device_id": device_id,
                "client_id": "test",
                "session_id": "session",
                "title": "title",
                "body": "body",
                "status": "done",
            })
            assert result["transport"] == "reverse"
            message = recv_json(client, 2)
            assert message["op"] == "notify"
            assert message["client_id"] == "test"
            assert message["session_id"] == "构建"
            assert message["status"] == "done"
        finally:
            client.close()
            server.shutdown()
            server.server_close()


def test_pair_uses_lan_http_without_serial() -> None:
    with tempfile.TemporaryDirectory() as directory:
        cfg = HostConfig()
        cfg.registry_path = Path(directory) / "devices.json"
        cfg.device_ip = "192.168.1.50"
        cfg.daemon_host = "192.168.1.20"
        calls = []
        original_http_call = esphookd.http_call
        original_serial = esphookd.serial_write_lines

        def fake_http_call(url, payload=None, token=""):
            calls.append((url, payload, token))
            if payload is None:
                return {"ok": True, "device_id": "8856a657dc64"}
            assert url == "http://192.168.1.50/pair"
            assert payload["daemon_host"] == "192.168.1.20"
            assert payload["daemon_port"] == cfg.device_port
            assert len(payload["daemon_secret"]) == 64
            assert token == ""
            return {"ok": True, "device_id": "8856a657dc64"}

        def fail_serial(*args, **kwargs):
            raise AssertionError("LAN pair must not access serial")

        esphookd.http_call = fake_http_call
        esphookd.serial_write_lines = fail_serial
        try:
            assert cmd_pair([
                "--server", "192.168.1.20:18765",
                "--esp",
            ], cfg) == 0
        finally:
            esphookd.http_call = original_http_call
            esphookd.serial_write_lines = original_serial

        assert [call[0] for call in calls] == [
            "http://192.168.1.50/health",
            "http://192.168.1.50/pair",
        ]
        data = json.loads(cfg.registry_path.read_text(encoding="utf-8"))
        pending = [entry for entry in data["devices"].values() if entry.get("secret")]
        assert len(pending) == 1
        assert pending[0]["secret"] == calls[1][1]["daemon_secret"]


def test_pair_without_esp_uses_lan_mode() -> None:
    with tempfile.TemporaryDirectory() as directory:
        cfg = HostConfig()
        cfg.registry_path = Path(directory) / "devices.json"
        cfg.device_ip = "192.168.1.50"  # Must be ignored without --esp.
        original_pair = esphookd.pair_over_udp
        original_http_call = esphookd.http_call
        calls = []

        def fail_http(*args, **kwargs):
            raise AssertionError("LAN mode must not probe ESP HTTP")

        def fake_pair(broadcast, host, port, secret, current_secret="", pair_port=0, **kwargs):
            calls.append((broadcast, host, port, secret, current_secret, pair_port, kwargs))
            return {"ok": True, "device_id": "8856a657dc64"}

        esphookd.http_call = fail_http
        esphookd.pair_over_udp = fake_pair
        try:
            assert cmd_pair(["--server", "192.168.1.20:19000"], cfg) == 0
        finally:
            esphookd.http_call = original_http_call
            esphookd.pair_over_udp = original_pair

        assert len(calls) == 1
        assert calls[0][0] == "255.255.255.255"
        assert calls[0][1:3] == ("192.168.1.20", 19000)
        assert calls[0][6]["device_id"] == ""


def test_pair_over_udp() -> None:
    server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server.bind(("127.0.0.1", 0))
    port = server.getsockname()[1]
    received = {}

    def respond() -> None:
        try:
            raw, address = server.recvfrom(1024)
            request = json.loads(raw.decode("utf-8"))
            received.update(request)
            reply = json.dumps({
                "op": "pair_ack",
                "pair_id": request["pair_id"],
                "ok": True,
                "device_id": "8856a657dc64",
            }).encode("utf-8")
            server.sendto(reply, address)
        finally:
            server.close()

    thread = threading.Thread(target=respond, daemon=True)
    thread.start()
    result = esphookd.pair_over_udp(
        "127.0.0.1", "192.168.1.20", 18765, "aa" * 32,
        pair_port=port, timeout=2.0,
    )
    thread.join(1)
    assert result["device_id"] == "8856a657dc64"
    assert received["op"] == "pair_request"
    assert received["daemon_host"] == "192.168.1.20"
    assert received["daemon_secret"] == "aa" * 32


if __name__ == "__main__":
    test_auth_and_reverse_notify()
    test_pair_uses_lan_http_without_serial()
    test_pair_without_esp_uses_lan_mode()
    test_pair_over_udp()
    print("device link and LAN pair tests passed")
