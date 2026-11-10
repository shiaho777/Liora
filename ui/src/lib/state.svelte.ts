import { call, describeSettings, setSetting as rpcSetSetting, start } from './bridge';
import { setLocale } from './i18n.svelte';

export interface Toast {
  id: number;
  text: string;
  kind: 'info' | 'error';
}

export type PageKind = 'browser' | 'newtab' | 'settings' | 'bookmarks' | 'history' | 'diagnostics';

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

export function toast(text: string, kind: 'info' | 'error' = 'info'): void {
  const id = toastSeq++;
  app.toasts.push({ id, text, kind });
  setTimeout(() => {
    const index = app.toasts.findIndex((item) => item.id === id);
    if (index >= 0) app.toasts.splice(index, 1);
  }, kind === 'error' ? 5200 : 2600);
}

function applyAppearance(): void {
  const root = document.documentElement;
  if (app.theme === 'system') delete root.dataset.theme;
  else root.dataset.theme = app.theme;
  root.dataset.density = app.density;
  root.dataset.mode = app.mode;
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
  const values = new Map(description.items.map((item) => [item.key, item.value]));

  applyMode(String(values.get('ui.mode') ?? 'simple'));
  app.theme = String(values.get('ui.theme') ?? 'system') as typeof app.theme;
  app.density = String(values.get('ui.density') ?? 'comfortable') as typeof app.density;
  app.tabsPosition = String(values.get('ui.tabs') ?? 'strip') as typeof app.tabsPosition;
  app.language = String(values.get('ui.language') ?? 'auto');
  setLocale(app.language);
  applyAppearance();

  app.ready = true;
}

export async function updateSetting(key: string, value: unknown): Promise<void> {
  await rpcSetSetting(key, value);
  switch (key) {
    case 'ui.mode':
      applyMode(String(value));
      break;
    case 'ui.theme':
      app.theme = String(value) as typeof app.theme;
      applyAppearance();
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
  if (name === 'newtab' || name === '') return 'newtab';
  return null;
}
