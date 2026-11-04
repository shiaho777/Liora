import { describeSettings, setSetting, resetSetting, type SettingItem } from './bridge';

export const settingsStore = $state({
  items: [] as SettingItem[],
  loaded: false,
});

export async function loadSettings(): Promise<void> {
  const description = await describeSettings();
  settingsStore.items = description.items;
  settingsStore.loaded = true;
}

export function value<T = unknown>(key: string): T {
  const item = settingsStore.items.find((entry) => entry.key === key);
  return (item ? item.value : undefined) as T;
}

export function choices(key: string): string[] {
  return settingsStore.items.find((entry) => entry.key === key)?.choices ?? [];
}

export async function set(key: string, next: unknown): Promise<void> {
  const item = settingsStore.items.find((entry) => entry.key === key);
  if (item) item.value = next;
  await setSetting(key, next);
}

export async function reset(key: string): Promise<void> {
  const item = settingsStore.items.find((entry) => entry.key === key);
  if (item) item.value = item.default;
  await resetSetting(key);
}
