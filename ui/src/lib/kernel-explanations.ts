// Plain-language explanations for the entries in a diagnostic report's
// Switches and Limits sections. Names mirror what Core actually produces
// (core/src/kernel.cpp), so a report never explains a switch that cannot occur.

export type Locale = 'zh' | 'en';

export interface Explanation {
  title: string;
  detail: string;
}

/** Facts from the report that turn a generic description into a concrete one. */
export interface ExplanationContext {
  ruleName?: string;
  upstream?: string;
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

/** The `=value` part of a switch, or an empty string. */
export function switchValue(raw: string): string {
  const trimmed = raw.trim();
  const withoutDashes = trimmed.startsWith('--') ? trimmed.slice(2) : trimmed;
  const eq = withoutDashes.indexOf('=');
  return eq >= 0 ? withoutDashes.slice(eq + 1) : '';
}

function ruleLabel(context: ExplanationContext, locale: Locale): string {
  if (!context.ruleName) return locale === 'zh' ? '该规则' : 'the rule';
  return locale === 'zh' ? `规则“${context.ruleName}”` : `rule “${context.ruleName}”`;
}

function explainOriginSwitch(value: string, locale: Locale): Explanation {
  const hosts = value
    .split(',')
    .map((host) => host.trim())
    .filter(Boolean);
  if (hosts.length === 0) return SWITCHES['origin-to-force-quic-on'][locale];
  if (locale === 'zh') {
    return {
      title: '强制这些源站走 QUIC',
      detail: `本报告强制以下主机只走 QUIC：${hosts.join('、')}。失败不会回退 TCP，因此这些站点会直接报错；通配符不受支持。`,
    };
  }
  return {
    title: 'Force QUIC for these origins',
    detail: `This report forces QUIC for: ${hosts.join(', ')}. These hosts do not fall back to TCP, so a QUIC failure fails the page; wildcards are not supported.`,
  };
}

function explainTemplatesSwitch(value: string, locale: Locale): Explanation {
  if (!value) return SWITCHES['dns-over-https-templates'][locale];
  const port = /:(\d+)(?:\/|$)/.exec(value)?.[1];
  if (locale === 'zh') {
    return {
      title: 'DoH 服务器',
      detail: `本报告使用 ${value}${port ? `（本地端口 ${port}）` : ' '}作为唯一的加密 DNS 地址。它不可用时所有解析都会失败。`,
    };
  }
  return {
    title: 'DoH server',
    detail: `This report uses ${value}${port ? ` (local port ${port})` : ''} as the only encrypted DNS endpoint. If it is unreachable, every lookup fails.`,
  };
}

function explainSpkiSwitch(value: string, locale: Locale): Explanation {
  if (!value) return SWITCHES['ignore-certificate-errors-spki-list'][locale];
  const prefix = value.length > 10 ? `${value.slice(0, 8)}…` : value;
  if (locale === 'zh') {
    return {
      title: '只信任本地解析器的证书',
      detail: `本报告只信任一个本地解析器公钥（${prefix}）。这是按公钥放行单个证书，其他站点的证书仍正常校验。`,
    };
  }
  return {
    title: 'Trust the local resolver certificate',
    detail: `This report trusts one local resolver key (${prefix}). It allows a single certificate by public key; certificates for other sites are still verified.`,
  };
}

export function explainSwitch(
  raw: string,
  locale: Locale,
  context: ExplanationContext = {},
): Explanation {
  const name = switchName(raw);
  const value = switchValue(raw);
  if (name === 'origin-to-force-quic-on') return explainOriginSwitch(value, locale);
  if (name === 'dns-over-https-templates') return explainTemplatesSwitch(value, locale);
  if (name === 'ignore-certificate-errors-spki-list') return explainSpkiSwitch(value, locale);
  if (name === 'enable-quic' && context.ruleName && locale === 'zh') {
    return {
      title: '启用 QUIC',
      detail: `本报告为${ruleLabel(context, locale)}启用了 QUIC，因此 HTTPS 记录里的 h3 可以被协商；但只有被强制列出的主机保证不走 TCP。`,
    };
  }
  return (SWITCHES[name] ?? UNKNOWN)[locale];
}

function explainLimitWithRule(
  name: string,
  locale: Locale,
  context: ExplanationContext,
): Explanation {
  // DNS is about the upstream, not a rule, so it must not depend on ruleName.
  if (name === 'dns' && context.upstream) {
    return locale === 'zh'
      ? {
          title: '加密 DNS',
          detail: `本报告的上游是 ${context.upstream}，但当前内核既不能原生设置 DoH，也不能走本地代理，因此加密 DNS 不会生效。`,
        }
      : {
          title: 'Encrypted DNS',
          detail: `This report uses ${context.upstream} upstream, but this kernel supports neither native DoH nor the local proxy, so encrypted DNS has no effect.`,
        };
  }
  if (!context.ruleName) return LIMITS[name][locale];
  const rule = ruleLabel(context, locale);
  if (name === 'ech') {
    return locale === 'zh'
      ? {
          title: 'ECH',
          detail: `${rule}启用了 ECH，但当前内核无法读取或注入 ECHConfig，所以这条规则的 ECH 不会生效。`,
        }
      : {
          title: 'ECH',
          detail: `${rule} enables ECH, but this kernel cannot read or inject ECHConfig, so ECH for this rule has no effect.`,
        };
  }
  if (name === 'forceQuic') {
    return locale === 'zh'
      ? {
          title: '强制 QUIC',
          detail: `${rule}要求强制 QUIC，但当前内核不允许禁止 TCP 回退，实际只会“优先 h3”。`,
        }
      : {
          title: 'Forced QUIC',
          detail: `${rule} asks for forced QUIC, but this kernel cannot forbid TCP fallback, so it only prefers h3 in practice.`,
        };
  }
  if (name === 'httpsRecords') {
    return locale === 'zh'
      ? {
          title: 'HTTPS 记录',
          detail: `${rule}依赖 alpn 与 ECH 提示，但当前内核不使用 HTTPS/SVCB 记录，这些提示传不过去。`,
        }
      : {
          title: 'HTTPS records',
          detail: `${rule} relies on alpn and ECH hints, but this kernel does not use HTTPS/SVCB records, so those hints never reach it.`,
        };
  }
  return LIMITS[name][locale];
}

export function explainLimit(
  name: string,
  locale: Locale,
  context: ExplanationContext = {},
): Explanation {
  return explainLimitWithRule(name, locale, context);
}

/** Names with a known explanation; used by tests and future UI affordances. */
export function knownSwitches(): string[] {
  return Object.keys(SWITCHES).map((name) => `--${name}`);
}

export function knownLimits(): string[] {
  return Object.keys(LIMITS);
}
