import json
import os
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).with_name("hooks")))
import esphook_notify_hook as hook
import install_hooks
from esphookd import HostConfig, resolve_client_id, resolve_session_id


def test_identity_resolution() -> None:
    cfg = HostConfig()
    cfg.values = {"CLIENT_ID": "config-client", "SESSION_ID": "config-session"}
    old = {name: os.environ.get(name) for name in ("ESPHOOK_CLIENT_ID", "ESPHOOK_SESSION_ID", "CODEX_THREAD_ID")}
    try:
        for name in old:
            os.environ.pop(name, None)
        assert resolve_client_id(cfg) == "config-client"
        assert resolve_session_id(cfg) == "config-session"
        os.environ["CODEX_THREAD_ID"] = "codex-thread"
        assert resolve_session_id(cfg) == "codex-thread"
        os.environ["ESPHOOK_SESSION_ID"] = "explicit-session"
        assert resolve_session_id(cfg) == "explicit-session"
    finally:
        for name, value in old.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value


def test_classification() -> None:
    assert hook.classify({"hook_event_name": "Stop", "session_id": "a"}, "claude")[0] == "done"
    assert hook.classify({"hook_event_name": "PermissionRequest", "session_id": "b", "tool_name": "Bash"}, "codex")[0] == "confirm"
    assert hook.classify({"hook_event_name": "StopFailure", "session_id": "c", "error": "failed"}, "kimi")[0] == "error"
    assert hook.classify({"hook_event_name": "stop", "conversation_id": "d", "status": "aborted"}, "cursor")[0] == "confirm"
    assert hook.classify({"hook_event_name": "Notification", "session_id": "e", "notification_type": "task.completed"}, "kimi")[0] == "done"
    assert hook.classify({"hook_event_name": "Notification", "session_id": "f", "notification_type": "unrelated"}, "claude") is None


def test_forward_carries_session_and_client() -> None:
    calls = []
    original_run = hook.subprocess.run
    original_client = os.environ.get("ESPHOOK_CLIENT_ID")
    try:
        os.environ.pop("ESPHOOK_CLIENT_ID", None)

        def fake_run(command, **kwargs):
            calls.append((command, kwargs))
            return None

        hook.subprocess.run = fake_run
        hook.forward({
            "hook_event_name": "Stop",
            "session_id": "thread-123",
            "last_assistant_message": "完成",
        }, "codex")
    finally:
        hook.subprocess.run = original_run
        if original_client is None:
            os.environ.pop("ESPHOOK_CLIENT_ID", None)
        else:
            os.environ["ESPHOOK_CLIENT_ID"] = original_client
    assert len(calls) == 1
    command = calls[0][0]
    assert "notify" in command
    assert command[command.index("--session-id") + 1] == "thread-123"
    assert command[command.index("--client-id") + 1].endswith(":codex")


def test_install_is_idempotent_and_preserves_custom_hooks() -> None:
    with tempfile.TemporaryDirectory() as directory:
        home = Path(directory)
        claude = home / ".claude" / "settings.json"
        claude.parent.mkdir(parents=True)
        claude.write_text(json.dumps({
            "hooks": {
                "Stop": [{"hooks": [{"type": "command", "command": "custom-stop"}]}]
            }
        }) + "\n", encoding="utf-8")
        kimi = home / ".kimi-code" / "config.toml"
        kimi.parent.mkdir(parents=True)
        kimi.write_text('theme = "custom"\n', encoding="utf-8")

        assert install_hooks.main(["--home", str(home), "--tools", "all"]) == 0
        assert install_hooks.main(["--home", str(home), "--tools", "all"]) == 0

        claude_data = json.loads(claude.read_text(encoding="utf-8"))
        stop_entries = claude_data["hooks"]["Stop"]
        assert any(entry["hooks"][0]["command"] == "custom-stop" for entry in stop_entries)
        assert sum("esphook_notify_hook.py" in json.dumps(entry) for entry in stop_entries) == 1
        kimi_text = kimi.read_text(encoding="utf-8")
        assert kimi_text.count(install_hooks.MANAGED_START) == 1
        assert 'theme = "custom"' in kimi_text

        assert install_hooks.main(["--home", str(home), "--tools", "all", "--uninstall"]) == 0
        assert "esphook_notify_hook.py" not in claude.read_text(encoding="utf-8")
        assert '"command": "custom-stop"' in claude.read_text(encoding="utf-8")
        assert install_hooks.MANAGED_START not in kimi.read_text(encoding="utf-8")


if __name__ == "__main__":
    test_identity_resolution()
    test_classification()
    test_forward_carries_session_and_client()
    test_install_is_idempotent_and_preserves_custom_hooks()
    print("hook tests passed")
