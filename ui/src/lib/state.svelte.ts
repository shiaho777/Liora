import { deriveAccent } from './accent';
import { call, describeSettings, setSetting as rpcSetSetting, start } from './bridge';
import { setLocale } from './i18n.svelte';
import { settingsStore } from './settings-store.svelte';

export interface ToastAction {
  label: string;
  path: string;
  /** Distinguishes opening the file itself from revealing its folder. */
  kind: 'file' | 'folder';
}

export interface Toast {
  id: number;
  text: string;
  kind: 'info' | 'error';
  /** Optional actions, e.g. open a downloaded file or reveal its folder. */
  actions?: ToastAction[];
}

export type PageKind =
  | 'browser'
  | 'newtab'
  | 'settings'
  | 'bookmarks'
  | 'history'
  | 'diagnostics'
  | 'downloads'
  | 'watches'
  | 'containers';

export const app = $state({
  ready: false,
  transport: 'none',
  version: '',
  platform: '',
  arch: '',
  profile: '',
  // ui.mode controls how much of the interface is visible.
  mode: 'simple' as 'simple' | 'standard' | 'advanced',
  theme: 'system' as 'system' | 'light' | 'dark',
  accent: '#3b6cf6',
  density: 'comfortable' as 'compact' | 'comfortable',
  tabsPosition: 'strip' as 'strip' | 'vertical' | 'hidden',
  language: 'auto',
  page: 'newtab' as PageKind,
  settingsSection: 'appearance',
  toasts: [] as Toast[],
  advancedVisible: false,
  // Reported by the shell: how the app is signed and whether Chromium started.
  runtime: { codeSigning: '', cefStatus: '' },
});

let toastSeq = 1;

export function toast(
  text: string,
  kind: 'info' | 'error' = 'info',
  actions?: ToastAction[],
): void {
  const id = toastSeq++;
  app.toasts.push({ id, text, kind, actions });
  setTimeout(() => {
    const index = app.toasts.findIndex((item) => item.id === id);
    if (index >= 0) app.toasts.splice(index, 1);
  }, actions && actions.length > 0 ? 8000 : kind === 'error' ? 5200 : 2600);
}

function applyAppearance(): void {
  const root = document.documentElement;
  if (app.theme === 'system') delete root.dataset.theme;
  else root.dataset.theme = app.theme;
  root.dataset.density = app.density;
  root.dataset.mode = app.mode;
  applyAccent();
}

// One user colour drives accent, readable text, soft fills and the gradient
// highlight. Re-derived whenever the theme flips so contrast stays correct.
function applyAccent(): void {
  const root = document.documentElement;
  const dark =
    app.theme === 'dark' ||
    (app.theme === 'system' && window.matchMedia('(prefers-color-scheme: dark)').matches);
  const palette = deriveAccent(app.accent, dark);
  if (!palette) return;
  root.style.setProperty('--accent', palette.accent);
  root.style.setProperty('--accent-text', palette.accentText);
  root.style.setProperty('--accent-soft', palette.accentSoft);
  root.style.setProperty('--accent-highlight', palette.accentHighlight);
}

// Follow the OS theme while in "system" mode so the accent stays readable.
if (typeof window !== 'undefined' && window.matchMedia) {
  window
    .matchMedia('(prefers-color-scheme: dark)')
    .addEventListener('change', () => {
      if (app.theme === 'system') applyAccent();
    });
}

export function applyMode(mode: string): void {
  app.mode = (['simple', 'standard', 'advanced'].includes(mode) ? mode : 'simple') as typeof app.mode;
  app.advancedVisible = app.mode === 'advanced';
  applyAppearance();
}

export async function boot(): Promise<void> {
  app.transport = await start();

  const info = await call<{
    version: string;
    platform: string;
    arch: string;
    profile: string;
  }>('core.info');
  app.version = info.version;
  app.platform = info.platform;
  app.arch = info.arch;
  app.profile = info.profile;

  // Load the handful of settings the shell itself needs before first paint.
  const description = await describeSettings();
  settingsStore.items = description.items;
  settingsStore.loaded = true;
  const values = new Map(description.items.map((item) => [item.key, item.value]));

  applyMode(String(values.get('ui.mode') ?? 'simple'));
  app.theme = String(values.get('ui.theme') ?? 'system') as typeof app.theme;
  app.accent = String(values.get('ui.accent') ?? '#3b6cf6');
  app.density = String(values.get('ui.density') ?? 'comfortable') as typeof app.density;
  // Hidden tabs are no longer offered; legacy profiles fall back to the strip.
  const tabs = String(values.get('ui.tabs') ?? 'strip');
  app.tabsPosition = (tabs === 'vertical' ? 'vertical' : 'strip') as typeof app.tabsPosition;
  app.language = String(values.get('ui.language') ?? 'auto');
  setLocale(app.language);
  applyAppearance();

  app.ready = true;
}

export async function updateSetting(key: string, value: unknown): Promise<void> {
  await rpcSetSetting(key, value);
  const stored = settingsStore.items.find((item) => item.key === key);
  if (stored) stored.value = value;
  switch (key) {
    case 'ui.mode':
      applyMode(String(value));
      break;
    case 'ui.theme':
      app.theme = String(value) as typeof app.theme;
      applyAppearance();
      break;
    case 'ui.accent':
      app.accent = String(value);
      applyAccent();
      break;
    case 'ui.density':
      app.density = String(value) as typeof app.density;
      applyAppearance();
      break;
    case 'ui.tabs':
      app.tabsPosition = String(value) as typeof app.tabsPosition;
      break;
    case 'ui.language':
      app.language = String(value);
      setLocale(String(value));
      break;
  }
}

export function openPage(page: PageKind, section?: string): void {
  app.page = page;
  if (section) app.settingsSection = section;
}

export function isInternalUrl(url: string): boolean {
  return url.startsWith('liora://');
}

export function internalPageFor(url: string): PageKind | null {
  if (!url.startsWith('liora://')) return null;
  const name = url.slice('liora://'.length).replace(/\/.*$/, '').toLowerCase();
  if (name === 'settings') return 'settings';
  if (name === 'bookmarks') return 'bookmarks';
  if (name === 'history') return 'history';
  if (name === 'diagnostics') return 'diagnostics';
  if (name === 'downloads') return 'downloads';
  if (name === 'watches') return 'watches';
  if (name === 'containers') return 'containers';
  if (name === 'newtab' || name === '') return 'newtab';
  return null;
}
