import { describe, expect, it } from 'vitest';
import {
  explainLimit,
  explainSwitch,
  knownLimits,
  knownSwitches,
  switchName,
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

  it('keeps the meaning of forceQuic distinct from ech', () => {
    expect(explainLimit('forceQuic', 'zh').detail).toContain('TCP');
    expect(explainLimit('ech', 'zh').detail).toContain('ECHConfig');
    expect(explainLimit('ech', 'zh').detail).not.toBe(explainLimit('forceQuic', 'zh').detail);
  });
});
