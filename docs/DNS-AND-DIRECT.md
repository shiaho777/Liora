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
Resolver: restarted 2026-11-10T08:42:58.529Z (run 2)
Timeline: 2026-11-10T08:42:58.528Z resolver run 2 · 2026-11-10T08:42:58.529Z applied · 2026-11-10T08:43:00.000Z report
Applied: 2026-11-10T08:27:51.284Z · Quad9 Secure (doh) · proxy http://127.0.0.1:57009
Delta: provider Quad9 Secure -> Cloudflare Standard · proxy http://127.0.0.1:57009 -> http://127.0.0.1:57051
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

`Resolver` 记录本地解析器最近一次成功启动的时间与累计次数，便于判断失败前后是否发生过解析器重启。`Applied` / `Delta` 记录上次应用的网络配置与当前差异（修改后未应用时尤其有用）；查看器会在“内核与签名”分区显示解析器重启信息，并单独渲染“上次应用与差异”分区。打开报告时还会重新读取 `net.lastApplied` 与当前 `dns.plan`，如果报告保存后状态又变了，会显示：

```
报告与当前状态不一致
报告时已应用: Quad9 Secure (doh) · http://127.0.0.1:58432
当前已应用: Cloudflare Standard (doh) · http://127.0.0.1:58632
报告时的差异: 提供商 Quad9 Secure → Cloudflare Standard · 代理 ...58432 → ...58499
当前的差异: 无
```

报告里的 `Timeline:` 保存了事件顺序；查看器优先使用它，并在后面追加当前状态。时间线旁的“复制时间线”与“保存时间线”会导出同一份纯文本（按时间排序，包含历史与当前事件）；保存时写入下载目录，文件名 `liora-timeline-<时间戳>.txt`：

```
Liora diagnostics timeline
Exported: 2026-11-10T08:53:48.271Z
2026-11-10T08:53:35.565Z  resolver  run 2
2026-11-10T08:53:35.566Z  applied  Cloudflare Standard (doh) · http://127.0.0.1:49704
2026-11-10T08:53:42.703Z  generated
2026-11-10T08:53:45.438Z  current-applied  Cloudflare Standard (doh) · http://127.0.0.1:49704
2026-11-10T08:53:45.438Z  current-resolver  run 3
```

打开报告时展示：

```
时间线
16:47:53  应用配置        Cloudflare Standard (doh) · :64077
16:47:53  解析器启动（第 2 次）
16:48:00  诊断报告生成
16:48:08  当前应用配置    Cloudflare Standard (doh) · :64142
16:48:11  当前解析器启动（第 6 次）
16:48:11  当前仍有 2 项未应用差异
```

报告里的 `Resolver` 次数也会与当前 `net.resolverStatus` 比较。若报告记录第 2 次、当前已是第 3 次，查看器显示：

```
报告生成后解析器又重启了 1 次。
```

这只是进程变化，不会触发“报告与当前状态不一致”。

提示旁的“保存当前状态的新报告”会用同一个报告生成器写出新文件：URL、错误码、TLS、规则与解析结果沿用原报告，内核参数、组件、签名、上次应用与差异取当前值。重新打开新报告时，上述“不一致”提示不会出现（代理端口随进程重启变化，不再单独把报告判为过期）。
`Switches` / `Limits` / `Components` / `Signature` / `CEF` 让报告也能解释渲染与签名问题：Chromium 的进程校验失败（`-67030`）在报告里直接可见，不必再靠猜。`Switches` 以空格分隔，单个开关值里的逗号（如 `--origin-to-force-quic-on=x.com:443,y.com:443`）不会被拆开。

查看器会为每个开关和每个 `Limits` 项显示一句作用与后果说明，并把报告里的具体值带进去（例如实际被强制的主机、本地 DoH 端口、受影响的规则名）：

```
--origin-to-force-quic-on=x.com:443,twitter.com:443,api.x.com:443
  本报告强制以下主机只走 QUIC：x.com:443、twitter.com:443、api.x.com:443。
  失败不会回退 TCP，因此这些站点会直接报错；通配符不受支持。

--dns-over-https-templates=https://127.0.0.1:5300/dns-query
  本报告使用 https://127.0.0.1:5300/dns-query（本地端口 5300）作为唯一的加密 DNS 地址。

ech
  规则“x.com”启用了 ECH，但当前内核无法读取或注入 ECHConfig…
```

查看器还会把 `--origin-to-force-quic-on` 与**当前启用的规则**交叉比对（逻辑与 `core/src/netplan.cpp` 一致：跳过 `*.`、去掉 `=`、补 `:443`、去重）。因为网络开关只在启动时读取，规则改过但内核未重启时，报告里的强制主机就会过期：

```
内核启动参数已过期
当前规则与报告里记录的强制 QUIC 主机不一致。网络开关只在启动时读取，
修改 DNS 或直连规则后需要重启内核才会生效。
报告里多出: x.com:443, twitter.com:443
当前规则新增: example.org:443
```

同样会比对 DoH 模板：优先使用 `kernels.setup` 给出的模板（它可能是 liora-net 的 `https://127.0.0.1:<port>/dns-query`），再辅以 `dns.plan` 的当前模式与提供商。

```
DNS 启动参数已过期
报告使用的 DoH 地址与当前 DNS 配置不一致。DNS 设置只在启动时读取，
修改 DNS 模式或提供商后需要重启内核。
报告: https://dns.quad9.net/dns-query
当前: https://cloudflare-dns.com/dns-query
当前配置: Cloudflare Standard · doh · secure
```

一致时显示“DoH 地址与当前 DNS 配置一致”；尾部斜杠差异不会误报。

代理端口也会比对，但仅端口变化不算配置过期：liora-net 每次启动都绑定新的临时端口，所以同主机、不同端口只表示解析器重启，查看器显示“本地解析器已重启”，不触发“内核需要重启”。只有主机变化或代理出现/消失才算真正的配置差异。

```
本地解析器已重启：报告中的代理端口 :49255，当前为 :49337。
这只是端口变化，不是配置差异。

主机变化时仍需重启：

代理启动参数已过期
报告: http://localhost:1
当前: http://127.0.0.1:49337
```

三类检查（强制 QUIC 主机、DoH 地址、代理端口）会合并成一条结论，逐项列出差异，而不是同时抛出三条警告：

```
内核需要重启
以下启动参数与当前配置不一致。网络开关只在启动时读取，
修改直连规则、DNS 或解析器后需要重启内核才会生效。
强制 QUIC 主机
  报告: x.com:443, twitter.com:443
  当前: 无
  报告里多出: x.com:443, twitter.com:443
DoH 地址
  报告: https://dns.quad9.net/dns-query
  当前: https://cloudflare-dns.com/dns-query
代理地址
  报告: http://127.0.0.1:1
  当前: 无
```

警告旁的“重启内核”会重启 liora-net（因此代理端口会变化）、重新应用 WebView 代理并重载内核视图；成功后查看器把开关列表刷新为当前值，重新比对并显示“启动参数与当前配置一致”。原始报告文本保持不变，便于对照。

DNS 设置页复用同一个壳端路径：当当前内核声明 `netNeedsRestart` 时显示“重启内核”；系统 WebView 虽然不需要重启内核，但依赖 liora-net 代理，所以显示“重启解析器”。两者都会让新设置生效并重载已打开的页面。

这个入口只在确有未应用的更改时出现。壳把内核视图最后一次加载的 `net.config` 快照写入 `net.runtime.appliedConfig`；界面把它与当前 `net.config` 做规范化比较（键排序、忽略空白）。修改提供商或规则后：

```
有未应用的更改
已打开的页面仍在使用旧的 DNS/规则与代理端口；重启解析器并重载页面后生效。
[重启解析器]
```

点击重启后快照更新，提示与按钮消失；启动时两者一致，所以面板不会常驻重启按钮。壳还会把这次应用写入 `net-applied.json`，DNS 面板显示：

```
上次应用 16:23:15 · Quad9 Secure (doh) · 代理 :55073
```

该记录来自 Core，离开 DNS 页再回来或重启应用后仍可查看；Core 启动时读取该文件，测试覆盖了重启后的恢复。

解析器每次成功启动也会记录到 `net-resolver.json`（时间与累计次数），DNS 面板显示：

```
解析器最近重启 16:41:08（第 2 次）
```

这个值在导航离开/返回以及应用重启后保持，自动刷新解析器或手动重启都会递增。它和代理端口提示配合，用来区分“解析器换端口”与“配置真的变了”。

如果之后又改了提供商、模式或代理端口但还没有应用，面板会在记录下方追加差异：

```
上次应用 16:25:21 · Quad9 Secure (doh) · 代理 :55977
与当前配置的差异: 提供商 Quad9 Secure → Cloudflare Standard · 代理 :55977 → :56030
```

差异字段只列出真正变化的项（提供商、协议、模式、代理），并忽略大小写与代理地址末尾的斜杠。设置变更触发的自动解析器刷新**不会**更新该快照，因为已打开的页面还没有重新加载。

这个判断是全局的：工具栏显示“有未应用的更改”徽标，设置侧栏的“DNS 与直连”显示“未应用”。离开 DNS 页后仍然可见。

点击工具栏徽标即可直接应用，不必打开 DNS 页：它会重启解析器、重新应用代理并重载页面。成功后短暂显示绿色徽标，文本为“已应用 · 代理 54231”，tooltip 为“最近应用：16:20:24 · 代理 127.0.0.1:54231”。若重启后仍未生效（例如解析器启动失败），显示“应用失败”并保留徽标，同时弹出错误提示。

全部一致时显示“启动参数与当前配置一致”；部分检查失败时额外注明哪些检查没有完成。旧格式报告（没有 `Switches` 字段）会明确显示“无法比对”，而不是误报一致。

条目与通用含义：

| 条目 | 说明要点 |
|---|---|
| `--enable-quic` | 允许 HTTP/3；缺失时 h3 不会被协商 |
| `--origin-to-force-quic-on` | 只尝试 QUIC、不回退 TCP；失败即页面失败 |
| `--ignore-certificate-errors-spki-list` | 仅按公钥信任本地 DoH 证书，不是全局关闭校验 |
| `--dns-over-https-mode` | secure 只走加密 DNS；与空模板组合会让解析全失败 |
| `--dns-over-https-templates` | 内核实际查询的 DoH 地址 |
| `--proxy-server` | 经 liora-net 转发；代理不可用则页面失败 |
| `ech` | 无法读取/注入 ECHConfig，规则的 ECH 被忽略 |
| `forceQuic` | 无法禁止 TCP 回退，退化为“优先 h3” |
| `httpsRecords` | 不使用 HTTPS/SVCB 记录，alpn 与 ECH 提示无法传递 |
| `dns` | 既无原生 DoH 也无代理，加密 DNS 与规则均不生效 |

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
