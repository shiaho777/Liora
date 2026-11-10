import { describe, expect, it } from 'vitest';
import { networkDelta } from './network-delta';

const applied = {
  provider: 'Quad9 Secure',
  protocol: 'doh',
  mode: 'secure',
  proxy: 'http://127.0.0.1:55073',
};

describe('networkDelta', () => {
  it('returns nothing when the current settings match what was applied', () => {
    expect(networkDelta(applied, { ...applied })).toEqual([]);
  });

  it('reports a provider change without repeating unchanged fields', () => {
    const delta = networkDelta(applied, { ...applied, provider: 'Cloudflare Standard' });
    expect(delta).toEqual([
      { kind: 'provider', from: 'Quad9 Secure', to: 'Cloudflare Standard' },
    ]);
  });

  it('reports a mode or protocol change', () => {
    expect(networkDelta(applied, { ...applied, mode: 'automatic' })).toEqual([
      { kind: 'mode', from: 'secure', to: 'automatic' },
    ]);
    expect(networkDelta(applied, { ...applied, protocol: 'dot' })).toEqual([
      { kind: 'protocol', from: 'doh', to: 'dot' },
    ]);
  });

  it('reports the ephemeral proxy port changing', () => {
    const delta = networkDelta(applied, {
      ...applied,
      proxy: 'http://127.0.0.1:55120',
    });
    expect(delta).toEqual([
      {
        kind: 'proxy',
        from: 'http://127.0.0.1:55073',
        to: 'http://127.0.0.1:55120',
      },
    ]);
  });

  it('compares case and a trailing slash leniently', () => {
    expect(
      networkDelta(
        { provider: 'Quad9 Secure', proxy: 'http://127.0.0.1:55073/' },
        { provider: 'quad9 secure', proxy: 'http://127.0.0.1:55073' },
      ),
    ).toEqual([]);
  });

  it('lists several changes in provider, mode, proxy order', () => {
    const delta = networkDelta(applied, {
      provider: 'Cloudflare Standard',
      protocol: 'doh',
      mode: 'automatic',
      proxy: '',
    });
    expect(delta.map((item) => item.kind)).toEqual(['provider', 'mode', 'proxy']);
    expect(delta[2]).toEqual({
      kind: 'proxy',
      from: 'http://127.0.0.1:55073',
      to: '',
    });
  });
});
