<script lang="ts">
  import Icon from './lib/Icon.svelte';
  import NewTab from './lib/NewTab.svelte';
  import Settings from './lib/Settings.svelte';
  import CommandPalette from './lib/CommandPalette.svelte';
  import ReaderView from './lib/ReaderView.svelte';
  import Watches from './lib/Watches.svelte';
  import WatchDialog from './lib/WatchDialog.svelte';
  import Containers from './lib/Containers.svelte';
  import ContainerPicker from './lib/ContainerPicker.svelte';
  import { parseReaderData, type ReaderData } from './lib/reader';
  import Library from './lib/Library.svelte';
  import ErrorPanel from './lib/ErrorPanel.svelte';
  import SiteCard from './lib/SiteCard.svelte';
  import DiagnosticsPage from './lib/DiagnosticsPage.svelte';
  import DownloadsPage from './lib/DownloadsPage.svelte';
  import {
    activeDownloadCount,
    downloads,
    handleDownloadEvent,
    seedDownloadHistory,
    type DownloadRecord,
  } from './lib/downloads.svelte';
  import { buildSuggestions, moveSelection, type Suggestion } from './lib/suggest';
  import { app, boot, internalPageFor, openPage, toast } from './lib/state.svelte';
  import { networkPending, refreshNetworkStatus } from './lib/network-status.svelte';
  import { addressBadges, type HostRule } from './lib/address-badges';
  import { runCommand } from './lib/commands';
  import { runTestScenario } from './lib/test-commands';
  import TabStrip from './lib/TabStrip.svelte';
  import TabRail from './lib/TabRail.svelte';
  import Toolbar from './lib/Toolbar.svelte';
  import { internalIcon, type Tab, type TabGroup } from './lib/tab-types';
  import { identityPalette } from './lib/palette';
  import { call, describeSettings, notify, onNotification } from './lib/bridge';
  import { t } from './lib/i18n.svelte';
  import { settingsStore } from './lib/settings-store.svelte';

  interface Container {
    id: string;
    name: string;
    color: string;
  }

  interface SplitState {
    primary: number;
    secondary: number;
  }

  const firstTab: Tab = { id: 1, title: t('tab.new'), url: 'liora://newtab', page: 'newtab' };
  let tabSeq = 2;
  let tabs = $state<Tab[]>([firstTab]);
  let activeId = $state(firstTab.id);
  let addressValue = $state('');
  let addressFocused = $state(false);
  let suggestions = $state<Suggestion[]>([]);
  let suggestSelected = $state(-1);
  let suggestEnabled = $state(true);
  let findOpen = $state(false);
  let findText = $state('');
  let findMatches = $state(0);
  let findActive = $state(0);
  let findMatchCase = $state(false);
  let findInputEl = $state<HTMLInputElement | null>(null);
  let findTabId = $state(0);
  let findAutoAdvanced = $state(false);
  let contentEl = $state<HTMLDivElement | null>(null);
  let addressEl = $state<HTMLInputElement | null>(null);
  let siteCardOpen = $state(false);
  let paletteOpen = $state(false);
  let groups = $state<TabGroup[]>([]);
  let closedTabs = $state<Array<{ title: string; url: string; page: Tab['page']; groupId?: string }>>(
    [],
  );
  let reader = $state<ReaderData | null>(null);
  let readerTab = $state(0);
  let readerUrl = $state('');
  let readerSummary = $state(false);
  let split = $state<SplitState | null>(null);
  let splitRatio = $state(0.5);
  let splitDragging = $state(false);
  let splitEl = $state<HTMLDivElement | null>(null);
  let layoutFrame = 0;
  let watchDialogOpen = $state(false);
  let watchDialogUrl = $state('');
  let watchDialogTitle = $state('');
  let containers = $state<Container[]>([]);
  let containerPickerOpen = $state(false);
  // Address bar badges are derived from real state, never hard-coded.
  let activeRule = $state<HostRule | null>(null);
  let kernelCaps = $state<Record<string, string[]>>({});
  let resolverRunning = $state(false);
  let badgeEpoch = $state(0);
  // Favicon cache shared by tabs and the start page. Persisted in Core so a
  // restart shows real icons immediately, without any third-party service.
  let faviconCache = $state<Record<string, string>>({});

  const active = $derived(tabs.find((tab) => tab.id === activeId) ?? tabs[0]);

  const activeHost = $derived.by(() => {
    try {
      return new URL(active?.url ?? '').hostname;
    } catch {
      return '';
    }
  });

  // Only claims what a matched rule asks for and this kernel can honour.
  const badges = $derived.by(() => {
    void badgeEpoch;
    if (!active || active.page !== 'web') return [];
    const kernelId = activeRule?.kernel ?? '';
    return addressBadges({
      url: active.url,
      rule: activeRule,
      caps: kernelCaps[kernelId] ?? [],
      resolverRunning,
    });
  });

  async function loadAddressState(): Promise<void> {
    const url = active?.url ?? '';
    if (active?.page !== 'web' || !activeHost) {
      activeRule = null;
      return;
    }
    const [matched, kernels, runtime] = await Promise.allSettled([
      call<HostRule>('rules.match', { host: activeHost }),
      call<Array<{ id: string; caps?: string[] }>>('kernels.list'),
      call<{ running?: boolean }>('net.runtime'),
    ]);
    // The user may have navigated again while these were in flight.
    if ((active?.url ?? '') !== url) return;
    activeRule = matched.status === 'fulfilled' ? matched.value : null;
    if (kernels.status === 'fulfilled') {
      const next: Record<string, string[]> = {};
      for (const kernel of kernels.value) next[kernel.id] = kernel.caps ?? [];
      kernelCaps = next;
    }
    resolverRunning = runtime.status === 'fulfilled' ? Boolean(runtime.value.running) : false;
  }

  $effect(() => {
    void active?.url;
    void active?.page;
    void activeHost;
    void badgeEpoch;
    void loadAddressState();
  });

  const showTabs = $derived(app.tabsPosition !== 'hidden');
  const verticalTabs = $derived(app.tabsPosition === 'vertical');
  const readerActive = $derived(
    Boolean(reader && readerTab === activeId && readerUrl === active.url),
  );
  const splitPrimary = $derived(split ? (tabs.find((tab) => tab.id === split!.primary) ?? null) : null);
  const splitSecondary = $derived(
    split ? (tabs.find((tab) => tab.id === split!.secondary) ?? null) : null,
  );
  const webTabCount = $derived(tabs.filter((tab) => tab.page === 'web').length);
  const containerColors = identityPalette;
  const activeContainer = $derived(
    containers.find((container) => container.id === active?.containerId) ?? null,
  );
  const containerTabCounts = $derived.by(() => {
    const counts: Record<string, number> = {};
    for (const tab of tabs) {
      if (!tab.containerId) continue;
      counts[tab.containerId] = (counts[tab.containerId] ?? 0) + 1;
    }
    return counts;
  });

  const searchTemplates: Record<string, string> = {
    duckduckgo: 'https://duckduckgo.com/?q=',
    google: 'https://www.google.com/search?q=',
    bing: 'https://www.bing.com/search?q=',
    brave: 'https://search.brave.com/search?q=',
    startpage: 'https://www.startpage.com/do/search?q=',
    baidu: 'https://www.baidu.com/s?wd=',
    sogou: 'https://www.sogou.com/web?query=',
  };
  const searchEngine = $derived.by(() => {
    const item = settingsStore.items.find((entry) => entry.key === 'search.engine');
    return typeof item?.value === 'string' ? item.value : 'duckduckgo';
  });
  const searchEngineNames: Record<string, string> = {
    duckduckgo: 'DuckDuckGo',
    google: 'Google',
    bing: 'Bing',
    brave: 'Brave Search',
    startpage: 'Startpage',
    baidu: '百度',
    sogou: '搜狗',
  };
  const homePage = $derived.by(() => {
    const item = settingsStore.items.find((entry) => entry.key === 'home.page');
    return typeof item?.value === 'string' && item.value ? item.value : 'liora://newtab';
  });
  const showHomeButton = $derived.by(() => {
    const item = settingsStore.items.find((entry) => entry.key === 'ui.showHomeButton');
    return item?.value === true;
  });
  let bookmarks = $state<Array<{ id: number; title: string; url: string }>>([]);
  let sessionReady = $state(false);
  let saveTimer: ReturnType<typeof setTimeout> | undefined;

  // The address bar shares the start page's local-suggestions switch.
  $effect(() => {
    const item = settingsStore.items.find((entry) => entry.key === 'search.suggest');
    suggestEnabled = item ? item.value !== false : true;
  });

  boot()
    .then(async () => {
      const description = await describeSettings();
      const restore = description.items.find((item) => item.key === 'startup.restore');
      if (restore?.value !== false) await restoreSession();
      sessionReady = true;
      await loadBookmarks();
      await loadDownloadHistory();
      await loadFaviconCache();
      void refreshNetworkStatus();
      // The shell's first-paint push can arrive before the bridge is ready.
      notify('ui.runtime', {});
    })
    .catch((error) => toast(String(error?.message ?? error), 'error'));

  function pageForUrl(url: string): Tab['page'] {
    const internal = internalPageFor(url);
    return (internal ?? 'web') as Tab['page'];
  }

  async function restoreSession(): Promise<void> {
    try {
      const session = await call<{
        tabs?: Array<{ url: string; title: string; page: string; group?: string; container?: string }>;
        groups?: Array<{ id: string; name: string; color: string; collapsed?: boolean }>;
        containers?: Array<{ id: string; name: string; color: string }>;
        split?: { primary?: number; secondary?: number; ratio?: number } | null;
        active?: number;
      }>('session.load');
      if (!session.tabs?.length) return;
      if (Array.isArray(session.groups)) {
        groups = session.groups
          .filter((group) => group && typeof group.id === 'string' && typeof group.name === 'string')
          .map((group) => ({
            id: group.id,
            name: group.name,
            color: typeof group.color === 'string' ? group.color : groupColors[0],
            collapsed: group.collapsed === true,
          }));
      }
      // A tab that turned into a download is transient; restoring it would
      // download the file again.
      const restorable = session.tabs.filter((item) => item.page !== 'download');
      if (restorable.length === 0) return;
      if (Array.isArray(session.containers)) {
        containers = session.containers
          .filter(
            (container) =>
              container &&
              typeof container.id === 'string' &&
              typeof container.name === 'string' &&
              typeof container.color === 'string',
          )
          .map((container) => ({
            id: container.id,
            name: container.name,
            color: container.color,
          }));
      }
      const restored: Tab[] = restorable.map((item, index) => ({
        id: index + 1,
        title: item.title || item.url,
        url: item.url,
        page: pageForUrl(item.url),
        groupId: item.group && groups.some((group) => group.id === item.group) ? item.group : undefined,
        containerId:
          item.container && containers.some((container) => container.id === item.container)
            ? item.container
            : undefined,
      }));
      tabs = restored;
      tabSeq = restored.length + 1;
      activeId = restored.some((tab) => tab.id === session.active) ? session.active! : restored[0].id;
      if (session.split) {
        const primary = restored.find((tab) => tab.id === Number(session.split!.primary));
        const secondary = restored.find((tab) => tab.id === Number(session.split!.secondary));
        if (primary?.page === 'web' && secondary?.page === 'web') {
          split = { primary: primary.id, secondary: secondary.id };
          const ratio = Number(session.split.ratio);
          if (Number.isFinite(ratio)) splitRatio = Math.min(0.8, Math.max(0.2, ratio));
          if (activeId !== primary.id && activeId !== secondary.id) activeId = primary.id;
        }
      }
      // Load every page tab; the shell keeps background views hidden.
      for (const tab of restored) {
        if (tab.page === 'web') {
          notify('ui.navigate', {
            url: tab.url,
            tab: tab.id,
            container: tab.containerId ?? '',
          });
          void applyStoredZoom(tab.url, tab.id);
        }
      }
    } catch {
      /* a damaged session file must not block startup */
    }
  }

  async function loadDownloadHistory(): Promise<void> {
    try {
      const records = await call<DownloadRecord[]>('downloads.list', { limit: 50 });
      seedDownloadHistory(records);
    } catch {
      /* history is optional; the panel still works without it */
    }
  }

  async function loadFaviconCache(): Promise<void> {
    try {
      const list = await call<Array<{ host: string; dataUrl: string }>>('favicon.list', {});
      const next: Record<string, string> = {};
      for (const item of list) {
        if (item.host && item.dataUrl) next[item.host] = item.dataUrl;
      }
      faviconCache = next;
      // Restored tabs get their icons back without waiting for a reload.
      let changed = false;
      for (const tab of tabs) {
        if (tab.page !== 'web' || tab.favicon) continue;
        const icon = next[hostOfUrl(tab.url)];
        if (icon) {
          tab.favicon = icon;
          changed = true;
        }
      }
      if (changed) tabs = [...tabs];
    } catch {
      /* the cache is an enhancement; browsing works without it */
    }
  }

  onNotification((message) => {
    if (message.method === 'bookmarks.changed') void loadBookmarks();
  });

  async function loadBookmarks(): Promise<void> {    try {
      const tree = await call<Record<string, { children?: Array<{ id: number; type: string; title: string; url?: string }> }>>(
        'bookmarks.tree',
      );
      // Every bookmark on the bar stays reachable; the bar scrolls instead of
      // silently dropping everything past an arbitrary cut-off.
      bookmarks = (tree.bar?.children ?? [])
        .filter((node) => node.type === 'url' && node.url)
        .map((node) => ({ id: node.id, title: node.title || node.url!, url: node.url! }));
    } catch {
      bookmarks = [];
    }
  }

  const activeBookmarked = $derived(
    active.page === 'web' && bookmarks.some((entry) => entry.url === active.url),
  );

  const groupColors = identityPalette;

  const groupedTabs = $derived(
    groups.map((group) => ({
      ...group,
      tabs: tabs.filter((tab) => tab.groupId === group.id),
    })),
  );

  const ungroupedTabs = $derived(tabs.filter((tab) => !tab.groupId));

  const paletteCommands = $derived([
    { id: 'newtab', label: t('tab.new'), icon: 'plus', hint: '⌘T' },
    { id: 'settings', label: t('nav.settings'), icon: 'settings', hint: '⌘,' },
    { id: 'downloads', label: t('nav.downloads'), icon: 'download' },
    { id: 'bookmarks', label: t('nav.bookmarks'), icon: 'star' },
    { id: 'history', label: t('nav.history'), icon: 'history' },
    { id: 'dns', label: t('nav.dns'), icon: 'shield' },
    { id: 'watches', label: t('nav.watches'), icon: 'eye' },
    { id: 'containers', label: t('containers.title'), icon: 'shield' },
    ...(active.page === 'web'
      ? [{ id: 'opencontainer', label: t('containers.open'), icon: 'shield' }]
      : []),
    ...(active.page === 'web'
      ? [{ id: 'watch', label: t('watch.create'), icon: 'eye' }]
      : []),
    { id: 'group', label: t('groups.new'), icon: 'folder' },
    ...(active.groupId
      ? [{ id: 'ungroup', label: t('groups.remove'), icon: 'close' }]
      : []),
    ...(active.page === 'web'
      ? [
          { id: 'find', label: t('nav.find'), icon: 'search', hint: '⌘F' },
          { id: 'reader', label: t('reader.open'), icon: 'book', hint: '⌘⌥R' },
          { id: 'summary', label: t('summary.command'), icon: 'sparkles' },
          { id: 'mini', label: t('mini.open'), icon: 'external' },
          ...(split || webTabCount > 1
            ? [
                {
                  id: 'split',
                  label: split ? t('split.close') : t('split.open'),
                  icon: 'columns',
                  hint: '⌘\\',
                },
              ]
            : []),
          { id: 'copylink', label: t('palette.copyClean'), icon: 'external' },
        ]
      : []),
    { id: 'reload', label: t('nav.reload'), icon: 'reload', hint: '⌘R' },
    { id: 'close', label: t('tab.close'), icon: 'close', hint: '⌘W' },
    ...(closedTabs.length > 0
      ? [{ id: 'reopen', label: t('tab.reopen'), icon: 'refresh', hint: '⌘⇧T' }]
      : []),
  ]);

  // A failed load is reported by the kernel; show the diagnosis panel instead
  // of a blank page.
  let networkTimer: ReturnType<typeof setTimeout> | undefined;
  let applyTimer: ReturnType<typeof setTimeout> | undefined;
  let applyState = $state<'idle' | 'applying' | 'applied' | 'error'>('idle');
  let appliedProxyPort = $state('');
  let appliedAt = $state('');

  function proxyPortOf(proxy: string | undefined): string {
    if (!proxy) return '';
    try {
      return new URL(proxy).port;
    } catch {
      return /:(\d+)\/?$/.exec(proxy)?.[1] ?? '';
    }
  }

  const applyTitle = $derived(
    applyState === 'applied'
      ? appliedProxyPort
        ? t('network.appliedHintPort', { port: appliedProxyPort, time: appliedAt })
        : t('network.appliedHint', { time: appliedAt })
      : t('network.applyHint'),
  );

  // The toolbar badge applies changes directly: restart the resolver, re-apply
  // the proxy and reload pages, then confirm or report failure.
  function applyNetworkChanges(): void {
    if (applyState === 'applying' || applyState === 'applied') return;
    applyState = 'applying';
    appliedProxyPort = '';
    appliedAt = '';
    notify('ui.restartKernel', {});
    clearTimeout(applyTimer);
    applyTimer = setTimeout(() => {
      if (applyState !== 'applying') return;
      applyState = 'error';
      toast(t('network.applyFailed'), 'error');
      applyTimer = setTimeout(() => {
        applyState = 'idle';
      }, 3000);
    }, 15000);
  }

  async function finishNetworkApply(proxy?: string): Promise<void> {
    await refreshNetworkStatus();
    clearTimeout(applyTimer);
    if (networkPending()) {
      applyState = 'error';
      appliedProxyPort = '';
      appliedAt = '';
      toast(t('network.applyFailed'), 'error');
      applyTimer = setTimeout(() => {
        applyState = 'idle';
      }, 3000);
    } else {
      applyState = 'applied';
      appliedProxyPort = proxyPortOf(proxy);
      appliedAt = new Date().toLocaleTimeString();
      applyTimer = setTimeout(() => {
        applyState = 'idle';
      }, 2500);
    }
  }

  // Network settings can change from the DNS panel, the rules list or a
  // restart. Refresh the global "unapplied changes" state shortly afterwards.
  function scheduleNetworkStatus(): void {
    clearTimeout(networkTimer);
    networkTimer = setTimeout(() => {
      void refreshNetworkStatus();
    }, 300);
  }

  onNotification((message) => {
    if (
      message.method === 'settings.changed' ||
      message.method === 'net.rulesChanged' ||
      message.method === 'net.runtimeChanged' ||
      message.method === 'browser.kernelRestarted' ||
      message.method === 'core.reloaded'
    ) {
      scheduleNetworkStatus();
      badgeEpoch += 1;
    }
    if (message.method === 'browser.kernelRestarted' && applyState === 'applying') {
      const params = (message.params ?? {}) as { proxy?: string };
      void finishNetworkApply(params.proxy);
    }
    if (message.method === 'browser.runtime' && message.params) {
      const params = message.params as { codeSigning?: string; cefStatus?: string };
      app.runtime = {
        codeSigning: params.codeSigning ?? '',
        cefStatus: params.cefStatus ?? '',
      };
      // Visible to the shell for support diagnostics.
      (window as unknown as { __lioraRuntimeSeen?: unknown }).__lioraRuntimeSeen = app.runtime;
      return;
    }
    if (message.method === 'browser.timelineSaved' && message.params) {
      const params = message.params as { path?: string };
      const path = params.path ?? '';
      const name = path.split('/').pop() ?? '';
      const directory = path.includes('/') ? path.slice(0, path.lastIndexOf('/')) : '';
      const actions = [];
      if (path) actions.push({ label: t('downloads.openFile'), path, kind: 'file' as const });
      if (directory) {
        actions.push({ label: t('downloads.reveal'), path: directory, kind: 'folder' as const });
      }
      toast(t('timeline.saved', { name }), 'info', actions);
      return;
    }
    if (message.method === 'browser.timelineFailed' && message.params) {
      const params = message.params as { message?: string };
      toast(params.message || t('timeline.saveFailed'), 'error');
      return;
    }
    if (message.method === 'browser.diagnosticSaved' && message.params) {
      const params = message.params as { path?: string };
      const path = params.path ?? '';
      const name = path.split('/').pop() ?? '';
      const directory = path.includes('/') ? path.slice(0, path.lastIndexOf('/')) : '';
      const actions = [];
      if (path) actions.push({ label: t('downloads.openFile'), path, kind: 'file' as const });
      if (directory) {
        actions.push({ label: t('downloads.reveal'), path: directory, kind: 'folder' as const });
      }
      toast(t('error.saved', { name }), 'info', actions);
      return;
    }
    if (message.method === 'browser.diagnosticFailed' && message.params) {
      const params = message.params as { message?: string };
      toast(params.message || t('error.saveFailed'), 'error');
      return;
    }
    if (message.method === 'browser.findResult' && message.params) {
      const params = message.params as { tab?: number; matches?: number; active?: number };
      if (Number(params.tab) === activeId && findOpen) {
        findMatches = Math.max(0, Number(params.matches ?? 0));
        findActive = Math.max(0, Number(params.active ?? 0));
        if (findActive > 0) findAutoAdvanced = true;
      }
      return;
    }
    if (message.method === 'browser.history' && message.params) {
      const params = message.params as { tab?: number; canBack?: boolean; canForward?: boolean };
      const tab = tabs.find((entry) => entry.id === Number(params.tab));
      if (tab) {
        tab.canBack = params.canBack === true;
        tab.canForward = params.canForward === true;
        tabs = [...tabs];
      }
      return;
    }
    if (message.method === 'browser.urlChanged' && message.params) {
      const params = message.params as { tab?: number; url?: string };
      const url = (params.url ?? '').trim();
      if (!url || url === 'about:blank') return;
      const tab = tabs.find((entry) => entry.id === Number(params.tab));
      if (tab && tab.page === 'web' && !tab.error) {
        // A different site must not keep the previous site's icon while the
        // new page loads. Same-host navigations keep it.
        if (tab.favicon && hostOfUrl(url) !== hostOfUrl(tab.url)) tab.favicon = undefined;
        tab.url = url;
        tabs = [...tabs];
        void applyStoredZoom(url, tab.id);
      }
      return;
    }
    if (message.method === 'browser.titleChanged' && message.params) {
      const params = message.params as { tab?: number; title?: string };
      const title = (params.title ?? '').trim();
      if (!title) return;
      const tab =
        tabs.find((entry) => entry.id === Number(params.tab)) ??
        tabs.find((entry) => entry.id === activeId);
      if (tab && tab.page === 'web' && !tab.error) {
        tab.title = title;
        tabs = [...tabs];
      }
      return;
    }
    if (message.method === 'browser.favicon' && message.params) {
      const params = message.params as { tab?: number; icon?: string };
      const icon = params.icon ?? '';
      // Only small inline images; the kernel already filters, this is belt
      // and braces for anything reaching the interface.
      if (icon && (!icon.startsWith('data:image/') || icon.length > 200000)) return;
      const tab = tabs.find((entry) => entry.id === Number(params.tab));
      if (tab && tab.page === 'web' && tab.favicon !== icon) {
        tab.favicon = icon || undefined;
        tabs = [...tabs];
      }
      if (tab && icon) {
        const host = hostOfUrl(tab.url);
        if (host && faviconCache[host] !== icon) {
          faviconCache = { ...faviconCache, [host]: icon };
          void call('favicon.set', { host, icon }).catch(() => {});
        }
      }
      return;
    }
    if (message.method === 'browser.security' && message.params) {
      const params = message.params as {
        tab?: number;
        host?: string;
        subject?: string;
        valid?: boolean;
        notAfter?: number;
        https?: boolean;
      };
      const tab = tabs.find((entry) => entry.id === Number(params.tab));
      if (tab) {
        tab.security = {
          host: params.host ?? '',
          subject: params.subject ?? '',
          valid: params.valid ?? false,
          notAfter: params.notAfter ?? 0,
          https: params.https ?? true,
        };
        tabs = [...tabs];
      }
      return;
    }
    if (message.method === 'browser.reader' && message.params) {
      const params = message.params as { tab?: number; data?: string };
      if (Number(params.tab) !== activeId) return;
      const parsed = parseReaderData(params.data ?? '');
      if (!parsed) {
        toast(t('reader.unavailable'), 'error');
        return;
      }
      reader = parsed;
      readerTab = activeId;
      readerUrl = active.url;
      return;
    }
    if (message.method === 'browser.readerFailed') {
      toast(t('reader.unavailable'), 'error');
      return;
    }
    if (message.method === 'browser.miniWindowFailed' && message.params) {
      const params = message.params as { message?: string };
      toast(params.message || t('mini.failed'), 'error');
      return;
    }
    if (message.method === 'browser.watchChanged' && message.params) {
      const params = message.params as { title?: string; url?: string };
      toast(t('watch.changedToast', { title: params.title || params.url || '' }));
      return;
    }
    if (message.method === 'browser.download' && message.params) {
      const params = message.params as {
        id: string;
        tab?: number;
        state?: string;
        path?: string;
        filename?: string;
      };
      handleDownloadEvent(params as never);
      if (params.state === 'finished') {
        const path = params.path ?? '';
        const name = path.split('/').pop() || params.filename || '';
        const directory = path.includes('/') ? path.slice(0, path.lastIndexOf('/')) : '';
        const actions = [];
        if (path) actions.push({ label: t('downloads.openFile'), path, kind: 'file' as const });
        if (directory) {
          actions.push({ label: t('downloads.reveal'), path: directory, kind: 'folder' as const });
        }
        toast(t('downloads.saved', { name }), 'info', actions);
      }
      return;
    }
    if (message.method !== 'browser.loadFailed' || !message.params) return;
    const params = message.params as { tab?: number; url?: string; code?: number; message?: string };
    const tab = tabs.find((entry) => entry.id === Number(params.tab)) ?? tabs.find((entry) => entry.id === activeId);
    if (!tab) return;

    // A failure from an earlier navigation can arrive after the user moved on.
    // Ignoring it keeps the panel and the address bar in agreement.
    const failed = (params.url || '').replace(/\/$/, '');
    const current = (tab.url || '').replace(/\/$/, '');
    if (failed && current && failed !== current) return;

    tab.error = { url: params.url || tab.url, code: params.code ?? 0, message: params.message ?? '' };
    tab.title = tab.error.url.replace(/^https?:\/\//, '').split('/')[0] || tab.title;
    tabs = [...tabs];
  });

  async function enableDirect(url: string): Promise<void> {
    const host = siteOf(url);
    if (!host) return;
    try {
      await call('rules.add', { name: host, domains: [host], h3: 'prefer', ech: true, dns: '' });
      toast(t('error.directOn'));
    } catch (error: any) {
      // A rule may already exist for the host; retrying is still useful.
      toast(String(error?.message ?? error), 'error');
    }
    updateTab(activeId, { error: undefined });
    notify('ui.navigate', { url, tab: activeId });
    void applyStoredZoom(url, activeId);
  }

  async function toggleBookmark(): Promise<void> {    if (active.page !== 'web') return;
    try {
      const existing = await call<Array<{ id: number }>>('bookmarks.forUrl', { url: active.url });
      for (const entry of existing) await call('bookmarks.remove', { id: entry.id });
      if (existing.length === 0) {
        await call('bookmarks.add', { title: active.title, url: active.url, parent: 2 });
        toast(t('bookmarks.added'));
      } else {
        toast(t('bookmarks.removed'));
      }
      await loadBookmarks();
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    }
  }

  $effect(() => {
    if (addressFocused) return;
    if (active) addressValue = addressLabel(active);
  });

  // Used by the shell for automated checks and deep links.
  $effect(() => {
    const hooks = window as unknown as {
      __lioraOpenUrl?: (url: string) => void;
      __lioraNewTab?: (url: string) => void;
    };
    hooks.__lioraOpenUrl = (url: string) => navigate(url);
    hooks.__lioraNewTab = (url: string) => newTab(url);
  });

  // Tell the shell where each native kernel view goes. Split view needs two
  // rects; the single-pane case is the same message with one entry.
  $effect(() => {
    // Read the state that changes the geometry so the effect reruns.
    void activeId;
    void split;
    void splitRatio;
    void paletteOpen;
    void watchDialogOpen;
    void containerPickerOpen;
    void readerActive;
    void tabs;
    void bookmarks.length;
    void app.density;

    scheduleContentLayout();

    const onResize = (): void => scheduleContentLayout();
    window.addEventListener('resize', onResize);
    const observer = new ResizeObserver(() => scheduleContentLayout());
    if (contentEl) observer.observe(contentEl);
    return () => {
      window.removeEventListener('resize', onResize);
      observer.disconnect();
    };
  });

  $effect(() => {
    if (reader && !readerActive) {
      reader = null;
      readerSummary = false;
    }
  });

  $effect(() => {
    if (!split) return;
    if (activeId !== split.primary && activeId !== split.secondary) {
      split = null;
      return;
    }
    const primary = tabs.find((tab) => tab.id === split!.primary);
    const secondary = tabs.find((tab) => tab.id === split!.secondary);
    if (
      !primary ||
      !secondary ||
      primary.page !== 'web' ||
      secondary.page !== 'web' ||
      primary.error ||
      secondary.error
    ) {
      split = null;
    }
  });

  $effect(() => {
    const group = groups.find((item) => item.id === active?.groupId);
    if (group?.collapsed) group.collapsed = false;
  });

  // Switching tabs closes transient panels.
  $effect(() => {
    siteCardOpen = false;
    if (findOpen && findTabId !== activeId) closeFind();
  });

  // Used by the shell's menu shortcuts, which work regardless of which view
  // has keyboard focus. Dispatch lives in lib/commands.ts; the DOM-driven
  // test scenarios are only registered when the shell runs in test mode.
  $effect(() => {
    const hooks = window as unknown as {
      __lioraCommand?: (name: string) => void;
      __lioraTestMode?: boolean;
      __lioraCopyReport?: () => void;
      __lioraSaveReport?: () => void;
      __lioraRestartKernel?: () => void;
      __lioraSaveTimeline?: () => void;
      __lioraOpenUrl?: (url: string) => void;
    };
    hooks.__lioraCommand = (name: string) => {
      runCommand(name, {
        activeId,
        activeUrl: active?.url ?? '',
        groups,
        splitSecondary: split?.secondary ?? null,
        siteCardOpen,
        addressEl,
        newTab,
        closeTab,
        navigate,
        openSettings,
        openFind,
        runFind,
        closeFind,
        openFindWith: (text) => {
          findOpen = true;
          findTabId = activeId;
          findAutoAdvanced = false;
          findText = text;
          setTimeout(() => runFind(true, true), 50);
        },
        goBack,
        goForward,
        changeZoom: (delta) => void changeZoom(delta),
        openDownloads,
        openPalette,
        closePalette,
        requestReader,
        closeReader,
        toggleSplit,
        closeSplit,
        setSplitRatio: (ratio) => {
          splitRatio = ratio;
          scheduleContentLayout();
        },
        focusSplitSecondary: () => {
          if (split) activeId = split.secondary;
        },
        openWatchDialog,
        openContainerPicker,
        openMiniWindow,
        createGroup: () => void createGroup(),
        assignGroup,
        reopenClosedTab,
        toggleSiteCard: () => {
          siteCardOpen = !siteCardOpen;
        },
        copyReport: () => hooks.__lioraCopyReport?.(),
        saveReport: () => hooks.__lioraSaveReport?.(),
        restartKernel: () => hooks.__lioraRestartKernel?.(),
        saveTimeline: () => hooks.__lioraSaveTimeline?.(),
        openUrl: (url) => hooks.__lioraOpenUrl?.(url),
        setFullscreen: (fullscreen) => notify('ui.testFullscreen', { fullscreen }),
        notify,
        setSetting: (key, value) => void call('settings.set', { key, value }),
        // Read the flag at call time: the shell sets it after the interface
        // mounts, so a snapshot taken here would always be false.
        runTestCommand: (name) => (hooks.__lioraTestMode ? runTestScenario(name) : false),
      });
    };
  });

  // Remember the tab set so the next launch can restore it.
  $effect(() => {
    const snapshot = tabs.map((tab) => ({
      url: tab.url,
      title: tab.title,
      page: tab.page,
      group: tab.groupId ?? '',
      container: tab.containerId ?? '',
    }));
    const savedGroups = groups.map((group) => ({
      id: group.id,
      name: group.name,
      color: group.color,
      collapsed: group.collapsed,
    }));
    const selected = activeId;
    const savedContainers = containers.map((container) => ({
      id: container.id,
      name: container.name,
      color: container.color,
    }));
    // Read split state here so changes to the layout alone also trigger a save.
    const savedSplit = split ? { ...split, ratio: splitRatio } : null;
    if (!sessionReady) return;
    clearTimeout(saveTimer);
    saveTimer = setTimeout(() => {
      void call('session.save', {
        tabs: snapshot,
        groups: savedGroups,
        containers: savedContainers,
        split: savedSplit,
        active: selected,
      });
    }, 700);
    return () => clearTimeout(saveTimer);
  });

  function updateTab(id: number, patch: Partial<Tab>): void {
    const index = tabs.findIndex((tab) => tab.id === id);
    if (index >= 0) tabs[index] = { ...tabs[index], ...patch };
  }

  function hostOfUrl(url: string): string {
    try {
      return new URL(url).hostname;
    } catch {
      return '';
    }
  }

  function newTab(
    url = 'liora://newtab',
    containerId: string | null = active?.containerId ?? null,
  ): Tab {
    const tab: Tab = {
      id: tabSeq++,
      title: t('tab.new'),
      url,
      page: 'newtab',
      groupId: active?.groupId,
      containerId: containerId ?? undefined,
    };
    tabs = [...tabs, tab];
    activeId = tab.id;
    if (url !== 'liora://newtab') navigate(url);
    return tab;
  }

  function closeTab(id: number): void {
    const closing = tabs.find((tab) => tab.id === id);
    if (closing) rememberClosed(closing);
    if (split && (id === split.primary || id === split.secondary)) {
      const otherId = id === split.primary ? split.secondary : split.primary;
      const other = tabs.find((tab) => tab.id === otherId);
      split = null;
      if (activeId === id && other) activeId = other.id;
    }
    if (tabs.length === 1) {
      updateTab(id, {
        title: t('tab.new'),
        url: 'liora://newtab',
        page: 'newtab',
        groupId: undefined,
        containerId: undefined,
        favicon: undefined,
      });
      notify('ui.setPage', { page: 'newtab' });
      return;
    }
    const index = tabs.findIndex((tab) => tab.id === id);
    tabs = tabs.filter((tab) => tab.id !== id);
    if (activeId === id) activeId = tabs[Math.max(0, index - 1)].id;
    notify('ui.closeTab', { tab: id });
  }

  function looksLikeUrl(input: string): boolean {
    if (/^[a-z][a-z0-9+.-]*:\/\//i.test(input)) return true;
    if (input.startsWith('liora:')) return true;
    if (input.startsWith('localhost')) return true;
    return /^[^\s/]+\.[a-z]{2,}([/:?#].*)?$/i.test(input);
  }

  function resolveInput(input: string): string {
    const value = input.trim();
    if (!value) return 'liora://newtab';
    if (value.startsWith('liora://')) return value;
    if (looksLikeUrl(value)) return /^[a-z][a-z0-9+.-]*:/i.test(value) ? value : `https://${value}`;
    const template = searchTemplates[searchEngine] ?? searchTemplates.duckduckgo;
    return template + encodeURIComponent(value);
  }

  function internalTitle(page: string): string {
    if (page === 'settings') return t('nav.settings');
    if (page === 'bookmarks') return t('nav.bookmarks');
    if (page === 'history') return t('nav.history');
    if (page === 'diagnostics') return t('nav.diagnostics');
    if (page === 'downloads') return t('nav.downloads');
    if (page === 'watches') return t('nav.watches');
    if (page === 'containers') return t('containers.title');
    return t('tab.new');
  }

  function addressLabel(tab: Tab): string {
    return tab.page === 'web' ? tab.url : internalTitle(tab.page);
  }

  function navigate(raw: string): void {
    siteCardOpen = false;
    reader = null;
    const url = resolveInput(raw);
    const internal = internalPageFor(url);

    if (internal) {
      updateTab(activeId, { url, page: internal as Tab['page'], title: internalTitle(internal) });
      if (internal === 'settings') app.settingsSection = app.settingsSection || 'appearance';
      notify('ui.setPage', { page: internal, url });
      return;
    }

    updateTab(activeId, {
      url,
      page: 'web',
      title: url.replace(/^https?:\/\//, '').split('/')[0],
      error: undefined,
      favicon: faviconCache[hostOfUrl(url)],
    });
    const container = tabs.find((tab) => tab.id === activeId)?.containerId ?? '';
    notify('ui.navigate', { url, tab: activeId, container });
    void applyStoredZoom(url, activeId);
  }

  function openFind(): void {
    if (active.page !== 'web' || readerActive) return;
    findOpen = true;
    findTabId = activeId;
    findAutoAdvanced = false;
    setTimeout(() => findInputEl?.focus(), 0);
  }

  function runFind(forward: boolean, newQuery = false): void {
    if (!findOpen) return;
    const text = findText.trim();
    if (!text) {
      findMatches = 0;
      findActive = 0;
      notify('ui.stopFind', { tab: activeId });
      return;
    }
    notify('ui.find', { tab: activeId, text, forward, matchCase: findMatchCase, newQuery });
  }

  function closeFind(): void {
    if (!findOpen) return;
    findOpen = false;
    findMatches = 0;
    findActive = 0;
    findAutoAdvanced = false;
    notify('ui.stopFind', { tab: activeId });
  }

  function findLabel(): string {
    if (!findText.trim()) return '';
    if (findMatches <= 0) return t('find.none');
    if (findActive <= 0) return t('find.total', { count: findMatches });
    return `${findActive}/${findMatches}`;
  }

  function onFindKey(event: KeyboardEvent): void {
    if (event.key === 'Enter') {
      event.preventDefault();
      runFind(!event.shiftKey, true);
    } else if (event.key === 'Escape') {
      event.preventDefault();
      closeFind();
    }
  }

  function goBack(): void {
    if (active.page !== 'web' || !active.canBack) return;
    reader = null;
    notify('ui.back', { tab: activeId });
  }

  function goForward(): void {
    if (active.page !== 'web' || !active.canForward) return;
    reader = null;
    notify('ui.forward', { tab: activeId });
  }

  function submitAddress(event: Event): void {
    event.preventDefault();
    const form = event.target as HTMLFormElement;
    // Clicking into the friendly label of an internal page and pressing Enter
    // should keep the page, not search for its name.
    if (active.page !== 'web' && addressValue.trim() === internalTitle(active.page)) {
      form.querySelector('input')?.blur();
      return;
    }
    navigate(addressValue);
    form.querySelector('input')?.blur();
  }

  function groupColor(groupId?: string): string {
    return groups.find((group) => group.id === groupId)?.color ?? 'transparent';
  }

  function groupName(groupId?: string): string {
    return groups.find((group) => group.id === groupId)?.name ?? '';
  }

  function createGroup(tabId = activeId): string {
    const id = `group-${Date.now()}-${groups.length}`;
    const group: TabGroup = {
      id,
      name: t('groups.defaultName', { count: groups.length + 1 }),
      color: groupColors[groups.length % groupColors.length],
      collapsed: false,
    };
    groups = [...groups, group];
    const tab = tabs.find((item) => item.id === tabId);
    if (tab) tab.groupId = id;
    tabs = [...tabs];
    return id;
  }

  function assignGroup(tabId: number, groupId: string): void {
    const tab = tabs.find((item) => item.id === tabId);
    if (!tab) return;
    tab.groupId = groupId;
    tabs = [...tabs];
    const group = groups.find((item) => item.id === groupId);
    if (group) group.collapsed = false;
  }

  function ungroupTab(tabId: number): void {
    const tab = tabs.find((item) => item.id === tabId);
    if (!tab) return;
    tab.groupId = undefined;
    tabs = [...tabs];
  }

  function closeGroup(groupId: string): void {
    for (const tab of tabs) {
      if (tab.groupId === groupId) tab.groupId = undefined;
    }
    groups = groups.filter((group) => group.id !== groupId);
    tabs = [...tabs];
  }

  function cycleGroupColor(group: TabGroup): void {
    const index = groupColors.indexOf(group.color);
    group.color = groupColors[(index + 1) % groupColors.length];
  }

  function rememberClosed(tab: Tab): void {
    closedTabs = [
      ...closedTabs.slice(-19),
      { title: tab.title, url: tab.url, page: tab.page, groupId: tab.groupId },
    ];
  }

  function reopenClosedTab(): void {
    const entry = closedTabs[closedTabs.length - 1];
    if (!entry) return;
    closedTabs = closedTabs.slice(0, -1);
    const tab: Tab = {
      id: tabSeq++,
      title: entry.title || internalTitle(entry.page),
      url: entry.url,
      page: entry.page,
      groupId: entry.groupId && groups.some((group) => group.id === entry.groupId)
        ? entry.groupId
        : undefined,
    };
    tabs = [...tabs, tab];
    activeId = tab.id;
    if (tab.page === 'web') {
      notify('ui.navigate', { url: tab.url, tab: tab.id });
      void applyStoredZoom(tab.url, tab.id);
    }
  }

  function closeSplit(): void {
    split = null;
    splitDragging = false;
  }

  function toggleSplit(): void {
    if (split) {
      closeSplit();
      return;
    }
    if (active.page !== 'web' || active.error) {
      toast(t('split.needWeb'), 'error');
      return;
    }
    const index = tabs.findIndex((tab) => tab.id === activeId);
    let secondary: Tab | undefined;
    for (let step = 1; step < tabs.length; step += 1) {
      const candidate = tabs[(index - step + tabs.length) % tabs.length];
      if (candidate.page === 'web' && !candidate.error && candidate.id !== activeId) {
        secondary = candidate;
        break;
      }
    }
    if (!secondary) {
      toast(t('split.needTwo'), 'error');
      return;
    }
    split = { primary: activeId, secondary: secondary.id };
    splitRatio = 0.5;
  }

  function focusPane(id: number): void {
    if (split && (id === split.primary || id === split.secondary)) activeId = id;
  }

  function startSplitDrag(event: PointerEvent): void {
    event.preventDefault();
    if (!splitEl) return;
    splitDragging = true;
    const onMove = (moveEvent: PointerEvent): void => {
      if (!splitEl) return;
      const rect = splitEl.getBoundingClientRect();
      if (rect.width <= 0) return;
      splitRatio = Math.min(0.8, Math.max(0.2, (moveEvent.clientX - rect.left) / rect.width));
    };
    const onUp = (): void => {
      splitDragging = false;
      window.removeEventListener('pointermove', onMove);
      window.removeEventListener('pointerup', onUp);
    };
    window.addEventListener('pointermove', onMove);
    window.addEventListener('pointerup', onUp);
  }

  function scheduleContentLayout(): void {
    cancelAnimationFrame(layoutFrame);
    layoutFrame = requestAnimationFrame(() => sendContentLayout());
  }

  function sendContentLayout(): void {
    const rects: Array<{
      tab: number;
      x: number;
      y: number;
      width: number;
      height: number;
      visible: boolean;
    }> = [];
    const add = (tabId: number, element: HTMLElement | null): void => {
      if (!element || paletteOpen || watchDialogOpen || containerPickerOpen) return;
      const tab = tabs.find((item) => item.id === tabId);
      if (!tab || tab.page !== 'web' || tab.error) return;
      if (readerActive && tabId === activeId) return;
      const rect = element.getBoundingClientRect();
      if (rect.width < 1 || rect.height < 1) return;
      rects.push({
        tab: tabId,
        x: Math.round(rect.x),
        y: Math.round(rect.y),
        width: Math.round(rect.width),
        height: Math.round(rect.height),
        visible: true,
      });
    };

    if (split) {
      add(split.primary, document.querySelector<HTMLElement>(`[data-split-tab="${split.primary}"]`));
      add(split.secondary, document.querySelector<HTMLElement>(`[data-split-tab="${split.secondary}"]`));
    } else {
      add(activeId, contentEl);
    }
    notify('ui.contentLayout', { rects, focused: activeId });
  }

  function closeReader(): void {
    reader = null;
    readerSummary = false;
  }

  function requestReader(summary = false): void {
    if (split) closeSplit();
    if (active.page !== 'web' || active.error) {
      toast(t('reader.onlyWeb'), 'error');
      return;
    }
    readerSummary = summary;
    toast(t('reader.extracting'));
    notify('ui.reader', { tab: activeId });
  }

  function containerColor(id?: string): string {
    if (!id) return 'transparent';
    return containers.find((container) => container.id === id)?.color ?? 'transparent';
  }

  function containerName(id?: string): string {
    if (!id) return t('containers.default');
    return containers.find((container) => container.id === id)?.name ?? t('containers.default');
  }

  function createContainerId(): string {
    if (typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function') {
      return crypto.randomUUID();
    }
    const bytes = crypto.getRandomValues(new Uint8Array(16));
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    const hex = [...bytes].map((value) => value.toString(16).padStart(2, '0')).join('');
    return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${hex.slice(16, 20)}-${hex.slice(20)}`;
  }

  function createContainer(name: string, color?: string): Container {
    const container: Container = {
      id: createContainerId(),
      name: name.trim() || t('containers.untitled'),
      color: color || containerColors[containers.length % containerColors.length],
    };
    containers = [...containers, container];
    return container;
  }

  function renameContainer(id: string, name: string): void {
    const container = containers.find((item) => item.id === id);
    if (container) container.name = name.trim() || container.name;
  }

  function recolorContainer(id: string, color: string): void {
    const container = containers.find((item) => item.id === id);
    if (container) container.color = color;
  }

  function deleteContainer(id: string): void {
    const tabsInContainer = tabs
      .filter((tab) => tab.containerId === id)
      .map((tab) => tab.id);
    for (const tabId of tabsInContainer) closeTab(tabId);
    containers = containers.filter((container) => container.id !== id);
  }

  function openContainerPicker(): void {
    if (active.page !== 'web' || active.error) {
      toast(t('containers.onlyWeb'), 'error');
      return;
    }
    containerPickerOpen = true;
  }

  function openInContainer(containerId?: string): void {
    containerPickerOpen = false;
    const url = active.url;
    newTab(url, containerId ?? null);
  }

  function openMiniWindow(): void {
    if (active.page !== 'web' || active.error) {
      toast(t('mini.onlyWeb'), 'error');
      return;
    }
    notify('ui.miniWindow', {
      url: active.url,
      title: active.title || active.url,
      container: active.containerId ?? '',
    });
    toast(t('mini.opened'));
  }

  function openWatchDialog(): void {
    if (active.page !== 'web' || active.error) {
      toast(t('watch.onlyWeb'), 'error');
      return;
    }
    watchDialogUrl = active.url;
    watchDialogTitle = active.title || active.url;
    watchDialogOpen = true;
  }

  function closeWatchDialog(): void {
    watchDialogOpen = false;
  }

  function openPalette(): void {
    siteCardOpen = false;
    paletteOpen = true;
  }

  function closePalette(): void {
    paletteOpen = false;
  }

  function cleanShareUrl(): string {
    try {
      const url = new URL(active.url);
      const drop = new Set([
        'fbclid',
        'gclid',
        'dclid',
        'msclkid',
        'mc_cid',
        'mc_eid',
        'igshid',
        'ref',
        'ref_src',
        'si',
      ]);
      for (const key of [...url.searchParams.keys()]) {
        if (key.toLowerCase().startsWith('utm_') || drop.has(key.toLowerCase())) {
          url.searchParams.delete(key);
        }
      }
      return url.toString();
    } catch {
      return active.url;
    }
  }

  async function copyCleanLink(): Promise<void> {
    const text = cleanShareUrl();
    try {
      await navigator.clipboard.writeText(text);
      toast(t('palette.cleanCopied'));
    } catch {
      const area = document.createElement('textarea');
      area.value = text;
      area.style.position = 'fixed';
      area.style.opacity = '0';
      document.body.appendChild(area);
      area.select();
      const copied = document.execCommand('copy');
      area.remove();
      toast(copied ? t('palette.cleanCopied') : t('palette.cleanCopyFailed'), copied ? 'info' : 'error');
    }
  }

  function runPaletteCommand(id: string): void {
    closePalette();
    if (id === 'newtab') newTab();
    else if (id === 'settings') openSettings();
    else if (id === 'downloads') openDownloads();
    else if (id === 'bookmarks') navigate('liora://bookmarks');
    else if (id === 'history') navigate('liora://history');
    else if (id === 'dns') openSettings('dns');
    else if (id === 'group') createGroup();
    else if (id === 'ungroup') ungroupTab(activeId);
    else if (id === 'find') openFind();
    else if (id === 'reload') navigate(active.url);
    else if (id === 'close') closeTab(activeId);
    else if (id === 'reopen') reopenClosedTab();
    else if (id === 'copylink') void copyCleanLink();
    else if (id === 'reader') requestReader();
    else if (id === 'summary') requestReader(true);
    else if (id === 'mini') openMiniWindow();
    else if (id === 'split') toggleSplit();
    else if (id === 'watches') navigate('liora://watches');
    else if (id === 'watch') openWatchDialog();
    else if (id === 'containers') navigate('liora://containers');
    else if (id === 'opencontainer') openContainerPicker();
  }

  function openPaletteTab(id: number): void {
    closePalette();
    activeId = id;
  }

  function paletteQuery(query: string): void {
    closePalette();
    navigate(query);
  }

  function openDownloads(): void {
    const existing = tabs.find((tab) => tab.page === 'downloads');
    if (existing) {
      activeId = existing.id;
      return;
    }
    newTab('liora://downloads');
  }

  function closeSettings(): void {
    if (tabs.length > 1) closeTab(activeId);
    else navigate('liora://newtab');
  }

  function openSettings(section?: string): void {
    app.settingsSection = section ?? 'appearance';
    updateTab(activeId, { url: 'liora://settings', page: 'settings', title: t('settings.title') });
    notify('ui.setPage', { page: 'settings' });
  }

  function onAddressFocus(): void {
    addressFocused = true;
    requestAnimationFrame(() => addressEl?.select());
  }

  function onAddressBlur(): void {
    addressFocused = false;
    suggestions = [];
    suggestSelected = -1;
    addressValue = addressLabel(active);
  }

  function onAddressInput(value: string): void {
    addressValue = value;
    suggestSelected = -1;
  }

  // Suggestions come only from local history and bookmarks — no remote
  // service, matching the start page's privacy stance.
  $effect(() => {
    const text = addressValue.trim();
    if (!addressFocused || !text || !suggestEnabled || active.page !== 'web') {
      suggestions = [];
      return;
    }
    let current = true;
    const timer = setTimeout(() => {
      Promise.allSettled([
        call<Array<{ url: string; title: string }>>('history.suggest', { input: text, limit: 6 }),
        call<Array<{ url: string; title: string }>>('bookmarks.search', { query: text, limit: 4 }),
      ]).then(([history, bookmarks]) => {
        if (!current) return;
        const engineName = searchEngineNames[searchEngine] ?? searchEngine;
        suggestions = buildSuggestions(
          text,
          history.status === 'fulfilled' && Array.isArray(history.value) ? history.value : [],
          bookmarks.status === 'fulfilled' && Array.isArray(bookmarks.value)
            ? bookmarks.value.filter((entry) => entry.url)
            : [],
          engineName,
          looksLikeUrl,
        );
        suggestSelected = -1;
      });
    }, 80);
    return () => {
      current = false;
      clearTimeout(timer);
    };
  });

  function pickSuggestion(item: Suggestion): void {
    suggestions = [];
    suggestSelected = -1;
    navigate(item.value);
    addressEl?.blur();
  }

  function onAddressKeydown(event: KeyboardEvent): void {
    if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
      if (suggestions.length === 0) return;
      event.preventDefault();
      suggestSelected = moveSelection(suggestSelected, event.key === 'ArrowDown' ? 1 : -1, suggestions.length);
      return;
    }
    if (event.key === 'Enter' && suggestSelected >= 0 && suggestions[suggestSelected]) {
      event.preventDefault();
      pickSuggestion(suggestions[suggestSelected]);
      return;
    }
    if (event.key === 'Escape' && suggestions.length > 0) {
      event.preventDefault();
      event.stopPropagation();
      suggestions = [];
      suggestSelected = -1;
    }
  }

  // Zoom is remembered per site, matching the settings model.
  function siteOf(url: string): string {
    try {
      return new URL(url).hostname;
    } catch {
      return '';
    }
  }

  // Page zoom is stored per site; re-apply it after navigation because a
  // kernel view starts at 100% for each new document.
  async function applyStoredZoom(url: string, tab: number): Promise<void> {
    const site = siteOf(url);
    if (!site) return;
    try {
      const current = Number(
        (await call<Record<string, number>>('settings.get', { key: 'content.zoom', site }))[
          'content.zoom'
        ] ?? 100,
      );
      if (Number.isFinite(current) && current !== 100) {
        notify('ui.setZoom', { tab, percent: current });
      }
    } catch {
      /* zoom is optional */
    }
  }

  async function changeZoom(delta: number): Promise<void> {
    if (active.page !== 'web') return;
    const site = siteOf(active.url);
    const current = Number(
      (await call<Record<string, number>>('settings.get', { key: 'content.zoom', site }))['content.zoom'] ?? 100,
    );
    const next = delta === 0 ? 100 : Math.min(500, Math.max(25, current + delta));
    await call('settings.set', { key: 'content.zoom', value: next, site });
    notify('ui.setZoom', { tab: activeId, percent: next });
    toast(`${next}%`);
  }

  // The shortcuts a browser must have before anything else.
  function onKeydown(event: KeyboardEvent): void {
    const mod = event.metaKey || event.ctrlKey;
    if (!mod) return;
    const key = event.key.toLowerCase();

    if (key === 'k' || (key === 'a' && event.shiftKey)) {
      event.preventDefault();
      paletteOpen = !paletteOpen;
      return;
    }
    if (key === 't' && event.shiftKey) {
      event.preventDefault();
      reopenClosedTab();
      return;
    }
    if (paletteOpen || watchDialogOpen || containerPickerOpen) return;

    if (key === 'f') {
      event.preventDefault();
      openFind();
      return;
    }
    if (key === 'l') {
      event.preventDefault();
      addressEl?.focus();
      addressEl?.select();
      return;
    }
    if (key === 't') {
      event.preventDefault();
      newTab();
      return;
    }
    if (key === 'w') {
      event.preventDefault();
      closeTab(activeId);
      return;
    }
    if (event.key === '\\') {
      event.preventDefault();
      toggleSplit();
      return;
    }
    if (key === 'r' && event.shiftKey) {
      // Hard reload, the universal convention; reader mode moved to ⌘⌥R.
      event.preventDefault();
      if (active.page === 'web') notify('ui.reload', { tab: activeId, bypassCache: true });
      return;
    }
    if (key === 'r' && event.altKey) {
      event.preventDefault();
      requestReader();
      return;
    }
    if (key === 'r') {
      event.preventDefault();
      navigate(active.url);
      return;
    }
    if (event.key === '[') {
      event.preventDefault();
      goBack();
      return;
    }
    if (event.key === ']') {
      event.preventDefault();
      goForward();
      return;
    }
    if (key === ',') {
      event.preventDefault();
      openSettings();
      return;
    }
    if (key === 'd') {
      event.preventDefault();
      void toggleBookmark();
      return;
    }
    if (event.key === '=' || event.key === '+') {
      event.preventDefault();
      void changeZoom(10);
      return;
    }
    if (event.key === '-') {
      event.preventDefault();
      void changeZoom(-10);
      return;
    }
    if (event.key === '0') {
      event.preventDefault();
      void changeZoom(0);
      return;
    }
    if (key === 'tab') {
      event.preventDefault();
      const index = tabs.findIndex((tab) => tab.id === activeId);
      const next = (index + (event.shiftKey ? -1 : 1) + tabs.length) % tabs.length;
      activeId = tabs[next].id;
      return;
    }
    if (/^[1-9]$/.test(key)) {
      // ⌘9 jumps to the last tab, not the ninth — browser convention.
      const index = key === '9' ? tabs.length - 1 : Number(key) - 1;
      if (index >= 0 && index < tabs.length) {
        event.preventDefault();
        activeId = tabs[index].id;
      }
    }
  }

</script>

<svelte:window onkeydown={onKeydown} />

{#snippet tabstrip()}
  <TabStrip
    {tabs}
    {activeId}
    {groupColor}
    {containerColor}
    {containerName}
    onselect={(id) => (activeId = id)}
    onclose={closeTab}
    onnew={() => newTab()}
  />
{/snippet}

{#if !app.ready}
  <div class="boot">
    <div class="boot__mark">L</div>
  </div>
{:else}
  <div class="app">
    {#if app.transport === 'mock'}
      <!-- Never let a preview pass for the real browser: without the shell the
           data below comes from an in-memory demo backend. -->
      <div class="mockbanner" role="status">
        <Icon name="info" size={13} />
        <span>{t('mock.banner')}</span>
      </div>
    {/if}
    {#if app.platform === 'macos'}
      <div class="titlebar titlebar--mac" class:titlebar--tabs={showTabs && !verticalTabs}>
        {#if verticalTabs}
          <span class="titlebar__title">{t('app.name')}</span>
        {:else if showTabs}
          {@render tabstrip()}
        {:else}
          <span class="titlebar__title">{t('app.name')}</span>
        {/if}
      </div>
    {:else if showTabs && !verticalTabs}
      {@render tabstrip()}
    {/if}

    <div class="workbench">
      {#if verticalTabs}
        <TabRail
          {tabs}
          {activeId}
          {groups}
          {groupedTabs}
          {ungroupedTabs}
          {groupColor}
          {groupName}
          {containerColor}
          {containerName}
          onselect={(id) => (activeId = id)}
          onclose={closeTab}
          onnew={() => newTab()}
          oncreategroup={() => createGroup()}
          onassigngroup={assignGroup}
          onungroup={ungroupTab}
          onclosegroup={closeGroup}
          oncyclecolor={cycleGroupColor}
          ontogglecollapse={(group) => (group.collapsed = !group.collapsed)}
          onrenamegroup={(groupId, name) => {
            const group = groups.find((item) => item.id === groupId);
            if (group) group.name = name;
          }}
        />
      {/if}

      <div class="maincolumn">
    <Toolbar
      {active}
      {addressValue}
      {addressFocused}
      {addressEl}
      {badges}
      {activeContainer}
      {showHomeButton}
      {homePage}
      showNetworkPending={networkPending() || applyState !== 'idle'}
      {applyState}
      {applyTitle}
      {appliedProxyPort}
      splitActive={Boolean(split)}
      showSplitButton={Boolean(split) || webTabCount > 1}
      {readerActive}
      {activeBookmarked}
      downloadCount={activeDownloadCount()}
      downloadsActive={active.page === 'downloads'}
      {suggestions}
      {suggestSelected}
      onsuggestpick={pickSuggestion}
      onsuggesthover={(index) => (suggestSelected = index)}
      onaddresskeydown={onAddressKeydown}
      onback={goBack}
      onforward={goForward}
      onreload={() => navigate(active.url)}
      onhome={() => navigate(homePage)}
      onaddressinput={onAddressInput}
      onaddressfocus={onAddressFocus}
      onaddressblur={onAddressBlur}
      onsubmit={submitAddress}
      onsitecard={() => (siteCardOpen = !siteCardOpen)}
      oncontainer={openContainerPicker}
      onapplynetwork={applyNetworkChanges}
      onpalette={openPalette}
      onsplit={toggleSplit}
      onreader={() => requestReader(false)}
      onbookmark={toggleBookmark}
      ondownloads={openDownloads}
      onsettings={() => openSettings()}
    />

    {#if siteCardOpen && active.page === 'web'}
      <SiteCard
        url={active.url}
        tab={activeId}
        security={active.security}
        onclose={() => (siteCardOpen = false)}
        onopenDns={() => {
          siteCardOpen = false;
          openSettings('dns');
        }}
      />
    {/if}

    {#if bookmarks.length > 0}
      <div class="bookmarksbar">
        <span class="bookmarksbar__label">
          <Icon name="star" size={11} />
          {t('bookmarks.bar')}
        </span>
        {#each bookmarks as entry (entry.id)}
          <button class="bookmark-chip" title={entry.url} onclick={() => navigate(entry.url)}>
            {entry.title}
          </button>
        {/each}
      </div>
    {/if}

    {#if findOpen && active.page === 'web'}
      <div class="findbar">
        <input
          class="input input--mono findbar__input"
          bind:this={findInputEl}
          bind:value={findText}
          oninput={() => {
            findAutoAdvanced = false;
            runFind(true, true);
          }}
          onkeydown={onFindKey}
          placeholder={t('find.placeholder')}
          spellcheck="false"
        />
        <span class="findbar__count" class:findbar__count--empty={findMatches === 0}>
          {findLabel()}
        </span>
        <button
          class="icon-button findbar__case"
          class:icon-button--active={findMatchCase}
          title={t('find.matchCase')}
          aria-label={t('find.matchCase')}
          aria-pressed={findMatchCase}
          onclick={() => {
            findMatchCase = !findMatchCase;
            findAutoAdvanced = false;
            runFind(true, true);
          }}
        >
          Aa
        </button>
        <button class="icon-button" title={t('find.previous')} aria-label={t('find.previous')} onclick={() => runFind(false)}>
          <Icon name="chevron-up" size={14} />
        </button>
        <button class="icon-button" title={t('find.next')} aria-label={t('find.next')} onclick={() => runFind(true)}>
          <Icon name="chevron-down" size={14} />
        </button>
        <button class="icon-button" title={t('find.close')} aria-label={t('find.close')} onclick={closeFind}>
          <Icon name="close" size={13} />
        </button>
      </div>
    {/if}

    <div class="content" class:content--native={!split && active.page === 'web'} bind:this={contentEl}>
      {#if split}
        <div
          class="split"
          class:split--dragging={splitDragging}
          bind:this={splitEl}
          style:grid-template-columns={`minmax(0, ${splitRatio}fr) 6px minmax(0, ${1 - splitRatio}fr)`}
        >
          {#if splitPrimary}
            <section class="split__pane" class:split__pane--active={activeId === split.primary}>
              <div
                class="split__header"
                role="button"
                tabindex="0"
                onclick={() => focusPane(split!.primary)}
                onkeydown={(event) => {
                  if (event.key === 'Enter' || event.key === ' ') {
                    event.preventDefault();
                    focusPane(split!.primary);
                  }
                }}
              >
                <span class="split__favicon">
                  {#if splitPrimary.favicon}
                    <img src={splitPrimary.favicon} alt="" />
                  {:else}
                    <Icon name="globe" size={10} />
                  {/if}
                </span>
                <span class="split__title">{splitPrimary.title || splitPrimary.url}</span>
                {#if splitPrimary.containerId}
                  <span
                    class="split__container"
                    style:background={containerColor(splitPrimary.containerId)}
                    title={containerName(splitPrimary.containerId)}
                  ></span>
                {/if}
                {#if splitPrimary.groupId}
                  <span
                    class="split__group"
                    style:background={groupColor(splitPrimary.groupId)}
                    title={groupName(splitPrimary.groupId)}
                  ></span>
                {/if}
                <button
                  class="split__close"
                  title={t('tab.close')}
                  aria-label={t('tab.close')}
                  onclick={(event) => {
                    event.stopPropagation();
                    closeTab(split!.primary);
                  }}
                >
                  <Icon name="close" size={9} />
                </button>
              </div>
              <div class="split__surface" data-split-tab={split.primary}></div>
            </section>
          {/if}

          <div
            class="split__divider"
            class:split__divider--dragging={splitDragging}
            role="separator"
            aria-orientation="vertical"
            onpointerdown={startSplitDrag}
          ></div>

          {#if splitSecondary}
            <section class="split__pane" class:split__pane--active={activeId === split.secondary}>
              <div
                class="split__header"
                role="button"
                tabindex="0"
                onclick={() => focusPane(split!.secondary)}
                onkeydown={(event) => {
                  if (event.key === 'Enter' || event.key === ' ') {
                    event.preventDefault();
                    focusPane(split!.secondary);
                  }
                }}
              >
                <span class="split__favicon">
                  {#if splitSecondary.favicon}
                    <img src={splitSecondary.favicon} alt="" />
                  {:else}
                    <Icon name="globe" size={10} />
                  {/if}
                </span>
                <span class="split__title">{splitSecondary.title || splitSecondary.url}</span>
                {#if splitSecondary.containerId}
                  <span
                    class="split__container"
                    style:background={containerColor(splitSecondary.containerId)}
                    title={containerName(splitSecondary.containerId)}
                  ></span>
                {/if}
                {#if splitSecondary.groupId}
                  <span
                    class="split__group"
                    style:background={groupColor(splitSecondary.groupId)}
                    title={groupName(splitSecondary.groupId)}
                  ></span>
                {/if}
                <button
                  class="split__close"
                  title={t('tab.close')}
                  aria-label={t('tab.close')}
                  onclick={(event) => {
                    event.stopPropagation();
                    closeTab(split!.secondary);
                  }}
                >
                  <Icon name="close" size={9} />
                </button>
              </div>
              <div class="split__surface" data-split-tab={split.secondary}></div>
            </section>
          {/if}
        </div>
      {:else}
      {#if active.page === 'newtab'}
        <NewTab
          favicons={faviconCache}
          onnavigate={(url) => navigate(url)}
          onopen={(page) => navigate(`liora://${page}`)}
          onpanel={(section) => openSettings(section)}
        />
      {:else if active.page === 'settings'}
        <Settings onclose={closeSettings} />
      {:else if active.page === 'diagnostics'}
        <DiagnosticsPage />
      {:else if active.page === 'downloads'}
        <DownloadsPage />
      {:else if active.page === 'watches'}
        <Watches onnavigate={(url) => navigate(url)} />
      {:else if active.page === 'containers'}
        <Containers
          {containers}
          defaultCount={tabs.filter((tab) => !tab.containerId).length}
          counts={containerTabCounts}
          oncreate={createContainer}
          onrename={renameContainer}
          onrecolor={recolorContainer}
          ondelete={deleteContainer}
          onopen={(id) => newTab('liora://newtab', id ?? null)}
        />
      {:else if active.page === 'bookmarks' || active.page === 'history'}
        <Library
          kind={active.page}
          onnavigate={(url) => navigate(url)}
          onopen={(page) => navigate(`liora://${page}`)}
        />
      {:else if active.page === 'web'}
        {#if readerActive && reader}
          <ReaderView data={reader} onclose={closeReader} startSummary={readerSummary} />
        {:else if active.error}
          <ErrorPanel
            url={active.error.url}
            message={active.error.message}
            code={active.error.code}
            security={active.security}
            onretry={() => {
              const target = active.error!.url || active.url;
              updateTab(activeId, { error: undefined });
              notify('ui.navigate', { url: target, tab: activeId });
            }}
            ondirect={() => enableDirect(active.error!.url || active.url)}
            onsettings={() => openSettings('dns')}
          />
        {:else if app.transport === 'mock'}
          <div class="webplaceholder">
            <div class="webplaceholder__bar">
              <Icon name="globe" size={14} />
              <span class="mono">{active.url}</span>
            </div>
            <div class="empty">
              页面由原生内核渲染在这里。<br />
              In the real app the kernel view is placed at this rectangle.
            </div>
          </div>
        {/if}
      {/if}
      {/if}
    </div>
      </div>
    </div>

    {#if containerPickerOpen}
      <ContainerPicker
        {containers}
        url={active.url}
        onclose={() => (containerPickerOpen = false)}
        onopen={openInContainer}
        oncreate={createContainer}
      />
    {/if}

    {#if watchDialogOpen}
      <WatchDialog
        url={watchDialogUrl}
        title={watchDialogTitle}
        onclose={closeWatchDialog}
      />
    {/if}

    {#if paletteOpen}
      <CommandPalette
        {tabs}
        {activeId}
        commands={paletteCommands}
        onclose={closePalette}
        onopentab={openPaletteTab}
        oncommand={runPaletteCommand}
        onquery={paletteQuery}
      />
    {/if}

    <div class="toasts" aria-live="polite">
      {#each app.toasts as item (item.id)}
        <div class="toast" class:toast--error={item.kind === 'error'}>
          <span>{item.text}</span>
          {#each item.actions ?? [] as action (action.kind + action.path)}
            <button
              class="toast__action"
              data-kind={action.kind}
              onclick={() => notify('ui.openPath', { path: action.path })}
            >
              {action.label}
            </button>
          {/each}
        </div>
      {/each}
    </div>
  </div>
{/if}

<style>
  .boot {
    height: 100%;
    display: grid;
    place-items: center;
    background: var(--bg);
    animation: fade 240ms var(--ease);
  }

  .boot__mark {
    width: 52px;
    height: 52px;
    border-radius: var(--radius-lg);
    display: grid;
    place-items: center;
    background: linear-gradient(145deg, var(--accent), var(--accent-highlight));
    color: #fff;
    font-size: var(--text-2xl);
    font-weight: var(--weight-bold);
    letter-spacing: -0.02em;
    box-shadow: 0 12px 34px var(--accent-soft);
  }

  @keyframes fade {
    from {
      opacity: 0;
    }
    to {
      opacity: 1;
    }
  }

  .webplaceholder {
    height: 100%;
    display: flex;
    flex-direction: column;
    background: var(--bg-elevated);
  }

  .webplaceholder__bar {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 10px 14px;
    border-bottom: 1px solid var(--border);
    color: var(--text-muted);
  }
</style>
