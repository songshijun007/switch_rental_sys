# PCTL All-in-One Rental Edition

基于 Nintendo Switch 家长控制系统的**出租机定时锁**工具。

- **Switch 端 NRO**：原生运行于 Atmosphere 自制系统，通过任天堂官方 Parental Control 调试接口实现每日可玩时长控制。
- **出租锁特性**：每隔 N 天必须输入基于机器码 + 盐值 + 时间周期的 8 位数字秘钥，未输入或错误则锁定机器，**完全离线校验**。
- **Web 秘钥生成器**：Next.js 写的纯浏览器端秘钥生成页面（机主用）。
- **GitHub Actions 自动构建**：推送代码后自动编译 NRO，在 Releases 页面下载。

---

## 目录结构

```
├── .github/workflows/build.yml   # CI: 用 devkitpro/devkita64 镜像编译 NRO，打 tag 后发 Release
├── switch-src/                   # Switch 端 C 源码（libnx + devkitA64）
│   ├── Makefile
│   └── source/
│       ├── main.c                # 入口 + 控制台 UI
│       ├── rental.c / rental.h   # 出租锁核心逻辑
│       ├── sha256.c / sha256.h   # 自包含 SHA-256 + HMAC
│       ├── pctl.c / pctl.h       # 家长控制 / setsys 封装
│       ├── miniserv.c / miniserv.h # 内嵌 HTTP 服务器（Web 解锁页）
│       └── config.h              # 默认盐值、周期、宽限期等
└── src/                          # Next.js 秘钥生成器 Web UI
    ├── app/
    └── lib/keygen.ts             # 与 Switch 端一致的秘钥派生算法
```

---

## 出租锁工作原理

1. 首次启动时 NRO 从 Switch 读取设备 ID 作为 **机器码**，写入 SD 卡 `sdmc:/switch/pctltcp/rental_state.bin`。
2. 每次启动以及运行中每隔几分钟：
   - 以 `unix_time` 计算当前处于第几个出租周期（`period = floor((now - epoch) / (period_days * 86400))`）。
   - 若当前周期 ≤ 已解锁周期：调用家长控制把每日可玩时长设为「不限」。
   - 否则：进入宽限期（默认 12 小时），之后把每日可玩时长设为 **0 分钟**，游戏将无法启动。
3. 锁定状态下，Switch 屏幕和 Web 界面（`http://<Switch-IP>:8081`）会显示机器码与解锁输入框。
4. 租客把机器码发给机主，机主在 Web 页面生成 8 位数字秘钥。
5. 租客在 Switch 上输入秘钥，NRO 本地用 **HMAC-SHA256(盐, 机器码:周期)** 验证，验证通过后把解锁周期写入 SD 卡并立即解除锁定。
6. 秘钥支持预支：机主可一次性生成未来 N 个周期的秘钥，租客输入最后一个即一次性解锁到该周期结束。

### 算法（必须与 Switch 端保持一致）

```
period       = floor((unix_ts - 1704067200) / (period_days * 86400))
mac          = HMAC-SHA256(utf8(salt), utf8(machine_code + ":" + period))
unlock_key   = (big-endian uint32(mac[0..4]) mod 100_000_000) → 8 位 10 进制
```

- 盐值在编译期通过 `DEFAULT_SALT` 硬编码进 NRO；也可在 SD 卡 `/switch/pctltcp/rental.ini` 中覆盖（格式见下）。
- 机器码是 Switch 的 DeviceId，即 16 位十六进制字符串（来自 `setcalGetDeviceId()`，自动回退到 serial number）。

---

## 机主上手指南

### 1. Fork 本仓库并设置自己的盐值（重要）

- 在 GitHub Repo → Settings → Secrets and variables → Actions 添加：
  - **Secret** `RENTAL_SALT`：你自己的盐值，越复杂越好（例如随机 32 位字符串）。
  - **Variable** `RENTAL_PERIOD_DAYS`：出租周期天数（默认 7）。
- 这些值会在 CI 构建时通过 `-DDEFAULT_SALT=...` 编译进 NRO。
- 不设置的话会使用源码里的默认盐值，任何人都能计算秘钥。

### 2. 触发构建

- 推送到 `main` 或打 tag `vX.Y.Z` 都会触发 Actions 构建。
- 打 tag 后 Release 页面会自动上传 `pctltcp-allinone.nro` 和 zip 包。

### 3. 部署到 Switch

- 下载 Release 里的 `pctltcp-allinone.nro`，复制到 SD 卡 `sdmc:/switch/pctltcp-allinone.nro`。
- 通过 Homebrew Menu 启动。
- 首次启动后，屏幕和 Web UI 上会显示机器码。

### 4. 日常运营

- 每次收租后，机主打开 Web 秘钥生成器（本仓库 `src/` 部署出来的 Next.js 站点，纯前端计算，不上传任何数据）：
  - 输入你设置的盐值、周期天数、租客机器码。
  - 生成「当前周期」或「批量 N 个周期」的 8 位数字秘钥。
  - 把秘钥发给租客，租客在 Switch 上输入即可续期。

---

## rental.ini 覆盖配置（可选）

在 SD 卡创建 `sdmc:/switch/pctltcp/rental.ini`：

```ini
# 覆盖编译时盐值（不推荐——放在 SD 卡上容易被租客读取）
salt = your-rental-secret
# 出租周期天数
period_days = 7
# 宽限期（小时），周期结束后过多久才真的锁机
grace_hours = 12
```

---

## 本地编译 NRO（不用 Actions）

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=$DEVKITPRO/devkitA64
export PATH=$DEVKITA64/bin:$DEVKITPRO/tools/bin:$PATH

cd switch-src
make -j$(nproc) RENTAL_SALT="your-secret" RENTAL_PERIOD_DAYS=7
# 产物: switch-src/out/pctltcp-allinone.nro
```

---

## 本地跑 Web 秘钥生成器

```bash
npm install
npm run dev
# 浏览器访问 http://localhost:3000
```

生产构建：

```bash
npm run build
npm start
```

秘钥生成完全在浏览器里用 Web Crypto 计算，不向服务器发送任何数据。

---

## 安全说明

- **盐值保密**：盐值是整个系统的根密钥，知道盐值的人可以为任何机器码/周期生成秘钥。
- **8 位数字秘钥**仅 1 亿种组合——在 Switch 端已经加入错误计数 + 冷却时间（5 次错误锁 60 秒），暴力破解在实际场景下不现实。
- 删除 SD 卡上的 `rental_state.bin` 会导致 NRO 下次启动视为未解锁状态，立即进入锁定。
- 锁定通过 Nintendo 官方的 `SetPlayTimerSettingsForDebug` 接口实现，重启、换游戏、换账号均持续生效。
- NRO 在解锁状态下把每日可玩时间设为「不限」（在 Nintendo 协议里用 `limit = -1` 表示），不会干扰正常使用。

---

## 致谢

- 原项目：[gmaitxqqq/switch-pctltcp-allinone](https://github.com/gmaitxqqq/switch-pctltcp-allinone)
- 本仓库在其基础上增加了出租锁逻辑、Web 解锁页、以及更便捷的 Release 流水线。
