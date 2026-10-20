"""Reset routing, authorization, ACK and no-retry regression tests."""
import hashlib
import hmac
import json
import socket
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

import esphookd as app


class ResetTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.cfg = app.HostConfig()
        self.cfg.registry_path = Path(self.tmp.name) / "devices.json"
        self.cfg.alias_path = Path(self.tmp.name) / "aliases.json"
        self.cfg.device_id = ""
        self.cfg.device_ip = "192.0.2.1"
        self.core = app.DaemonCore(self.cfg)

    def connection(self, device="c3", ok=True):
        connection = Mock(device_id=device)
        def send(message):
            self.core.handle_device_message(connection, {
                "op": "ack", "request_id": message["request_id"],
                "ok": ok, "error": "reset busy or cooling down",
            })
            return True
        connection.send.side_effect = send
        self.core.connections[device] = connection
        return connection

    def test_success_requires_matching_ack(self):
        connection = self.connection()
        result = self.core.reset_board({})
        self.assertEqual(result["state"], "released")
        self.assertEqual(result["device_id"], "c3")
        self.assertEqual(connection.send.call_count, 1)
        self.assertEqual(connection.send.call_args.args[0]["op"], "reset_board")
        self.assertFalse(self.core.reset_pending)

    def test_rejected_pulse_is_failure(self):
        connection = self.connection(ok=False)
        with self.assertRaisesRegex(RuntimeError, "cooling down"):
            self.core.reset_board({})
        self.assertEqual(connection.send.call_count, 1)

    def test_wrong_connection_or_request_cannot_ack(self):
        connection = self.connection()
        other = Mock(device_id="other")
        def send(message):
            self.core.handle_device_message(other, {
                "op": "ack", "request_id": message["request_id"], "ok": True})
            self.core.handle_device_message(connection, {
                "op": "ack", "request_id": "wrong", "ok": True})
            return True
        connection.send.side_effect = send
        with patch.object(self.core, "_direct_request") as direct:
            with self.assertRaisesRegex(RuntimeError, "outcome unknown"):
                self.core.reset_board({}, timeout=0.01)
            direct.assert_not_called()
        self.assertEqual(connection.send.call_count, 1)
        self.assertFalse(self.core.reset_pending)

    def test_no_offline_queue_or_ambiguous_target(self):
        with patch.object(self.core, "_direct_request") as direct:
            with self.assertRaises(RuntimeError):
                self.core.reset_board({})
            first = self.connection("first")
            second = self.connection("second")
            with self.assertRaises(RuntimeError):
                self.core.reset_board({})
            with self.assertRaises(RuntimeError):
                self.core.reset_board({"device_id": "missing"})
            first.send.assert_not_called()
            second.send.assert_not_called()
            self.assertEqual(self.core.reset_board({"device_id": "second"})["device_id"], "second")
            direct.assert_not_called()

    def test_send_failure_does_not_retry(self):
        connection = self.connection()
        connection.send.side_effect = None
        connection.send.return_value = False
        with self.assertRaisesRegex(RuntimeError, "outcome unknown"):
            self.core.reset_board({})
        self.assertEqual(connection.send.call_count, 1)
        self.assertFalse(self.core.reset_pending)

    def test_cli_failure_is_nonzero_and_never_falls_back(self):
        with patch.object(app, "daemon_call", side_effect=TimeoutError), \
             patch.object(app, "direct_call") as direct:
            self.assertEqual(app.cmd_reset_board([], self.cfg), 1)
            direct.assert_not_called()

    def test_authenticated_cli_web_and_device_round_trip(self):
        secret = "22" * 32
        self.core.registry.add_pending(secret)
        self.cfg.admin_token = "test-admin-token"
        web = app.WebServer(("127.0.0.1", 0), self.core)
        tcp = app.DeviceTCPServer(("127.0.0.1", 0), self.core)
        for server in (web, tcp):
            threading.Thread(target=server.serve_forever, daemon=True).start()
        self.cfg.daemon_url = "http://127.0.0.1:" + str(web.server_address[1])
        client = socket.create_connection(tcp.server_address, timeout=2)
        try:
            app.send_json(client, {"op": "hello", "device_id": "c3", "client_nonce": "nonce"})
            challenge = app.recv_json(client, 2)
            material = f"c3:nonce:{challenge['server_nonce']}".encode()
            proof = hmac.new(bytes.fromhex(secret), material, hashlib.sha256).hexdigest()
            app.send_json(client, {"op": "auth", "proof": proof})
            self.assertEqual(app.recv_json(client, 2)["op"], "auth_ok")
            # An unauthenticated request must never reach the device.
            with self.assertRaises(app.urllib.error.HTTPError) as error:
                app.http_call(self.cfg.daemon_url + "/api/reset-board", {})
            self.assertEqual(error.exception.code, 401)
            observed = []
            def reply():
                message = app.recv_json(client, 2)
                observed.append(message)
                app.send_json(client, {"op": "ack", "request_id": message["request_id"], "ok": True})
            responder = threading.Thread(target=reply)
            responder.start()
            self.assertEqual(app.cmd_reset_board([], self.cfg), 0)
            responder.join(2)
            self.assertEqual(len(observed), 1)
            self.assertEqual(observed[0]["op"], "reset_board")
        finally:
            client.close()
            for server in (web, tcp):
                server.shutdown()
                server.server_close()


if __name__ == "__main__":
    unittest.main()
