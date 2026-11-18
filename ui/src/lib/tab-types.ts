// Shared tab shape and helpers used by the main interface and the tab strip
// component. Kept in one place so the two never drift apart.

export type TabPage =
  | 'newtab'
  | 'settings'
  | 'bookmarks'
  | 'history'
  | 'diagnostics'
  | 'downloads'
  | 'watches'
  | 'containers'
  | 'web';

export interface Tab {
  id: number;
  title: string;
  url: string;
  page: TabPage;
  error?: { url: string; code: number; message: string };
  security?: { host: string; subject: string; valid: boolean; notAfter: number; https: boolean };
  /** Favicon as a data: URL, reported by the kernel. Session-only. */
  favicon?: string;
  canBack?: boolean;
  canForward?: boolean;
  groupId?: string;
  containerId?: string;
}

export interface TabGroup {
  id: string;
  name: string;
  color: string;
  collapsed: boolean;
}

/** A group with its member tabs resolved, for rendering the sidebar. */
export interface GroupedTabs extends TabGroup {
  tabs: Tab[];
}

/** Icon for an internal page tab. Web tabs use their favicon instead. */
export function internalIcon(page: string): string {
  if (page === 'settings') return 'settings';
  if (page === 'bookmarks') return 'star';
  if (page === 'history') return 'history';
  if (page === 'diagnostics') return 'info';
  if (page === 'downloads') return 'download';
  if (page === 'watches') return 'eye';
  if (page === 'containers') return 'shield';
  return 'home';
}