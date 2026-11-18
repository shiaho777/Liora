import { describe, expect, it } from 'vitest';
import { deriveAccent, luminance, mix, parseHex, toHex, whiteTextReadable } from './accent';

describe('parseHex', () => {
  it('accepts six-digit hex with and without hash', () => {
    expect(parseHex('#3b6cf6')).toEqual({ r: 59, g: 108, b: 246 });
    expect(parseHex('3b6cf6')).toEqual({ r: 59, g: 108, b: 246 });
  });

  it('expands three-digit shorthand', () => {
    expect(parseHex('#f00')).toEqual({ r: 255, g: 0, b: 0 });
  });

  it('rejects anything that is not a hex colour', () => {
    expect(parseHex('red')).toBeNull();
    expect(parseHex('#12345')).toBeNull();
    expect(parseHex('')).toBeNull();
    expect(parseHex('rgb(1,2,3)')).toBeNull();
  });
});

describe('mix and toHex', () => {
  it('mixes endpoints exactly', () => {
    const black = { r: 0, g: 0, b: 0 };
    const white = { r: 255, g: 255, b: 255 };
    expect(mix(black, white, 0)).toEqual(black);
    expect(mix(black, white, 1)).toEqual(white);
    expect(toHex(mix(black, white, 0.5))).toBe('#808080');
  });

  it('clamps out-of-range weights', () => {
    const a = { r: 10, g: 20, b: 30 };
    const b = { r: 200, g: 100, b: 50 };
    expect(mix(a, b, -1)).toEqual(a);
    expect(mix(a, b, 2)).toEqual(b);
  });
});

describe('deriveAccent', () => {
  it('returns null for invalid input instead of throwing', () => {
    expect(deriveAccent('not-a-colour', false)).toBeNull();
  });

  it('keeps the chosen colour as the accent itself', () => {
    const palette = deriveAccent('#3b6cf6', false);
    expect(palette?.accent).toBe('#3b6cf6');
  });

  it('darkens the text variant in light mode for readability', () => {
    const light = deriveAccent('#7cc4ff', false)!;
    const base = parseHex('#7cc4ff')!;
    expect(luminance(parseHex(light.accentText)!)).toBeLessThan(luminance(base));
  });

  it('lightens the text variant in dark mode', () => {
    const dark = deriveAccent('#1a3a8f', true)!;
    const base = parseHex('#1a3a8f')!;
    expect(luminance(parseHex(dark.accentText)!)).toBeGreaterThan(luminance(base));
  });

  it('produces a soft background close to the page colour', () => {
    const palette = deriveAccent('#e64980', false)!;
    // Soft fill is mostly white in light mode.
    expect(luminance(parseHex(palette.accentSoft)!)).toBeGreaterThan(0.7);
  });

  it('produces distinct values for every slot', () => {
    const palette = deriveAccent('#0f9d8f', true)!;
    const values = [palette.accent, palette.accentText, palette.accentSoft, palette.accentHighlight];
    expect(new Set(values).size).toBe(4);
  });
});

describe('whiteTextReadable', () => {
  it('accepts the default blue', () => {
    expect(whiteTextReadable('#3b6cf6')).toBe(true);
  });

  it('rejects pale colours that would hide white text', () => {
    expect(whiteTextReadable('#ffee99')).toBe(false);
  });

  it('rejects invalid input', () => {
    expect(whiteTextReadable('nope')).toBe(false);
  });
});