import { describe, expect, it } from 'vitest';
import { buildDiagnosticReport } from './diagnostic-report-build';

const base = {
  version: '0.1.0',
  platform: 'macos',
  arch: 'arm64',
  url: 'https://example.com/',
  host: 'example.com',
  error: '-1005 - lost',
  kernel: 'system',
  switches: [],
  limits: [],
  components: [],
  signature: 'ad-hoc',
  cef: 'not selected',
  tls: 'not established',
  rule: 'none',
  applied: null,
  delta: [],
  upstream: 'doh cloudflare https://cloudflare-dns.com/dns-query 300ms',
  addresses: ['93.184.216.34'],
  https: { alpn: [], echBytes: 0 },
};

describe('buildDiagnosticReport', () => {
  it('writes resolver restarts and a stable report header', () => {
    const report = buildDiagnosticReport({
      ...base,
      generated: new Date('2026-11-10T16:41:08.000Z'),
      resolver: { at: '2026-11-10T16:41:08.000Z', count: 2 },
    });
    expect(report.startsWith('report: 1\nLiora diagnostic report\n')).toBe(true);
    expect(report).toContain('Resolver: restarted 2026-11-10T16:41:08.000Z (run 2)');
    expect(report).toContain('Applied: none');
    expect(report).toContain('Delta: none');
    expect(report).toContain('Addresses: 93.184.216.34');
  });

  it('writes Resolver: none when no resolver has run', () => {
    const report = buildDiagnosticReport({ ...base, resolver: null });
    expect(report).toContain('Resolver: none');
  });
});
