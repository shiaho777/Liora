// Chronological view of what happened around a saved report: when it was
// generated, which network configuration was applied, when the resolver
// started, and what has changed since.

export interface TimelineApplied {
  at?: string | number;
  provider?: string;
  protocol?: string;
  proxy?: string;
}

export interface TimelineResolver {
  at?: string | number;
  count?: number;
}

export interface TimelineInput {
  generatedAt?: string;
  reportApplied?: TimelineApplied | null;
  reportResolver?: TimelineResolver | null;
  reportDeltaCount: number;
  currentApplied?: TimelineApplied | null;
  currentResolver?: TimelineResolver | null;
  currentDeltaCount: number;
  currentDeltaChanged: boolean;
  /** Events read from a saved report; replaces the synthesized report events. */
  reportedEvents?: TimelineEvent[];
  now?: number;
}

export type TimelineKind =
  | 'generated'
  | 'applied'
  | 'resolver'
  | 'report-delta'
  | 'current-applied'
  | 'current-resolver'
  | 'current-delta';

export interface TimelineEvent {
  kind: TimelineKind;
  /** Unix milliseconds, or 0 when the time is unknown. */
  at: number;
  provider?: string;
  protocol?: string;
  proxy?: string;
  count?: number;
  differences?: number;
}

function timeOf(value: string | number | undefined): number {
  if (typeof value === 'number') return Number.isFinite(value) ? value : 0;
  if (!value) return 0;
  const parsed = new Date(value).getTime();
  return Number.isNaN(parsed) ? 0 : parsed;
}

function providerKey(value: TimelineApplied | null | undefined): string {
  return `${value?.provider ?? ''}|${value?.protocol ?? ''}`.toLowerCase();
}

export function buildTimeline(input: TimelineInput): TimelineEvent[] {
  const events: TimelineEvent[] = [];
  const generatedAt = timeOf(input.generatedAt);

  if (input.reportedEvents && input.reportedEvents.length > 0) {
    // A saved report carries its own chronology; keep that exact order/content
    // instead of re-deriving it from the summary lines.
    events.push(...input.reportedEvents.map((event) => ({ ...event })));
  } else {
    if (generatedAt > 0) {
      events.push({ kind: 'generated', at: generatedAt });
    }
    if (input.reportApplied) {
      events.push({
        kind: 'applied',
        at: timeOf(input.reportApplied.at),
        provider: input.reportApplied.provider,
        protocol: input.reportApplied.protocol,
        proxy: input.reportApplied.proxy,
      });
    }
    if (input.reportResolver) {
      events.push({
        kind: 'resolver',
        at: timeOf(input.reportResolver.at),
        count: input.reportResolver.count,
      });
    }
    if (input.reportDeltaCount > 0) {
      events.push({
        kind: 'report-delta',
        at: generatedAt,
        differences: input.reportDeltaCount,
      });
    }
  }

  // Only add "current" events when they carry new information; a fresh report
  // read in the same process would otherwise duplicate the report's rows.
  if (
    input.currentApplied &&
    (timeOf(input.currentApplied.at) !== timeOf(input.reportApplied?.at) ||
      providerKey(input.currentApplied) !== providerKey(input.reportApplied))
  ) {
    events.push({
      kind: 'current-applied',
      at: timeOf(input.currentApplied.at),
      provider: input.currentApplied.provider,
      protocol: input.currentApplied.protocol,
      proxy: input.currentApplied.proxy,
    });
  }
  if (
    input.currentResolver &&
    (input.currentResolver.count !== input.reportResolver?.count ||
      timeOf(input.currentResolver.at) !== timeOf(input.reportResolver?.at))
  ) {
    events.push({
      kind: 'current-resolver',
      at: timeOf(input.currentResolver.at),
      count: input.currentResolver.count,
    });
  }
  if (input.currentDeltaCount > 0 && input.currentDeltaChanged) {
    events.push({
      kind: 'current-delta',
      at: input.now ?? Date.now(),
      differences: input.currentDeltaCount,
    });
  }

  return events.sort((a, b) => {
    if (a.at === 0 && b.at === 0) return 0;
    if (a.at === 0) return 1;
    if (b.at === 0) return -1;
    return a.at - b.at;
  });
}

function timelineDetail(event: TimelineEvent): string {
  if (event.kind === 'resolver' || event.kind === 'current-resolver') {
    return `run ${event.count ?? 0}`;
  }
  if (event.kind === 'report-delta' || event.kind === 'current-delta') {
    return `${event.differences ?? 0} difference(s)`;
  }
  const parts: string[] = [];
  if (event.provider) {
    parts.push(`${event.provider}${event.protocol ? ` (${event.protocol})` : ''}`);
  }
  if (event.proxy) parts.push(event.proxy);
  return parts.join(' · ');
}

/**
 * Plain-text chronology suitable for pasting into an issue. Timestamps are ISO
 * and events are sorted, so the output does not depend on how a report was
 * rendered in the interface.
 */
export function timelineToText(
  events: TimelineEvent[],
  exportedAt: Date = new Date(),
): string {
  const lines = ['Liora diagnostics timeline', `Exported: ${exportedAt.toISOString()}`];
  const sorted = [...events].sort((a, b) => {
    if (a.at === 0 && b.at === 0) return 0;
    if (a.at === 0) return 1;
    if (b.at === 0) return -1;
    return a.at - b.at;
  });
  for (const event of sorted) {
    const at = event.at > 0 ? new Date(event.at).toISOString() : 'unknown';
    const detail = timelineDetail(event);
    lines.push(`${at}  ${event.kind}${detail ? `  ${detail}` : ''}`);
  }
  return lines.join('\n');
}

export interface ExportedTimelineEntry {
  at: string;
  kind: string;
  detail: string;
}

export interface ExportedTimeline {
  exported?: string;
  entries: ExportedTimelineEntry[];
  raw: string;
}

/** Parses the plain-text file written by 'save timeline'. */
export function parseTimelineText(text: string): ExportedTimeline | null {
  const lines = text.replace(/\r\n/g, '\n').split('\n');
  const first = lines.findIndex((line) => line.trim().length > 0);
  if (first < 0 || lines[first].trim() !== 'Liora diagnostics timeline') return null;

  let exported: string | undefined;
  const entries: ExportedTimelineEntry[] = [];
  for (let i = first + 1; i < lines.length; i += 1) {
    const line = lines[i].trim();
    if (!line) continue;
    if (line.startsWith('Exported:')) {
      exported = line.slice('Exported:'.length).trim();
      continue;
    }
    const parts = line.split(/\s{2,}/).filter(Boolean);
    if (parts.length < 2) continue;
    entries.push({
      at: parts[0],
      kind: parts[1],
      detail: parts.slice(2).join('  '),
    });
  }
  return { exported, entries, raw: text };
}
