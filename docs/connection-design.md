# AI-Hook 连接与管理设计

> 实施状态（2026-07-20）：主机 daemon、管理页、client/session alias、notify/dismiss、设备专属 HMAC 配对、反向 device link、按键回传和四种 Agent Hook 已实现。设备直连 HTTP OTA 已实现；daemon 经 reverse device link 的固件分块传输、离线持久化队列和 TLS/WSS 尚未完成。

## 目标

AI-Hook 的显示和按键逻辑运行在 ESP32-C3 上，但通知来源可能位于不同网段。项目需要同时支持：

1. 主机可以直接访问 ESP 时的直连模式。
2. 主机无法进入 ESP 所在 NAT 网段，但 ESP 可以访问主机时的反向连接模式。

两种模式使用同一套客户端 API、消息格式和认证方式。管理网页以主机上的 `esphook daemon` 为主入口，ESP 自带网页只保留为调试和救援入口。

## 网络拓扑

```text
通知客户端 / 浏览器 / CLI
              |
              | HTTP API
              v
       esphook daemon
       (管理网页、认证、路由、队列)
          |             ^
          | 直连 HTTP   | ESP 主动建立长连接
          v             |
             ESP32-C3
```

### 直连模式

主机通过 `GET /health` 能访问 ESP 时，主机侧可以直接调用 ESP 的 `/notify`、`/dismiss` 和 `/ota`。这是当前固件的兼容路径，也适合首次调试。OTA 只写下一个应用槽，不覆盖固定 `fontdata` 字库分区；具体命令见 [ota.md](ota.md)。

### 反向连接模式

ESP 启动 Wi-Fi 后主动连接主机的 device link 端口。NAT 只需要允许下游 ESP 发起到主机的 TCP 连接，主机不需要访问 ESP 的内网 IP。主机上的 daemon 通过这条连接发送通知和控制命令，并接收按键事件。

实际路由以 `/health` 探测结果为准，不以“是否同网段”作为唯一判断条件；VLAN、客户端隔离和防火墙都可能让同网段设备不可达。

## 组件职责

### 主机 daemon

- 提供管理网页和 HTTP API。
- 接收所有外部客户端的通知，不让客户端直接依赖 ESP IP。
- 管理设备注册、在线状态、设备专属密钥和 alias。
- 将 `client_id` 映射为屏幕显示名称，但不把标题、正文和状态拼成不可逆的单一字符串。
- 根据设备直连可达性选择直连 HTTP 或反向 device link。
- 保存必要的 request ID、ACK 和待发送消息。
- 收到按键事件后，根据 `device_id/client_id/session_id` 路由回原始客户端。
- 提供固件上传入口；OTA 仍然只更新 app 分区，不写入 `fontdata`。

### ESP 固件

- 保留现有 LVGL、状态颜色、中文字体和长文本滚动逻辑。
- 保留本地 HTTP server 作为直连、调试和救援入口。
- 主动建立到 daemon 的 device link 长连接。
- 接收结构化通知后推入现有 `display_q`，由 `display_task` 统一修改显示模型。
- 按键事件优先通过 device link 回传；无反向连接时兼容旧的 HTTP callback。
- 负责实际屏幕渲染、蜂鸣器、dismiss 和 OTA 写入。

## Device link 协议

第一版使用长度前缀 TCP，监听主机的 `18765` 端口，避免为 ESP32-C3 引入额外 WebSocket 客户端组件。每条消息为：

```text
4 字节网络字节序长度 + UTF-8 JSON payload
```

单条 JSON 限制在 8 KiB 内。传输层只负责可靠双向连接，业务协议不依赖 TCP 的分包边界，后续可以替换为 WebSocket/WSS。

主要消息：

```json
{"op":"hello","device_id":"8856a657dc64","client_nonce":"...","fw":"..."}
{"op":"challenge","server_nonce":"..."}
{"op":"auth","proof":"hmac-sha256-hex"}
{"op":"auth_ok"}
```

认证 proof 为：

```text
HMAC-SHA256(device_secret, device_id + ":" + client_nonce + ":" + server_nonce)
```

`device_secret` 为每台设备单独生成的随机 32 字节密钥，只在首次 USB 配网时写入 ESP 和主机注册表。MAC 只作为设备 ID，不作为密钥。

认证后的业务消息示例：

```json
{
  "op":"notify",
  "request_id":"r123",
  "client_id":"github-actions",
  "session_id":"build-42",
  "title":"构建完成",
  "body":"固件已生成",
  "status":"done"
}
```

ESP 返回：

```json
{"op":"ack","request_id":"r123","ok":true}
```

按键事件通过同一连接回传，不再要求 ESP 根据通知中的 callback 地址主动新建 HTTP 请求：

```json
{
  "op":"key",
  "request_id":"r123",
  "device_id":"8856a657dc64",
  "client_id":"github-actions",
  "session_id":"build-42",
  "word":"ok"
}
```

## 首次配网与配对

```bash
esphook provision <ssid> <password>
```

配网程序通过 USB 写入：

- Wi-Fi SSID 和密码
- daemon 主机地址和 device link 端口
- 设备专属随机密钥

主机保存同一密钥到用户配置目录，权限限制为当前用户。设备重启后自动连接 daemon。恢复出厂会清除 Wi-Fi、daemon 和认证配置，需要重新 USB 配对。

如果设备已经有可用 Wi-Fi 配置，只更新主机地址、端口和密钥而不覆盖 Wi-Fi：

```bash
esphook pair --server-host <调用端主机IP>
```

直连 HTTP 也使用同一设备密钥；不要因为处于同一局域网就开放未认证的 `/notify`。

## 管理网页与客户端 API

日常网页位于：

```text
http://<调用端主机>:8787/
```

浏览器只访问 daemon。daemon 再根据当前连接状态选择：

```text
浏览器 -> daemon -> ESP HTTP
浏览器 -> daemon -> ESP device link
```

CLI、Claude/Codex/Kimi/Cursor hook 和其他通知程序也只调用 daemon 的统一 API。这样客户端不需要知道 ESP 的内网地址，也不需要处理网络切换。

## Agent Hook 与会话标识

Hook 进程由各 Agent 启动，读取 Agent 通过 stdin 传来的事件 JSON。它不能依赖 daemon 启动时的环境变量，因为同一台主机上的多个 Agent 会共享一个 daemon。Hook 每次发送通知时都显式携带：

```json
{
  "client_id": "workstation:codex",
  "session_id": "019f7ac7-bda3-7fa0-830d-4a254bdbe38e",
  "status": "done"
}
```

`client_id` 默认区分工具，`session_id` 区分同一工具下的不同会话。会话 ID 的来源是：

1. Claude Code / Codex / Kimi Code Hook 输入的 `session_id`。
2. Cursor Agent Hook 输入的 `conversation_id`，转换后作为 `session_id`。
3. 直接调用 CLI 时的 `--session-id`、`ESPHOOK_SESSION_ID` 或 Codex 的 `CODEX_THREAD_ID`。
4. 配置文件中的 `SESSION_ID` 只作为没有原生会话 ID 的手动调用 fallback。

Hook 适配器使用 fail-open 策略：daemon 或 ESP 不在线时丢弃提醒并返回成功，不阻塞 Agent 主流程。配置入口和安装命令见 README 的“Agent Hook 安装”一节。

## OTA

设备网页或主机直连设备的 HTTP API 上传 app `.bin` 后：

1. 主机通过直连 HTTP 向设备上传 app `.bin`。
2. ESP 写入下一个 OTA app 分区并重启；reverse device link 的分块传输尚未实现。
3. `fontdata` 固定分区不参与 OTA。

ESP 原有 `/ota` 保留为直连救援路径。

## 分阶段实施

1. 增加设计文档、主机 daemon 和统一 API。
2. 增加 device link、设备密钥和 USB 配对命令。
3. 将通知、dismiss、按键事件迁移到结构化消息和 ACK。
4. 管理网页迁移到 daemon，ESP 页面保留最小救援功能。
5. 接入直连/反向自动路由和离线重连。
6. 最后接入 daemon OTA，并在真机上验证断线、重启、重复消息和回滚。
