<script lang="ts">
  import Icon from './lib/Icon.svelte';
  import NewTab from './lib/NewTab.svelte';
  import Settings from './lib/Settings.svelte';
  import Library from './lib/Library.svelte';
  import ErrorPanel from './lib/ErrorPanel.svelte';
  import SiteCard from './lib/SiteCard.svelte';
  import DiagnosticsPage from './lib/DiagnosticsPage.svelte';
  import DownloadsPanel from './lib/DownloadsPanel.svelte';
  import DownloadPage from './lib/DownloadPage.svelte';
  import {
    activeDownloadCount,
    downloads,
    handleDownloadEvent,
    seedDownloadHistory,
    type DownloadItem,
    type DownloadRecord,
  } from './lib/downloads.svelte';
  import { app, boot, internalPageFor, openPage, toast } from './lib/state.svelte';
  import { networkPending, refreshNetworkStatus } from './lib/network-status.svelte';
  import { call, describeSettings, notify, onNotification } from './lib/bridge';
  import { t } from './lib/i18n.svelte';

  interface Tab {
    id: number;
    title: string;
    url: string;
    page: 'newtab' | 'settings' | 'bookmarks' | 'history' | 'diagnostics' | 'web';
    error?: { url: string; code: number; message: string };
    download?: DownloadItem;
    security?: { host: string; subject: string; valid: boolean; notAfter: number; https: boolean };
  }

  const firstTab: Tab = { id: 1, title: t('tab.new'), url: 'liora://newtab', page: 'newtab' };
  let tabSeq = 2;
  let tabs = $state<Tab[]>([firstTab]);
  let activeId = $state(firstTab.id);
  let addressValue = $state('');
  let addressFocused = $state(false);
  let contentEl = $state<HTMLDivElement | null>(null);
  let addressEl = $state<HTMLInputElement | null>(null);
  let siteCardOpen = $state(false);

  const active = $derived(tabs.find((tab) => tab.id === activeId) ?? tabs[0]);
  const showTabs = $derived(app.tabsPosition !== 'hidden');

  const searchTemplates: Record<string, string> = {
    duckduckgo: 'https://duckduckgo.com/?q=',
    google: 'https://www.google.com/search?q=',
    bing: 'https://www.bing.com/search?q=',
    brave: 'https://search.brave.com/search?q=',
    startpage: 'https://www.startpage.com/do/search?q=',
    baidu: 'https://www.baidu.com/s?wd=',
    sogou: 'https://www.sogou.com/web?query=',
  };
  let searchEngine = $state('duckduckgo');
  let bookmarks = $state<Array<{ id: number; title: string; url: string }>>([]);
  let sessionReady = $state(false);
  let saveTimer: ReturnType<typeof setTimeout> | undefined;

  boot()
    .then(async () => {
      const description = await describeSettings();
      const engine = description.items.find((item) => item.key === 'search.engine');
      if (engine && typeof engine.value === 'string') searchEngine = engine.value;

      const restore = description.items.find((item) => item.key === 'startup.restore');
      if (restore?.value !== false) await restoreSession();
      sessionReady = true;
      await loadBookmarks();
      await loadDownloadHistory();
      void refreshNetworkStatus();
    })
    .catch((error) => toast(String(error?.message ?? error), 'error'));

  function pageForUrl(url: string): Tab['page'] {
    const internal = internalPageFor(url);
    return (internal ?? 'web') as Tab['page'];
  }

  async function restoreSession(): Promise<void> {
    try {
      const session = await call<{
        tabs?: Array<{ url: string; title: string; page: string }>;
        active?: number;
      }>('session.load');
      if (!session.tabs?.length) return;
      // A tab that turned into a download is transient; restoring it would
      // download the file again.
      const restorable = session.tabs.filter((item) => item.page !== 'download');
      if (restorable.length === 0) return;
      const restored: Tab[] = restorable.map((item, index) => ({
        id: index + 1,
        title: item.title || item.url,
        url: item.url,
        page: pageForUrl(item.url),
      }));
      tabs = restored;
      tabSeq = restored.length + 1;
      activeId = restored.some((tab) => tab.id === session.active) ? session.active! : restored[0].id;
      // Load every page tab; the shell keeps background views hidden.
      for (const tab of restored) {
        if (tab.page === 'web') notify('ui.navigate', { url: tab.url, tab: tab.id });
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

  async function loadBookmarks(): Promise<void> {    try {
      const tree = await call<Record<string, { children?: Array<{ id: number; type: string; title: string; url?: string }> }>>(
        'bookmarks.tree',
      );
      bookmarks = (tree.bar?.children ?? [])
        .filter((node) => node.type === 'url' && node.url)
        .slice(0, 12)
        .map((node) => ({ id: node.id, title: node.title || node.url!, url: node.url! }));
    } catch {
      bookmarks = [];
    }
  }

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
      const name = (params.path ?? '').split('/').pop() ?? '';
      toast(t('timeline.saved', { name }));
      return;
    }
    if (message.method === 'browser.timelineFailed' && message.params) {
      const params = message.params as { message?: string };
      toast(params.message || t('timeline.saveFailed'), 'error');
      return;
    }
    if (message.method === 'browser.diagnosticSaved' && message.params) {
      const params = message.params as { path?: string };
      const name = (params.path ?? '').split('/').pop() ?? '';
      toast(t('error.saved', { name }));
      return;
    }
    if (message.method === 'browser.diagnosticFailed' && message.params) {
      const params = message.params as { message?: string };
      toast(params.message || t('error.saveFailed'), 'error');
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
    if (message.method === 'browser.download' && message.params) {
      const params = message.params as { id: string; tab?: number };
      handleDownloadEvent(params as never);
      const tab = tabs.find((entry) => entry.id === Number(params.tab));
      const item = downloads.items.find((entry) => entry.id === params.id);
      if (tab && item) {
        tab.download = { ...item };
        tab.title = item.filename;
        tabs = [...tabs];
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
    if (active) addressValue = active.page === 'web' ? active.url : `liora://${active.page}`;
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

  // Tell the shell where the kernel view goes. It is repositioned on resize.
  $effect(() => {
    if (!contentEl) return;
    const observer = new ResizeObserver(() => {
      const rect = contentEl!.getBoundingClientRect();
      notify('ui.contentRect', {
        x: Math.round(rect.x),
        y: Math.round(rect.y),
        width: Math.round(rect.width),
        height: Math.round(rect.height),
        visible: active?.page === 'web' && !active?.error && !active?.download,
        tab: active?.id,
      });
    });
    observer.observe(contentEl);
    return () => observer.disconnect();
  });

  // Switching tabs activates that tab's kernel view.
  $effect(() => {
    notify('ui.showTab', { tab: activeId, visible: active?.page === 'web' && !active?.error && !active?.download });
    siteCardOpen = false;
  });

  // Used by the shell's menu shortcuts, which work regardless of which view
  // has keyboard focus.
  $effect(() => {
    const hooks = window as unknown as { __lioraCommand?: (name: string) => void };
    hooks.__lioraCommand = (name: string) => {
      if (name.startsWith('settings:')) {
        openSettings(name.slice('settings:'.length));
        return;
      }
      switch (name) {
        case 'newTab': newTab(); break;
        case 'closeTab': closeTab(activeId); break;
        case 'reload': navigate(active.url); break;
        case 'focusAddress': addressEl?.focus(); addressEl?.select(); break;
        case 'settings': openSettings(); break;
        case 'downloads': downloads.open = !downloads.open; break;
        case 'sitecard': siteCardOpen = !siteCardOpen; break;
        case 'copyreport':
          (window as unknown as { __lioraCopyReport?: () => void }).__lioraCopyReport?.();
          break;
        case 'savereport':
          (window as unknown as { __lioraSaveReport?: () => void }).__lioraSaveReport?.();
          break;
        case 'restartkernel':
          (window as unknown as { __lioraRestartKernel?: () => void }).__lioraRestartKernel?.();
          break;
        case 'dnschange': {
          // Test hook: switch the DNS provider from the panel without
          // restarting, so the pending-changes state can be inspected.
          const mode = [...document.querySelectorAll('button')].find(
            (button) => button.textContent.trim() === '选择提供商',
          );
          mode?.click();
          setTimeout(() => {
            const quad9 = [...document.querySelectorAll<HTMLElement>('.provider')].find((card) =>
              card.textContent?.includes('Quad9'),
            );
            quad9?.click();
          }, 600);
          break;
        }
        case 'restartnetwork': {
          const restart = [...document.querySelectorAll('button')].find((button) =>
            button.textContent?.includes('重启'),
          );
          restart?.click();
          break;
        }
        case 'savetimeline': {
          (window as unknown as { __lioraSaveTimeline?: () => void }).__lioraSaveTimeline?.();
          break;
        }
        case 'setprovider': {
          void call('settings.set', { key: 'dns.mode', value: 'provider' });
          void call('settings.set', { key: 'dns.provider', value: 'quad9' });
          break;
        }
        case 'restartservice': {
          notify('ui.restartKernel', {});
          break;
        }
        case 'applyfrombadge': {
          document.querySelector<HTMLElement>('.network-pending')?.click();
          break;
        }
        case 'dnschangeback': {
          const mode = [...document.querySelectorAll('button')].find(
            (button) => button.textContent.trim() === '选择提供商',
          );
          mode?.click();
          setTimeout(() => {
            const cloudflare = [...document.querySelectorAll<HTMLElement>('.provider')].find(
              (card) => card.textContent?.includes('Cloudflare'),
            );
            cloudflare?.click();
          }, 600);
          break;
        }
        case 'openfail': {
          (window as unknown as { __lioraOpenUrl?: (url: string) => void }).__lioraOpenUrl?.(
            'https://this-host-does-not-exist.invalid/',
          );
          break;
        }
        case 'saverefreshed': {
          const save = [...document.querySelectorAll('button')].find((button) =>
            button.textContent?.includes('保存当前状态'),
          );
          save?.click();
          break;
        }
        case 'opendns': {
          const dns = [...document.querySelectorAll<HTMLElement>('.nav-item')].find((item) =>
            item.textContent?.includes('DNS 与直连'),
          );
          dns?.click();
          break;
        }
        case 'leavepage': {
          const appearance = [...document.querySelectorAll<HTMLElement>('.nav-item')].find(
            (item) => item.textContent.trim() === '外观',
          );
          appearance?.click();
          break;
        }
        case 'opendnsrestart': {
          const chip = document.querySelector<HTMLElement>('.network-pending');
          chip?.click();
          setTimeout(() => {
            const restart = [...document.querySelectorAll('button')].find((button) =>
              button.textContent?.includes('重启'),
            );
            restart?.click();
          }, 1500);
          break;
        }
        case 'pagedump': {
          const errors = (window as unknown as { __lioraErrors?: string[] }).__lioraErrors ?? [];
          const content = document.querySelector('.content');
          const badge = document.querySelector<HTMLElement>('.network-pending');
          (window as unknown as { __lioraPageText?: string }).__lioraPageText = [
            document.body.innerText,
            '-- badge --',
            badge
              ? JSON.stringify({ text: badge.textContent?.trim(), title: badge.title })
              : 'none',
            '-- content --',
            content?.innerHTML.slice(0, 700) ?? 'no content element',
            '-- errors --',
            JSON.stringify(errors),
          ].join('\n');
          break;
        }
      }
    };
  });

  // Remember the tab set so the next launch can restore it.
  $effect(() => {
    const snapshot = tabs.map((tab) => ({
      url: tab.url,
      title: tab.title,
      page: tab.download ? 'download' : tab.page,
    }));
    const selected = activeId;
    if (!sessionReady) return;
    clearTimeout(saveTimer);
    saveTimer = setTimeout(() => {
      void call('session.save', { tabs: snapshot, active: selected });
    }, 700);
    return () => clearTimeout(saveTimer);
  });

  function updateTab(id: number, patch: Partial<Tab>): void {
    const index = tabs.findIndex((tab) => tab.id === id);
    if (index >= 0) tabs[index] = { ...tabs[index], ...patch };
  }

  function newTab(url = 'liora://newtab'): void {
    const tab: Tab = { id: tabSeq++, title: t('tab.new'), url, page: 'newtab' };
    tabs = [...tabs, tab];
    activeId = tab.id;
    if (url !== 'liora://newtab') navigate(url);
  }

  function closeTab(id: number): void {
    if (tabs.length === 1) {
      updateTab(id, { title: t('tab.new'), url: 'liora://newtab', page: 'newtab' });
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

  function navigate(raw: string): void {
    siteCardOpen = false;
    const url = resolveInput(raw);
    const internal = internalPageFor(url);

    if (internal) {
      updateTab(activeId, { url, page: internal as Tab['page'], title: t('tab.new') });
      if (internal === 'settings') app.settingsSection = app.settingsSection || 'appearance';
      notify('ui.setPage', { page: internal, url });
      return;
    }

    updateTab(activeId, {
      url,
      page: 'web',
      title: url.replace(/^https?:\/\//, '').split('/')[0],
      error: undefined,
      download: undefined,
    });
    notify('ui.navigate', { url, tab: activeId });
  }

  function submitAddress(event: Event): void {
    event.preventDefault();
    navigate(addressValue);
    (event.target as HTMLFormElement).querySelector('input')?.blur();
  }

  function openSettings(section?: string): void {
    app.settingsSection = section ?? 'appearance';
    updateTab(activeId, { url: 'liora://settings', page: 'settings', title: t('settings.title') });
    notify('ui.setPage', { page: 'settings' });
  }

  function onAddressFocus(): void {
    addressFocused = true;
  }

  function onAddressBlur(): void {
    addressFocused = false;
    addressValue = active.page === 'web' ? active.url : `liora://${active.page}`;
  }

  // Zoom is remembered per site, matching the settings model.
  function siteOf(url: string): string {
    try {
      return new URL(url).hostname;
    } catch {
      return '';
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
    if (key === 'r') {
      event.preventDefault();
      navigate(active.url);
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
      const index = Number(key) - 1;
      if (index < tabs.length) {
        event.preventDefault();
        activeId = tabs[index].id;
      }
    }
  }
</script>

<svelte:window onkeydown={onKeydown} />

{#if !app.ready}
  <div class="boot">
    <div class="newtab__mark">L</div>
  </div>
{:else}
  <div class="app">
    {#if app.platform === 'macos'}
      <div class="titlebar titlebar--mac">
        <span class="titlebar__title">{t('app.name')}</span>
      </div>
    {/if}

    {#if showTabs}
      <div class="tabstrip">
        <div class="tabstrip__rail">
          {#each tabs as tab (tab.id)}
            <div
              class="tab"
              class:tab--active={tab.id === activeId}
              role="tab"
              tabindex="0"
              aria-selected={tab.id === activeId}
              onclick={() => (activeId = tab.id)}
              onkeydown={(event) => {
                if (event.key === 'Enter' || event.key === ' ') {
                  event.preventDefault();
                  activeId = tab.id;
                }
              }}
              ondblclick={() => newTab()}
              title={tab.title}
            >
              <span class="tab__favicon">
                {#if tab.page === 'settings'}
                  <Icon name="settings" size={10} />
                {:else if tab.page === 'newtab'}
                  L
                {:else}
                  <Icon name="globe" size={10} />
                {/if}
              </span>
              <span class="tab__title">{tab.title}</span>
              <button
                class="tab__close"
                title={t('tab.close')}
                aria-label={t('tab.close')}
                onclick={(event) => {
                  event.stopPropagation();
                  closeTab(tab.id);
                }}
              >
                <Icon name="close" size={10} />
              </button>
            </div>
          {/each}
        </div>
        <button class="icon-button" title={t('tab.new')} onclick={() => newTab()}>
          <Icon name="plus" />
        </button>
      </div>
    {/if}

    <div class="toolbar">
      <div class="toolbar__group">
        <button class="icon-button" title={t('nav.back')} disabled={active.page !== 'web'}>
          <Icon name="back" />
        </button>
        <button class="icon-button" title={t('nav.forward')} disabled={active.page !== 'web'}>
          <Icon name="forward" />
        </button>
        <button class="icon-button" title={t('nav.reload')} onclick={() => navigate(active.url)}>
          <Icon name="reload" />
        </button>
      </div>

      <form class="address" onsubmit={submitAddress}>
        <span class="address__scheme">
          <button
            type="button"
            class="address__scheme-button"
            title={t('site.title')}
            aria-label={t('site.title')}
            onclick={() => (siteCardOpen = !siteCardOpen)}
          >
            <Icon name={active.page === 'web' ? 'globe' : 'shield'} size={12} />
          </button>
        </span>
        <input
          class="address__input"
          bind:this={addressEl}
          bind:value={addressValue}
          oninput={(event) => (addressValue = (event.target as HTMLInputElement).value)}
          onfocus={onAddressFocus}
          onblur={onAddressBlur}
          placeholder={t('address.placeholder')}
          spellcheck="false"
          autocomplete="off"
        />
        {#if !addressFocused && active.page === 'web'}
          <span class="address__hint">H3 · ECH</span>
        {/if}
      </form>

      <div class="toolbar__group">
        {#if networkPending() || applyState !== 'idle'}
          <button
            class="network-pending"
            class:network-pending--applied={applyState === 'applied'}
            class:network-pending--error={applyState === 'error'}
            title={applyTitle}
            disabled={applyState === 'applying' || applyState === 'applied'}
            onclick={applyNetworkChanges}
          >
            <span class="dot"></span>
            {applyState === 'applying'
              ? t('network.applying')
              : applyState === 'applied'
                ? appliedProxyPort
                  ? t('network.appliedWithPort', { port: appliedProxyPort })
                  : t('network.applied')
                : applyState === 'error'
                  ? t('network.applyFailed')
                  : t('network.pending')}
          </button>
        {/if}
        <button
          class="icon-button"
          class:icon-button--active={downloads.open}
          title={t('downloads.title')}
          onclick={() => (downloads.open = !downloads.open)}
        >
          <Icon name="download" />
          {#if activeDownloadCount() > 0}
            <span class="icon-badge">{activeDownloadCount()}</span>
          {/if}
        </button>
        <button class="icon-button" title={t('nav.settings')} onclick={() => openSettings()}>
          <Icon name="settings" />
        </button>
      </div>

      {#if downloads.open}
        <DownloadsPanel />
      {/if}
    </div>

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

    <div class="bookmarksbar">
      <button class="bookmark-chip" onclick={() => navigate('liora://bookmarks')}>
        <Icon name="star" size={12} />
        {t('bookmarks.bar')}
      </button>
      {#each bookmarks as entry (entry.id)}
        <button class="bookmark-chip" title={entry.url} onclick={() => navigate(entry.url)}>
          {entry.title}
        </button>
      {/each}
      <button class="bookmark-chip" onclick={() => navigate('liora://history')}>
        <Icon name="history" size={12} />
        {t('nav.history')}
      </button>
    </div>

    <div class="content" class:content--native={active.page === 'web'} bind:this={contentEl}>
      {#if active.page === 'newtab'}
        <NewTab onnavigate={(url) => navigate(url)} onopen={(page) => navigate(`liora://${page}`)} />
      {:else if active.page === 'settings'}
        <Settings />
      {:else if active.page === 'diagnostics'}
        <DiagnosticsPage />
      {:else if active.page === 'bookmarks' || active.page === 'history'}
        <Library kind={active.page} onnavigate={(url) => navigate(url)} />
      {:else if active.download}
        <DownloadPage item={active.download} />
      {:else if active.page === 'web'}
        {#if active.error}
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
    </div>

    <div class="toasts">
      {#each app.toasts as item (item.id)}
        <div class="toast" class:toast--error={item.kind === 'error'}>{item.text}</div>
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
