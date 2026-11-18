// Accent colour derivation. One user-chosen colour drives the whole palette:
// the accent itself, a readable text variant, soft backgrounds and the
// gradient highlight. Pure functions so the rules are unit-testable.

export interface Rgb {
  r: number;
  g: number;
  b: number;
}

const HEX_PATTERN = /^#?([0-9a-f]{3}|[0-9a-f]{6})$/i;

/** Parses #rgb / #rrggbb (with or without #). Returns null for anything else. */
export function parseHex(input: string): Rgb | null {
  const match = HEX_PATTERN.exec(input.trim());
  if (!match) return null;
  let hex = match[1];
  if (hex.length === 3) {
    hex = hex
      .split('')
      .map((char) => char + char)
      .join('');
  }
  const value = parseInt(hex, 16);
  return { r: (value >> 16) & 255, g: (value >> 8) & 255, b: value & 255 };
}

export function toHex(color: Rgb): string {
  const part = (value: number) =>
    Math.max(0, Math.min(255, Math.round(value))).toString(16).padStart(2, '0');
  return `#${part(color.r)}${part(color.g)}${part(color.b)}`;
}

/** Mixes two colours; weight 0 returns a, weight 1 returns b. */
export function mix(a: Rgb, b: Rgb, weight: number): Rgb {
  const t = Math.max(0, Math.min(1, weight));
  return {
    r: a.r + (b.r - a.r) * t,
    g: a.g + (b.g - a.g) * t,
    b: a.b + (b.b - a.b) * t,
  };
}

/** Relative luminance per WCAG, 0 (black) to 1 (white). */
export function luminance(color: Rgb): number {
  const channel = (value: number) => {
    const c = value / 255;
    return c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * channel(color.r) + 0.7152 * channel(color.g) + 0.0722 * channel(color.b);
}

export interface AccentPalette {
  accent: string;
  /** Readable on the page background: darker in light mode, lighter in dark. */
  accentText: string;
  /** Tinted background for badges and selected rows. */
  accentSoft: string;
  /** Second stop of the brand gradient. */
  accentHighlight: string;
}

const WHITE: Rgb = { r: 255, g: 255, b: 255 };
const BLACK: Rgb = { r: 0, g: 0, b: 0 };

/**
 * Derives the palette from one colour. Light mode darkens the text variant so
 * pale accents stay readable; dark mode lightens it. Soft backgrounds mix
 * toward the page colour so they never overpower content.
 */
export function deriveAccent(hex: string, dark: boolean): AccentPalette | null {
  const base = parseHex(hex);
  if (!base) return null;
  const text = dark ? mix(base, WHITE, 0.45) : mix(base, BLACK, 0.28);
  const softBase = dark ? mix(base, BLACK, 0.55) : mix(base, WHITE, 0.82);
  const highlight = mix(base, WHITE, 0.35);
  return {
    accent: toHex(base),
    accentText: toHex(text),
    accentSoft: toHex(softBase),
    accentHighlight: toHex(highlight),
  };
}

/** True when white text on this colour reaches WCAG AA for large text (3:1). */
export function whiteTextReadable(hex: string): boolean {
  const color = parseHex(hex);
  if (!color) return false;
  const contrast = (1 + 0.05) / (luminance(color) + 0.05);
  return contrast >= 3;
}