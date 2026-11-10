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
    expect(freshness.appliedFields).toEqual(['provider', 'proxy']);
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
    expect(freshness.appliedFields).toEqual(['provider', 'protocol', 'proxy']);
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
