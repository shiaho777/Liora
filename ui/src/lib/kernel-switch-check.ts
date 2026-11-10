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
  /** True only for a real configuration change, not an ephemeral port move. */
  stale: boolean;
  /** Same host, different port: liora-net restarted and bound a new port. */
  portOnlyChange: boolean;
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
  const hostChanged = before.host !== now.host;
  const portChanged = before.port !== now.port;
  // The resolver binds a new ephemeral port on every start. A report that only
  // differs by that port is from an earlier process, not misconfigured; only a
  // host change or a proxy appearing/disappearing needs a kernel restart.
  const portOnlyChange = Boolean(reported && expected && !hostChanged && portChanged);
  return {
    reported,
    expected,
    stale: reported !== expected && !portOnlyChange,
    portOnlyChange,
    hostChanged,
    portChanged,
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

export interface KernelSwitchSource {
  switches?: string[];
  dohTemplates?: string;
  dnsMode?: string;
  proxy?: string;
}

/**
 * Builds the effective start-up flags from a KernelNetSetup. Kept next to the
 * staleness checks so the report, the viewer refresh and the tests agree on
 * what "the kernel received" means.
 */
export function buildKernelSwitches(source: KernelSwitchSource): string[] {
  const out = [...(source.switches ?? [])];
  if (source.dohTemplates) {
    out.push(`--dns-over-https-mode=${source.dnsMode || 'secure'}`);
    out.push(`--dns-over-https-templates=${source.dohTemplates}`);
  }
  if (source.proxy) out.push(`--proxy-server=${source.proxy}`);
  return out;
}

export type StaleKind = 'rules' | 'dns' | 'proxy';

export interface StaleItem {
  kind: StaleKind;
  /** Human-readable values from the report and from the current config. */
  reported: string;
  expected: string;
  missing: string[];
  extra: string[];
}

/**
 * Merges the individual checks into one answer, so a report shows a single
 * "restart the kernel" conclusion with every reason instead of three separate
 * warnings the reader has to reconcile.
 */
export function staleItems(
  rules: ForcedOriginsCheck | null,
  dns: DohTemplateCheck | null,
  proxy: ProxyCheck | null,
): StaleItem[] {
  const out: StaleItem[] = [];
  if (rules?.stale) {
    out.push({
      kind: 'rules',
      reported: rules.reported.join(', '),
      expected: rules.expected.join(', '),
      missing: rules.missing,
      extra: rules.extra,
    });
  }
  if (dns?.stale) {
    out.push({
      kind: 'dns',
      reported: dns.reported,
      expected: dns.expected,
      missing: [],
      extra: [],
    });
  }
  if (proxy?.stale) {
    out.push({
      kind: 'proxy',
      reported: proxy.reported,
      expected: proxy.expected,
      missing: [],
      extra: [],
    });
  }
  return out;
}

function sortedValue(value: unknown): unknown {
  if (Array.isArray(value)) return value.map(sortedValue);
  if (value && typeof value === 'object') {
    const record = value as Record<string, unknown>;
    const out: Record<string, unknown> = {};
    for (const key of Object.keys(record).sort()) out[key] = sortedValue(record[key]);
    return out;
  }
  return value;
}

/** Stable fingerprint of a network config, whether it arrives as JSON or an object. */
export function configFingerprint(value: unknown): string {
  if (value === undefined || value === null || value === '') return '';
  try {
    const parsed = typeof value === 'string' ? JSON.parse(value) : value;
    return JSON.stringify(sortedValue(parsed));
  } catch {
    return typeof value === 'string' ? value : '';
  }
}

/**
 * True when the settings active now differ from the snapshot the kernel views
 * were last loaded with. An empty applied snapshot (old shell) counts as
 * different so the user can apply it once.
 */
export function hasUnappliedChanges(currentConfig: string, appliedConfig: string): boolean {
  if (!currentConfig) return false;
  return configFingerprint(appliedConfig) !== configFingerprint(currentConfig);
}
