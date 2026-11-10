import { describe, expect, it } from 'vitest';
import { buildTimeline } from './diagnostics-timeline';

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
