# Liora

**一个开源的浏览器框架：壳是核心，内核可替换。**

Liora 不是"又一个 Chromium 换皮"。它把浏览器拆成两层：

- **壳**（本仓库的 Core + Shell + WebUI）持有书签、历史、设置、网络规则和扩展元数据，负责界面与产品体验；
- **内核**只负责渲染页面，可以按需更换、按需下载。

所以切换内核不会丢数据，网络能力也不必绑死在某个内核上。

```
WebUI (Svelte 5)  ──  JSON-RPC  ──  Shell (C++ / Cocoa · Win32 · GTK · JNI)
                                        │
                                        ├── Core (C++20)：设置、DNS、规则、数据、备份、迁移、内核、组件
                                        ├── liora-net (Rust)：DoH / DoT、按域名分流、HTTPS 记录合成、CONNECT 代理
                                        └── 内核：系统 WebView（默认）/ CEF（按需下载）
```

## 当前状态

这是一个**可运行的骨架**，核心链路已经打通并有测试覆盖，但还不是功能完整的成品浏览器。

| 模块 | 状态 |
|---|---|
| Core（设置分层、DNS 目录、直连规则、书签、历史、备份、迁移、内核与组件管理、JSON-RPC） | 可用，62 个单元测试 |
| WebUI（浏览器界面、设置、DNS/直连面板、迁移向导，中英双语，明暗主题） | 可用，`svelte-check` 零错误 |
| macOS 壳（Cocoa + WKWebView + liora:// 资源协议 + 桥接） | 可构建运行 |
| liora-net（本地 DoH、DoT 上游、HTTPS 记录改写、CONNECT 代理） | 可用，已对真实 Cloudflare 上游验证 |
| Windows / Linux / Android / iOS 壳 | 未实现，接口已预留 |
| CEF 内核适配 | 可编译、可初始化；helper 进程需 Developer ID 签名才能启动（见 `kernels/cef/README.md`） |
| MV3 扩展 | 未实现 |

## 快速开始

依赖：CMake ≥ 3.24、Ninja、C++20 编译器、Node ≥ 20 + pnpm、Rust stable。

```bash
./scripts/dev.sh      # 构建界面、Core、macOS 壳，并跑全部测试
open build/shell/liora.app
```

只跑某一部分：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLIORA_BUILD_SHELL=OFF
cmake --build build --parallel && ctest --test-dir build --output-on-failure

cd ui && pnpm install && pnpm dev      # 在普通浏览器里开发界面（内置 mock 后端）
cd net && cargo test

# CEF 内核（可选，约 135MB）
./scripts/fetch-cef.sh
cmake -S . -B build-cef -G Ninja -DCMAKE_BUILD_TYPE=Release -DLIORA_BUILD_SHELL=OFF \
  -DLIORA_CEF_ROOT="$PWD/build/cef/cef_binary_156.0.26+g8ea0c73+chromium-156.0.8078.88_macosarm64_minimal"
cmake --build build-cef --target liora_cef_kernel
```

> 如果构建报 "Toolchain Swift 6.3.0 could not be located"，说明 `PATH` 里有损坏的编译器转发脚本。`scripts/dev.sh` 已经固定使用 Xcode 的 `clang` / `ar` / `ranlib`，Rust 也使用同一个链接器。

## 已经能做什么

### DNS 与直连（核心差异化能力）

- 内置 AdGuard DNS 知识库里的主流提供商，按广告拦截、家庭保护、恶意网站拦截、无过滤、隐私优先、国内可用分类。
- 一键切换 DoH / DoT / DoQ；DoT 与 DoQ 由 `liora-net` 承接，因为 Chromium 原生只支持 DoH。
- 支持自定义 DoH / DoT / DoQ 地址，输入即校验。
- 按域名规则：优先或强制 H3、启用 ECH、单独指定 DNS。
- **只对规则命中的域名生效**，其他站点保持标准浏览器行为。

实测（本地 DoH → Cloudflare 上游）：

```
x.com        → HTTPS 记录 alpn=['h3','h2']，ECH 71 字节（来自 cloudflare-ech.com）
example.com  → 原样转发，未做任何改写
```

### 数据与迁移

- 书签、历史、设置、站点权限存在 Liora 自己的 SQLite 里，与内核无关。
- `.liora` 备份包含校验和，可分区块恢复；自动备份保留最近 7 份。
- 迁移支持 Chrome / Edge / Brave / Arc / Vivaldi / Opera / Yandex、Firefox / LibreWolf / Zen、Safari，以及通用的书签 HTML。
- 迁移是只读的（先复制数据库），重复执行不会产生重复条目。

### 日常浏览

- 标签页、会话恢复、书签栏与 `Cmd+D` 收藏。
- 下载：无法渲染的响应自动保存到下载目录（重名自动编号），工具栏面板显示进度，页面显示下载状态；完成时会弹出带“在文件夹中显示”的提示；`downloads.ask` 打开时改为询问保存位置。完成/失败的记录写入 Core，重启后仍在。
- 站点权限：摄像头 / 麦克风请求会先询问，可勾选“记住”；记住的决定在 设置 → 数据与备份 → 站点权限 中列出并可撤销。
- 快捷键由菜单栏转发，页面获得焦点时同样生效（`Cmd+T/W/R/L/,`、`Cmd+Shift+J` 下载面板）。
- 页面加载失败时显示诊断面板：失败原因（按系统错误码翻译）、是否建立 TLS/证书主体、是否命中直连规则、实际解析到的地址与所用上游，并可一键为该站点启用直连后重试。面板可展开并复制一份纯文本诊断报告（首行 `report: 1` 版本标记，含 URL、错误码、TLS、规则、地址、上游、内核启动参数、组件、签名状态、解析器最近重启时间/次数，以及上次应用的网络配置与当前差异），也可直接保存到下载目录。
- 诊断报告可在应用内重新打开：`liora://diagnostics`（或 设置 → 诊断报告）会解析保存的 `liora-diagnostic-*.txt`，按页面、内核与签名、上次应用与差异、连接、解析分区还原失败现场；也支持直接粘贴报告文本。查看器会重新读取当前状态，如果报告保存后配置或已应用快照又变了，会显示“报告与当前状态不一致”，并列出报告时与当前的应用状态/差异，避免把历史快照当成现在；报告文本本身也包含 `Timeline:` 段落，记录生成、配置应用与解析器启动的顺序；查看器优先按保存的顺序展示历史事件，再追加当前状态，因此分享出去的报告不会丢失事件顺序。时间线旁还有“复制时间线”和“保存时间线”：前者导出到剪贴板，后者通过壳写入下载目录（`liora-timeline-<时间戳>.txt`），内容都是带 ISO 时间戳、按时间排序的纯文本。保存成功后的提示带有“在文件夹中显示”，可直接打开下载目录。如果报告生成后解析器又重启过，会单独提示“报告生成后解析器又重启了 N 次”，但不会因此误报配置不一致。提示旁可一键“保存当前状态的新报告”，沿用原失败上下文、网络配置取当前值。每个启动参数与“内核不支持”项都带有作用与后果说明（例如 `--origin-to-force-quic-on` 失败时不回退 TCP），不需要读者自己翻译 Chromium 开关。查看器还会把报告与**当前配置**交叉比对：强制 QUIC 主机对照当前规则，`--dns-over-https-templates` 对照当前 DNS 模式/提供商与内核实际会拿到的模板，`--proxy-server` 对照当前解析器端口。网络开关只在启动时读取，规则/DNS/代理主机不一致会合并成一条“内核需要重启”，逐项列出报告值与当前值，并提供**重启内核**按钮：代理端口因解析器重启而变化时不算配置过期，查看器只提示“本地解析器已重启”。它会重启 liora-net、重新应用代理并重载内核视图，成功后把报告里的启动参数刷新为当前值。DNS 设置页也有同样的操作，但只在确有未应用的更改时显示：壳记录内核视图最后一次加载的配置快照，当前 DNS/规则与之不同才出现“有未应用的更改 / 重启”，重启后提示消失。这个状态是全局的：工具栏会显示“有未应用的更改”徽标，设置导航的“DNS 与直连”也会标记“未应用”，离开 DNS 页后仍然可见。点击工具栏徽标会直接应用：重启解析器、重载页面，成功后短暂显示“已应用 · 代理 <端口>”，tooltip 给出应用时间与代理地址；失败则显示“应用失败”并保留徽标。DNS 面板还会显示“解析器最近重启”的时间与累计次数（持久化在 `net-resolver.json`，跨导航与重启保留），以及持久化的“上次应用”时间、提供商与代理端口，离开页面或重启应用后仍可查看；如果之后又改了设置但尚未应用，会追加“与当前配置的差异”（例如“提供商 Quad9 Secure → Cloudflare Standard · 代理 :55977 → :56030”）。需要重启内核的内核显示“重启内核”，系统 WebView 走代理时显示“重启解析器”。未知字段会保留显示，版本不匹配会明确报错。
- 地址栏站点卡片：一个入口看到该站点的权限（可撤销）、证书摘要（主体与有效期）、解析诊断（上游、实际地址、ALPN/ECH、耗时）、直连规则命中情况（未命中时可一键添加）以及所用内核。

## 设计原则

1. **自动 → 一键 → 专家**：默认就能用；遇到问题点一下；需要时再进高级设置。
2. **按需加载**：默认只用系统 WebView，CEF 等组件下载后校验签名与哈希再安装。
3. **不假装支持**：内核做不到的能力会明确标注，而不是给一个无效开关。
4. **默认零遥测**：崩溃报告需要主动开启。
5. **可跟进上游**：与 Chromium 的耦合集中在 `liora-net` 的本地 DoH 与少量启动参数，便于跟随上游更新。

## 目录

```
core/       C++20：设置、DNS、规则、存储、备份、迁移、内核与组件、RPC
net/        Rust：本地 DoH、DoT 上游、HTTPS 记录合成、CONNECT 代理
ui/         Svelte 5 + Vite：浏览器界面与全部设置面板
shell/      平台宿主：macos/ 已实现，其余平台预留
kernels/    内核适配（CEF 为下一步）
data/       内置数据：DNS 提供商目录、规则预设、搜索引擎
docs/       架构与能力边界说明
scripts/    开发脚本
```

## 已知边界

- **ECH 只对托管在支持 ECH 的 CDN 上的站点有效。** 外层 SNI 由服务端下发的 ECHConfig 决定，客户端无法指定。借用 `cloudflare-ech.com` 的配置只对 Cloudflare 站点成立；其他站点会连接失败，界面上有对应提示。
- **强制 H3 只对规则里写死的域名生效**（`--origin-to-force-quic-on` 不支持通配符），子域依靠 HTTPS 记录优先走 h3，仍可能回落 TCP。
- **系统 WebView 不支持 ECH / 强制 H3 / 自定义 DoH**，这些功能需要 CEF 内核。
- 本仓库的 macOS 壳是单窗口实现；多标签的原生视图编排是下一阶段工作。

更完整的说明见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) 与 [`docs/DNS-AND-DIRECT.md`](docs/DNS-AND-DIRECT.md)。

## 路线图

1. **CEF 内核适配**：`--enable-quic`、`--origin-to-force-quic-on`、本地 DoH 连接、MV3 扩展。
2. **多标签原生编排**：每个标签一个内核视图，支持按站点/按标签切换内核。
3. **Windows / Linux 壳**：WebView2 与 WebKitGTK。
4. **扩展系统**：自有插件 API + MV3 兼容层。
5. **同步**：端到端加密、可自托管。

## 参与

- 提交信息使用祈使句，说明"做了什么、为什么"。
- 改动 Core 时请同时补 `core/tests` 里的测试。
- 新增依赖前先确认许可证兼容（Apache-2.0）。

## 许可证

[Apache-2.0](LICENSE)。"Liora" 名称与标识不在许可证授权范围内。
