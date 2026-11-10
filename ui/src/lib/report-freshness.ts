// Compares the applied/Delta snapshot inside a saved report with the state the
// app is in now. Reports are historical, so the viewer must not present their
// snapshot as current after a restart or another settings change.
import { networkDelta, type NetworkSnapshot } from './network-delta';

export interface DeltaLike {
  kind: string;
  from: string;
  to: string;
}

export interface ReportApplied {
  at?: string;
  provider?: string;
  protocol?: string;
  proxy?: string;
}

export interface CurrentApplied {
  at?: number | string;
  provider?: string;
  protocol?: string;
  mode?: string;
  proxy?: string;
}

export type AppliedField = 'provider' | 'protocol';

export interface ReportFreshness {
  changed: boolean;
  appliedChanged: boolean;
  deltaChanged: boolean;
  appliedFields: AppliedField[];
  reportApplied: ReportApplied | null;
  currentApplied: CurrentApplied | null;
  reportDelta: DeltaLike[];
  currentDelta: DeltaLike[];
}

function text(value: string | undefined): string {
  return (value ?? '').trim().toLowerCase();
}

function proxyText(value: string | undefined): string {
  return (value ?? '').trim().replace(/\/+$/, '').toLowerCase();
}

function appliedFields(report: ReportApplied | null, current: CurrentApplied | null): AppliedField[] {
  const fields: AppliedField[] = [];
  if (text(report?.provider) !== text(current?.provider)) fields.push('provider');
  if (text(report?.protocol) !== text(current?.protocol)) fields.push('protocol');
  // The proxy port is ephemeral and changes on every launch, so a port-only
  // difference says nothing about the configuration. It is still displayed in
  // the applied snapshot and in a delta that also changes another field.
  return fields;
}

function deltaKey(items: DeltaLike[]): string {
  return items
    .map((item) => {
      const from = item.kind === 'proxy' ? proxyText(item.from) : text(item.from);
      const to = item.kind === 'proxy' ? proxyText(item.to) : text(item.to);
      return `${text(item.kind)}:${from}->${to}`;
    })
    .sort()
    .join('|');
}

export function reportFreshness(
  reportApplied: ReportApplied | null,
  reportDelta: DeltaLike[],
  currentApplied: CurrentApplied | null,
  currentConfig: NetworkSnapshot,
): ReportFreshness {
  const fields = appliedFields(reportApplied, currentApplied);
  const currentDelta = currentApplied
    ? networkDelta(
        {
          provider: currentApplied.provider,
          protocol: currentApplied.protocol,
          mode: currentApplied.mode,
          proxy: currentApplied.proxy,
        },
        currentConfig,
      )
    : [];

  const appliedChanged = fields.length > 0;
  const deltaChanged = deltaKey(reportDelta) !== deltaKey(currentDelta);

  return {
    changed: appliedChanged || deltaChanged,
    appliedChanged,
    deltaChanged,
    appliedFields: fields,
    reportApplied,
    currentApplied,
    reportDelta,
    currentDelta,
  };
}
