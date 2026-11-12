<script lang="ts">
  import Icon from '../Icon.svelte';
  import PageHeader from '../PageHeader.svelte';
  import Segmented from '../Segmented.svelte';
  import Toggle from '../Toggle.svelte';
  import Rules from './Rules.svelte';
  import { call, notify, onNotification } from '../bridge';
  import { app, toast, updateSetting } from '../state.svelte';
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

  interface ResolverStatus {
    at: number;
    count: number;
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
  let resolverStatus = $state<ResolverStatus | null>(null);
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

  const modeChoices = $derived([
    {
      id: 'auto',
      icon: 'shield',
      title: t('dns.mode.auto'),
      desc: t('dns.mode.auto.desc'),
    },
    {
      id: 'provider',
      icon: 'globe',
      title: t('dns.mode.provider'),
      desc: t('dns.mode.provider.desc'),
    },
    {
      id: 'custom',
      icon: 'server',
      title: t('dns.mode.custom'),
      desc: t('dns.mode.custom.desc'),
    },
    {
      id: 'off',
      icon: 'settings',
      title: t('dns.mode.off'),
      desc: t('dns.mode.off.desc'),
    },
  ]);

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
    const [plan, current, kernelSetup, kernelList, config, applied, resolver] =
      await Promise.allSettled([
        call<ActivePlan>('dns.plan'),
        call<Runtime>('net.runtime'),
        call<KernelSetup>('kernels.setup', {}),
        call<KernelInfo[]>('kernels.list'),
        call<{ config: unknown }>('net.config', {}),
        call<LastApplied>('net.lastApplied'),
        call<ResolverStatus>('net.resolverStatus'),
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
    if (resolver.status === 'fulfilled') {
      const value = resolver.value;
      resolverStatus =
        value && typeof value === 'object' && typeof value.count === 'number' && value.count > 0
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
    if (mode === 'off') {
      return {
        kind: 'muted' as const,
        title: t('dns.status.system'),
        detail: t('dns.status.systemHint'),
        badge: t('dns.status.systemBadge'),
      };
    }
    if (resolverMissing) {
      return {
        kind: 'warning' as const,
        title: t('dns.status.resolverDown'),
        detail: t('dns.status.resolverDownHint'),
        badge: t('dns.notRunning'),
      };
    }
    if (mode === 'auto') {
      return {
        kind: 'muted' as const,
        title: t('dns.status.autoTitle'),
        detail: t('dns.status.autoHint'),
        badge: t('dns.mode.auto'),
      };
    }
    return {
      kind: 'success' as const,
      title: t('dns.status.secure'),
      detail: active?.endpoint
        ? `${active.endpoint.protocol.toUpperCase()} · ${active.endpoint.name}`
        : t('dns.status.secureHint'),
      badge: t('dns.status.encrypted'),
    };
  });

  const modeLabel = $derived(
    modes.find((item) => item.value === mode)?.label ?? t('dns.mode.auto'),
  );
  const advanced = $derived(app.mode === 'advanced');
  const quickProviderIds = ['cloudflare', 'quad9', 'google'];
  const quickProviders = $derived(
    quickProviderIds.flatMap((id) => {
      const variant = allVariants.find((item) => item.id === id);
      return variant ? [variant] : [];
    }),
  );
  const simpleChoice = $derived(
    mode === 'off'
      ? 'off'
      : mode === 'auto'
        ? 'auto'
        : quickProviderIds.includes(providerId)
          ? providerId
          : 'current',
  );
  const simpleProviderName = $derived.by(() => {
    if (mode === 'custom') {
      return custom.find((entry) => entry.id === customId)?.name || t('dns.mode.custom');
    }
    if (mode === 'provider') {
      return selectedVariant?.provider || selectedVariant?.id || t('dns.mode.provider');
    }
    return t('dns.mode.auto');
  });

  // Turning encryption off and on again should not forget a custom provider.
  let previousMode = $state('auto');
  $effect(() => {
    if (mode !== 'off') previousMode = mode;
  });

  async function toggleEncryption(next: boolean): Promise<void> {
    await set('dns.mode', next ? previousMode : 'off');
    await refreshPlan();
  }

  async function applySimpleChoice(event: Event): Promise<void> {
    const choice = (event.target as HTMLSelectElement).value;
    if (choice === 'current' || choice === 'off') return;
    if (choice === 'auto') {
      await set('dns.mode', 'auto');
      await refreshPlan();
      return;
    }
    const variant = allVariants.find((item) => item.id === choice);
    if (variant) await chooseVariant(variant);
  }

  async function openAdvanced(): Promise<void> {
    await updateSetting('ui.mode', 'advanced');
  }

  const resolverValue = $derived.by(() => {
    if (mode === 'off' || !needsResolver) return t('dns.notNeeded');
    if (runtime?.running) return t('dns.runtime.running');
    return t('dns.notRunning');
  });

  const resolverHint = $derived.by(() => {
    if (mode === 'off' || !needsResolver) return '';
    if (runtime?.running) {
      const proxy = runtime.proxyPort ? ` · ${t('dns.runtime.proxy')} :${runtime.proxyPort}` : '';
      return `DoH :${runtime.dohPort}${proxy}`;
    }
    return needsResolver ? t('dns.status.resolverDownHint') : '';
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
      const result = await call<{ valid: boolean; error?: string }>('dns.validate', {
        address: address,
      });
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
  <PageHeader
    icon="shield"
    title={t('dns.title')}
    subtitle={advanced ? t('dns.ledeAdvanced') : t('dns.lede')}
  />

  {#if advanced}
  <section class="card status">
    <div class="status__hero">
      <span
        class="status__icon"
        class:status__icon--success={status.kind === 'success'}
        class:status__icon--warning={status.kind === 'warning'}
      >
        <Icon name={status.kind === 'warning' ? 'warning' : 'shield'} size={20} />
      </span>
      <div class="status__main">
        <div class="status__eyebrow">{t('dns.current')}</div>
        <div class="status__title">{status.title}</div>
        <div class="status__detail">{status.detail}</div>
      </div>
      <span
        class="pill"
        class:pill--success={status.kind === 'success'}
        class:pill--warning={status.kind === 'warning'}
      >
        <span class="dot"></span>
        {status.badge}
      </span>
    </div>

    <div class="metrics">
      <div class="metric">
        <span class="metric__label">{t('dns.mode')}</span>
        <span class="metric__value">{modeLabel}</span>
      </div>
      <div class="metric">
        <span class="metric__label">{t('dns.upstream')}</span>
        <span class="metric__value">
          {#if active?.endpoint}
            {active.endpoint.protocol.toUpperCase()} · {active.endpoint.name}
          {:else}
            {mode === 'off'
              ? t('dns.systemResolver')
              : mode === 'auto'
                ? t('dns.autoUpstream')
                : t('dns.deltaNone')}
          {/if}
        </span>
      </div>
      <div class="metric">
        <span class="metric__label">{t('dns.resolver')}</span>
        <span class="metric__value" class:metric__value--muted={mode === 'off'}>
          {resolverValue}
        </span>
        {#if resolverHint}<span class="metric__hint mono">{resolverHint}</span>{/if}
      </div>
      <div class="metric">
        <span class="metric__label">{t('dns.lastAppliedLabel')}</span>
        <span class="metric__value">
          {lastApplied
            ? new Date(lastApplied.at).toLocaleTimeString()
            : t('dns.neverApplied')}
        </span>
        {#if lastApplied?.provider}
          <span class="metric__hint">{lastApplied.provider}</span>
        {/if}
      </div>
    </div>

    {#if resolverStatus}
      <div class="status__note">
        {t('dns.resolverRestarted', {
          time: new Date(resolverStatus.at).toLocaleTimeString(),
          count: resolverStatus.count,
        })}
      </div>
    {/if}

    {#if resolverMissing}
      <div class="callout callout--warning status__notice">
        <span class="callout__icon"><Icon name="warning" size={14} /></span>
        <span>{t('dns.status.resolverDownHint')}</span>
      </div>
    {/if}

    {#if setup && setup.unsupported.length > 0}
      <div class="callout status__notice">
        <span class="callout__icon"><Icon name="info" size={14} /></span>
        <span>{t('dns.unsupported')}: {setup.unsupported.map(capLabel).join('、')}</span>
      </div>
    {/if}

    {#if hasPendingChanges}
      <div class="pending">
        <span class="pending__icon"><Icon name="refresh" size={15} /></span>
        <div class="pending__main">
          <div class="pending__title">{t('dns.pendingTitle')}</div>
          <div class="pending__desc">
            {restartRequired ? t('dns.pendingKernelHint') : t('dns.pendingResolverHint')}
          </div>
          {#if delta.length > 0}
            <div class="pending__delta">
              {#each delta as item, index (item.kind)}
                {#if index > 0}<span class="pending__sep">·</span>{/if}
                <span>{deltaLabel(item.kind)}</span>
                <code>{deltaValue(item.kind, item.from)} → {deltaValue(item.kind, item.to)}</code>
              {/each}
            </div>
          {/if}
        </div>
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
    {/if}
  </section>

  <section class="card">
    <div class="card__header">
      <div>
        <div class="card__title">{t('dns.modeTitle')}</div>
        <div class="card__subtitle">{t('dns.modeSubtitle')}</div>
      </div>
    </div>
    <div class="card__body">
      <div class="modes">
        {#each modeChoices as choice (choice.id)}
          <button
            class="mode"
            class:mode--active={mode === choice.id}
            onclick={() => chooseMode(choice.id)}
          >
            <span class="mode__top">
              <span class="mode__icon"><Icon name={choice.icon} size={15} /></span>
              <span class="mode__radio" class:mode__radio--on={mode === choice.id}>
                {#if mode === choice.id}<Icon name="check" size={10} />{/if}
              </span>
            </span>
            <span class="mode__title">{choice.title}</span>
            <span class="mode__desc">{choice.desc}</span>
          </button>
        {/each}
      </div>
    </div>
  </section>

  {#if mode === 'provider'}
    <section class="card">
      <div class="card__header">
        <div class="card__main">
          <div class="card__title">{t('dns.providerTitle')}</div>
          <div class="card__subtitle">{t('dns.protocol')}</div>
        </div>
        <Segmented
          value={protocol}
          options={protocols.filter((item) =>
            selectedVariant ? selectedVariant.protocols.includes(item.value) : true,
          )}
          ariaLabel={t('dns.protocol')}
          onchange={chooseProtocol}
        />
      </div>

      <div class="card__body">
        <div class="chips">
          <button
            class="chip"
            class:chip--active={category === 'all'}
            onclick={() => (category = 'all')}
          >
            {t('dns.category.all')}
          </button>
          {#each categories as item (item.id)}
            <button
              class="chip"
              class:chip--active={category === item.id}
              onclick={() => (category = item.id)}
            >
              {categoryLabel(item)}
            </button>
          {/each}
        </div>

        {#if loading}
          <div class="providers providers--loading" aria-busy="true">
            {#each Array(6) as _, index (index)}
              <span class="provider-skeleton"></span>
            {/each}
          </div>
        {:else if visible.length === 0}
          <div class="empty-state empty-state--compact">
            <span class="empty-state__icon"><Icon name="globe" size={18} /></span>
            <div class="empty-state__title">{t('dns.providersEmpty')}</div>
          </div>
        {:else}
          <div class="providers">
            {#each visible as variant (variant.id)}
              <button
                class="provider"
                class:provider--active={variant.id === providerId}
                onclick={() => chooseVariant(variant)}
              >
                <span class="provider__check" class:provider__check--on={variant.id === providerId}>
                  {#if variant.id === providerId}<Icon name="check" size={10} />{/if}
                </span>
                <span class="provider__name">{variant.provider ?? ''}</span>
                <span class="provider__desc">
                  {localized(variant.label)} · {localized(variant.desc)}
                </span>
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
    </section>
  {/if}

  {#if mode === 'custom'}
    <section class="card">
      <div class="card__header">
        <div>
          <div class="card__title">{t('dns.custom.title')}</div>
          <div class="card__subtitle">{t('dns.custom.hint')}</div>
        </div>
      </div>
      <div class="card__body">
        {#if custom.length === 0}
          <div class="empty-state empty-state--compact">
            <span class="empty-state__icon"><Icon name="server" size={18} /></span>
            <div class="empty-state__title">{t('dns.custom.empty')}</div>
            <p class="empty-state__body">{t('dns.custom.emptyHint')}</p>
          </div>
        {:else}
          <div class="custom-list">
            {#each custom as entry (entry.id)}
              <div class="custom-row">
                <span class="custom-row__icon"><Icon name="server" size={15} /></span>
                <div class="custom-row__main">
                  <div class="custom-row__title">
                    {entry.name}
                    {#if entry.id === customId}
                      <span class="badge badge--accent">{t('dns.current')}</span>
                    {/if}
                  </div>
                  <div class="custom-row__address mono">{entry.address}</div>
                </div>
                <div class="custom-row__control">
                  {#if entry.id === customId}
                    <span class="badge badge--success">{t('dns.custom.inUse')}</span>
                  {:else}
                    <button class="button button--sm" onclick={() => chooseCustom(entry)}>
                      {t('dns.custom.use')}
                    </button>
                  {/if}
                  <button
                    class="icon-button"
                    title={t('dns.custom.remove')}
                    aria-label={t('dns.custom.remove')}
                    onclick={() => removeCustom(entry)}
                  >
                    <Icon name="trash" size={14} />
                  </button>
                </div>
              </div>
            {/each}
          </div>
        {/if}

        <div class="form-panel">
          <div class="form-panel__title">{t('dns.custom.formTitle')}</div>
          <label class="field">
            <span class="field__label">{t('dns.custom.address')}</span>
            <input
              class="input input--mono"
              bind:value={address}
              placeholder={t('dns.custom.placeholder')}
              spellcheck="false"
            />
          </label>
          <div class="form-panel__row">
            <label class="field field--grow">
              <span class="field__label">{t('dns.custom.nameLabel')}</span>
              <input class="input" bind:value={name} placeholder={t('dns.custom.name')} />
            </label>
            <button class="button button--primary" onclick={addCustom}>
              <Icon name="plus" size={13} />
              {t('dns.custom.add')}
            </button>
          </div>
          {#if addError}
            <div class="callout callout--danger form-panel__error">
              <span class="callout__icon"><Icon name="warning" size={14} /></span>
              <span>{addError}</span>
            </div>
          {/if}
        </div>
      </div>
    </section>
  {/if}

  <Rules />
  {:else}
    <section class="card privacy">
      <div class="privacy__row">
        <span
          class="privacy__icon"
          class:privacy__icon--success={status.kind === 'success'}
          class:privacy__icon--warning={status.kind === 'warning'}
        >
          <Icon name={status.kind === 'warning' ? 'warning' : 'shield'} size={20} />
        </span>
        <div class="privacy__main">
          <div class="privacy__eyebrow">{t('dns.current')}</div>
          <div class="privacy__title">{status.title}</div>
          <div class="privacy__desc">{status.detail}</div>
          <div class="privacy__meta">
            <span class="badge">{modeLabel}</span>
            {#if active?.endpoint}
              <span class="badge">{active.endpoint.protocol.toUpperCase()} · {active.endpoint.name}</span>
            {:else if mode === 'off'}
              <span class="badge">{t('dns.systemResolver')}</span>
            {/if}
            {#if resolverMissing}
              <span class="badge badge--warning">{t('dns.notRunning')}</span>
            {/if}
          </div>
        </div>
        <div class="privacy__switch">
          <span class="privacy__switch-label">{t('dns.useEncrypted')}</span>
          <Toggle
            checked={mode !== 'off'}
            label={t('dns.useEncrypted')}
            onchange={toggleEncryption}
          />
        </div>
      </div>

      <div class="privacy__divider"></div>

      <div class="privacy__choice">
        <div class="privacy__choice-main">
          <div class="row__title">{t('dns.providerChoice')}</div>
          <div class="row__desc">{t('dns.providerChoiceDesc')}</div>
        </div>
        <select
          class="select privacy__select"
          value={simpleChoice}
          disabled={mode === 'off'}
          aria-label={t('dns.providerChoice')}
          onchange={applySimpleChoice}
        >
          <option value="auto">{t('dns.autoChoice')}</option>
          {#if simpleChoice === 'current'}
            <option value="current">{simpleProviderName}</option>
          {/if}
          {#each quickProviders as provider (provider.id)}
            <option value={provider.id}>{provider.provider ?? provider.id} · {localized(provider.label)}</option>
          {/each}
        </select>
      </div>

      {#if resolverMissing}
        <div class="callout callout--warning privacy__notice">
          <span class="callout__icon"><Icon name="warning" size={14} /></span>
          <span>{t('dns.status.resolverDownHint')}</span>
        </div>
      {/if}

      <div class="privacy__footer">
        <span class="privacy__hint">{t('dns.advancedHint')}</span>
        <button class="button button--ghost button--sm" onclick={openAdvanced}>
          {t('dns.advanced')}
          <Icon name="chevron" size={11} />
        </button>
      </div>
    </section>

    {#if hasPendingChanges}
      <section class="card pending-card">
        <div class="pending">
          <span class="pending__icon"><Icon name="refresh" size={15} /></span>
          <div class="pending__main">
            <div class="pending__title">{t('dns.pendingTitle')}</div>
            <div class="pending__desc">
              {restartRequired ? t('dns.pendingKernelHint') : t('dns.pendingResolverHint')}
            </div>
          </div>
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
      </section>
    {/if}
  {/if}
</div>

<style>
  .status__hero {
    display: flex;
    align-items: flex-start;
    gap: 13px;
    padding: 18px 18px 15px;
  }

  .status__icon {
    width: 42px;
    height: 42px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 13px;
    color: var(--text-secondary);
    background: var(--bg-sunken);
  }

  .status__icon--success {
    color: var(--success);
    background: var(--success-soft);
  }

  .status__icon--warning {
    color: var(--warning);
    background: var(--warning-soft);
  }

  .status__main {
    flex: 1;
    min-width: 0;
  }

  .status__eyebrow {
    font-size: 10.5px;
    font-weight: 650;
    letter-spacing: 0.04em;
    text-transform: uppercase;
    color: var(--text-muted);
  }

  .status__title {
    margin-top: 2px;
    font-size: 16px;
    font-weight: 650;
    letter-spacing: -0.01em;
  }

  .status__detail {
    margin-top: 2px;
    font-size: 12px;
    line-height: 1.5;
    color: var(--text-muted);
  }

  .pill {
    flex: none;
    display: inline-flex;
    align-items: center;
    gap: 5px;
    height: 22px;
    padding: 0 8px;
    border-radius: var(--radius-pill);
    font-size: 11px;
    font-weight: 550;
    color: var(--text-secondary);
    background: var(--bg-sunken);
    white-space: nowrap;
  }

  .pill--success {
    color: var(--success);
    background: var(--success-soft);
  }

  .pill--warning {
    color: var(--warning);
    background: var(--warning-soft);
  }

  .metrics {
    display: grid;
    grid-template-columns: repeat(4, minmax(0, 1fr));
    gap: 1px;
    margin: 0 18px 16px;
    border-radius: var(--radius-sm);
    overflow: hidden;
    background: var(--border);
    border: 1px solid var(--border);
  }

  .metric {
    display: flex;
    flex-direction: column;
    gap: 2px;
    min-width: 0;
    padding: 10px 12px;
    background: var(--bg-sunken);
  }

  .metric__label {
    font-size: 10px;
    font-weight: 550;
    color: var(--text-muted);
  }

  .metric__value {
    font-size: 12px;
    font-weight: 550;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .metric__value--muted {
    color: var(--text-muted);
  }

  .metric__hint {
    font-size: 10px;
    color: var(--text-muted);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .status__note {
    margin: 0 18px 12px;
    font-size: 11px;
    color: var(--text-muted);
  }

  .status__notice {
    margin: 0 18px 14px;
  }

  .pending {
    display: flex;
    align-items: flex-start;
    gap: 10px;
    margin: 0 18px 16px;
    padding: 12px;
    border-radius: var(--radius-md);
    background: var(--warning-soft);
    color: var(--warning);
  }

  .pending__icon {
    flex: none;
    margin-top: 1px;
  }

  .pending__main {
    flex: 1;
    min-width: 0;
  }

  .pending__title {
    font-size: 12.5px;
    font-weight: 600;
  }

  .pending__desc {
    margin-top: 2px;
    font-size: 11.5px;
    line-height: 1.5;
    color: var(--text-secondary);
  }

  .pending__delta {
    display: flex;
    flex-wrap: wrap;
    gap: 5px;
    margin-top: 6px;
    font-size: 11px;
    color: var(--text-secondary);
  }

  .pending__delta code {
    font-family: var(--font-mono);
    font-size: 10.5px;
    padding: 1px 5px;
    border-radius: 4px;
    background: var(--bg-hover);
    color: var(--warning);
  }

  .pending__sep {
    color: var(--text-muted);
  }

  .modes {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 8px;
  }

  .mode {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    gap: 3px;
    padding: 12px 13px;
    border-radius: var(--radius-sm);
    border: 1px solid var(--border);
    background: var(--surface);
    text-align: left;
    transition:
      border-color var(--fast) var(--ease),
      background var(--fast) var(--ease),
      box-shadow var(--fast) var(--ease);
  }

  .mode:hover {
    border-color: var(--border-strong);
    background: var(--bg-hover);
  }

  .mode--active {
    border-color: var(--accent);
    background: var(--accent-soft);
    box-shadow: 0 0 0 1px var(--accent);
  }

  .mode__top {
    width: 100%;
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 5px;
  }

  .mode__icon {
    width: 28px;
    height: 28px;
    display: grid;
    place-items: center;
    border-radius: 8px;
    color: var(--text-secondary);
    background: var(--bg-sunken);
  }

  .mode--active .mode__icon {
    color: var(--accent);
    background: var(--surface);
  }

  .mode__radio {
    width: 17px;
    height: 17px;
    display: grid;
    place-items: center;
    border-radius: 50%;
    border: 1px solid var(--border-strong);
    color: #fff;
  }

  .mode__radio--on {
    border-color: var(--accent);
    background: var(--accent);
  }

  .mode__title {
    font-size: 12.5px;
    font-weight: 600;
  }

  .mode__desc {
    font-size: 11.5px;
    line-height: 1.45;
    color: var(--text-muted);
  }

  .card__main {
    flex: 1;
    min-width: 0;
  }

  .chips {
    display: flex;
    flex-wrap: wrap;
    gap: 5px;
    margin-bottom: 12px;
  }

  .providers--loading {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(228px, 1fr));
    gap: 10px;
  }

  .provider-skeleton {
    height: 86px;
    border-radius: var(--radius-md);
    background: linear-gradient(90deg, var(--bg-sunken), var(--bg-hover), var(--bg-sunken));
    background-size: 200% 100%;
    animation: shimmer 1.4s linear infinite;
  }

  @keyframes shimmer {
    from {
      background-position: 200% 0;
    }
    to {
      background-position: -200% 0;
    }
  }

  .provider {
    padding-right: 34px;
  }

  .provider__check {
    position: absolute;
    top: 12px;
    right: 12px;
    width: 17px;
    height: 17px;
    display: grid;
    place-items: center;
    border-radius: 50%;
    border: 1px solid var(--border-strong);
    color: #fff;
  }

  .provider__check--on {
    border-color: var(--accent);
    background: var(--accent);
  }

  .custom-list {
    display: flex;
    flex-direction: column;
    margin-bottom: 4px;
  }

  .custom-row {
    display: flex;
    align-items: center;
    gap: 11px;
    padding: 11px 0;
  }

  .custom-row + .custom-row {
    border-top: 1px solid var(--border);
  }

  .custom-row__icon {
    width: 30px;
    height: 30px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 9px;
    color: var(--text-secondary);
    background: var(--bg-sunken);
  }

  .custom-row__main {
    flex: 1;
    min-width: 0;
  }

  .custom-row__title {
    display: flex;
    align-items: center;
    gap: 7px;
    font-size: 12.5px;
    font-weight: 550;
  }

  .custom-row__address {
    margin-top: 1px;
    font-size: 10.5px;
    color: var(--text-muted);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .custom-row__control {
    flex: none;
    display: flex;
    align-items: center;
    gap: 6px;
  }

  .form-panel {
    display: flex;
    flex-direction: column;
    gap: 10px;
    margin-top: 12px;
    padding: 13px;
    border-radius: var(--radius-sm);
    background: var(--bg-sunken);
    border: 1px solid var(--border);
  }

  .form-panel__title {
    font-size: 12px;
    font-weight: 600;
    color: var(--text-secondary);
  }

  .form-panel__row {
    display: flex;
    align-items: flex-end;
    gap: 8px;
  }

  .field {
    display: flex;
    flex-direction: column;
    gap: 5px;
  }

  .field--grow {
    flex: 1;
    min-width: 0;
  }

  .field__label {
    font-size: 11px;
    font-weight: 550;
    color: var(--text-muted);
  }

  .form-panel__error {
    margin-top: 2px;
  }

  @media (max-width: 720px) {
    .status__hero {
      flex-wrap: wrap;
    }

    .metrics {
      grid-template-columns: repeat(2, minmax(0, 1fr));
    }

    .modes {
      grid-template-columns: 1fr;
    }

    .pending {
      flex-wrap: wrap;
    }
  }

  .privacy__row {
    display: flex;
    align-items: flex-start;
    gap: 13px;
    padding: 18px;
  }

  .privacy__icon {
    width: 42px;
    height: 42px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 13px;
    color: var(--text-secondary);
    background: var(--bg-sunken);
  }

  .privacy__icon--success {
    color: var(--success);
    background: var(--success-soft);
  }

  .privacy__icon--warning {
    color: var(--warning);
    background: var(--warning-soft);
  }

  .privacy__main {
    flex: 1;
    min-width: 0;
  }

  .privacy__eyebrow {
    font-size: 10.5px;
    font-weight: 650;
    letter-spacing: 0.04em;
    text-transform: uppercase;
    color: var(--text-muted);
  }

  .privacy__title {
    margin-top: 2px;
    font-size: 16px;
    font-weight: 650;
    letter-spacing: -0.01em;
  }

  .privacy__desc {
    margin-top: 2px;
    font-size: 12.5px;
    line-height: 1.5;
    color: var(--text-muted);
  }

  .privacy__meta {
    display: flex;
    flex-wrap: wrap;
    gap: 5px;
    margin-top: 9px;
  }

  .privacy__divider {
    height: 1px;
    margin: 0 18px;
    background: var(--border);
  }

  .privacy__choice {
    display: flex;
    align-items: center;
    gap: 16px;
    padding: 15px 18px;
  }

  .privacy__choice-main {
    flex: 1;
    min-width: 0;
  }

  .privacy__select {
    flex: none;
    width: 230px;
    max-width: 50%;
  }

  .privacy__notice {
    margin: 0 18px 14px;
  }

  .privacy__footer {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 12px 18px 16px;
    border-top: 1px solid var(--border);
  }

  .privacy__hint {
    flex: 1;
    font-size: 11px;
    color: var(--text-muted);
  }

  .pending-card {
    padding: 0;
  }

  .pending-card .pending {
    margin: 0;
  }



  .privacy__switch {
    flex: none;
    display: flex;
    flex-direction: column;
    align-items: flex-end;
    gap: 5px;
  }

  .privacy__switch-label {
    font-size: 10.5px;
    color: var(--text-muted);
    white-space: nowrap;
  }


</style>
