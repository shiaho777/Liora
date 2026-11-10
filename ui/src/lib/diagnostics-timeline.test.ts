import { describe, expect, it } from 'vitest';
import { buildTimeline, parseTimelineText, timelineToText } from './diagnostics-timeline';

const base = {
  generatedAt: '2026-11-10T08:00:00.000Z',
  reportApplied: {
    at: '2026-11-10T07:59:50.000Z',
    provider: 'Quad9 Secure',
    protocol: 'doh',
    proxy: 'http://127.0.0.1:55000',
  },
  reportResolver: { at: '2026-11-10T07:59:40.000Z', count: 2 },
  reportDeltaCount: 1,
  currentApplied: {
    at: 1794300000000,
    provider: 'Cloudflare Standard',
    protocol: 'doh',
    proxy: 'http://127.0.0.1:56000',
  },
  currentResolver: { at: 1794300000000, count: 4 },
  currentDeltaCount: 2,
  currentDeltaChanged: true,
  now: 1794300100000,
};

describe('buildTimeline', () => {
  it('orders events chronologically and includes current changes', () => {
    const events = buildTimeline(base);
    const kinds = events.map((event) => event.kind);
    expect(kinds).toEqual([
      'resolver',
      'applied',
      'generated',
      'report-delta',
      'current-applied',
      'current-resolver',
      'current-delta',
    ]);
    expect(events.map((event) => event.at)).toEqual([...events.map((event) => event.at)].sort((a, b) => a - b));
    expect(events[0].count).toBe(2);
    expect(events[4].provider).toBe('Cloudflare Standard');
  });

  it('does not duplicate events that match the report', () => {
    const events = buildTimeline({
      ...base,
      generatedAt: '2026-11-10T07:59:55.000Z',
      reportApplied: { at: 1794300000000, provider: 'Cloudflare Standard', protocol: 'doh' },
      reportResolver: { at: 1794300000000, count: 4 },
      reportDeltaCount: 2,
      currentDeltaCount: 2,
      currentDeltaChanged: false,
    });
    expect(events.map((event) => event.kind)).toEqual(['generated', 'report-delta', 'applied', 'resolver']);
  });

  it('keeps unknown times but sorts them last', () => {
    const events = buildTimeline({
      ...base,
      generatedAt: undefined,
      reportApplied: { provider: 'Unknown', at: undefined },
      reportResolver: null,
      reportDeltaCount: 0,
      currentApplied: null,
      currentResolver: null,
      currentDeltaCount: 0,
      currentDeltaChanged: false,
    });
    expect(events.map((event) => event.kind)).toEqual(['applied']);
    expect(events[0].at).toBe(0);
  });

  it('keeps the saved report order when reported events are provided', () => {
    const events = buildTimeline({
      ...base,
      reportedEvents: [
        { kind: 'resolver', at: 1794299000000, count: 2 },
        { kind: 'applied', at: 1794299100000, provider: 'Quad9 Secure' },
        { kind: 'generated', at: 1794299200000 },
      ],
      currentApplied: null,
      currentResolver: null,
      currentDeltaCount: 0,
      currentDeltaChanged: false,
    });
    expect(events.map((event) => event.kind)).toEqual(['resolver', 'applied', 'generated']);
    expect(events[0].at).toBe(1794299000000);
  });

  it('shows the current delta only when it changed', () => {
    const events = buildTimeline({
      ...base,
      currentApplied: null,
      currentResolver: null,
      currentDeltaCount: 0,
      currentDeltaChanged: false,
    });
    expect(events.map((event) => event.kind)).not.toContain('current-delta');
  });
});

describe('timelineToText', () => {
  it('exports chronological saved and current events', () => {
    const text = timelineToText(
      [
        { kind: 'current-resolver', at: 1794299300000, count: 4 },
        { kind: 'generated', at: 1794299200000 },
        {
          kind: 'applied',
          at: 1794299100000,
          provider: 'Quad9 Secure',
          protocol: 'doh',
          proxy: 'http://127.0.0.1:55000',
        },
        { kind: 'resolver', at: 1794299000000, count: 2 },
      ],
      new Date('2026-11-10T09:00:00.000Z'),
    );
    expect(text).toContain('Liora diagnostics timeline');
    expect(text).toContain('Exported: 2026-11-10T09:00:00.000Z');

    const lines = text.split('\n').slice(2);
    expect(lines.map((line) => line.split('  ')[1])).toEqual([
      'resolver',
      'applied',
      'generated',
      'current-resolver',
    ]);
    expect(lines[0]).toContain('run 2');
    expect(lines[1]).toContain('Quad9 Secure (doh)');
    expect(lines[1]).toContain('http://127.0.0.1:55000');
    expect(lines[3]).toContain('run 4');
  });

  it('writes differences and unknown times explicitly', () => {
    const text = timelineToText(
      [
        { kind: 'report-delta', at: 0, differences: 2 },
        { kind: 'current-delta', at: 0, differences: 1 },
      ],
      new Date('2026-11-10T09:00:00.000Z'),
    );
    expect(text).toContain('unknown  report-delta  2 difference(s)');
    expect(text).toContain('unknown  current-delta  1 difference(s)');
  });
});

describe('parseTimelineText', () => {
  const text = `Liora diagnostics timeline
Exported: 2026-11-10T08:53:48.271Z
2026-11-10T08:53:35.565Z  resolver  run 2
2026-11-10T08:53:35.566Z  applied  Cloudflare Standard (doh) · http://127.0.0.1:49704
2026-11-10T08:53:42.703Z  generated
2026-11-10T08:53:45.438Z  current-resolver  run 3
`;

  it('parses the header, export time and ordered entries', () => {
    const parsed = parseTimelineText(text);
    expect(parsed).not.toBeNull();
    if (!parsed) return;
    expect(parsed.exported).toBe('2026-11-10T08:53:48.271Z');
    expect(parsed.entries.map((entry) => entry.kind)).toEqual([
      'resolver',
      'applied',
      'generated',
      'current-resolver',
    ]);
    expect(parsed.entries[1].detail).toContain('Cloudflare Standard (doh)');
    expect(parsed.entries[3].detail).toBe('run 3');
  });

  it('rejects files that are not timeline exports', () => {
    expect(parseTimelineText('Liora diagnostic report\nreport: 1')).toBeNull();
    expect(parseTimelineText('')).toBeNull();
  });
});
