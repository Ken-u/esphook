import hashlib
import hmac
import socket
import tempfile
import threading
import time
from pathlib import Path

from esphookd import (
    DaemonCore,
    DeviceRegistry,
    DeviceTCPServer,
    HostConfig,
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


if __name__ == "__main__":
    test_auth_and_reverse_notify()
    print("device link test passed")
