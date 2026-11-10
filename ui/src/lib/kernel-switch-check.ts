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
