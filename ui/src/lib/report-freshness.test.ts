import { describe, expect, it } from 'vitest';
import { reportFreshness } from './report-freshness';

const reportApplied = {
  at: '2026-11-10T08:27:51.284Z',
  provider: 'Quad9 Secure',
  protocol: 'doh',
  proxy: 'http://127.0.0.1:57009',
};

const currentApplied = {
  at: 1794299900000,
  provider: 'Cloudflare Standard',
  protocol: 'doh',
  mode: 'secure',
  proxy: 'http://127.0.0.1:57120',
};

const currentConfig = {
  provider: 'Cloudflare Standard',
  protocol: 'doh',
  mode: 'secure',
  proxy: 'http://127.0.0.1:57120',
};

const reportDelta = [
  { kind: 'provider', from: 'Quad9 Secure', to: 'Cloudflare Standard' },
  { kind: 'proxy', from: 'http://127.0.0.1:57009', to: 'http://127.0.0.1:57051' },
];

describe('reportFreshness', () => {
  it('is clean when the snapshot still matches what is applied', () => {
    const freshness = reportFreshness(reportApplied, [], { ...currentApplied, ...reportApplied }, {
      ...currentConfig,
      provider: 'Quad9 Secure',
      proxy: 'http://127.0.0.1:57009',
    });
    expect(freshness.changed).toBe(false);
    expect(freshness.appliedChanged).toBe(false);
    expect(freshness.deltaChanged).toBe(false);
  });

  it('detects a provider and proxy applied since the report', () => {
    const freshness = reportFreshness(reportApplied, reportDelta, currentApplied, currentConfig);
    expect(freshness.changed).toBe(true);
    expect(freshness.appliedFields).toEqual(['provider']);
    expect(freshness.currentDelta).toEqual([]);
    expect(freshness.deltaChanged).toBe(true);
  });

  it('detects that only the unapplied delta changed', () => {
    const freshness = reportFreshness(
      reportApplied,
      [],
      { ...reportApplied, mode: 'secure' },
      { ...currentConfig, provider: 'Quad9 Secure', proxy: 'http://127.0.0.1:57009', mode: 'automatic' },
    );
    expect(freshness.appliedChanged).toBe(false);
    expect(freshness.deltaChanged).toBe(true);
    expect(freshness.currentDelta).toEqual([
      { kind: 'mode', from: 'secure', to: 'automatic' },
    ]);
  });

  it('flags a missing current snapshot', () => {
    const freshness = reportFreshness(reportApplied, [], null, currentConfig);
    expect(freshness.appliedChanged).toBe(true);
    expect(freshness.appliedFields).toEqual(['provider', 'protocol']);
  });

  it('does not mark the report stale for a proxy port alone', () => {
    const freshness = reportFreshness(
      reportApplied,
      [],
      { ...currentApplied, provider: 'Quad9 Secure', proxy: 'http://127.0.0.1:9999' },
      { ...currentConfig, provider: 'Quad9 Secure', proxy: 'http://127.0.0.1:9999' },
    );
    expect(freshness.appliedChanged).toBe(false);
    expect(freshness.deltaChanged).toBe(false);
    expect(freshness.changed).toBe(false);
  });

  it('ignores case and trailing slashes', () => {
    const freshness = reportFreshness(
      { ...reportApplied, provider: 'quad9 secure', proxy: 'http://127.0.0.1:57009/' },
      [],
      { ...reportApplied, provider: 'Quad9 Secure', proxy: 'http://127.0.0.1:57009', mode: 'secure' },
      { ...currentConfig, provider: 'Quad9 Secure', proxy: 'http://127.0.0.1:57009' },
    );
    expect(freshness.changed).toBe(false);
  });
});

describe('resolver restart count', () => {
  it('reports how many runs happened since the report', () => {
    const freshness = reportFreshness(
      reportApplied,
      [],
      { ...currentApplied, ...reportApplied },
      { ...currentConfig, provider: 'Quad9 Secure', proxy: 'http://127.0.0.1:57009' },
      2,
      4,
    );
    expect(freshness.resolverRestarts).toBe(2);
    // A resolver restart alone is not a configuration mismatch.
    expect(freshness.changed).toBe(false);
  });

  it('ignores equal, older or missing counts', () => {
    const base = { ...currentApplied, ...reportApplied };
    const config = { ...currentConfig, provider: 'Quad9 Secure', proxy: 'http://127.0.0.1:57009' };
    expect(reportFreshness(reportApplied, [], base, config, 3, 3).resolverRestarts).toBe(0);
    expect(reportFreshness(reportApplied, [], base, config, 4, 2).resolverRestarts).toBe(0);
    expect(reportFreshness(reportApplied, [], base, config).resolverRestarts).toBe(0);
    expect(reportFreshness(reportApplied, [], base, config, 2, undefined).resolverRestarts).toBe(0);
  });
});
