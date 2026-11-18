// Shared colour palettes. These are intentional data values (the choices a
// user picks from), not theme tokens — centralised here so every surface that
// offers group/container colours stays in sync.

/** Palette for tab groups and containers. */
export const identityPalette: string[] = [
  '#5b8cff',
  '#2bb673',
  '#f2a93b',
  '#e06464',
  '#a879ff',
  '#e06bb5',
];

/** Curated accent presets offered in Appearance settings. */
export const accentPresets: Array<{ value: string; name: string }> = [
  { value: '#3b6cf6', name: 'appearance.accent.blue' },
  { value: '#7c5cf0', name: 'appearance.accent.violet' },
  { value: '#0f9d8f', name: 'appearance.accent.teal' },
  { value: '#2f9e44', name: 'appearance.accent.green' },
  { value: '#e8590c', name: 'appearance.accent.orange' },
  { value: '#e64980', name: 'appearance.accent.pink' },
  { value: '#d6336c', name: 'appearance.accent.red' },
  { value: '#495057', name: 'appearance.accent.graphite' },
];