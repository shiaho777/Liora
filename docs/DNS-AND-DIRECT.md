# DNS 与直连：它到底做了什么

这份文档只讲两件事：**数据怎么流动**，以及**哪些事情做不到**。后者同样重要，因为一个诚实的能力边界比一个看起来很强但时灵时不灵的开关更有用。

## 1. 全局 DNS

设置里的 `dns.mode` 有四种取值：

| 模式 | 行为 |
|---|---|
| `auto`（默认） | 内核支持加密 DNS 时自动升级，否则用系统解析 |
| `provider` | 使用选定的提供商与协议，加密失败不回退明文 |
| `custom` | 使用自定义 DoH / DoT / DoQ 地址 |
| `off` | 完全使用系统解析 |

Chromium 原生只支持 **DoH**。DoT / DoQ 由 `liora-net` 承接：它对外提供一个本地 DoH 端点，内核把它当作普通 DoH 服务器。

```
内核 ──DoH(本地)──▶ liora-net ──DoH/DoT──▶ 上游提供商
```

本地端点使用每次启动生成的自签证书。内核通过 `--ignore-certificate-errors-spki-list=<公钥哈希>` 只信任这一张证书，**不影响其他站点的证书校验**。

## 2. 直连规则

一条规则长这样：

```json
{
  "domains": ["x.com", "*.twimg.com", "=exact.example"],
  "h3": "prefer",
  "ech": true,
  "echSource": "cloudflare-ech.com",
  "dns": "cloudflare",
  "alpn": ["h3", "h2"]
}
```

域名写法：

| 写法 | 匹配 |
|---|---|
| `x.com` | `x.com` 及其所有子域 |
| `*.x.com` | 仅子域 |
| `=x.com` | 仅 `x.com` 本身 |

规则只在命中域名时生效。命中后：

1. 用规则指定的 DNS（或全局 DNS）解析；
2. 如果查询的是 HTTPS 记录（类型 65），把 ALPN 改成配置值，并从 `echSource` 取 ECHConfig 注入；
3. 未命中 HTTPS 查询的规则域名，其余记录原样返回。

### H3 的两种强度

- **prefer**：在 HTTPS 记录里声明 `alpn=h3`。Chromium 会让 QUIC 与 TCP 竞速，通常走 QUIC。
- **force**：额外加上 `--origin-to-force-quic-on=host:443`，并让 HTTPS 记录只声明 `h3`。QUIC 不通时页面直接失败，**不回退 TCP**。

`--origin-to-force-quic-on` 只接受具体主机名，**不支持通配符**。所以：

- 规则里写 `x.com` → `x.com` 被强制；
- `api.x.com` 这类子域只能靠 HTTPS 记录优先走 h3，仍可能回落 TCP。

界面对此有明确提示。

## 3. ECH：必须说清楚的部分

ECH 的**外层 SNI 由服务端下发的 ECHConfig 里的 `public_name` 决定**，客户端无法指定。所以"把外层 SNI 设成 cloudflare-ech.com"这个说法本身不成立：我们能做的是**借用一份 Cloudflare 发布的 ECHConfig**，把它放进目标域名的 HTTPS 记录里。

由此得到三条硬约束：

1. 只有**实际托管在 Cloudflare 且启用了 ECH** 的站点才能用这份配置成功握手；
2. 对其他站点注入这份配置，握手会失败，页面打不开；
3. 想要"万能 ECH"是不存在的，这是协议设计，不是实现缺陷。

因此界面对 ECH 有显式说明，并且在规则面板里可以用"检查域名"查看命中情况。

如果目标站点自己就发布了 ECHConfig（例如很多 Cloudflare 站点通过 HTTPS 记录自带），`liora-net` **优先使用站点自己的配置**，不会覆盖。

## 4. 诊断

### 界面内的诊断

地址栏站点卡片会向 `liora-net` 的本地诊断端点发起一次真实查询，展示：

- 命中的规则（名称、H3 强度、ECH、单独指定的 DNS）
- 使用的上游（协议、地址）
- 实际解析出的地址
- 该域名 HTTPS 记录的 ALPN 与 ECH 长度
- 往返耗时

诊断端点**不是** DoH 端口：DoH 使用自签证书，网页无法直接访问，所以另开一个只绑定 `127.0.0.1` 的明文 HTTP 端口，并返回 `Access-Control-Allow-Origin: *`。它只做只读查询，不修改任何配置。

站点卡片与**加载失败页**共用这个端点：失败页会同时显示错误码含义、TLS 是否建立、是否命中规则、解析到的地址与上游，用来区分"DNS 没解析出来"和"解析出来了但连接/TLS 失败"。

失败页还能复制或保存一份纯文本报告。首行 `report: 1` 是格式版本，便于以后解析；保存时写入下载目录（`liora-diagnostic-<时间戳>.txt`）。

保存下来的报告可以在应用内重新打开：访问 `liora://diagnostics`，或 设置 → 诊断报告。界面用同一套字段定义解析文件并分区还原（页面 / 连接 / 解析 / 备注 / 未知字段 + 原始文本），解析器有 7 个单元测试覆盖版本校验、TLS、规则、上游与地址格式。

```
report: 1
Liora diagnostic report
Generated: 2026-11-10T07:29:50.007Z
Version: 0.1.0 (macos arm64)
URL: https://expired.badssl.com/
Host: expired.badssl.com
Error: -1202 - 证书校验未通过（可能已过期或不受信任）。
Kernel: system
Kernel: cef
Switches: --enable-quic --origin-to-force-quic-on=x.com:443 --dns-over-https-mode=secure
Limits: none
Components: kernel-cef@156.0.26 *
Signature: ad-hoc (no Team ID, io.liora.browser)
CEF: failed: Signature validation of current process failed (-67030)
TLS: subject=*.badssl.com valid=no expires=2015-04-12
Rule: none
Upstream: doh cloudflare https://cloudflare-dns.com/dns-query 413ms
Addresses: 104.154.89.105
HTTPS: alpn=- ech=0B
```

`Switches` / `Limits` / `Components` / `Signature` / `CEF` 让报告也能解释渲染与签名问题：Chromium 的进程校验失败（`-67030`）在报告里直接可见，不必再靠猜。`Switches` 以空格分隔，单个开关值里的逗号（如 `--origin-to-force-quic-on=x.com:443,y.com:443`）不会被拆开。

```bash
curl "http://127.0.0.1:<diagPort>/diagnose?host=x.com"
```

端口由 `liora-net` 启动时输出，壳写入 Core（`net.runtime`）。

### 规则检查

`rules.match` 返回命中规则、H3 强度、ECH 状态与生效的 DNS。设置面板的"检查域名"直接调用它，用来回答"为什么这个站点没有走直连"。

## 5. 代码位置

| 职责 | 位置 |
|---|---|
| 规则模型、匹配、预设 | `core/src/rules.cpp` |
| 规则 → 内核配置 / liora-net 配置 | `core/src/netplan.cpp` |
| 内核能力与启动参数 | `core/src/kernel.cpp` |
| DNS 提供商目录与自定义地址校验 | `core/src/dns.cpp` |
| DNS 报文编解码、HTTPS 改写 | `net/src/wire.rs` |
| 本地 DoH 服务、规则应用 | `net/src/server.rs` |
| DoH / DoT 上游 | `net/src/upstream.rs` |
| CONNECT 代理（系统 WebView 用） | `net/src/proxy.rs` |

## 6. 测试

```bash
cargo test --manifest-path net/Cargo.toml    # 报文编解码、规则匹配、配置
ctest --test-dir build --output-on-failure   # Core：规则、netplan、DNS 目录
```

真实链路验证：把 `net/` 指向 Cloudflare DoH，查询 `x.com` 的 HTTPS 记录，应得到 `alpn=['h3','h2']` 并带有非零长度的 ECH 参数；查询未命中规则的域名应原样返回。
