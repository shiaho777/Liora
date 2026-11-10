# CEF 内核适配

## 现状

**适配层能用真实 CEF 156 编译、链接并渲染页面。**

- 有 Apple 签名（Developer ID / Apple Development）时使用正常的多进程模式。
- 没有签名时，Chromium 会拒绝 helper 进程（`-67030`）。内核页提供“单进程兼容模式”，把渲染与 GPU 放进主进程，可在本地运行。

## 单进程兼容模式

在 设置 → 内核（高级模式）中开启，或设置环境变量 `LIORA_CEF_SINGLE_PROCESS=1`。启动参数：

```
--single-process
--in-process-gpu
--disable-gpu
--enable-features=NetworkServiceInProcess
```

权衡：

- 渲染进程与浏览器进程合并，隔离性明显弱于多进程；
- 无 GPU 加速；
- 单进程无法使用 V8 代理解析器，因此兼容模式跳过 liora-net 的 DoH/代理，改用系统 DNS；`ECH`、强制 H3 等依赖本地解析器的能力在此模式下不生效。

兼容模式仅用于本地开发/验证。正式分发应使用 Apple 签名并关闭该选项。

## 失败页、下载与弹窗

- `OnLoadError` 把 Chromium net error 映射为界面认识的 NSURLError 码（例如 `ERR_NAME_NOT_RESOLVED` → `-1003`、证书错误 → `-1202`），交给现有错误页显示，不再留下空白视图。
- `CefDownloadHandler` 把附件保存到 `downloads.dir`（默认下载目录，重名自动编号；`downloads.ask` 打开时询问位置），进度与完成事件复用现有下载页、历史记录与 toast。
- `target="_blank"` / `window.open` 通过 `OnBeforePopup` 交给界面新建标签；壳返回 `true` 后 Chromium 不再创建自己的弹出窗口。
- 站点卡片对 CEF 的 HTTPS 页面显示“CEF 内核暂不提供证书摘要；Chromium 仍会校验证书”，而不是误报“未建立 TLS”。
- 摄像头/麦克风请求通过 `CefPermissionHandler`（`OnShowPermissionPrompt` 与 `OnRequestMediaAccessPermission`）回到主线程弹出与系统 WebView 相同的授权框，并沿用“记住这个决定”。其他权限类型暂交给 CEF 默认处理。
- 单进程兼容模式下同样支持下载与错误页；DoH/代理仍不会启用。

## 多标签与布局

每个标签拥有独立的 `CefBrowser` 与原生视图。视图缓存 bounds/visibility，`OnAfterCreated` 后重新应用布局并触发一次工具栏重绘；否则新建标签可能短暂盖住工具栏。已验证三个标签之间的切换与内容隔离。

## 启动顺序修复

`ui.navigate` 可能在 `OnAfterCreated` 之前请求 URL。视图现在会缓存待加载 URL、尺寸与可见性，并在浏览器创建后一次性应用；否则 CEF 首次导航会被丢弃，表现为空白页。

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

安装成功只代表组件文件就位。CEF 适配层仍需 `LIORA_WITH_CEF` 构建；ad-hoc 签名在多进程模式下会触发 `-67030`，可改用上面的单进程兼容模式。界面会在内核卡片上注明“组件已安装，但当前构建未启用该内核”。

目录可带 Ed25519 签名；配置更新公钥后，签名不匹配的目录会被拒绝，`kernel-cef` 不会出现在可安装列表，也不会触发下载。

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
