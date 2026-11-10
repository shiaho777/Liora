// Plain-language explanations for the entries in a diagnostic report's
// Switches and Limits sections. Names mirror what Core actually produces
// (core/src/kernel.cpp), so a report never explains a switch that cannot occur.

export type Locale = 'zh' | 'en';

export interface Explanation {
  title: string;
  detail: string;
}

type Copy = Record<Locale, Explanation>;

const SWITCHES: Record<string, Copy> = {
  'enable-quic': {
    zh: {
      title: '启用 QUIC',
      detail: '允许内核使用 HTTP/3。没有它，HTTPS 记录里的 h3 不会被协商，页面只能走 TCP。',
    },
    en: {
      title: 'Enable QUIC',
      detail: 'Lets the kernel speak HTTP/3. Without it, h3 in an HTTPS record is never negotiated and pages stay on TCP.',
    },
  },
  'origin-to-force-quic-on': {
    zh: {
      title: '强制这些源站走 QUIC',
      detail:
        '对列出的主机只尝试 QUIC，失败时不回退 TCP，因此页面会直接报错。只对写明的域名生效，不支持通配符。',
    },
    en: {
      title: 'Force QUIC for these origins',
      detail:
        'Attempts QUIC only for the listed hosts and does not fall back to TCP, so a failure fails the page. Wildcards are not supported.',
    },
  },
  'ignore-certificate-errors-spki-list': {
    zh: {
      title: '只信任本地解析器的证书',
      detail:
        '按公钥哈希信任 liora-net 的自签证书，用于本地 DoH。其他站点的证书仍正常校验，这不是全局关闭证书检查。',
    },
    en: {
      title: 'Trust the local resolver certificate',
      detail:
        'Trusts liora-net’s self-signed certificate by public-key hash for local DoH. Certificates for other sites are still verified; this is not a global certificate bypass.',
    },
  },
  'dns-over-https-mode': {
    zh: {
      title: 'DoH 模式',
      detail:
        'secure 表示只使用加密 DNS；automatic 允许在加密不可用时回退。secure 配合空模板会让所有解析失败。',
    },
    en: {
      title: 'DoH mode',
      detail:
        'secure uses encrypted DNS only; automatic may fall back when encryption is unavailable. secure with no template breaks every lookup.',
    },
  },
  'dns-over-https-templates': {
    zh: {
      title: 'DoH 服务器',
      detail: '内核实际查询的加密 DNS 地址。Liora 的本地解析器地址形如 https://127.0.0.1:<port>/dns-query。',
    },
    en: {
      title: 'DoH server',
      detail:
        'The encrypted DNS endpoint the kernel queries. Liora’s local resolver looks like https://127.0.0.1:<port>/dns-query.',
    },
  },
  'proxy-server': {
    zh: {
      title: '本地代理',
      detail:
        '把内核流量交给 liora-net 的 CONNECT 代理，使无法原生设置 DoH 的内核也能走加密解析。代理不可用时页面会加载失败。',
    },
    en: {
      title: 'Local proxy',
      detail:
        'Routes kernel traffic through liora-net’s CONNECT proxy so a kernel without native DoH still resolves encrypted. If the proxy is down, pages fail to load.',
    },
  },
  'use-mock-keychain': {
    zh: {
      title: '不访问系统钥匙串',
      detail: 'Chromium 使用内存中的钥匙串，保存的密码不可用。仅用于没有正式签名的测试构建。',
    },
    en: {
      title: 'Do not use the system keychain',
      detail:
        'Chromium keeps credentials in memory, so saved passwords are unavailable. Intended for unsigned test builds only.',
    },
  },
  'no-sandbox': {
    zh: {
      title: '关闭沙箱',
      detail: '降低进程隔离；只在签名或调试受限时临时使用，正式构建不应出现。',
    },
    en: {
      title: 'Disable the sandbox',
      detail:
        'Reduces process isolation. Only for constrained signing or debugging; a release build should never carry it.',
    },
  },
};

const LIMITS: Record<string, Copy> = {
  dns: {
    zh: {
      title: '加密 DNS',
      detail:
        '当前内核既不能原生设置 DoH，也不能走本地代理，所以全局加密 DNS 与直连规则都不会生效。',
    },
    en: {
      title: 'Encrypted DNS',
      detail:
        'This kernel supports neither native DoH nor the local proxy, so encrypted DNS and direct rules have no effect.',
    },
  },
  ech: {
    zh: {
      title: 'ECH',
      detail:
        '内核无法读取或注入 ECHConfig，规则里的 ECH 会被忽略。这不影响其他站点，但该规则的隐藏 SNI 目标无法实现。',
    },
    en: {
      title: 'ECH',
      detail:
        'The kernel cannot read or inject ECHConfig, so ECH on the rule is ignored. Other sites still work; the hidden-SNI goal for this rule does not.',
    },
  },
  forceQuic: {
    zh: {
      title: '强制 QUIC',
      detail:
        '内核不支持在 QUIC 失败时禁止 TCP 回退，规则会退化为“优先 h3”，而不是强制。',
    },
    en: {
      title: 'Forced QUIC',
      detail:
        'The kernel cannot forbid TCP fallback when QUIC fails, so the rule degrades to “prefer h3” instead of forcing it.',
    },
  },
  httpsRecords: {
    zh: {
      title: 'HTTPS 记录',
      detail:
        '内核不使用 HTTPS/SVCB 记录，规则里的 alpn 与 ECH 提示无法传递，直连规则基本失效。',
    },
    en: {
      title: 'HTTPS records',
      detail:
        'The kernel does not use HTTPS/SVCB records, so alpn and ECH hints from a rule never reach it and direct rules are largely inert.',
    },
  },
};

const UNKNOWN: Copy = {
  zh: {
    title: '未识别的开关',
    detail: '来自内核启动参数，当前版本没有对应说明。它仍会影响行为，排查时可保留在报告里。',
  },
  en: {
    title: 'Unrecognised switch',
    detail:
      'Comes from the kernel start-up arguments and has no explanation in this version. It still affects behaviour, so keep it in the report.',
  },
};

/** Extracts the flag name, dropping the leading `--` and any `=value`. */
export function switchName(raw: string): string {
  const trimmed = raw.trim();
  const withoutDashes = trimmed.startsWith('--') ? trimmed.slice(2) : trimmed;
  const eq = withoutDashes.indexOf('=');
  return eq >= 0 ? withoutDashes.slice(0, eq) : withoutDashes;
}

export function explainSwitch(raw: string, locale: Locale): Explanation {
  return (SWITCHES[switchName(raw)] ?? UNKNOWN)[locale];
}

export function explainLimit(name: string, locale: Locale): Explanation {
  return (LIMITS[name] ?? UNKNOWN)[locale];
}

/** Names with a known explanation; used by tests and future UI affordances. */
export function knownSwitches(): string[] {
  return Object.keys(SWITCHES).map((name) => `--${name}`);
}

export function knownLimits(): string[] {
  return Object.keys(LIMITS);
}
