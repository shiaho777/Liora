import { describe, expect, it } from 'vitest';
import { parseDiagnosticReport } from './diagnostic-report';

const tlsReport = `report: 1
Liora diagnostic report
Generated: 2026-11-10T07:29:50.007Z
Version: 0.1.0 (macos arm64)
URL: https://expired.badssl.com/
Host: expired.badssl.com
Error: -1202 - 证书校验未通过（可能已过期或不受信任）。
Kernel: system
TLS: subject=*.badssl.com valid=no expires=2015-04-12
Rule: none
Upstream: doh cloudflare https://cloudflare-dns.com/dns-query 413ms
Addresses: 104.154.89.105
HTTPS: alpn=- ech=0B
`;

const dnsReport = `report: 1
Liora diagnostic report
Generated: 2026-11-10T07:35:44.966Z
Version: 0.1.0 (macos arm64)
URL: https://this-host-does-not-exist.invalid/
Host: this-host-does-not-exist.invalid
Error: -1005 - 连接在加载过程中中断。
Kernel: system
TLS: not established
Rule: none
Upstream: doh cloudflare https://cloudflare-dns.com/dns-query 339ms
Addresses: none
HTTPS: alpn=- ech=0B
`;

describe('parseDiagnosticReport', () => {
  it('parses a TLS failure report', () => {
    const result = parseDiagnosticReport(tlsReport);
    expect(result.ok).toBe(true);
    if (!result.ok) return;

    expect(result.report.version).toBe(1);
    expect(result.report.host).toBe('expired.badssl.com');
    expect(result.report.errorCode).toBe(-1202);
    expect(result.report.errorReason).toContain('证书校验未通过');
    expect(result.report.kernel).toBe('system');
    expect(result.report.tls.status).toBe('invalid');
    expect(result.report.tls.subject).toBe('*.badssl.com');
    expect(result.report.tls.expires).toBe('2015-04-12');
    expect(result.report.rule.matched).toBe(false);
    expect(result.report.upstream).toEqual({
      protocol: 'doh',
      id: 'cloudflare',
      address: 'https://cloudflare-dns.com/dns-query',
      elapsedMs: 413,
    });
    expect(result.report.addresses).toEqual(['104.154.89.105']);
    expect(result.report.https).toEqual({ alpn: [], echBytes: 0 });
  });

  it('parses a DNS failure report without addresses', () => {
    const result = parseDiagnosticReport(dnsReport);
    expect(result.ok).toBe(true);
    if (!result.ok) return;

    expect(result.report.errorCode).toBe(-1005);
    expect(result.report.tls.status).toBe('absent');
    expect(result.report.addresses).toEqual([]);
    expect(result.report.upstream?.elapsedMs).toBe(339);
  });

  it('parses a matched rule and ALPN', () => {
    const report = dnsReport
      .replace('Rule: none', 'Rule: x.com (h3=prefer, ech=true)')
      .replace('HTTPS: alpn=- ech=0B', 'HTTPS: alpn=h3,h2 ech=71B');
    const result = parseDiagnosticReport(report);
    expect(result.ok).toBe(true);
    if (!result.ok) return;

    expect(result.report.rule).toMatchObject({ matched: true, name: 'x.com', h3: 'prefer', ech: true });
    expect(result.report.https).toEqual({ alpn: ['h3', 'h2'], echBytes: 71 });
  });

  it('rejects reports without a version header', () => {
    const result = parseDiagnosticReport('Liora diagnostic report\nURL: https://example.com\n');
    expect(result).toEqual({ ok: false, error: 'missing-version' });
  });

  it('rejects a newer report version', () => {
    const result = parseDiagnosticReport('report: 9\nURL: https://example.com\n');
    expect(result).toEqual({ ok: false, error: 'unsupported-version' });
  });

  it('keeps unknown fields instead of dropping them', () => {
    const result = parseDiagnosticReport(`${tlsReport}Proxy: http://127.0.0.1:5300\n`);
    expect(result.ok).toBe(true);
    if (!result.ok) return;
    expect(result.report.extra.proxy).toBe('http://127.0.0.1:5300');
  });

  it('parses kernel switches, components and signing state', () => {
    const report = `${dnsReport}Switches: --enable-quic --origin-to-force-quic-on=x.com:443,twitter.com:443 --dns-over-https-mode=secure
Limits: ech, forceQuic
Components: kernel-cef@156.0.26 *, liora-net@0.1.0
Signature: ad-hoc (no Team ID, io.liora.browser)
CEF: failed: process requirement -67030
`;
    const result = parseDiagnosticReport(report);
    expect(result.ok).toBe(true);
    if (!result.ok) return;

    // A comma inside a switch value must not split it into two switches.
    expect(result.report.switchesKnown).toBe(true);
    expect(result.report.switches).toEqual([
      '--enable-quic',
      '--origin-to-force-quic-on=x.com:443,twitter.com:443',
      '--dns-over-https-mode=secure',
    ]);
    expect(result.report.limits).toEqual(['ech', 'forceQuic']);
    expect(result.report.components).toEqual(['kernel-cef@156.0.26 *', 'liora-net@0.1.0']);
    expect(result.report.signature).toBe('ad-hoc (no Team ID, io.liora.browser)');
    expect(result.report.cef).toBe('failed: process requirement -67030');
  });

  it('treats absent runtime fields as empty', () => {
    const result = parseDiagnosticReport(dnsReport);
    expect(result.ok).toBe(true);
    if (!result.ok) return;
    expect(result.report.switches).toEqual([]);
    expect(result.report.switchesKnown).toBe(false);
    expect(result.report.limits).toEqual([]);
    expect(result.report.components).toEqual([]);
    expect(result.report.signature).toBeUndefined();
    expect(result.report.cef).toBeUndefined();
  });

  it('handles handshake failures without certificate details', () => {
    const result = parseDiagnosticReport(
      tlsReport.replace('TLS: subject=*.badssl.com valid=no expires=2015-04-12', 'TLS: handshake failed'),
    );
    expect(result.ok).toBe(true);
    if (!result.ok) return;
    expect(result.report.tls.status).toBe('failed');
    expect(result.report.tls.raw).toBe('handshake failed');
  });
});

describe('applied snapshot and delta', () => {
  it('parses the applied provider, proxy, time and delta', () => {
    const report = `${dnsReport}Applied: 2026-11-10T16:25:21.000Z · Quad9 Secure (doh) · proxy http://127.0.0.1:55977
Delta: provider Quad9 Secure -> Cloudflare Standard · proxy http://127.0.0.1:55977 -> http://127.0.0.1:56030
`;
    const result = parseDiagnosticReport(report);
    expect(result.ok).toBe(true);
    if (!result.ok) return;

    expect(result.report.appliedKnown).toBe(true);
    expect(result.report.applied).toMatchObject({
      at: '2026-11-10T16:25:21.000Z',
      provider: 'Quad9 Secure',
      protocol: 'doh',
      proxy: 'http://127.0.0.1:55977',
    });
    expect(result.report.deltaKnown).toBe(true);
    expect(result.report.delta).toEqual([
      {
        kind: 'provider',
        from: 'Quad9 Secure',
        to: 'Cloudflare Standard',
      },
      {
        kind: 'proxy',
        from: 'http://127.0.0.1:55977',
        to: 'http://127.0.0.1:56030',
      },
    ]);
  });

  it('parses explicit none and marks missing fields as unknown', () => {
    const none = parseDiagnosticReport(`${dnsReport}Applied: none
Delta: none
`);
    expect(none.ok).toBe(true);
    if (!none.ok) return;
    expect(none.report.appliedKnown).toBe(true);
    expect(none.report.applied).toBeNull();
    expect(none.report.deltaKnown).toBe(true);
    expect(none.report.delta).toEqual([]);

    const missing = parseDiagnosticReport(dnsReport);
    expect(missing.ok).toBe(true);
    if (!missing.ok) return;
    expect(missing.report.appliedKnown).toBe(false);
    expect(missing.report.deltaKnown).toBe(false);
  });

  it('handles providers without a protocol suffix', () => {
    const report = `${dnsReport}Applied: 2026-11-10 · Custom DNS · proxy none
Delta: mode secure -> automatic
`;
    const result = parseDiagnosticReport(report);
    expect(result.ok).toBe(true);
    if (!result.ok) return;
    expect(result.report.applied?.provider).toBe('Custom DNS');
    expect(result.report.applied?.protocol).toBe('');
    expect(result.report.delta).toEqual([{ kind: 'mode', from: 'secure', to: 'automatic' }]);
  });
});

describe('resolver restart metadata', () => {
  it('parses the restart time and run count', () => {
    const result = parseDiagnosticReport(
      `${dnsReport}Resolver: restarted 2026-11-10T16:41:08.000Z (run 2)
`,
    );
    expect(result.ok).toBe(true);
    if (!result.ok) return;
    expect(result.report.resolverKnown).toBe(true);
    expect(result.report.resolver).toEqual({
      at: '2026-11-10T16:41:08.000Z',
      count: 2,
      raw: 'restarted 2026-11-10T16:41:08.000Z (run 2)',
    });
  });

  it('handles none and reports written before the field existed', () => {
    const none = parseDiagnosticReport(`${dnsReport}Resolver: none
`);
    expect(none.ok).toBe(true);
    if (!none.ok) return;
    expect(none.report.resolverKnown).toBe(true);
    expect(none.report.resolver).toBeNull();

    const missing = parseDiagnosticReport(dnsReport);
    expect(missing.ok).toBe(true);
    if (!missing.ok) return;
    expect(missing.report.resolverKnown).toBe(false);
  });
});
