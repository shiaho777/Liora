// Cross-checks the forced-QUIC hosts recorded in a diagnostic report against
// the rules that are active now.
//
// Chromium reads network switches once at start-up, so changing a rule does not
// affect a running kernel. A report captured before a restart can therefore
// list origins that no longer exist (or miss new ones). This comparison makes
// that staleness explicit instead of letting the report mislead.

export interface RuleLike {
  name?: string;
  enabled: boolean;
  domains: string[];
  h3: string;
}

export interface ForcedOriginsCheck {
  /** Origins parsed from the report's --origin-to-force-quic-on switch. */
  reported: string[];
  /** Origins the current enabled rules would produce. */
  expected: string[];
  /** In the current rules but absent from the report. */
  missing: string[];
  /** In the report but no longer produced by the rules. */
  extra: string[];
  stale: boolean;
}

/** The `=value` part of a specific switch, or an empty string. */
function switchValue(switches: string[], name: string): string {
  for (const raw of switches) {
    const trimmed = raw.trim();
    const withoutDashes = trimmed.startsWith('--') ? trimmed.slice(2) : trimmed;
    const eq = withoutDashes.indexOf('=');
    const key = eq >= 0 ? withoutDashes.slice(0, eq) : withoutDashes;
    if (key === name) return eq >= 0 ? withoutDashes.slice(eq + 1) : '';
  }
  return '';
}

export function parseForcedOrigins(switches: string[]): string[] {
  const value = switchValue(switches, 'origin-to-force-quic-on');
  if (!value.trim()) return [];
  const seen = new Set<string>();
  const out: string[] = [];
  for (const item of value.split(',')) {
    const origin = item.trim().toLowerCase();
    if (!origin || seen.has(origin)) continue;
    seen.add(origin);
    out.push(origin);
  }
  return out;
}

/** Mirrors core/src/netplan.cpp: `*.` is skipped, `=` is stripped, port 443. */
export function expectedForcedOrigins(rules: RuleLike[], rulesEnabled: boolean): string[] {
  if (!rulesEnabled) return [];
  const seen = new Set<string>();
  const out: string[] = [];
  for (const rule of rules) {
    if (!rule.enabled || rule.h3 !== 'force') continue;
    for (const pattern of rule.domains) {
      if (pattern.startsWith('*.')) continue;
      const host = pattern.startsWith('=') ? pattern.slice(1) : pattern;
      if (!host) continue;
      const origin = `${host.toLowerCase()}:443`;
      if (seen.has(origin)) continue;
      seen.add(origin);
      out.push(origin);
    }
  }
  return out;
}

export interface DnsPlanLike {
  mode: string;
  dohTemplates: string;
  endpoint?: { name?: string; id?: string; protocol?: string } | null;
}

export interface KernelSetupLike {
  dohTemplates: string;
  dnsMode: string;
  proxy?: string;
  usesNet: boolean;
  netMissing: boolean;
}

export interface DohTemplateCheck {
  /** Template recorded in the report, or an empty string. */
  reported: string;
  /** Template the current settings would hand to this kernel now. */
  expected: string;
  stale: boolean;
  mode: string;
  provider?: string;
  protocol?: string;
}

function normalizeTemplate(value: string): string {
  return value.trim().replace(/\/+$/, '');
}

/**
 * Compares the kernel's start-up DoH template with the current DNS plan and
 * the template the kernel would receive now. The kernel setup is preferred
 * because it accounts for liora-net (a local https://127.0.0.1:... template)
 * and for kernels that do not use native DoH at all.
 */
export function checkDohTemplate(
  switches: string[],
  plan: DnsPlanLike,
  setup: KernelSetupLike | null,
): DohTemplateCheck {
  const reported = normalizeTemplate(switchValue(switches, 'dns-over-https-templates'));
  const expected = normalizeTemplate(setup ? setup.dohTemplates : plan.dohTemplates);
  return {
    reported,
    expected,
    stale: reported !== expected,
    mode: plan.mode,
    provider: plan.endpoint?.name || plan.endpoint?.id || undefined,
    protocol: plan.endpoint?.protocol,
  };
}

export interface ProxyRuntimeLike {
  running: boolean;
  proxyPort: number;
}

export interface ProxyCheck {
  /** Proxy address recorded in the report. */
  reported: string;
  /** Proxy the kernel would use now. */
  expected: string;
  stale: boolean;
  hostChanged: boolean;
  portChanged: boolean;
}

function trimProxy(value: string): string {
  return value.trim().replace(/\/+$/, '');
}

/** Splits an http proxy address into host and port for a precise comparison. */
function proxyParts(value: string): { host: string; port: string } {
  const trimmed = value.trim().replace(/\/+$/, '');
  if (!trimmed) return { host: '', port: '' };
  try {
    const url = new URL(trimmed.includes('://') ? trimmed : `http://${trimmed}`);
    return {
      host: url.hostname.toLowerCase(),
      port: url.port || (url.protocol === 'https:' ? '443' : '80'),
    };
  } catch {
    return { host: trimmed.toLowerCase(), port: '' };
  }
}

/**
 * liora-net binds an ephemeral port on every start, so a report's proxy address
 * goes stale even when no setting changed. The kernel setup is authoritative
 * when available; otherwise the running resolver's port is used.
 */
export function checkProxy(
  switches: string[],
  setup: KernelSetupLike | null,
  runtime: ProxyRuntimeLike | null,
): ProxyCheck {
  const reported = trimProxy(switchValue(switches, 'proxy-server'));
  const expected = setup
    ? trimProxy(setup.proxy ?? '')
    : runtime && runtime.running && runtime.proxyPort > 0
      ? `http://127.0.0.1:${runtime.proxyPort}`
      : '';
  const before = proxyParts(reported);
  const now = proxyParts(expected);
  return {
    reported,
    expected,
    stale: reported !== expected,
    hostChanged: before.host !== now.host,
    portChanged: before.port !== now.port,
  };
}

export function checkForcedOrigins(
  switches: string[],
  rules: RuleLike[],
  rulesEnabled: boolean,
): ForcedOriginsCheck {
  const reported = parseForcedOrigins(switches);
  const expected = expectedForcedOrigins(rules, rulesEnabled);
  const reportedSet = new Set(reported);
  const expectedSet = new Set(expected);

  const missing = expected.filter((origin) => !reportedSet.has(origin));
  const extra = reported.filter((origin) => !expectedSet.has(origin));

  return {
    reported,
    expected,
    missing,
    extra,
    stale: missing.length > 0 || extra.length > 0,
  };
}
