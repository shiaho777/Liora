<script lang="ts">
  import Icon from '../Icon.svelte';
  import { call } from '../bridge';
  import { app, toast } from '../state.svelte';
  import { value, set } from '../settings-store.svelte';
  import { cefReadiness } from '../kernel-signing';
  import { t } from '../i18n.svelte';

  interface Kernel {
    id: string;
    name: string;
    version: string;
    component: string;
    caps: string[];
    available: boolean;
    netNeedsRestart: boolean;
  }

  interface Component {
    id: string;
    name: string;
    kind: string;
    description: string;
    installedVersion?: string;
  }

  let kernels = $state<Kernel[]>([]);
  let components = $state<Component[]>([]);
  let loading = $state(true);
  let busy = $state('');

  const selected = $derived(String(value('kernel.default') ?? 'system'));
  const rows = $derived(kernels.filter((kernel) => app.mode === 'advanced' || kernel.id === 'system' || kernel.available));
  // Chromium validates helpers against the app's Team ID, so an ad-hoc build
  // cannot run the CEF kernel even when the component is installed.
  const cefState = $derived(cefReadiness(app.runtime.codeSigning, app.runtime.cefStatus));

  async function load(): Promise<void> {
    loading = true;
    try {
      kernels = await call<Kernel[]>('kernels.list');
      components = await call<Component[]>('components.catalog');
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    } finally {
      loading = false;
    }
  }

  $effect(() => {
    void load();
  });

  async function choose(kernel: Kernel): Promise<void> {
    if (!kernel.available) return;
    busy = kernel.id;
    try {
      const result = await call<{ restartRequired: boolean }>('kernels.select', { id: kernel.id });
      await set('kernel.default', kernel.id);
      toast(t('settings.changed'));
      if (result.restartRequired) toast(t('kernels.restart'));
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    } finally {
      busy = '';
    }
  }

  function capLabel(cap: string): string {
    return t(`kernels.cap.${cap}`);
  }
</script>

<div class="section">
  <header class="settings__header">
    <h1 class="settings__title">{t('kernels.title')}</h1>
    <p class="settings__lede">{t('kernels.lede')}</p>
  </header>

  {#if app.runtime.codeSigning || app.runtime.cefStatus}
    <div class="kernel-status">
      <span>{t('kernels.signing')}</span>
      <span class="mono">{app.runtime.codeSigning || t('kernels.unknown')}</span>
      <span class="muted">·</span>
      <span>CEF</span>
      <span class="mono">{app.runtime.cefStatus || t('kernels.unknown')}</span>
    </div>
  {/if}

  {#if loading}
    <div class="empty">…</div>
  {:else}
    {#each rows as kernel (kernel.id)}
      <div class="card">
        <div class="card__body">
          <div class="row">
            <div class="row__main">
              <div class="row__title">
                {kernel.name}
                {#if selected === kernel.id}
                  <span class="badge badge--accent">{t('kernels.default')}</span>
                {/if}
                {#if !kernel.available}
                  <span class="badge">{t('kernels.notInstalled')}</span>
                {/if}
              </div>
              <div class="row__desc">
                {#if kernel.version}v{kernel.version}{/if}
                {#if kernel.component} · {kernel.component}{/if}
              </div>
              <div class="rule__meta">
                {#each kernel.caps as cap (cap)}
                  <span class="badge">{capLabel(cap)}</span>
                {/each}
              </div>
            </div>
            <div class="row__control">
              {#if kernel.available}
                <button
                  class="button"
                  class:button--primary={selected !== kernel.id}
                  disabled={selected === kernel.id || busy === kernel.id}
                  onclick={() => choose(kernel)}
                >
                  {selected === kernel.id ? t('kernels.installed') : t('kernels.default')}
                </button>
              {:else}
                <button class="button button--ghost" onclick={() => toast(t('kernels.notInstalled'))}>
                  <Icon name="download" size={13} />
                  {t('kernels.download')}
                </button>
              {/if}
            </div>
          </div>
          {#if kernel.id === 'cef' && cefState.reason === 'failed'}
            <div class="callout callout--danger" style="margin-top: 10px">
              <span class="callout__icon"><Icon name="warning" size={14} /></span>
              <span>
                {t('kernels.cefFailed')}
                <span class="mono">{app.runtime.cefStatus}</span>
              </span>
            </div>
          {:else if kernel.id === 'cef' && cefState.reason === 'ad-hoc'}
            <div class="callout callout--warning" style="margin-top: 10px">
              <span class="callout__icon"><Icon name="warning" size={14} /></span>
              <span>
                {t('kernels.adHocWarning')}
                <span class="mono">{app.runtime.codeSigning}</span>
              </span>
            </div>
          {/if}
        </div>
      </div>
    {/each}

    {#if app.mode === 'advanced' && components.length > 0}
      <div class="card">
        <div class="card__header">
          <div>
            <div class="card__title">{t('kernels.available')}</div>
            <div class="card__subtitle">{t('kernels.lede')}</div>
          </div>
        </div>
        <div class="card__body">
          {#each components as component (component.id)}
            <div class="row">
              <div class="row__main">
                <div class="row__title">{component.name}</div>
                <div class="row__desc">{component.description}</div>
              </div>
              <div class="row__control">
                <span class="badge" class:badge--success={!!component.installedVersion}>
                  {component.installedVersion ?? t('kernels.notInstalled')}
                </span>
              </div>
            </div>
          {/each}
        </div>
      </div>
    {/if}
  {/if}
</div>
