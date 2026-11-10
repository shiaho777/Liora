import { notify } from './bridge';

export interface DownloadItem {
  id: string;
  filename: string;
  path: string;
  received: number;
  total: number;
  state: 'started' | 'progress' | 'finished' | 'failed';
  error?: string;
}

export const downloads = $state({
  items: [] as DownloadItem[],
  open: false,
});

export function handleDownloadEvent(params: Partial<DownloadItem> & { id: string; state: DownloadItem['state'] }): void {
  const index = downloads.items.findIndex((item) => item.id === params.id);
  const merged: DownloadItem = {
    id: params.id,
    filename: params.filename ?? 'download',
    path: params.path ?? '',
    received: params.received ?? 0,
    total: params.total ?? 0,
    state: params.state,
    error: params.error,
  };

  if (index >= 0) downloads.items[index] = { ...downloads.items[index], ...merged };
  else downloads.items = [merged, ...downloads.items];

  // Surfacing the list when a download finishes mirrors every other browser.
  if (params.state === 'finished') downloads.open = true;
}

export function activeDownloadCount(): number {
  return downloads.items.filter((item) => item.state === 'started' || item.state === 'progress').length;
}

export function clearFinishedDownloads(): void {
  downloads.items = downloads.items.filter((item) => item.state === 'started' || item.state === 'progress');
}

export function revealDownload(item: DownloadItem): void {
  const parts = item.path.split('/');
  parts.pop();
  notify('ui.openPath', { path: parts.join('/') || item.path });
}

export function percentOf(item: DownloadItem): number {
  if (item.state === 'finished') return 100;
  if (item.total > 0) return Math.min(100, Math.round((item.received / item.total) * 100));
  return 0;
}

export function humanSize(bytes: number): string {
  if (bytes <= 0) return '';
  const units = ['B', 'KB', 'MB', 'GB'];
  let value = bytes;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit += 1;
  }
  return `${value.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`;
}
