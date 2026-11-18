// Pure helpers for the start page background. Kept free of Svelte so the
// style-building rules can be unit-tested.

export type BackgroundId = 'default' | 'dawn' | 'dusk' | 'forest' | 'mono' | 'custom';

export interface BackgroundPreset {
  id: BackgroundId;
  /** i18n key suffix for the display name. */
  name: string;
  /** Two glow colours used in the radial gradients; empty for flat themes. */
  glowA: string;
  glowB: string;
}

export const BACKGROUND_PRESETS: BackgroundPreset[] = [
  { id: 'default', name: 'default', glowA: 'var(--accent-soft)', glowB: 'var(--success-soft)' },
  { id: 'dawn', name: 'dawn', glowA: 'rgba(255, 138, 92, 0.20)', glowB: 'rgba(255, 205, 112, 0.16)' },
  { id: 'dusk', name: 'dusk', glowA: 'rgba(124, 108, 255, 0.20)', glowB: 'rgba(64, 158, 255, 0.14)' },
  { id: 'forest', name: 'forest', glowA: 'rgba(52, 199, 123, 0.16)', glowB: 'rgba(24, 160, 140, 0.14)' },
  { id: 'mono', name: 'mono', glowA: '', glowB: '' },
];

// Default seed for the user-customisable background colour. This is a setting
// default (the user can change it), not a fixed interface colour.
// quality-audit-ignore: user-editable colour default
export const DEFAULT_CUSTOM_COLOR = '#3b6cf6';

const HEX_COLOR = /^#(?:[0-9a-f]{3}|[0-9a-f]{6})$/i;

export function isHexColor(value: string): boolean {
  return HEX_COLOR.test(value.trim());
}

/** Converts #rgb / #rrggbb to an rgba() string with the given alpha. */
export function hexToRgba(hex: string, alpha: number): string {
  let h = hex.trim().replace('#', '');
  if (h.length === 3) h = h.split('').map((c) => c + c).join('');
  const r = parseInt(h.slice(0, 2), 16);
  const g = parseInt(h.slice(2, 4), 16);
  const b = parseInt(h.slice(4, 6), 16);
  return `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

/**
 * Builds the CSS background value for the start page. `default` reproduces the
 * original accent/success glows; presets swap the glow colours; `mono` is flat;
 * `custom` tints the glows from a user colour. Unknown ids fall back to default.
 */
export function backgroundStyle(id: string, customColor: string): string {
  if (id === 'mono') return 'var(--bg)';
  if (id === 'custom') {
    if (!isHexColor(customColor)) return backgroundStyle('default', '');
    const a = hexToRgba(customColor, 0.2);
    const b = hexToRgba(customColor, 0.12);
    return `radial-gradient(900px 520px at 82% -18%, ${a}, transparent 62%), ` +
      `radial-gradient(760px 480px at -12% 112%, ${b}, transparent 60%), var(--bg)`;
  }
  const preset = BACKGROUND_PRESETS.find((p) => p.id === id) ?? BACKGROUND_PRESETS[0];
  return `radial-gradient(900px 520px at 82% -18%, ${preset.glowA}, transparent 62%), ` +
    `radial-gradient(760px 480px at -12% 112%, ${preset.glowB}, transparent 60%), var(--bg)`;
}