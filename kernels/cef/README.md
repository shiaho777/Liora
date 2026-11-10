# CEF 内核适配

## 现状

**适配层能用真实 CEF 156 编译、链接并初始化；Chromium 的渲染进程目前无法启动。**

已验证：

- `cmake -DLIORA_CEF_ROOT=...` 可构建 `liora_cef_kernel`（含完整 `libcef_dll_wrapper`，246 个目标）。
- 应用运行时 `CefInitialize` 成功（日志：`CEF kernel ready`），框架能从 app bundle 正确加载。
- 4 个 helper app（Helper / GPU / Renderer / Alerts）已生成，框架符号链接布局正确。

**阻塞点：** Chromium 启动 helper 时做代码签名校验，失败并拒绝启动：

```
ERROR:base/mac/process_requirement.cc:159] Unable to derive validation category
for current process. Signature validation of current process failed:
Error Domain=NSOSStatusErrorDomain Code=-67030
```

原因：ad-hoc 签名（`codesign --sign -`）没有 Team ID，无法导出进程校验类别。这不是代码问题，需要 **Developer ID 证书**对主程序、helper 与 CEF 框架统一签名（并公证）。这也正是正式发行本来就需要的东西。

因此 `kernel.default` 不应设为 `cef`；默认仍是系统 WebView。

## 复现构建

```bash
./scripts/fetch-cef.sh                    # 下载并校验 CEF 156（macOS arm64）
cmake -S . -B build-cef -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIORA_CEF_ROOT="$PWD/build/cef/cef_binary_156.0.26+g8ea0c73+chromium-156.0.8078.88_macosarm64_minimal"
cmake --build build-cef --target liora_cef_kernel
```

## 为什么需要它

系统 WebView 拿不到 DNS 控制权：解析由系统完成，页面 TLS 由 WebKit 发起。因此以下能力只有在 CEF 内核下才成立：

| 能力 | 系统 WebView | CEF |
|---|---|---|
| 自定义 DoH | 经 `liora-net` 代理 | Chromium 原生 |
| ECH | 否 | 是 |
| 强制 H3、禁止 TCP 回退 | 否 | 是 |
| MV3 扩展 | 否 | 是 |

## 距离可运行还差什么

1. **Developer ID 签名（阻塞项）**：见上，ad-hoc 签名无法通过 Chromium 的进程校验。
2. **框架嵌入与 helper**：已完成（`scripts/bundle-cef.sh`），需要在签名后重新验证。
3. **主进程入口**：已完成，`main()` 先调用 `CefExecuteProcess`。
4. **消息泵**：当前用 `NSTimer` 驱动 `CefDoMessageLoopWork()`；正式版应改为按需调用，避免 120Hz 空转。
5. **生命周期**：`CefInitialize`/`CefShutdown` 必须成对且在主线程；退出时先关闭全部 browser。
6. **壳接入**：已完成，`kernel.default = cef` 时初始化并 attach；未就绪时回退系统内核。

## 组件安装

`设置 → 内核 → 下载` 使用通用安装器：`components.plan` → 壳下载归档 → `tar` 解包到 staging → Core 逐文件校验大小与 SHA-256 → `components.commit`。任一哈希不一致都会失败并清理 staging，不会留下半安装状态。

安装成功只代表组件文件就位。CEF 适配层仍需 `LIORA_WITH_CEF` 构建，ad-hoc 签名仍会导致 `-67030`；界面会在内核卡片上注明“组件已安装，但当前构建未启用该内核”。

## 接口位置

| 职责 | 位置 |
|---|---|
| 能力声明、启动参数 | `kernels/cef/cef_kernel.mm` 的 `CefKernel::info()` |
| 内核视图 | 同文件的 `CefView` |
| 启动/关闭 | `liora::cef::initialize()` / `shutdown()` |
| 启动参数计算 | `core/src/kernel.cpp` 的 `KernelManager::netSetup` |

`KernelNetSetup` 直接映射到 Chromium 开关：

| 字段 | 启动参数 |
|---|---|
| `dohTemplates` | `--dns-over-https-mode=secure --dns-over-https-templates=<url>` |
| SPKI（在 `switches` 中） | `--ignore-certificate-errors-spki-list=<hash>` |
| `forceQuicOrigins` | `--enable-quic --origin-to-force-quic-on=host:443,...` |
| `proxy` | `--proxy-server=http://127.0.0.1:<port>` |

网络开关是启动参数，`KernelInfo::netNeedsRestart` 已经标记为 `true`，界面会提示重启内核。

## 已知风险

- **扩展 UI 接入**：不用 Chrome 自带工具栏时，MV3 扩展的图标、弹窗、右键菜单能否完整接入自定义界面，需要先做原型。
- **签名**：下载的 CEF 框架必须与宿主同 Team ID 签名并公证，否则 macOS 加固运行时会拒绝加载。界面中的内核页会显示当前签名与 CEF 启动结果；ad-hoc 构建会直接说明 `-67030`，下载与选择 CEF 前都需要先确认，而不是让用户等到页面空白。
- **进程内切换**：不同内核家族可以同时加载；同家族不同 CEF 版本需要重启并恢复会话。
