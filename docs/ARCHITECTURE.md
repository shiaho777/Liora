# Liora 架构

## 原则

1. **壳是核心，内核可替换。** 书签、历史、设置、规则、扩展元数据都归 Liora Core 管理，与内核无关。切换内核不丢数据。
2. **三层体验。** 每个功能都要有三层：自动（默认即最优）、一键（遇到问题点一下）、专家（完全掌控）。
3. **按需加载。** 默认安装包只使用系统 WebView。CEF 内核、liora-net、迁移器、扩展运行时都是可下载组件，经签名校验后安装。
4. **网络深度定制只对规则命中的域名生效。** 其他网站保持内核的标准行为。
5. **默认零遥测。** 崩溃报告需要用户主动开启。
6. **许可证：** Apache-2.0。

## 分层

```
┌──────────── WebUI（Svelte 5 + Vite，静态资源打包进二进制）────────────┐
│ 浏览器界面（地址栏、标签） · 设置 · 新标签页 · DNS/直连 · 迁移向导   │
└──────────────── liora:// 桥接（JSON-RPC 2.0 over postMessage）────────┘
┌──────────────── Shell（C++20，每个平台一个薄宿主）────────────────────┐
│ macOS: Cocoa + WKWebView      Windows: Win32 + WebView2               │
│ Linux: GTK + WebKitGTK        Android: JNI + System WebView           │
│ iOS: UIKit + WKWebView                                                │
└───────────────────────────────────────────────────────────────────────┘
┌──────────────── Core（C++20，平台无关，不含 UI）──────────────────────┐
│ Settings（全局 → 站点 → 标签页 三级覆盖） · DnsCatalog · RuleEngine   │
│ Profiles · Bookmarks/History（SQLite） · Backup（.liora 包）          │
│ Migration · KernelManager · ComponentManager · RpcRouter              │
└───────────────────────────────────────────────────────────────────────┘
┌──── Kernel 接口（IKernel / IKernelView）────┐ ┌── liora-net（Rust，可选）──┐
│ SystemWebView（内置） · CEF（按需下载）      │ │ 本地 DoH 服务 + 规则改写   │
└──────────────────────────────────────────────┘ │ CONNECT 代理（按域名解析） │
                                                 └────────────────────────────┘
```

### 为什么这样分

- **Core 用 C++20：** 所有平台都能直接链接，包括 Android（NDK）和 iOS。没有运行时依赖。
- **liora-net 用 Rust：** 需要 DoH、DNS 报文解析、HTTP/2、QUIC。Rust 生态（hickory-proto、rustls、quinn）在这些方面成熟且内存安全。它是独立进程或静态库，通过 C ABI 与 Core 通信，不污染 Core。
- **WebUI 用 Svelte：** 编译后体积小，没有虚拟 DOM 运行时。同一套界面在所有平台和所有内核上复用。
- **第三方依赖：** Core 只依赖 SQLite（amalgamation）和 nlohmann/json，通过 CMake FetchContent 固定版本获取。

### 视图层级的一个实际约束

壳把内核视图作为原生子视图叠加在 UI WebView 之上。因此 **HTML 浮层无法覆盖网页内容区**（下载面板被空白视图挡住过，站点卡片也被裁切过）。

约定：

- 工具栏内的浮层（下载面板）只在工具栏高度内展开；
- 需要延伸到内容区的面板（站点卡片）作为**流式抽屉**插入布局，内容区随之缩小，原生视图通过 `ResizeObserver` 上报的矩形重新定位；
- 全屏状态（错误页、下载状态页）通过隐藏内核视图实现。

新增 UI 浮层时先想清楚它落在哪一类，否则会被原生视图遮住。

## 内核抽象

```cpp
class IKernel {
  virtual KernelInfo info() const = 0;            // id、版本、能力位
  virtual Capabilities capabilities() const = 0;   // DoH、ECH、强制 QUIC、MV3、代理……
  virtual std::unique_ptr<IKernelView> createView(const ViewOptions&) = 0;
  virtual void applyNetworkConfig(const NetworkConfig&) = 0;
};
```

- 界面根据 `capabilities()` 决定显示什么。某个内核不支持的功能会明确标注，而不是假装生效。
- 切换内核的粒度：全局默认、按站点、按标签页。

## 网络能力在各内核上的实现

| 能力 | CEF 内核 | 系统 WebView 内核 |
|---|---|---|
| 全局加密 DNS（DoH） | Chromium 原生 Secure DNS | 经 liora-net CONNECT 代理，由代理用 DoH 解析 |
| DoT / DoQ 提供商 | liora-net 转换为本地 DoH | 同左 |
| 按域名指定 DNS | 本地 DoH 服务按域名分流 | CONNECT 代理按域名分流 |
| HTTPS 记录注入（`alpn=h3`、`ech`） | 本地 DoH 服务改写响应 | 不支持（TLS 由系统 WebView 发起，无法注入） |
| ECH | Chromium 原生 | 不支持 |
| 强制 H3、禁止 TCP 回退 | `--origin-to-force-quic-on` | 不支持 |

系统 WebView 的代理设置途径：

- macOS 14+ / iOS 17+：`WKWebsiteDataStore.proxyConfigurations`
- Windows：WebView2 浏览器参数 `--proxy-server`
- Linux：`webkit_network_session_set_proxy_settings`
- Android：androidx.webkit `ProxyController`

## 直连规则

### 数据模型

```json
{
  "id": "r-x",
  "match": "x.com",
  "includeSubdomains": true,
  "enabled": true,
  "dnsProvider": "cloudflare",
  "h3": "force",
  "ech": { "enabled": true, "configSource": "cloudflare-ech.com" },
  "httpsRecord": { "alpn": ["h3", "h2"], "injectEch": true }
}
```

- `h3`：`off`（不干预）、`prefer`（HTTPS 记录声明 `alpn=h3`，Chromium 会让 QUIC 与 TCP 竞速）、`force`（强制 QUIC，失败即失败，不回退 TCP）。
- `ech.configSource`：从该域名的 HTTPS 记录中取 ECHConfigList，注入到规则域名的 HTTPS 记录中。默认是 `cloudflare-ech.com`，外层 SNI 由 ECHConfig 中的 public_name 决定。

### 已知边界

- 借用 `cloudflare-ech.com` 的 ECH 配置，只对托管在 Cloudflare 上的站点有效。目标服务器必须持有对应的 ECH 私钥。规则面板会对每条规则做诊断，结果分为可用、仅 H3 可用、不可用三种，并说明原因。
- 强制 H3 需要重启 CEF 进程才能生效，因为它是启动参数。界面会提示。
- 本地 DoH 服务使用自签证书。CEF 通过 `--ignore-certificate-errors-spki-list` 只信任这一个证书公钥，不影响其他站点的证书校验。

### CEF 下的数据流

```
Chromium Secure DNS ──DoH──▶ liora-net（127.0.0.1）
                              ├─ 命中规则：用规则指定的提供商查询，改写 HTTPS 记录
                              └─ 未命中：用全局提供商转发，原样返回
```

## 数据与迁移

- 用户数据目录：`<平台数据目录>/Liora/Profiles/<profile>/`
  - `settings.json`：设置和规则
  - `liora.db`：书签、历史（SQLite，WAL 模式）
- 备份格式 `.liora`：一个 JSON 清单加各数据段，可选择包含的内容。导入时按段合并，冲突以 URL 去重。
- 迁移器优先支持 Chrome / Edge / Brave（`Bookmarks` JSON 与 `History` SQLite）、Firefox（`places.sqlite`）、Safari（`Bookmarks.plist`）。迁移前会复制源数据库，避免与正在运行的浏览器冲突。

## 组件下载

- 组件清单：`components.json`，包含 id、版本、平台、URL、SHA-256、Ed25519 签名。
- 下载后先校验哈希和签名，再解压到 `<数据目录>/Components/<id>/<version>/`，最后原子切换 `current` 指针。

## 目录结构

```
Liora/
├── core/            C++20 Core（静态库 + 单元测试）
├── net/             liora-net（Rust）
├── ui/              WebUI（Svelte 5 + Vite）
├── shell/
│   ├── common/      跨平台宿主逻辑（RPC 桥、窗口与标签模型）
│   ├── macos/       Cocoa + WKWebView
│   ├── windows/     Win32 + WebView2
│   ├── linux/       GTK + WebKitGTK
│   ├── android/     Gradle 工程 + JNI
│   └── ios/         Xcode 工程
├── kernels/
│   └── cef/         CEF 内核适配（按需构建、按需下载）
├── data/            内置数据（DNS 提供商目录等）
├── docs/
└── .github/workflows/
```
