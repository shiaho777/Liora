// What the address bar may claim about the current page.
//
// The address bar used to print a fixed "H3 · ECH" for every web page. That
// contradicted the project's own rule — never pretend a capability works — and
// it was wrong for the default kernel, which supports neither. A badge is only
// emitted when a rule actually asks for it *and* the kernel that renders this
// site can honour it.

/** Capability names as Core serialises them (see KernelManager::capNames). */
export type KernelCap =
  | 'secureDns'
  | 'httpsRecords'
  | 'ech'
  | 'forceQuic'
  | 'proxy'
  | 'extensions'
  | 'devtools'
  | 'isolatedProfiles';

/** Shape of `rules.match` for a matched host (see HostPlan::toJson). */
export interface HostRule {
  matched: boolean;
  ruleName?: string;
  h3?: string;
  ech?: boolean;
  alpn?: string[];
  dns?: { name?: string; protocol?: string } | null;
  kernel?: string;
}

export type BadgeTone = 'neutral' | 'accent' | 'warn';

export interface AddressBadge {
  /** Stable key, also used as the i18n suffix. */
  key: 'https' | 'insecure' | 'h3' | 'h3force' | 'ech' | 'dns';
  label: string;
  tone: BadgeTone;
  /** Sentence shown on hover: what this badge means and what it does not. */
  detail: string;
}

export interface BadgeInput {
  url: string;
  rule: HostRule | null;
  /** Capabilities of the kernel that renders this site. */
  caps: string[];
  /** Whether the local resolver is running; DoH claims depend on it. */
  resolverRunning: boolean;
}

function hostOf(url: string): string {
  try {
    return new URL(url).hostname;
  } catch {
    return '';
  }
}

function isHttps(url: string): boolean {
  return /^https:/i.test(url);
}

function isHttp(url: string): boolean {
  return /^http:/i.test(url);
}

/**
 * Builds the badges for one page. Returns an empty list for internal pages and
 * for anything we cannot substantiate, so silence means "nothing special",
 * never "we did not check".
 */
export function addressBadges(input: BadgeInput): AddressBadge[] {
  const { url, rule, caps, resolverRunning } = input;
  if (!isHttps(url) && !isHttp(url)) return [];

  const badges: AddressBadge[] = [];
  const host = hostOf(url);
  const has = (cap: KernelCap): boolean => caps.includes(cap);

  if (isHttp(url)) {
    badges.push({
      key: 'insecure',
      label: 'HTTP',
      tone: 'warn',
      detail: '这个页面用明文 HTTP 加载，内容可能被网络中间人读取或改写。',
    });
    return badges;
  }

  badges.push({
    key: 'https',
    label: 'HTTPS',
    tone: 'neutral',
    detail: '连接已加密。这不代表网站本身可信。',
  });

  const matched = Boolean(rule?.matched);
  const h3 = rule?.h3 ?? 'off';

  // Forced QUIC needs the startup switch; prefer only reorders the race and
  // still needs the kernel to read HTTPS records at all.
  if (matched && h3 === 'force' && has('forceQuic')) {
    badges.push({
      key: 'h3force',
      label: 'H3 强制',
      tone: 'accent',
      detail: `已对 ${host} 强制 HTTP/3，失败时不回退 TCP。这是启动参数，改动后需要重启内核。`,
    });
  } else if (matched && (h3 === 'force' || h3 === 'prefer') && has('httpsRecords')) {
    badges.push({
      key: 'h3',
      label: 'H3 优先',
      tone: 'accent',
      detail: `HTTPS 记录已为 ${host} 声明 h3，QUIC 与 TCP 竞速，仍可能走 TCP。`,
    });
  }

  if (matched && rule?.ech && has('ech') && has('httpsRecords')) {
    badges.push({
      key: 'ech',
      label: 'ECH',
      tone: 'accent',
      detail: `已为 ${host} 注入 ECHConfig，隐藏内层 SNI。只对持有对应私钥的站点有效。`,
    });
  }

  // A per-site DNS override is only real while the local resolver is up.
  const protocol = rule?.dns?.protocol;
  if (matched && protocol && resolverRunning) {
    badges.push({
      key: 'dns',
      label: protocol.toUpperCase(),
      tone: 'neutral',
      detail: `${host} 由规则指定的解析服务处理：${rule?.dns?.name ?? protocol}。`,
    });
  }

  return badges;
}