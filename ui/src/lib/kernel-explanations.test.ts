import { describe, expect, it } from 'vitest';
import {
  explainLimit,
  explainSwitch,
  knownLimits,
  knownSwitches,
  switchName,
  switchValue,
} from './kernel-explanations';

describe('switchName', () => {
  it('strips the leading dashes and any value', () => {
    expect(switchName('--enable-quic')).toBe('enable-quic');
    expect(switchName('--origin-to-force-quic-on=x.com:443,twitter.com:443')).toBe(
      'origin-to-force-quic-on',
    );
    expect(switchName('--ignore-certificate-errors-spki-list=abc=')).toBe(
      'ignore-certificate-errors-spki-list',
    );
    expect(switchName('enable-quic')).toBe('enable-quic');
  });
});

describe('switchValue', () => {
  it('returns the value after the first equals sign and nothing otherwise', () => {
    expect(switchValue('--enable-quic')).toBe('');
    expect(switchValue('--origin-to-force-quic-on=x.com:443,twitter.com:443')).toBe(
      'x.com:443,twitter.com:443',
    );
    expect(switchValue('--ignore-certificate-errors-spki-list=abc=')).toBe('abc=');
  });
});

describe('explainSwitch', () => {
  it('explains switches Core actually emits', () => {
    for (const raw of knownSwitches()) {
      const zh = explainSwitch(raw, 'zh');
      const en = explainSwitch(raw, 'en');
      expect(zh.title.length).toBeGreaterThan(0);
      expect(zh.detail.length).toBeGreaterThan(10);
      expect(en.title.length).toBeGreaterThan(0);
      expect(en.detail.length).toBeGreaterThan(10);
    }
  });

  it('matches by flag name even when a value is present', () => {
    const withValue = explainSwitch('--origin-to-force-quic-on=x.com:443', 'zh');
    const withoutValue = explainSwitch('--origin-to-force-quic-on', 'zh');
    expect(withValue.title).toBe(withoutValue.title);
    expect(withValue.detail).toContain('TCP');
  });

  it('names the forced QUIC hosts this report affects', () => {
    const explanation = explainSwitch(
      '--origin-to-force-quic-on=x.com:443,twitter.com:443',
      'zh',
    );
    expect(explanation.detail).toContain('x.com:443');
    expect(explanation.detail).toContain('twitter.com:443');
    expect(explanation.detail).toContain('不会回退 TCP');

    const english = explainSwitch(
      '--origin-to-force-quic-on=x.com:443,twitter.com:443',
      'en',
    );
    expect(english.detail).toContain('x.com:443, twitter.com:443');
    expect(english.detail).toContain('do not fall back to TCP');
  });

  it('states the local DoH address and port', () => {
    const explanation = explainSwitch(
      '--dns-over-https-templates=https://127.0.0.1:5300/dns-query',
      'zh',
    );
    expect(explanation.detail).toContain('https://127.0.0.1:5300/dns-query');
    expect(explanation.detail).toContain('本地端口 5300');

    // A public DoH URL has no explicit port; do not invent one.
    const publicDoh = explainSwitch(
      '--dns-over-https-templates=https://dns.example/dns-query',
      'en',
    );
    expect(publicDoh.detail).not.toContain('local port');
  });

  it('shows which resolver key is trusted', () => {
    const explanation = explainSwitch(
      '--ignore-certificate-errors-spki-list=abcdef0123456789=',
      'zh',
    );
    expect(explanation.detail).toContain('abcdef01…');
    expect(explanation.detail).toContain('其他站点的证书仍正常校验');
  });

  it('mentions the rule that enables QUIC', () => {
    const explanation = explainSwitch('--enable-quic', 'zh', { ruleName: 'x.com' });
    expect(explanation.detail).toContain('规则“x.com”');
    expect(explanation.detail).toContain('h3');
  });

  it('falls back instead of returning an empty explanation', () => {
    const unknown = explainSwitch('--future-switch=1', 'en');
    expect(unknown.title).toBe('Unrecognised switch');
    expect(unknown.detail.length).toBeGreaterThan(10);
  });
});

describe('explainLimit', () => {
  it('explains every limit Core can report', () => {
    expect(knownLimits().sort()).toEqual(['dns', 'ech', 'forceQuic', 'httpsRecords']);
    for (const name of knownLimits()) {
      expect(explainLimit(name, 'zh').detail.length).toBeGreaterThan(10);
      expect(explainLimit(name, 'en').detail.length).toBeGreaterThan(10);
    }
  });

  it('names the affected rule for each capability limitation', () => {
    const ech = explainLimit('ech', 'zh', { ruleName: 'x.com' });
    expect(ech.detail).toContain('规则“x.com”');
    expect(ech.detail).toContain('ECHConfig');

    const force = explainLimit('forceQuic', 'en', { ruleName: 'x.com' });
    expect(force.detail).toContain('rule “x.com”');
    expect(force.detail).toContain('prefers h3');

    const records = explainLimit('httpsRecords', 'zh', { ruleName: 'cdn.example' });
    expect(records.detail).toContain('规则“cdn.example”');
    expect(records.detail).toContain('HTTPS/SVCB');
  });

  it('names the upstream when DNS cannot be applied', () => {
    const explanation = explainLimit('dns', 'zh', { upstream: 'cloudflare' });
    expect(explanation.detail).toContain('cloudflare');
    expect(explanation.detail).toContain('加密 DNS 不会生效');
  });

  it('keeps the meaning of forceQuic distinct from ech', () => {
    expect(explainLimit('forceQuic', 'zh').detail).toContain('TCP');
    expect(explainLimit('ech', 'zh').detail).toContain('ECHConfig');
    expect(explainLimit('ech', 'zh').detail).not.toBe(explainLimit('forceQuic', 'zh').detail);
  });
});
