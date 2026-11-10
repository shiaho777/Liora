# CEF 内核适配

## 现状

**适配层已能用真实 CEF 156 编译并链接，但还不能运行。**

- 已完成：`IKernel` 实现、`SetAsChild` 子视图渲染、加载/前进后退/缩放/注入消息、启动参数映射、外部消息泵。
- 已验证：`cmake -DLIORA_CEF_ROOT=...` 后构建 `liora_cef_kernel` 成功，包含完整的 `libcef_dll_wrapper`（246 个目标）。
- 未完成：让 Chromium 真正跑起来所需的打包与进程集成（见下）。

默认内核仍然是系统 WebView。CEF 是可选构建，不拉取就不会参与编译。

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

1. **Helper 进程包**：macOS 上 Chromium 需要 `Liora Helper.app`、`Liora Helper (GPU).app`、`Liora Helper (Renderer).app`，放在 `Contents/Frameworks`。CEF 发行包不生成它们，需要按 cefclient 的布局自行创建，并确保与主程序同一签名。
2. **主进程入口**：`main()` 必须先调用 `CefExecuteProcess()` 并把非零返回值作为退出码；只有浏览器进程继续初始化。
3. **框架嵌入**：把 `Chromium Embedded Framework.framework` 拷进 `Contents/Frameworks` 并修正 `@rpath`。
4. **消息泵**：当前用 `NSTimer` 驱动 `CefDoMessageLoopWork()`；正式版应改为在 `NSApplication` 事件循环里按需调用，避免 120Hz 空转。
5. **生命周期**：`CefInitialize`/`CefShutdown` 必须成对且在主线程；应用退出时要先关闭全部 browser。
6. **壳接入**：`shell/macos` 目前只注册系统内核；需要按 `kernels.list` 中 `cef` 是否可用决定是否调用 `cef::initialize()` 并 `attach` 内核。

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
- **签名**：下载的 CEF 框架必须与宿主同 Team ID 签名并公证，否则 macOS 加固运行时会拒绝加载。
- **进程内切换**：不同内核家族可以同时加载；同家族不同 CEF 版本需要重启并恢复会话。
