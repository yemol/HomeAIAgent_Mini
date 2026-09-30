# HomeAIAgent Mini · A1.36.4 Clean

**目标设备：** M5Stack StickC-Plus SE（经典 ESP32）+ Hat SPK2。

这是由 A1.36.4 A6 NVS + Dock20 完整源码整理出的 Git 提交候选。保留已验收的 A6 挑战应答认证、A 键 PTT、Mini 独立 Gateway/OpenClaw 会话、Dock NetworkSpeaker 路由及 Follow-up。**不提供任何语音唤醒**，也不通过 Gateway ASR 检测唤醒词。旧 Wake Probe 代码已从 Mini 删除。Gateway 本次不修改。

> 用户实机已验证：A6、PTT、Dock Speaker、Follow-up（此前 A1.36.3）。本仓库的 20% 背光配置及代码清理尚需在 Mini 上完成最终烧录验收。不要把静态检查当作实机通过。

## 1. 当前行为

- **外接电源 / Dock：** 通过 VBUS 电压（并兼容正在充电的状态）判断，不只依赖 `isCharging()`；待机亮度 **51/255（约 20%）**。显示待机不执行深度睡眠，Wi-Fi 与已认证 WebSocket 继续在线，**麦克风不持续监听唤醒**。
- **拔出电源：** 恢复手持亮度和原有操作。
- **交互：** A 键按住录音，松开发送；回答由现有 Gateway 路由至 Dock Speaker。播放完成后，由 Gateway 原有 `followup.arm` 事件开启 Follow-up，沿用服务端配置的约 10 秒窗口及现有 VAD。
- **安全：** 每次连接均执行 A6 挑战应答，只有认证成功且收到正确 `gateway.ready` 才发送业务消息；密钥由设备 NVS 保存，串口 `AUTH SET` / `AUTH SHOW` / `AUTH CLEAR` 管理。
- B 键表情预览和长按本地诊断音保留；原音频/显示资源及现有 Gateway 协议不改。

## 2. 源码与秘密配置

源码目录为 `src/`、`include/`，官方审定的表情参考图片保留在 `docs/`。`tools/` 放置本地环境迁移和主机端认证测试。不要提交 `.pio/`、`.pio-local/`、`.pio-venv/`、`include/secrets.h` 或任何设备密钥，`.gitignore` 已配置排除。

首次从干净仓库部署，将 `include/secrets.example.h` **复制到本机** `include/secrets.h` 并填写 Wi-Fi、Gateway 地址和 `/companion` 路径；不要把已填写的文件发到聊天或提交 Git。设备 A6 密钥已存 NVS 时，**普通 Upload 不会清除**；不要擦除整片 Flash 或清除 NVS。

## 3. Mac 上从已有已验证工程迁移（推荐）

以下约定最新工程路径沿用 `/Volumes/yemol_HDDisk/HomeAIAgent_Mini`，这样既有 `platformio.ini` 的本地平台固定路径继续有效。如果你的工程路径不同，在编译前先更新 `platformio.ini` 中 `platform = symlink:///...` 为该工程内真实 `.pio-local/platforms/pioarduino-espressif32-55.03.37` 的绝对路径（仅本地修改，不必将个人路径提交为共享配置）。

```zsh
cd /Volumes/yemol_HDDisk
# 先退出 PlatformIO 和串口 Monitor。若备份名已存在，先修改备份名。
mv HomeAIAgent_Mini HomeAIAgent_Mini_backup_before_clean
# 解压完整工程，生成新的 HomeAIAgent_Mini 文件夹；不要覆盖旧目录。
unzip "$HOME/Downloads/HomeAIAgent_Mini_A1_36_4_CLEAN_GIT_READY_20260930.zip" -d /Volumes/yemol_HDDisk
cd HomeAIAgent_Mini
./tools/migrate_from_backup.sh ../HomeAIAgent_Mini_backup_before_clean
# 如果此前已使用 Git，请保留历史，切勿在新目录直接 git init：
if [[ -d ../HomeAIAgent_Mini_backup_before_clean/.git && ! -e .git ]]; then
  /usr/bin/ditto ../HomeAIAgent_Mini_backup_before_clean/.git .git
fi
```

迁移脚本只从本机旧工程复制 `include/secrets.h`、`.pio-local/` 与 `.pio-venv/`，不从旧工程覆盖新版源代码；如果无可用本地缓存，请执行 `./tools/setup_project_local.sh`，该操作可能需要网络访问且依赖 Mac 上既有的全局离线工具包。

**编译、烧录与监视：**

```zsh
cd /Volumes/yemol_HDDisk/HomeAIAgent_Mini
python3 tools/test_clean_repo.py
./.pio-venv/bin/pio run -e m5stick-c-plus-se -t clean
./.pio-venv/bin/pio run -e m5stick-c-plus-se
# 仅在上一条 SUCCESS 后烧录，不要 erase_flash。
./.pio-venv/bin/pio run -e m5stick-c-plus-se -t upload
./.pio-venv/bin/pio device monitor -b 115200
```

如果串口显示凭据尚未配置，再使用此前已配对密钥执行 `AUTH SET <密钥>`；否则保留 NVS 中的现有凭据。使用 `AUTH SHOW` 查看状态，并在 Gateway 工程中执行 `security_cli.py devices` 验证 `homeai-mini-bedroom-01` 已认证。

## 4. 快速回归与 Git 提交

实机先测试：Dock 接入后 20% 背光、满电仍维持 Dock；拔出后恢复手持亮度；A 键问答、Dock Speaker 输出、Follow-up 及断线重连后 A6 认证。休眠/待机状态下说“逐光同学”**不应自动录音或触发问答**。

```zsh
python3 tools/test_clean_repo.py
python3 tools/test_mini_main_syntax.py
python3 tools/test_mini_a6_syntax.py
python3 tools/test_mini_a6_crypto.py
python3 tools/test_mini_a6_nvs.py
# 与手头真实 Gateway 代码交叉测试（只读，无需修改 Gateway）：
python3 tools/test_mini_a6_exchange.py --gateway /path/to/current/gateway

# 新仓库可先 git init；已有 Git 历史请按上面的步骤从备份恢复 .git。
# 如果过去误把私密配置纳入追踪，先解决历史与索引再继续：
git ls-files include/secrets.h
# 确认上条命令无输出后，记录清理造成的历史文件删除：
git add -A
git diff --cached --name-only
git status --short
git commit -m "chore(mini): clean A6 NVS Dock20 baseline and retire wake probe"
```

如果你原来的 Git 历史已经记录过私人配置，`.gitignore` 不会删除既有提交，请先从版本历史和 Git 索引中处理，再推送。这里没有自动删除或改写 Git 历史。

## 5. 范围与验证

本次只整理 Mini，不包含 Gateway 更新包，不附带私密配置或数 GB 的本地 SDK/依赖缓存。随包提供主机端回归脚本，**不能代替目标 ESP32 编译、内存与真实硬件运行验证**。旧 A1.36.4 完整包及本机备份应保存至新版本实机验收通过。
