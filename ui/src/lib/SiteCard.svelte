<script lang="ts">
  import Icon from './Icon.svelte';
  import { call } from './bridge';
  import { toast } from './state.svelte';
  import { t } from './i18n.svelte';

  interface Props {
    url: string;
    tab: number;
    security?: { host: string; subject: string; valid: boolean; notAfter: number; https: boolean } | null;
    onclose?: () => void;
    onopenDns?: () => void;
  }

  let { url, tab, security = null, onclose, onopenDns }: Props = $props();

  interface Permission {
    origin: string;
    name: string;
    value: string;
    modified: number;
  }

  interface RuleMatch {
    matched: boolean;
    host?: string;
    ruleName?: string;
    h3?: string;
    ech?: boolean;
    dns?: { name: string; protocol: string } | null;
  }

  interface Diagnostics {
    host: string;
    rule: { name: string; h3: string; ech: boolean; dns: string | null } | null;
    upstream: { id: string; protocol: string; address: string };
    addresses: string[];
    https: { alpn: string[]; echBytes: number } | null;
    resolved: boolean;
    elapsedMs: number;
  }

  let permissions = $state<Permission[]>([]);
  let match = $state<RuleMatch | null>(null);
  let kernelId = $state('');
  let adding = $state(false);
  let diag = $state<Diagnostics | null>(null);
  let diagError = $state('');

  const host = $derived.by(() => {
    try {
      return new URL(url).hostname;
    } catch {
      return '';
    }
  });

  const origin = $derived.by(() => {
    try {
      const parsed = new URL(url);
      return `${parsed.protocol}//${parsed.hostname}`;
    } catch {
      return '';
    }
  });

  const isWeb = $derived(/^https?:/i.test(url));

  async function load(): Promise<void> {
    if (!isWeb || !host) return;
    const [perms, matched, kernel] = await Promise.allSettled([
      call<Permission[]>('permissions.list', { origin }),
      call<RuleMatch>('rules.match', { host }),
      call<{ id: string }>('kernels.current', { site: host }),
    ]);
    permissions = perms.status === 'fulfilled' ? perms.value : [];
    match = matched.status === 'fulfilled' ? matched.value : null;
    kernelId = kernel.status === 'fulfilled' ? kernel.value.id : '';
  }

  $effect(() => {
    void url;
    void load();
    void loadDiagnostics();
  });

  async function loadDiagnostics(): Promise<void> {
    diag = null;
    diagError = '';
    if (!isWeb || !host) return;
    try {
      const runtime = await call<{ running: boolean; diagPort: number }>('net.runtime');
      if (!runtime.running || !runtime.diagPort) {
        diagError = t('site.diagOff');
        return;
      }
      const response = await fetch(
        `http://127.0.0.1:${runtime.diagPort}/diagnose?host=${encodeURIComponent(host)}`,
      );
      if (!response.ok) {
        diagError = t('site.diagFailed');
        return;
      }
      diag = (await response.json()) as Diagnostics;
    } catch {
      diagError = t('site.diagFailed');
    }
  }

  async function revoke(entry: Permission): Promise<void> {
    await call('permissions.set', { origin: entry.origin, name: entry.name, value: '' });
    permissions = permissions.filter((item) => item.name !== entry.name);
    toast(t('site.revoked'));
  }

  async function revokeAll(): Promise<void> {
    for (const entry of permissions) {
      await call('permissions.set', { origin: entry.origin, name: entry.name, value: '' });
    }
    permissions = [];
    toast(t('site.revoked'));
  }

  async function addDirectRule(): Promise<void> {
    if (!host || adding) return;
    adding = true;
    try {
      await call('rules.add', { name: host, domains: [host], h3: 'prefer', ech: true, dns: '' });
      toast(t('site.ruleAdded'));
      await load();
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    } finally {
      adding = false;
    }
  }

  function permissionLabel(name: string): string {
    if (name === 'camera') return t('site.camera');
    if (name === 'microphone') return t('site.microphone');
    if (name === 'camera+microphone') return t('site.cameraMicrophone');
    return name;
  }

  function h3Label(mode?: string): string {
    if (mode === 'force') return t('rules.h3.force');
    if (mode === 'off') return t('rules.h3.off');
    return t('rules.h3.prefer');
  }
</script>

<div class="sitecard" role="dialog" aria-label={t('site.title')}>
  <header class="sitecard__header">
    <span class="sitecard__icon"><Icon name={isWeb ? 'globe' : 'shield'} size={14} /></span>
    <div class="sitecard__identity">
      <div class="sitecard__host">{host || url}</div>
      <div class="sitecard__origin mono">{origin}</div>
    </div>
    <button class="icon-button" title={t('site.close')} onclick={() => onclose?.()}>
      <Icon name="close" size={12} />
    </button>
  </header>

  {#if !isWeb}
    <div class="sitecard__empty">{t('site.internalPage')}</div>
  {:else}
    <section class="sitecard__section">
      <div class="sitecard__label">
        {t('site.permissions')}
        {#if permissions.length > 1}
          <button class="link" onclick={revokeAll}>{t('site.revokeAll')}</button>
        {/if}
      </div>
      {#if permissions.length === 0}
        <div class="sitecard__empty">{t('site.noPermissions')}</div>
      {:else}
        {#each permissions as entry (entry.name)}
          <div class="sitecard__row">
            <span class="badge" class:badge--success={entry.value === 'allow'} class:badge--danger={entry.value !== 'allow'}>
              {entry.value === 'allow' ? t('permissions.allow') : t('permissions.block')}
            </span>
            <span class="sitecard__row-main">{permissionLabel(entry.name)}</span>
            <button class="button button--sm" onclick={() => revoke(entry)}>{t('site.revoke')}</button>
          </div>
        {/each}
      {/if}
    </section>

    <section class="sitecard__section">
      <div class="sitecard__label">{t('site.connection')}</div>
      {#if security?.https}
        <div class="sitecard__row">
          <span class="badge" class:badge--success={security.valid} class:badge--danger={!security.valid}>
            {security.valid ? t('site.valid') : t('site.invalid')}
          </span>
          <span class="sitecard__row-main mono">{security.subject || host}</span>
        </div>
        {#if security.notAfter > 0}
          <div class="sitecard__empty">
            {t('site.expires')}: {new Date(security.notAfter).toLocaleDateString()}
          </div>
        {/if}
      {:else if kernelId === 'cef' && url.startsWith('https://')}
        <div class="sitecard__empty">{t('site.cefNoCertificate')}</div>
      {:else}
        <div class="sitecard__empty">{t('site.noTls')}</div>
      {/if}
    </section>

    <section class="sitecard__section">
      <div class="sitecard__label">{t('site.resolve')}</div>
      {#if diagError}
        <div class="sitecard__empty">{diagError}</div>
      {:else if !diag}
        <div class="sitecard__empty">{t('site.resolving')}</div>
      {:else}
        <div class="sitecard__facts">
          <span class="badge mono">{diag.upstream.protocol.toUpperCase()}</span>
          {#if diag.rule}<span class="badge badge--accent">{t('site.ruleMatched')}</span>{/if}
          <span class="badge">{diag.elapsedMs} ms</span>
        </div>
        <div class="sitecard__empty mono">{diag.upstream.id} · {diag.upstream.address}</div>
        {#if diag.addresses.length > 0}
          <div class="sitecard__empty mono">{diag.addresses.slice(0, 4).join(', ')}</div>
        {:else}
          <div class="sitecard__empty">{t('site.noAddress')}</div>
        {/if}
        {#if diag.https}
          <div class="sitecard__facts">
            <span class="badge">ALPN {diag.https.alpn.join('/') || '—'}</span>
            <span class="badge" class:badge--accent={diag.https.echBytes > 0}>ECH {diag.https.echBytes}B</span>
          </div>
        {/if}
      {/if}
    </section>

    <section class="sitecard__section">
      <div class="sitecard__label">{t('site.network')}</div>
      {#if match?.matched}
        <div class="sitecard__row">
          <span class="badge badge--accent">{t('site.direct')}</span>
          <span class="sitecard__row-main">{match.ruleName}</span>
        </div>
        <div class="sitecard__facts">
          <span class="badge">H3 {h3Label(match.h3)}</span>
          {#if match.ech}<span class="badge">ECH</span>{/if}
          {#if match.dns}<span class="badge mono">{match.dns.protocol.toUpperCase()}</span>{/if}
        </div>
      {:else}
        <div class="sitecard__empty">{t('site.noRule')}</div>
        <button class="button button--primary button--sm" disabled={adding} onclick={addDirectRule}>
          <Icon name="shield" size={12} />
          {t('site.addDirect')}
        </button>
      {/if}
    </section>

    <section class="sitecard__section">
      <div class="sitecard__label">{t('site.kernel')}</div>
      <div class="sitecard__row">
        <span class="badge">{kernelId || '—'}</span>
        <span class="sitecard__row-main muted">{t('site.kernelHint')}</span>
      </div>
    </section>

    <footer class="sitecard__footer">
      <button class="link" onclick={() => onopenDns?.()}>{t('error.opensettings')}</button>
    </footer>
  {/if}
</div>

<style>
  .sitecard {
    width: min(720px, calc(100% - 24px));
    margin: 0 12px 8px;
    max-height: 320px;
    overflow: auto;
    border-radius: var(--radius-md);
    border: 1px solid var(--border);
    background: var(--surface-strong);
    box-shadow: var(--shadow-sm);
  }

  .sitecard__header {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 12px;
    border-bottom: 1px solid var(--border);
  }

  .sitecard__icon {
    width: 28px;
    height: 28px;
    flex: none;
    border-radius: 9px;
    display: grid;
    place-items: center;
    background: var(--bg-sunken);
    color: var(--text-secondary);
  }

  .sitecard__identity {
    flex: 1;
    min-width: 0;
  }

  .sitecard__host {
    font-size: 13px;
    font-weight: 600;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .sitecard__origin {
    color: var(--text-muted);
    font-size: 11px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .sitecard__section {
    padding: 10px 12px;
    border-bottom: 1px solid var(--border);
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .sitecard__label {
    display: flex;
    align-items: center;
    justify-content: space-between;
    font-size: 11.5px;
    font-weight: 600;
    color: var(--text-secondary);
  }

  .sitecard__row {
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .sitecard__row-main {
    flex: 1;
    min-width: 0;
    font-size: 12.5px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .sitecard__facts {
    display: flex;
    gap: 5px;
    flex-wrap: wrap;
  }

  .sitecard__empty {
    font-size: 12px;
    color: var(--text-muted);
  }

  .sitecard__footer {
    padding: 8px 12px;
    display: flex;
    justify-content: flex-end;
  }

  .link {
    font-size: 11.5px;
    color: var(--accent);
  }
</style>
