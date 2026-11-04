# CEF 内核适配（未实现）

这个目录预留给 Chromium Embedded Framework 内核。目前 **没有可用的 CEF 集成**，默认内核是系统 WebView。写这份说明是为了让下一步工作有明确的落点，而不是留一个空目录让人猜。

## 为什么需要它

系统 WebView 拿不到 DNS 控制权：解析由系统完成，页面 TLS 由 WebKit 发起。因此以下能力只有在 CEF 内核下才成立：

| 能力 | 系统 WebView | CEF |
|---|---|---|
| 自定义 DoH | 需走 `liora-net` 代理 | Chromium 原生 |
| ECH | 否 | 是 |
| 强制 H3、禁止 TCP 回退 | 否 | 是（启动参数 + HTTPS 记录） |
| MV3 扩展 | 否 | 是 |

## 需要实现什么

`IKernel` 的三个方法，加上一个视图：

```cpp
class CefKernel final : public liora::IKernel {
  liora::KernelInfo info() const override;      // caps: secureDns | httpsRecords | ech | forceQuic | extensions
  std::unique_ptr<IKernelView> createView(...) override;
  void applyNet(const liora::KernelNetSetup& setup) override;  // 启动参数
};
```

`KernelNetSetup` 由 Core 计算好（见 `core/src/kernel.cpp`），直接映射到 Chromium 开关：

| 字段 | 启动参数 |
|---|---|
| `dohTemplates` | `--dns-over-https-mode=secure --dns-over-https-templates=<url>` |
| `switches` 中的 SPKI | `--ignore-certificate-errors-spki-list=<hash>` |
| `forceQuicOrigins` | `--enable-quic --origin-to-force-quic-on=host:443,...` |
| `proxy`（如果需要） | `--proxy-server=http://127.0.0.1:<port>` |

网络开关是**启动参数**，所以 `KernelInfo::netNeedsRestart = true`：修改 DNS 或规则后需要重启内核进程，界面已经会提示。

## 分发方式

CEF 体积大，不适合进默认安装包。按设计走组件系统：

1. CI 产出各平台的 CEF 二进制包（macOS 需要与壳相同的 Team ID 签名并公证）；
2. 生成 `components.json`（含每个文件的 SHA-256 与签名）；
3. 应用内下载 → `ComponentManager::commit` 校验 → 激活。

`core/src/components.cpp` 已经实现了校验、原子安装与回滚，缺的是真正产出这个包。

## 已知风险

- **扩展 UI 接入**：不用 Chrome 自带工具栏时，MV3 扩展的图标、弹窗、右键菜单能否完整接入自定义界面，需要先做原型验证。
- **macOS 加固运行时**：下载的 CEF 框架必须与宿主同签名，否则加载会被拒绝。
- **进程内切换**：不同内核家族可以同时加载；同家族不同 CEF 版本需要重启并恢复会话。

在这些问题有结论之前，不建议直接开始写适配层。
