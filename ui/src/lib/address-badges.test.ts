import { describe, expect, it } from 'vitest';
import { addressBadges, type HostRule } from './address-badges';

const SYSTEM_CAPS = ['proxy'];
const CEF_CAPS = [
  'secureDns',
  'httpsRecords',
  'ech',
  'forceQuic',
  'proxy',
  'devtools',
  'isolatedProfiles',
];

function rule(overrides: Partial<HostRule> = {}): HostRule {
  return { matched: true, ruleName: 'x.com', h3: 'prefer', ech: true, ...overrides };
}

function keys(url: string, r: HostRule | null, caps: string[], resolverRunning = true): string[] {
  return addressBadges({ url, rule: r, caps, resolverRunning }).map((badge) => badge.key);
}

describe('addressBadges', () => {
  it('never claims H3 or ECH on the default system kernel', () => {
    // This is the regression that mattered: the bar used to print "H3 · ECH"
    // for every page regardless of the kernel.
    expect(keys('https://x.com/', rule(), SYSTEM_CAPS)).toEqual(['https']);
  });

  it('says nothing at all for internal pages', () => {
    expect(keys('liora://settings', rule(), CEF_CAPS)).toEqual([]);
    expect(keys('', rule(), CEF_CAPS)).toEqual([]);
  });

  it('warns on plain HTTP and stops there', () => {
    expect(keys('http://x.com/', rule(), CEF_CAPS)).toEqual(['insecure']);
  });

  it('shows preferred H3 when a rule asks and the kernel reads HTTPS records', () => {
    expect(keys('https://x.com/', rule(), CEF_CAPS)).toEqual(['https', 'h3', 'ech']);
  });

  it('distinguishes forced QUIC from a preference', () => {
    expect(keys('https://x.com/', rule({ h3: 'force' }), CEF_CAPS)).toEqual([
      'https',
      'h3force',
      'ech',
    ]);
  });

  it('falls back to prefer when the kernel cannot force QUIC', () => {
    const caps = CEF_CAPS.filter((cap) => cap !== 'forceQuic');
    expect(keys('https://x.com/', rule({ h3: 'force' }), caps)).toEqual(['https', 'h3', 'ech']);
  });

  it('hides ECH when the rule does not enable it', () => {
    expect(keys('https://x.com/', rule({ ech: false }), CEF_CAPS)).toEqual(['https', 'h3']);
  });

  it('hides every rule badge when no rule matched', () => {
    expect(keys('https://example.com/', { matched: false }, CEF_CAPS)).toEqual(['https']);
    expect(keys('https://example.com/', null, CEF_CAPS)).toEqual(['https']);
  });

  it('does not claim a per-site resolver that is not running', () => {
    const withDns = rule({ dns: { name: 'Cloudflare', protocol: 'doh' } });
    expect(keys('https://x.com/', withDns, CEF_CAPS, true)).toContain('dns');
    expect(keys('https://x.com/', withDns, CEF_CAPS, false)).not.toContain('dns');
  });

  it('labels the DNS badge with the actual protocol', () => {
    const badges = addressBadges({
      url: 'https://x.com/',
      rule: rule({ dns: { name: 'Quad9', protocol: 'dot' } }),
      caps: CEF_CAPS,
      resolverRunning: true,
    });
    expect(badges.find((badge) => badge.key === 'dns')?.label).toBe('DOT');
  });

  it('gives every badge an explanation', () => {
    const badges = addressBadges({
      url: 'https://x.com/',
      rule: rule({ h3: 'force', dns: { name: 'Quad9', protocol: 'dot' } }),
      caps: CEF_CAPS,
      resolverRunning: true,
    });
    expect(badges.length).toBeGreaterThan(0);
    for (const badge of badges) expect(badge.detail.length).toBeGreaterThan(0);
  });
});