# esphook OTA 使用说明

## OTA 分区策略

4 MB Flash 使用两个 1700 KiB 应用槽：`ota_0` 和 `ota_1`。中文字体放在独立的 `fontdata` 数据分区（512 KiB），不属于应用镜像：

```text
USB 完整刷写：bootloader + partition table + OTA data + app + fontdata
网页 / curl OTA：只写当前未运行的 ota_0 或 ota_1 app 分区
```

因此普通 OTA 不会擦掉 Wi-Fi、别名、配对密钥，也不会覆盖中文字体。固件启用了启动回滚保护：新应用首次启动并完成初始化后才确认，启动失败可以回到旧槽位。

## 方式一：设备网页上传

这适用于主机能直接访问设备 IP 的场景：

1. 从 GitHub 最新 Release 下载 `esphook-firmware.bin`，从 CI 的 firmware artifact 下载应用 `.bin`，或在本地使用 `build/supermini-aihook.bin`。
2. 浏览器打开 `http://<设备 IP>/`。
3. 在 `Firmware OTA` 中选择应用 `.bin` 并上传。
4. 等待设备自动重启，再刷新网页或用 `status`/串口确认。

如果设备已经通过 `esphook provision`/`pair` 配对，页面的 Token 填入 daemon 注册表中对应设备的 64 位十六进制 secret。未配对的旧设备没有 Token，留空即可。

## 方式二：curl 上传

未配置设备 secret 时：

```bash
curl --fail --data-binary @build/supermini-aihook.bin \
  -H 'Content-Type: application/octet-stream' \
  http://<设备 IP>/ota
```

已配对设备需要带认证头：

```bash
export ESPHOOK_TOKEN='<设备对应的 64 位十六进制 secret>'
curl --fail --data-binary @build/supermini-aihook.bin \
  -H 'Content-Type: application/octet-stream' \
  -H "X-Esphook-Token: $ESPHOOK_TOKEN" \
  http://<设备 IP>/ota
```

成功响应为 `{"ok":true,"reboot":true}`，设备随后重启。上传的是应用镜像，不是 `main/fontdata.bin`、`partition-table.bin`，也不是带有多个地址的整机合并镜像。

## 方式三：USB 完整刷写

GitHub Release 中的 `esphook-full-flash.zip` 已经包含 bootloader、分区表、OTA data、应用和固定中文字库，并附有 `FLASH_LAYOUT.txt`。它适合开发机无法连接设备、需要更新字库或设备无法联网的情况；应用固件仍然可以单独用 `esphook-firmware.bin` 做 OTA。

如果修改了分区表、中文字库或设备无法联网，使用完整刷写：

```bash
source "$IDF_PATH/export.sh"
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyACM0 flash
```

该流程会同步更新固定 `fontdata` 分区。CI 的资源 artifact 提供 `main/fontdata.bin` 和 `partitions_aihook.csv`；固件 artifact 提供应用及启动文件。最可靠的完整刷写仍然是在源码仓库中执行 `idf.py flash`，因为项目的 CMake 会自动把字库加入 flash 参数。

## 反向 device link 的限制

当 ESP 位于主机无法直接访问的下游网络时，通知可以经 TCP `18765` 的反向 device link 发送，但当前版本的 device link 只转发通知、关闭和按键事件，尚未实现固件分块传输。因此此场景的 OTA 需要：

- 临时让主机能访问设备 HTTP 地址后使用网页/curl；或
- 通过 USB 执行完整刷写。

CI 会在每次 push、Pull Request 和手动运行时生成应用固件，方便拿到 `.bin` 后走上述任一可达路径。未来若给 device link 增加 OTA 操作，仍应保持“只写另一个 app 槽、不动 fontdata”的策略。
