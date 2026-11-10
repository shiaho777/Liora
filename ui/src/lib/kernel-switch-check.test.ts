import { describe, expect, it } from 'vitest';
import {
  checkForcedOrigins,
  expectedForcedOrigins,
  parseForcedOrigins,
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
