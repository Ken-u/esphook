# Agent Hook 集成

## 目标

四种 Agent 都通过本地 Hook 调用同一个 `esphook notify`，不直接连接 ESP，也不把 session ID 写进共享配置文件。

```text
Agent Hook stdin JSON
          │
          ▼
host/hooks/esphook_notify_hook.py
          │ session_id + client_id
          ▼
esphook notify → esphook daemon → ESP
```

## 会话身份

daemon 是主机上的共享进程，无法读取发起 HTTP 请求的 Agent 环境。因此身份必须作为每次请求的字段传递：

```json
{
  "client_id": "workstation:codex",
  "session_id": "019f7ac7-bda3-7fa0-830d-4a254bdbe38e",
  "status": "done",
  "title": "Codex 完成",
  "body": "本轮任务已完成"
}
```

`client_id` 默认按工具区分，例如 `hostname:codex`；`session_id` 区分同一工具下的不同会话。三个 Codex 会话会产生三个不同的 `session_id`，可以分别设置 alias。

来源和优先级：

1. Hook stdin 的 `session_id`。
2. Cursor Hook stdin 的 `conversation_id`，直接作为 `session_id`。
3. CLI 的 `--session-id`。
4. `ESPHOOK_SESSION_ID`。
5. Codex 直接调用 CLI 时的 `CODEX_THREAD_ID`。
6. `~/bin/esphook.conf` 的 `SESSION_ID`，只作为手动调用 fallback。

## 事件映射

| Agent | 事件 | esphook 状态 | 说明 |
| --- | --- | --- | --- |
| Claude Code | `Stop` | `done` | 本轮回答结束 |
| Claude Code | `StopFailure` | `error` | 本轮失败 |
| Claude Code | `Notification` | `confirm` / `error` / `done` | 仅转发权限、等待确认、失败或完成类通知 |
| Codex | `Stop` | `done` 或按状态映射 | 本轮结束 |
| Codex | `PermissionRequest` | `confirm` | 等待用户审批；同一 turn 后续 `PostToolUse` 会自动 dismiss |
| Kimi Code | `Stop` / `StopFailure` | `done` / `error` | 本轮结束 |
| Kimi Code | `PermissionRequest` / `Interrupt` | `confirm` | 等待审批或被中断 |
| Kimi Code | `Notification` | 按通知类型映射 | 仅匹配确认类通知 |
| Cursor Agent | `stop` | 按 `status` 映射 | `completed`、`error`、`aborted` |

适配器只输出通知，不向 Agent stdout 写内容，并且始终 fail-open。daemon、网络或 ESP 失败时，Hook 在超时后返回成功，不阻塞 Agent。

Codex 当前的 `PermissionRequest` 是“即将询问用户”的事件，并不单独提供一个确认完成事件。因此适配器会按会话和 `turn_id` 记录待确认状态：用户在 Codex 端批准后，工具执行完成触发 `PostToolUse`，适配器才调用 `esphook dismiss`。这样不会把普通工具调用误认为确认完成；如果用户拒绝，通常不会有 `PostToolUse`，确认提示会保留到后续 `Stop`/错误通知覆盖它。未来若 Agent 提供带有 `approved`/`allowed` 的 `PermissionResult` 或 `PermissionResponse`，适配器也会直接将其映射为 `dismiss`。

## 安装器

```bash
./scripts/install-hooks.sh --tools all
```

安装器只修改以下用户级文件：

- `~/.claude/settings.json`
- `~/.codex/hooks.json`
- `~/.kimi-code/config.toml`
- `~/.cursor/hooks.json`

JSON 配置按事件合并；Kimi 配置追加带有 `BEGIN ESPHOOK MANAGED HOOKS` / `END ESPHOOK MANAGED HOOKS` 标记的 TOML 区块。重复安装会先移除自己的旧条目，再写入一份新条目；其他 Hook 保持不变。

```bash
./scripts/install-hooks.sh --tools all --uninstall
```

卸载只删除 esphook 管理的条目，不删除配置文件和其他 Hook。已有配置首次被修改时会保存为 `.esphook.bak`。

## 官方配置入口

- [Claude Code Hooks](https://code.claude.com/docs/en/hooks)
- [Codex Hooks](https://learn.chatgpt.com/docs/hooks)
- [Kimi Code Hooks](https://www.kimi.com/code/docs/kimi-code-cli/customization/hooks.html)
- [Cursor Hooks](https://cursor.com/docs/hooks)
