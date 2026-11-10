import { describe, expect, it } from 'vitest';
import {
  buildKernelSwitches,
  checkDohTemplate,
  checkForcedOrigins,
  checkProxy,
  expectedForcedOrigins,
  parseForcedOrigins,
  staleItems,
} from './kernel-switch-check';

const forceRule = (domains: string[], enabled = true) => ({
  name: domains.join(','),
  enabled,
  domains,
  h3: 'force',
});

describe('parseForcedOrigins', () => {
  it('reads the switch value and keeps a comma-separated list intact', () => {
    expect(
      parseForcedOrigins([
        '--enable-quic',
        '--origin-to-force-quic-on=x.com:443,twitter.com:443',
      ]),
    ).toEqual(['x.com:443', 'twitter.com:443']);
  });

  it('lowercases, trims and de-duplicates', () => {
    expect(
      parseForcedOrigins(['--origin-to-force-quic-on= X.com:443 , x.com:443 ,api.x.com:443']),
    ).toEqual(['x.com:443', 'api.x.com:443']);
  });

  it('returns nothing when the switch is absent or has no value', () => {
    expect(parseForcedOrigins([])).toEqual([]);
    expect(parseForcedOrigins(['--enable-quic'])).toEqual([]);
    expect(parseForcedOrigins(['--origin-to-force-quic-on='])).toEqual([]);
  });
});

describe('expectedForcedOrigins', () => {
  it('matches the Core rules: skip wildcards, strip =, append :443, de-duplicate', () => {
    const origins = expectedForcedOrigins(
      [
        forceRule(['x.com', '*.cdn.com', '=exact.example', 'x.com']),
        forceRule(['api.x.com']),
        { name: 'off', enabled: true, domains: ['ignored.example'], h3: 'prefer' },
        forceRule(['disabled.example'], false),
      ],
      true,
    );
    expect(origins).toEqual(['x.com:443', 'exact.example:443', 'api.x.com:443']);
  });

  it('returns nothing when direct rules are switched off globally', () => {
    expect(expectedForcedOrigins([forceRule(['x.com'])], false)).toEqual([]);
  });
});

describe('checkForcedOrigins', () => {
  it('is clean when the reported and current origins match regardless of order', () => {
    const check = checkForcedOrigins(
      ['--origin-to-force-quic-on=b.com:443,a.com:443'],
      [forceRule(['a.com', 'b.com'])],
      true,
    );
    expect(check.stale).toBe(false);
    expect(check.missing).toEqual([]);
    expect(check.extra).toEqual([]);
  });

  it('flags hosts that changed since the kernel started', () => {
    // The report captured x.com; the rules now force example.org instead.
    const check = checkForcedOrigins(
      ['--origin-to-force-quic-on=x.com:443,twitter.com:443'],
      [forceRule(['example.org'])],
      true,
    );
    expect(check.stale).toBe(true);
    expect(check.extra).toEqual(['x.com:443', 'twitter.com:443']);
    expect(check.missing).toEqual(['example.org:443']);
  });

  it('flags a newly added rule that the running kernel has not picked up', () => {
    const check = checkForcedOrigins([], [forceRule(['new.example'])], true);
    expect(check.stale).toBe(true);
    expect(check.missing).toEqual(['new.example:443']);
  });

  it('flags origins that were removed from the rules', () => {
    const check = checkForcedOrigins(['--origin-to-force-quic-on=old.example:443'], [], true);
    expect(check.stale).toBe(true);
    expect(check.extra).toEqual(['old.example:443']);
  });

  it('is clean when neither side forces anything', () => {
    const check = checkForcedOrigins([], [forceRule(['x.com'])], false);
    expect(check.stale).toBe(false);
  });
});

describe('checkDohTemplate', () => {
  const plan = {
    mode: 'provider',
    dohTemplates: 'https://cloudflare-dns.com/dns-query',
    endpoint: { name: 'Cloudflare', id: 'cloudflare', protocol: 'doh' },
  };

  it('is clean when the report matches the current template', () => {
    const check = checkDohTemplate(
      ['--dns-over-https-templates=https://cloudflare-dns.com/dns-query'],
      plan,
      {
        dohTemplates: 'https://cloudflare-dns.com/dns-query',
        dnsMode: 'secure',
        usesNet: false,
        netMissing: false,
      },
    );
    expect(check.stale).toBe(false);
    expect(check.provider).toBe('Cloudflare');
    expect(check.mode).toBe('provider');
  });

  it('flags a provider change since the kernel started', () => {
    const check = checkDohTemplate(
      ['--dns-over-https-templates=https://dns.quad9.net/dns-query'],
      plan,
      {
        dohTemplates: 'https://cloudflare-dns.com/dns-query',
        dnsMode: 'secure',
        usesNet: false,
        netMissing: false,
      },
    );
    expect(check.stale).toBe(true);
    expect(check.reported).toBe('https://dns.quad9.net/dns-query');
    expect(check.expected).toBe('https://cloudflare-dns.com/dns-query');
  });

  it('prefers the kernel setup because it may point at liora-net', () => {
    const check = checkDohTemplate(
      ['--dns-over-https-templates=https://dns.quad9.net/dns-query'],
      plan,
      {
        dohTemplates: 'https://127.0.0.1:5300/dns-query',
        dnsMode: 'secure',
        usesNet: true,
        netMissing: false,
      },
    );
    expect(check.expected).toBe('https://127.0.0.1:5300/dns-query');
    expect(check.stale).toBe(true);
  });

  it('flags encryption being turned off or on', () => {
    const disabled = checkDohTemplate(
      ['--dns-over-https-templates=https://cloudflare-dns.com/dns-query'],
      { mode: 'system', dohTemplates: '', endpoint: null },
      null,
    );
    expect(disabled.stale).toBe(true);
    expect(disabled.expected).toBe('');

    const enabledSinceStart = checkDohTemplate([], plan, {
      dohTemplates: 'https://cloudflare-dns.com/dns-query',
      dnsMode: 'secure',
      usesNet: false,
      netMissing: false,
    });
    expect(enabledSinceStart.stale).toBe(true);
    expect(enabledSinceStart.reported).toBe('');
  });

  it('ignores a trailing slash difference', () => {
    const check = checkDohTemplate(
      ['--dns-over-https-templates=https://cloudflare-dns.com/dns-query/'],
      plan,
      null,
    );
    expect(check.stale).toBe(false);
  });
});

describe('checkProxy', () => {
  const setup = (proxy: string) => ({
    dohTemplates: '',
    dnsMode: 'system',
    proxy,
    usesNet: true,
    netMissing: false,
  });

  it('is clean when the ports match', () => {
    const check = checkProxy(
      ['--proxy-server=http://127.0.0.1:5300'],
      setup('http://127.0.0.1:5300'),
      { running: true, proxyPort: 5300 },
    );
    expect(check.stale).toBe(false);
    expect(check.portChanged).toBe(false);
  });

  it('flags the ephemeral port changing after liora-net restarts', () => {
    const check = checkProxy(
      ['--proxy-server=http://127.0.0.1:5300'],
      setup('http://127.0.0.1:62411'),
      { running: true, proxyPort: 62411 },
    );
    expect(check.stale).toBe(true);
    expect(check.portChanged).toBe(true);
    expect(check.hostChanged).toBe(false);
    expect(check.reported).toBe('http://127.0.0.1:5300');
    expect(check.expected).toBe('http://127.0.0.1:62411');
  });

  it('flags a host change even when the port matches', () => {
    const check = checkProxy(
      ['--proxy-server=http://localhost:5300'],
      setup('http://127.0.0.1:5300'),
      null,
    );
    expect(check.stale).toBe(true);
    expect(check.hostChanged).toBe(true);
  });

  it('flags the proxy being removed or newly required', () => {
    const removed = checkProxy(['--proxy-server=http://127.0.0.1:5300'], setup(''), null);
    expect(removed.stale).toBe(true);
    expect(removed.expected).toBe('');

    const added = checkProxy([], setup('http://127.0.0.1:5300'), null);
    expect(added.stale).toBe(true);
    expect(added.reported).toBe('');
  });

  it('falls back to the running resolver when setup is unavailable', () => {
    const check = checkProxy(['--proxy-server=http://127.0.0.1:5300'], null, {
      running: true,
      proxyPort: 62411,
    });
    expect(check.expected).toBe('http://127.0.0.1:62411');
    expect(check.stale).toBe(true);
  });

  it('does not use a stale port when the resolver is not running', () => {
    const check = checkProxy(['--proxy-server=http://127.0.0.1:5300'], null, {
      running: false,
      proxyPort: 62411,
    });
    expect(check.expected).toBe('');
    expect(check.stale).toBe(true);
  });

  it('ignores a trailing slash', () => {
    const check = checkProxy(
      ['--proxy-server=http://127.0.0.1:5300/'],
      setup('http://127.0.0.1:5300'),
      null,
    );
    expect(check.stale).toBe(false);
  });
});

describe('staleItems', () => {
  it('returns nothing when every check is clean', () => {
    expect(staleItems(null, null, null)).toEqual([]);
  });

  it('merges rule, DoH and proxy staleness in a stable order', () => {
    const rules = checkForcedOrigins(
      ['--origin-to-force-quic-on=x.com:443'],
      [{ enabled: true, domains: ['example.org'], h3: 'force' }],
      true,
    );
    const dns = checkDohTemplate(
      ['--dns-over-https-templates=https://dns.quad9.net/dns-query'],
      {
        mode: 'provider',
        dohTemplates: 'https://cloudflare-dns.com/dns-query',
        endpoint: { name: 'Cloudflare', protocol: 'doh' },
      },
      null,
    );
    const proxy = checkProxy(
      ['--proxy-server=http://127.0.0.1:5300'],
      {
        dohTemplates: '',
        dnsMode: 'system',
        proxy: 'http://127.0.0.1:62411',
        usesNet: true,
        netMissing: false,
      },
      null,
    );

    const items = staleItems(rules, dns, proxy);
    expect(items.map((item) => item.kind)).toEqual(['rules', 'dns', 'proxy']);
    expect(items[0]).toMatchObject({
      reported: 'x.com:443',
      expected: 'example.org:443',
      extra: ['x.com:443'],
      missing: ['example.org:443'],
    });
    expect(items[1].reported).toContain('quad9');
    expect(items[2].reported).toContain('5300');
  });

  it('skips checks that are not stale', () => {
    const dns = checkDohTemplate(
      ['--dns-over-https-templates=https://cloudflare-dns.com/dns-query'],
      {
        mode: 'provider',
        dohTemplates: 'https://cloudflare-dns.com/dns-query',
        endpoint: { name: 'Cloudflare', protocol: 'doh' },
      },
      null,
    );
    const proxy = checkProxy(
      ['--proxy-server=http://127.0.0.1:5300'],
      null,
      { running: true, proxyPort: 5300 },
    );
    expect(staleItems(null, dns, proxy)).toEqual([]);
  });
});

describe('buildKernelSwitches', () => {
  it('keeps base switches and adds the effective network flags', () => {
    expect(
      buildKernelSwitches({
        switches: ['--enable-quic', '--origin-to-force-quic-on=x.com:443'],
        dohTemplates: 'https://cloudflare-dns.com/dns-query',
        dnsMode: 'secure',
        proxy: 'http://127.0.0.1:5300',
      }),
    ).toEqual([
      '--enable-quic',
      '--origin-to-force-quic-on=x.com:443',
      '--dns-over-https-mode=secure',
      '--dns-over-https-templates=https://cloudflare-dns.com/dns-query',
      '--proxy-server=http://127.0.0.1:5300',
    ]);
  });

  it('omits DoH and proxy flags when they are not used', () => {
    expect(buildKernelSwitches({ switches: ['--enable-quic'] })).toEqual(['--enable-quic']);
    expect(buildKernelSwitches({})).toEqual([]);
  });

  it('defaults the DoH mode when only a template is present', () => {
    expect(buildKernelSwitches({ dohTemplates: 'https://dns.example/q', dnsMode: '' })).toEqual([
      '--dns-over-https-mode=secure',
      '--dns-over-https-templates=https://dns.example/q',
    ]);
  });
});
