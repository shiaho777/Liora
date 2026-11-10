// Difference between the network configuration last applied to the kernel and
// the settings that are active now, so the DNS panel can say what changed
// without the user applying it yet.

export type DeltaKind = 'provider' | 'protocol' | 'mode' | 'proxy';

export interface NetworkSnapshot {
  provider?: string;
  protocol?: string;
  mode?: string;
  proxy?: string;
}

export interface NetworkDeltaItem {
  kind: DeltaKind;
  from: string;
  to: string;
}

function normalized(value: string | undefined, kind: DeltaKind): string {
  const text = (value ?? '').trim();
  if (kind === 'proxy') return text.replace(/\/+$/, '').toLowerCase();
  return text.toLowerCase();
}

/** Returns only the fields that differ, in a stable provider/mode/proxy order. */
export function networkDelta(
  applied: NetworkSnapshot,
  current: NetworkSnapshot,
): NetworkDeltaItem[] {
  const fields: Array<[DeltaKind, string | undefined, string | undefined]> = [
    ['provider', applied.provider, current.provider],
    ['protocol', applied.protocol, current.protocol],
    ['mode', applied.mode, current.mode],
    ['proxy', applied.proxy, current.proxy],
  ];

  const out: NetworkDeltaItem[] = [];
  for (const [kind, before, after] of fields) {
    if (normalized(before, kind) === normalized(after, kind)) continue;
    out.push({ kind, from: (before ?? '').trim(), to: (after ?? '').trim() });
  }
  return out;
}
