<script lang="ts">
  import Icon from '../Icon.svelte';
  import Segmented from '../Segmented.svelte';
  import Rules from './Rules.svelte';
  import { call, notify, onNotification } from '../bridge';
  import { app, toast } from '../state.svelte';
  import { value, set } from '../settings-store.svelte';
  import { localized, t } from '../i18n.svelte';
  import { configFingerprint, hasUnappliedChanges } from '../kernel-switch-check';
  import { networkDelta } from '../network-delta';

  interface Category {
    id: string;
    name: Record<string, string> | string;
  }

  interface Variant {
    id: string;
    label: Record<string, string> | string;
    desc: Record<string, string> | string;
    categories: string[];
    protocols: string[];
    doh?: string;
    dot?: string;
    doq?: string;
    provider?: string;
  }

  interface Provider {
    id: string;
    name: string;
    variants: Variant[];
  }

  interface CustomDns {
    id: string;
    name: string;
    address: string;
    protocol: string;
  }

  interface ActivePlan {
    mode: string;
    dohTemplates: string;
    needsNet: boolean;
    summary: string;
    endpoint: { name: string; protocol: string; address: string } | null;
  }

  interface Runtime {
    running: boolean;
    dohPort: number;
    proxyPort: number;
    appliedConfig?: string;
  }

  interface KernelSetup {
    kernel: string;
    usesNet: boolean;
    netMissing: boolean;
    unsupported: string[];
  }

  interface KernelInfo {
    id: string;
    name: string;
    netNeedsRestart: boolean;
    available: boolean;
  }

  interface LastApplied {
    at: number;
    proxy: string;
    provider: string;
    protocol: string;
    mode: string;
    summary: string;
  }

  let categories = $state<Category[]>([]);
  let providers = $state<Provider[]>([]);
  let custom = $state<CustomDns[]>([]);
  let active = $state<ActivePlan | null>(null);
  let runtime = $state<Runtime | null>(null);
  let setup = $state<KernelSetup | null>(null);
  let currentConfig = $state('');
  let kernels = $state<KernelInfo[]>([]);
  let lastApplied = $state<LastApplied | null>(null);
  let restarting = $state(false);
  let category = $state('all');
  let loading = $state(true);

  let address = $state('');
  let name = $state('');
  let addError = $state('');

  const mode = $derived(String(value('dns.mode') ?? 'auto'));
  const protocol = $derived(String(value('dns.protocol') ?? 'doh'));
  const providerId = $derived(String(value('dns.provider') ?? 'cloudflare'));
  const customId = $derived(String(value('dns.customId') ?? ''));

  const modes = $derived([
    { value: 'auto', label: t('dns.mode.auto') },
    { value: 'provider', label: t('dns.mode.provider') },
    { value: 'custom', label: t('dns.mode.custom') },
    { value: 'off', label: t('dns.mode.off') },
  ]);

  const protocols = [
    { value: 'doh', label: 'DoH' },
    { value: 'dot', label: 'DoT' },
    { value: 'doq', label: 'DoQ' },
  ];

  async function load(): Promise<void> {
    loading = true;
    try {
      const catalog = await call<{
        categories: Category[];
        providers: Provider[];
        custom: CustomDns[];
        active: ActivePlan;
      }>('dns.catalog');
      categories = catalog.categories;
      providers = catalog.providers;
      custom = catalog.custom ?? [];
      active = catalog.active;
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    } finally {
      loading = false;
    }
  }

  async function refreshPlan(): Promise<void> {
    const [plan, current, kernelSetup, kernelList, config, applied] = await Promise.allSettled([
      call<ActivePlan>('dns.plan'),
      call<Runtime>('net.runtime'),
      call<KernelSetup>('kernels.setup', {}),
      call<KernelInfo[]>('kernels.list'),
      call<{ config: unknown }>('net.config', {}),
      call<LastApplied>('net.lastApplied'),
    ]);
    if (plan.status === 'fulfilled') active = plan.value;
    if (current.status === 'fulfilled') runtime = current.value;
    if (kernelSetup.status === 'fulfilled') setup = kernelSetup.value;
    if (kernelList.status === 'fulfilled' && Array.isArray(kernelList.value)) {
      kernels = kernelList.value;
    }
    if (config.status === 'fulfilled') currentConfig = configFingerprint(config.value.config);
    if (applied.status === 'fulfilled') {
      const value = applied.value;
      lastApplied =
        value && typeof value === 'object' && typeof value.at === 'number' && value.at > 0
          ? value
          : null;
    }
  }

  function proxyPort(value: string): string {
    try {
      return new URL(value).port;
    } catch {
      return /:(\d+)\/?$/.exec(value)?.[1] ?? '';
    }
  }

  // Reuses the shell's restart path: new resolver process, new proxy port,
  // re-applied proxy and reloaded kernel views.
  function restartKernel(): void {
    if (restarting) return;
    restarting = true;
    notify('ui.restartKernel', { kernel: setup?.kernel ?? '' });
    setTimeout(() => {
      restarting = false;
    }, 15000);
  }

  $effect(() => {
    return onNotification((message) => {
      if (message.method === 'browser.kernelRestarted') {
        restarting = false;
        toast(t('restart.done'));
        void refreshPlan();
        return;
      }
      // The shell restarts the resolver shortly after a settings change, so
      // re-read the plan and ports once it has settled.
      if (message.method === 'settings.changed' || message.method === 'net.rulesChanged') {
        setTimeout(() => {
          void refreshPlan();
        }, 700);
      }
    });
  });

  $effect(() => {
    void load();
  });

  $effect(() => {
    void refreshPlan();
  });

  const allVariants = $derived(
    providers.flatMap((provider) =>
      provider.variants.map((variant) => ({ ...variant, provider: provider.name })),
    ),
  );

  const visible = $derived(
    category === 'all'
      ? allVariants
      : allVariants.filter((variant) => variant.categories.includes(category)),
  );

  const selectedVariant = $derived(allVariants.find((variant) => variant.id === providerId));

  const activeKernel = $derived(kernels.find((kernel) => kernel.id === setup?.kernel) ?? null);
  const restartRequired = $derived(Boolean(activeKernel?.netNeedsRestart));
  // Compare the settings that are active now with the snapshot the kernel
  // views were last loaded with. A scheduled resolver refresh does not update
  // that snapshot, so pending changes stay visible until an explicit restart.
  const hasPendingChanges = $derived(
    hasUnappliedChanges(currentConfig, runtime?.appliedConfig ?? ''),
  );

  const delta = $derived.by(() => {
    if (!lastApplied) return [];
    return networkDelta(
      {
        provider: lastApplied.provider,
        protocol: lastApplied.protocol,
        mode: lastApplied.mode,
        proxy: lastApplied.proxy,
      },
      {
        provider: active?.endpoint?.name ?? '',
        protocol: active?.endpoint?.protocol ?? '',
        mode: active?.mode ?? '',
        proxy: runtime?.proxyPort ? `http://127.0.0.1:${runtime.proxyPort}` : '',
      },
    );
  });

  function deltaLabel(kind: 'provider' | 'protocol' | 'mode' | 'proxy'): string {
    if (kind === 'provider') return t('dns.deltaProvider');
    if (kind === 'protocol') return t('dns.deltaProtocol');
    if (kind === 'mode') return t('dns.deltaMode');
    return t('dns.deltaProxy');
  }

  function deltaValue(kind: 'provider' | 'protocol' | 'mode' | 'proxy', value: string): string {
    if (!value) return t('dns.deltaNone');
    if (kind === 'proxy') {
      const port = proxyPort(value);
      return port ? `:${port}` : value;
    }
    return value;
  }
  const needsResolver = $derived(Boolean(active?.needsNet || setup?.usesNet || setup?.netMissing));
  const resolverMissing = $derived(needsResolver && runtime?.running === false);

  const status = $derived.by(() => {
    if (mode === 'off') return { kind: 'muted', text: t('dns.status.system') };
    if (resolverMissing) return { kind: 'warning', text: t('dns.status.resolverDown') };
    if (mode === 'auto') return { kind: 'muted', text: t('dns.status.auto') };
    return { kind: 'success', text: t('dns.status.secure') };
  });

  function capLabel(cap: string): string {
    const labels: Record<string, string> = {
      ech: 'ECH',
      forceQuic: t('kernels.cap.forceQuic'),
      httpsRecords: t('kernels.cap.httpsRecords'),
      dns: t('nav.dns'),
    };
    return labels[cap] ?? cap;
  }

  async function chooseMode(next: string): Promise<void> {
    await set('dns.mode', next);
    await refreshPlan();
  }

  async function chooseProtocol(next: string): Promise<void> {
    await set('dns.protocol', next);
    await refreshPlan();
  }

  async function chooseVariant(variant: Variant): Promise<void> {
    if (!variant.protocols.includes(protocol)) {
      await set('dns.protocol', variant.protocols[0] ?? 'doh');
    }
    await set('dns.provider', variant.id);
    await set('dns.mode', 'provider');
    await refreshPlan();
  }

  async function chooseCustom(entry: CustomDns): Promise<void> {
    await set('dns.customId', entry.id);
    await set('dns.mode', 'custom');
    await refreshPlan();
  }

  async function addCustom(): Promise<void> {
    addError = '';
    if (!address.trim()) return;
    try {
      const result = await call<{ valid: boolean; error?: string }>('dns.validate', { address });
      if (!result.valid) {
        addError = result.error || t('dns.custom.invalid');
        return;
      }
      const entry = await call<CustomDns>('dns.custom.add', { address, name });
      custom = [...custom, entry];
      address = '';
      name = '';
      toast(t('settings.changed'));
      await refreshPlan();
    } catch (error: any) {
      addError = String(error?.message ?? error);
    }
  }

  async function removeCustom(entry: CustomDns): Promise<void> {
    await call('dns.custom.remove', { id: entry.id });
    custom = custom.filter((item) => item.id !== entry.id);
    if (customId === entry.id) await set('dns.mode', 'auto');
    await refreshPlan();
  }

  function categoryLabel(item: Category): string {
    return typeof item.name === 'string' ? item.name : localized(item.name);
  }
</script>

<div class="section">
  <header class="settings__header">
    <h1 class="settings__title">{t('dns.title')}</h1>
    <p class="settings__lede">{t('dns.lede')}</p>
  </header>

  <div class="card">
    <div class="card__header">
      <div class="card__main">
        <div class="card__title">{t('dns.current')}</div>
      </div>
    </div>
    <div class="card__body">
      <div class="status">
        <span
          class="badge"
          class:badge--success={status.kind === 'success'}
          class:badge--warning={status.kind === 'warning'}
        >
          <span class="dot"></span>
          {status.text}
        </span>
        {#if active?.endpoint}
          <span class="mono muted">{active.endpoint.protocol.toUpperCase()} · {active.endpoint.name}</span>
        {/if}
      </div>

      {#if mode !== 'off'}
        <div class="row" style:margin-top="6px">
          <div class="row__main">
            <div class="row__title">{t('dns.mode')}</div>
            <div class="row__desc">
              {mode === 'auto' ? t('dns.mode.auto.desc') : mode === 'off' ? t('dns.mode.off.desc') : active?.summary}
            </div>
          </div>
        </div>
      {/if}

      {#if lastApplied && lastApplied.at > 0}
        <div class="applied-line">
          {t('dns.lastApplied', { time: new Date(lastApplied.at).toLocaleTimeString() })}
          {#if lastApplied.provider}
            · {lastApplied.provider}{#if lastApplied.protocol}{' ('}{lastApplied.protocol}){/if}
          {/if}
          {#if lastApplied.proxy}
            · {t('dns.runtime.proxy')} :{proxyPort(lastApplied.proxy)}
          {/if}
        </div>
        {#if delta.length > 0}
          <div class="applied-line applied-line--delta">
            {t('dns.delta')}:
            {#each delta as item, index (item.kind)}
              {#if index > 0}{' · '}{/if}
              {deltaLabel(item.kind)}
              {deltaValue(item.kind, item.from)} → {deltaValue(item.kind, item.to)}
            {/each}
          </div>
        {/if}
      {/if}

      {#if runtime?.running}
        <div class="cluster" style:margin-top="10px">
          <span class="badge badge--success">
            <span class="dot"></span>
            {t('dns.runtime.running')}
          </span>
          <span class="mono muted">DoH :{runtime.dohPort} · {t('dns.runtime.proxy')} :{runtime.proxyPort}</span>
        </div>
      {:else if resolverMissing}
        <div class="callout callout--warning" style:margin-top="10px">
          <span class="callout__icon"><Icon name="warning" size={14} /></span>
          <span>{t('dns.status.resolverDownHint')}</span>
        </div>
      {/if}

      {#if setup && setup.unsupported.length > 0}
        <div class="callout" style:margin-top="10px">
          <span class="callout__icon"><Icon name="info" size={14} /></span>
          <span>{t('dns.unsupported')}: {setup.unsupported.map(capLabel).join('、')}</span>
        </div>
      {/if}

      {#if hasPendingChanges}
        <div class="row" style:margin-top="10px">
          <div class="row__main">
            <div class="row__title">{t('dns.pendingTitle')}</div>
            <div class="row__desc">
              {restartRequired ? t('dns.pendingKernelHint') : t('dns.pendingResolverHint')}
            </div>
          </div>
          <div class="row__control">
            <button
              class="button button--sm"
              class:button--primary={restartRequired}
              disabled={restarting}
              onclick={restartKernel}
            >
              <Icon name="refresh" size={12} />
              {restarting
                ? t('restart.working')
                : restartRequired
                  ? t('restart.action')
                  : t('restart.panelResolverAction')}
            </button>
          </div>
        </div>
      {/if}

      <div class="row">
        <div class="row__control" style:flex-wrap="wrap">
          <Segmented value={mode} options={modes} ariaLabel={t('dns.mode')} onchange={chooseMode} />
        </div>
      </div>

      {#if mode === 'off'}
        <div class="callout" style:margin-top="10px">
          <span class="callout__icon"><Icon name="info" size={14} /></span>
          <span>{t('dns.mode.off.desc')}</span>
        </div>
      {/if}
    </div>
  </div>

  {#if mode === 'provider'}
    <div class="card">
      <div class="card__header">
        <div>
          <div class="card__title">{t('dns.mode.provider')}</div>
          <div class="card__subtitle">{t('dns.protocol')}</div>
        </div>
        <div style:margin-left="auto">
          <Segmented
            value={protocol}
            options={protocols.filter((item) =>
              selectedVariant ? selectedVariant.protocols.includes(item.value) : true,
            )}
            ariaLabel={t('dns.protocol')}
            onchange={chooseProtocol}
          />
        </div>
      </div>

      <div class="card__body">
        <div class="cluster" style:margin-bottom="12px">
          <button class="chip" class:chip--active={category === 'all'} onclick={() => (category = 'all')}>
            {t('dns.category.all')}
          </button>
          {#each categories as item (item.id)}
            <button class="chip" class:chip--active={category === item.id} onclick={() => (category = item.id)}>
              {categoryLabel(item)}
            </button>
          {/each}
        </div>

        {#if loading}
          <div class="empty">…</div>
        {:else}
          <div class="providers">
            {#each visible as variant (variant.id)}
              <button
                class="provider"
                class:provider--active={variant.id === providerId}
                onclick={() => chooseVariant(variant)}
              >
                <span class="provider__name">
                  {variant.provider ?? ''}
                  {#if variant.id === providerId}
                    <Icon name="check" size={13} />
                  {/if}
                </span>
                <span class="provider__desc">{localized(variant.label)} · {localized(variant.desc)}</span>
                <span class="provider__tags">
                  {#each variant.protocols as item (item)}
                    <span class="badge">{item.toUpperCase()}</span>
                  {/each}
                </span>
              </button>
            {/each}
          </div>
        {/if}
      </div>
    </div>
  {/if}

  {#if mode === 'custom'}
    <div class="card">
      <div class="card__header">
        <div>
          <div class="card__title">{t('dns.custom.title')}</div>
          <div class="card__subtitle">{t('dns.custom.placeholder')}</div>
        </div>
      </div>
      <div class="card__body">
        {#if custom.length === 0}
          <div class="empty">{t('dns.custom.empty')}</div>
        {:else}
          {#each custom as entry (entry.id)}
            <div class="row">
              <div class="row__main">
                <div class="row__title">
                  {entry.name}
                  {#if entry.id === customId}
                    <span class="badge badge--accent">{t('dns.current')}</span>
                  {/if}
                </div>
                <div class="row__desc mono">{entry.address}</div>
              </div>
              <div class="row__control">
                <button class="button button--sm" onclick={() => chooseCustom(entry)}>{t('dns.protocol')}</button>
                <button class="icon-button" title={t('dns.custom.remove')} onclick={() => removeCustom(entry)}>
                  <Icon name="trash" size={14} />
                </button>
              </div>
            </div>
          {/each}
        {/if}

        <div class="divider" style:margin="12px 0"></div>

        <div class="stack">
          <input class="input input--mono" bind:value={address} placeholder={t('dns.custom.placeholder')} />
          <div class="cluster">
            <input class="input" bind:value={name} placeholder={t('dns.custom.name')} style:flex="1" />
            <button class="button button--primary" onclick={addCustom}>{t('dns.custom.add')}</button>
          </div>
          {#if addError}
            <div class="callout callout--danger">
              <span class="callout__icon"><Icon name="warning" size={14} /></span>
              <span>{addError}</span>
            </div>
          {/if}
        </div>
      </div>
    </div>
  {/if}

  {#if app.mode === 'advanced' || mode === 'provider'}
    <Rules />
  {/if}
</div>

<style>
  .applied-line {
    margin-top: 8px;
    font-size: 11.5px;
    color: var(--text-muted);
  }

  .applied-line--delta {
    color: var(--warning);
  }

  .status {
    display: flex;
    align-items: center;
    gap: 10px;
    flex-wrap: wrap;
  }

  .card__main {
    flex: 1;
  }
</style>
