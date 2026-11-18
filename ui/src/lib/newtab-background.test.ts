import { describe, expect, it } from 'vitest';
import { backgroundStyle, DEFAULT_CUSTOM_COLOR, hexToRgba, isHexColor } from './newtab-background';

describe('isHexColor', () => {
  it('accepts 3- and 6-digit hex', () => {
    expect(isHexColor('#fff')).toBe(true);
    expect(isHexColor('#3b6cf6')).toBe(true);
    expect(isHexColor('#3B6CF6')).toBe(true);
  });

  it('rejects non-hex values', () => {
    expect(isHexColor('red')).toBe(false);
    expect(isHexColor('#12345')).toBe(false);
    expect(isHexColor('javascript:alert(1)')).toBe(false);
    expect(isHexColor('')).toBe(false);
  });
});

describe('hexToRgba', () => {
  it('expands 3-digit hex', () => {
    expect(hexToRgba('#f00', 0.5)).toBe('rgba(255, 0, 0, 0.5)');
  });

  it('parses 6-digit hex', () => {
    expect(hexToRgba('#3b6cf6', 0.2)).toBe('rgba(59, 108, 246, 0.2)');
  });
});

describe('backgroundStyle', () => {
  it('default uses the accent/success glows', () => {
    const style = backgroundStyle('default', '');
    expect(style).toContain('var(--accent-soft)');
    expect(style).toContain('var(--success-soft)');
    expect(style).toContain('var(--bg)');
  });

  it('mono is flat', () => {
    expect(backgroundStyle('mono', '')).toBe('var(--bg)');
  });

  it('presets swap the glow colours', () => {
    const dawn = backgroundStyle('dawn', '');
    expect(dawn).toContain('255, 138, 92');
    expect(dawn).not.toContain('var(--accent-soft)');
  });

  it('custom tints from a valid colour', () => {
    const style = backgroundStyle('custom', '#ff0000');
    expect(style).toContain('rgba(255, 0, 0, 0.2)');
  });

  it('custom with an invalid colour falls back to default', () => {
    expect(backgroundStyle('custom', 'not-a-color')).toBe(backgroundStyle('default', ''));
  });

  it('unknown id falls back to default', () => {
    expect(backgroundStyle('nope', '')).toBe(backgroundStyle('default', ''));
  });

  it('the custom default seed is a valid tintable colour', () => {
    expect(isHexColor(DEFAULT_CUSTOM_COLOR)).toBe(true);
    expect(backgroundStyle('custom', DEFAULT_CUSTOM_COLOR)).toContain('rgba(');
  });
});