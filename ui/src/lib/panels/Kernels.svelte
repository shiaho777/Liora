<script lang="ts">
  import Icon from '../Icon.svelte';
  import Toggle from '../Toggle.svelte';
  import { call, notify, onNotification } from '../bridge';
  import { app, toast } from '../state.svelte';
  import { value, set } from '../settings-store.svelte';
  import { cefNeedsConfirmation, cefReadiness } from '../kernel-signing';
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
  let updates = $state<string[]>([]);
  let updating = $state('');
  let installing = $state('');
  let installingComponent = $state('');
  let catalogUrl = $state(String(value('components.catalogUrl') ?? ''));
  let autoUpdate = $state(value('components.autoUpdate') !== false);
  let catalogBusy = $state(false);
  let loading = $state(true);
  let busy = $state('');
  // CEF actions on an ad-hoc build need an explicit acknowledgement first.
  let pending = $state<{ kind: 'download' | 'select'; kernelId: string } | null>(null);

  const selected = $derived(String(value('kernel.default') ?? 'system'));
  const rows = $derived(
    kernels.filter(
      (kernel) => app.mode === 'advanced' || kernel.id === 'system' || kernel.available,
    ),
  );
  // Chromium validates helpers against the app's Team ID, so an ad-hoc build
  // cannot run the CEF kernel even when the component is installed.
  const cefState = $derived(cefReadiness(app.runtime.codeSigning, app.runtime.cefStatus));
  const compat = $derived(value('kernel.cef.singleProcess') === true);

  const signature = $derived.by(() => {
    const raw = (app.runtime.codeSigning ?? '').trim();
    if (!raw) {
      return {
        tone: 'muted',
        label: t('kernels.unknown'),
        detail: t('kernels.signatureUnknown'),
        raw: '',
      };
    }
    if (/ad-?hoc|unsigned/i.test(raw)) {
      return {
        tone: 'warning',
        label: t('kernels.signatureAdHoc'),
        detail: t('kernels.signatureAdHocHint'),
        raw,
      };
    }
    if (/developer id/i.test(raw)) {
      return {
        tone: 'success',
        label: t('kernels.signatureDeveloperId'),
        detail: t('kernels.signatureDeveloperIdHint'),
        raw,
      };
    }
    return { tone: 'muted', label: raw, detail: '', raw: '' };
  });

  const helper = $derived.by(() => {
    const raw = (app.runtime.cefStatus ?? '').trim();
    if (!raw) {
      return { tone: 'muted', label: t('kernels.helperUnknown'), detail: '', raw: '' };
    }
    if (/^ready/i.test(raw)) {
      return { tone: 'success', label: t('kernels.helperReady'), detail: '', raw };
    }
    if (/^failed/i.test(raw)) {
      return { tone: 'danger', label: t('kernels.helperFailed'), detail: '', raw };
    }
    if (/not selected|not-selected/i.test(raw)) {
      return { tone: 'muted', label: t('kernels.helperNotSelected'), detail: '', raw: '' };
    }
    return { tone: 'muted', label: raw, detail: '', raw: '' };
  });

  async function load(): Promise<void> {
    loading = true;
    try {
      const [kernelList, componentList, updateList] = await Promise.all([
        call<Kernel[]>('kernels.list'),
        call<Component[]>('components.catalog'),
        call<string[]>('components.updates'),
      ]);
      kernels = kernelList;
      components = componentList;
      updates = Array.isArray(updateList) ? updateList : [];
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    } finally {
      loading = false;
    }
  }

  $effect(() => {
    void load();
  });

  function requestDownload(kernel: Kernel): void {
    if (cefNeedsConfirmation(kernel.id, cefState)) {
      pending = { kind: 'download', kernelId: kernel.id };
      return;
    }
    downloadKernel(kernel);
  }

  function requestSelect(kernel: Kernel): void {
    if (cefNeedsConfirmation(kernel.id, cefState)) {
      pending = { kind: 'select', kernelId: kernel.id };
      return;
    }
    void choose(kernel);
  }

  function downloadKernel(kernel: Kernel): void {
    if (!kernel.component) {
      toast(t('kernels.noComponent'), 'error');
      return;
    }
    installing = kernel.id;
    // The shell plans, downloads, unpacks and commits; Core verifies hashes.
    notify('ui.installComponent', { id: kernel.component, version: '' });
  }

  $effect(() => {
    return onNotification((message) => {
      if (message.method === 'browser.componentInstalled') {
        const params = (message.params ?? {}) as { id?: string; version?: string };
        const kernel = kernels.find((item) => item.component === params.id);
        const component = components.find((item) => item.id === params.id);
        installing = '';
        updating = '';
        installingComponent = '';
        toast(
          t('kernels.installDone', {
            name: kernel?.name ?? component?.name ?? params.id ?? '',
          }),
        );
        void load();
        return;
      }
      if (message.method === 'browser.componentFailed') {
        const params = (message.params ?? {}) as { message?: string };
        installing = '';
        updating = '';
        installingComponent = '';
        toast(params.message || t('kernels.installFailed'), 'error');
        void load();
        return;
      }
      if (message.method === 'browser.catalogUpdated') {
        const params = (message.params ?? {}) as { count?: number };
        catalogBusy = false;
        toast(t('kernels.catalogUpdated', { count: params.count ?? 0 }));
        void load();
        return;
      }
      if (message.method === 'browser.catalogFailed') {
        const params = (message.params ?? {}) as { message?: string };
        catalogBusy = false;
        toast(params.message || t('kernels.catalogFailed'), 'error');
        void load();
        return;
      }
      if (message.method === 'components.changed') {
        void load();
      }
    });
  });

  function confirmPending(): void {
    const entry = pending;
    pending = null;
    if (!entry) return;
    const kernel = kernels.find((item) => item.id === entry.kernelId);
    if (!kernel) return;
    if (entry.kind === 'download') downloadKernel(kernel);
    else void choose(kernel);
  }

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

  async function toggleCompat(): Promise<void> {
    busy = 'compat';
    try {
      await set('kernel.cef.singleProcess', !compat);
      toast(t('kernels.compatSaved'));
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    } finally {
      busy = '';
    }
  }

  async function toggleAutoUpdate(): Promise<void> {
    autoUpdate = !autoUpdate;
    try {
      await set('components.autoUpdate', autoUpdate);
    } catch (error: any) {
      autoUpdate = !autoUpdate;
      toast(String(error?.message ?? error), 'error');
    }
  }

  function capLabel(cap: string): string {
    return t(`kernels.cap.${cap}`);
  }

  function updateAvailable(component: Component): boolean {
    return Boolean(component.installedVersion) && updates.includes(component.id);
  }

  async function refreshCatalog(): Promise<void> {
    const url = catalogUrl.trim();
    if (!url) {
      toast(t('kernels.catalogUrlMissing'), 'error');
      return;
    }
    catalogBusy = true;
    try {
      await set('components.catalogUrl', url);
      notify('ui.refreshCatalog', { url });
    } catch (error: any) {
      catalogBusy = false;
      toast(String(error?.message ?? error), 'error');
    }
  }

  function updateComponent(component: Component): void {
    if (updating) return;
    updating = component.id;
    notify('ui.installComponent', { id: component.id, version: '' });
  }

  function installComponent(component: Component): void {
    if (installingComponent) return;
    installingComponent = component.id;
    notify('ui.installComponent', { id: component.id, version: '' });
  }

  function installedComponentVersion(kernel: Kernel): string {
    if (!kernel.component) return '';
    return components.find((item) => item.id === kernel.component)?.installedVersion ?? '';
  }

  function kernelDescription(kernel: Kernel): string {
    return kernel.id === 'system' ? t('kernels.systemDesc') : t('kernels.cefDesc');
  }

  function kernelKind(kernel: Kernel): string {
    return kernel.id === 'system' ? t('kernels.builtIn') : t('kernels.componentKind');
  }
</script>

<div class="section">
  <header class="settings__header">
    <h1 class="settings__title">{t('kernels.title')}</h1>
    <p class="settings__lede">{t('kernels.lede')}</p>
  </header>

  <section class="card runtime">
    <div class="card__header">
      <div>
        <div class="card__title">{t('kernels.runtime')}</div>
        <div class="card__subtitle">{t('kernels.runtimeDesc')}</div>
      </div>
    </div>
    <div class="card__body runtime__body">
      <div class="runtime__row">
        <span class="runtime__icon"><Icon name="shield" size={15} /></span>
        <div class="runtime__main">
          <div class="runtime__label">{t('kernels.signature')}</div>
          <div class="runtime__detail">{signature.detail}</div>
          {#if signature.raw && signature.raw !== signature.label}
            <div class="runtime__code mono">{signature.raw}</div>
          {/if}
        </div>
        <span class="pill" class:pill--success={signature.tone === 'success'} class:pill--warning={signature.tone === 'warning'}>
          <span class="dot"></span>
          {signature.label}
        </span>
      </div>
      <div class="runtime__row">
        <span class="runtime__icon"><Icon name="puzzle" size={15} /></span>
        <div class="runtime__main">
          <div class="runtime__label">{t('kernels.helper')}</div>
          <div class="runtime__detail">
            {helper.tone === 'success'
              ? t('kernels.helperReadyHint')
              : helper.tone === 'danger'
                ? t('kernels.helperFailedHint')
                : t('kernels.helperIdleHint')}
          </div>
          {#if helper.raw && helper.tone === 'danger'}
            <div class="runtime__code mono">{helper.raw}</div>
          {/if}
        </div>
        <span class="pill" class:pill--success={helper.tone === 'success'} class:pill--danger={helper.tone === 'danger'}>
          <span class="dot"></span>
          {helper.label}
        </span>
      </div>
    </div>
  </section>

  {#if loading}
    <section class="card loading-card" aria-busy="true">
      <span class="skeleton skeleton--title"></span>
      <span class="skeleton skeleton--line"></span>
      <span class="skeleton skeleton--line skeleton--short"></span>
    </section>
  {:else}
    {#each rows as kernel (kernel.id)}
      <article class="card kernel" class:kernel--active={selected === kernel.id}>
        <div class="kernel__body">
          <div class="kernel__head">
            <span class="kernel__icon" class:kernel__icon--accent={kernel.id !== 'system'}>
              <Icon name={kernel.id === 'system' ? 'globe' : 'puzzle'} size={18} />
            </span>
            <div class="kernel__identity">
              <div class="kernel__name">
                {kernel.name}
                {#if !kernel.available}
                  <span class="badge">{t('kernels.notInstalled')}</span>
                {/if}
              </div>
              <div class="kernel__meta">
                {kernelKind(kernel)}
                {#if kernel.version}· v{kernel.version}{/if}
                {#if kernel.component}· {kernel.component}{/if}
              </div>
            </div>
            <div class="kernel__action">
              {#if kernel.available}
                {#if selected === kernel.id}
                  <span class="badge badge--accent">{t('kernels.default')}</span>
                {:else}
                  <button
                    class="button button--primary button--sm"
                    disabled={busy === kernel.id}
                    onclick={() => requestSelect(kernel)}
                  >
                    {t('kernels.makeDefault')}
                  </button>
                {/if}
              {:else}
                <button
                  class="button button--ghost button--sm"
                  disabled={installing === kernel.id}
                  onclick={() => requestDownload(kernel)}
                >
                  <Icon name="download" size={13} />
                  {installing === kernel.id ? t('kernels.installing') : t('kernels.download')}
                </button>
              {/if}
            </div>
          </div>

          <p class="kernel__desc">{kernelDescription(kernel)}</p>

          <div class="kernel__caps">
            {#each kernel.caps as cap (cap)}
              <span class="badge">{capLabel(cap)}</span>
            {/each}
          </div>

          {#if kernel.id === 'cef' && cefState.reason === 'failed'}
            <div class="callout callout--danger kernel__notice">
              <span class="callout__icon"><Icon name="warning" size={14} /></span>
              <span>
                {t('kernels.cefFailed')}
                <span class="mono">{app.runtime.cefStatus}</span>
              </span>
            </div>
          {:else if kernel.id === 'cef' && cefState.reason === 'ad-hoc'}
            <div class="callout callout--warning kernel__notice">
              <span class="callout__icon"><Icon name="warning" size={14} /></span>
              <span>{t('kernels.adHocWarning')}</span>
            </div>
          {/if}

          {#if !kernel.available && installedComponentVersion(kernel)}
            <div class="callout kernel__notice">
              <span class="callout__icon"><Icon name="info" size={14} /></span>
              <span>
                {t('kernels.componentInstalledUnavailable', {
                  version: installedComponentVersion(kernel),
                })}
              </span>
            </div>
          {/if}

          {#if kernel.id === 'cef'}
            <div class="kernel__setting">
              <div class="kernel__setting-main">
                <div class="kernel__setting-title">{t('kernels.compatTitle')}</div>
                <div class="kernel__setting-desc">{t('kernels.compatBody')}</div>
              </div>
              <Toggle
                checked={compat}
                label={t('kernels.compatTitle')}
                disabled={busy === 'compat'}
                onchange={toggleCompat}
              />
            </div>
          {/if}

          {#if pending?.kernelId === kernel.id}
            <div class="confirm">
              <div class="confirm__icon"><Icon name="warning" size={15} /></div>
              <div class="confirm__main">
                <div class="confirm__title">
                  {pending.kind === 'download'
                    ? t('kernels.confirmDownloadTitle')
                    : t('kernels.confirmSelectTitle')}
                </div>
                <p class="confirm__body">
                  {pending.kind === 'download'
                    ? t('kernels.confirmDownloadBody')
                    : t('kernels.confirmSelectBody')}
                </p>
                <div class="cluster">
                  <button class="button button--ghost button--sm" onclick={() => (pending = null)}>
                    {pending.kind === 'select' ? t('kernels.keepSystem') : t('kernels.cancel')}
                  </button>
                  <button class="button button--primary button--sm" onclick={confirmPending}>
                    {pending.kind === 'download'
                      ? t('kernels.confirmDownloadAction')
                      : t('kernels.confirmSelectAction')}
                  </button>
                </div>
              </div>
            </div>
          {/if}
        </div>
      </article>
    {/each}

    {#if app.mode === 'advanced'}
      <section class="card catalog">
        <div class="card__header">
          <div class="card__main">
            <div class="card__title">{t('kernels.catalogTitle')}</div>
            <div class="card__subtitle">{t('kernels.catalogDesc')}</div>
          </div>
          <button
            class="button button--sm catalog-refresh"
            disabled={catalogBusy}
            onclick={refreshCatalog}
          >
            <Icon name="refresh" size={13} />
            {catalogBusy ? t('kernels.catalogChecking') : t('kernels.catalogCheck')}
          </button>
        </div>

        <div class="card__body">
          <label class="field">
            <span class="field__label">{t('kernels.catalogSource')}</span>
            <input
              class="input input--mono"
              bind:value={catalogUrl}
              placeholder="https://…/components.json"
              spellcheck="false"
            />
          </label>
          <p class="field__hint">{t('kernels.catalogHint')}</p>

          <div class="setting-line">
            <div class="setting-line__main">
              <div class="setting-line__title">{t('kernels.autoUpdateTitle')}</div>
              <div class="setting-line__desc">{t('kernels.autoUpdateDesc')}</div>
            </div>
            <Toggle
              checked={autoUpdate}
              label={t('kernels.autoUpdateTitle')}
              onchange={toggleAutoUpdate}
            />
          </div>

          <div class="divider catalog__divider"></div>

          {#if components.length === 0}
            <div class="empty-state empty-state--compact">
              <span class="empty-state__icon"><Icon name="puzzle" size={18} /></span>
              <div class="empty-state__title">{t('kernels.catalogEmptyTitle')}</div>
              <p class="empty-state__body">{t('kernels.catalogEmptyBody')}</p>
            </div>
          {:else}
            <div class="component-list">
              {#each components as component (component.id)}
                <div class="component-row">
                  <span class="component-row__icon"><Icon name="puzzle" size={15} /></span>
                  <div class="component-row__main">
                    <div class="component-row__title">
                      {component.name}
                      {#if updateAvailable(component)}
                        <span class="badge badge--accent">{t('kernels.updateAvailable')}</span>
                      {/if}
                    </div>
                    <div class="component-row__desc">{component.description}</div>
                  </div>
                  <div class="component-row__control">
                    {#if component.installedVersion}
                      <span class="badge badge--success">v{component.installedVersion}</span>
                      {#if updateAvailable(component)}
                        <button
                          class="button button--primary button--sm component-update"
                          disabled={updating === component.id}
                          onclick={() => updateComponent(component)}
                        >
                          {updating === component.id ? t('kernels.updating') : t('kernels.update')}
                        </button>
                      {/if}
                    {:else}
                      <button
                        class="button button--sm"
                        disabled={installingComponent === component.id}
                        onclick={() => installComponent(component)}
                      >
                        <Icon name="download" size={13} />
                        {installingComponent === component.id
                          ? t('kernels.installing')
                          : t('kernels.install')}
                      </button>
                    {/if}
                  </div>
                </div>
              {/each}
            </div>
          {/if}
        </div>
      </section>
    {/if}
  {/if}
</div>

<style>
  .runtime__body {
    display: flex;
    flex-direction: column;
    padding-top: 6px;
  }

  .runtime__row {
    display: flex;
    align-items: flex-start;
    gap: 11px;
    padding: 11px 0;
  }

  .runtime__row + .runtime__row {
    border-top: 1px solid var(--border);
  }

  .runtime__icon {
    width: 30px;
    height: 30px;
    flex: none;
    display: grid;
    place-items: center;
    margin-top: 1px;
    border-radius: 9px;
    color: var(--text-secondary);
    background: var(--bg-sunken);
  }

  .runtime__main {
    flex: 1;
    min-width: 0;
  }

  .runtime__label {
    font-size: 12.5px;
    font-weight: 550;
  }

  .runtime__detail {
    margin-top: 2px;
    font-size: 11.5px;
    line-height: 1.45;
    color: var(--text-muted);
  }

  .runtime__code {
    margin-top: 5px;
    font-size: 10.5px;
    color: var(--text-muted);
    word-break: break-all;
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

  .pill--danger {
    color: var(--danger);
    background: var(--danger-soft);
  }

  .kernel {
    overflow: hidden;
  }

  .kernel__body {
    padding: 16px 18px;
  }

  .kernel__head {
    display: flex;
    align-items: flex-start;
    gap: 12px;
  }

  .kernel__icon {
    width: 38px;
    height: 38px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 11px;
    color: var(--text-secondary);
    background: var(--bg-sunken);
  }

  .kernel__icon--accent {
    color: var(--accent);
    background: var(--accent-soft);
  }

  .kernel__identity {
    flex: 1;
    min-width: 0;
  }

  .kernel__name {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 7px;
    font-size: 14px;
    font-weight: 600;
    letter-spacing: -0.005em;
  }

  .kernel__meta {
    margin-top: 2px;
    font-family: var(--font-mono);
    font-size: 10.5px;
    color: var(--text-muted);
  }

  .kernel__action {
    flex: none;
    display: flex;
    align-items: center;
    gap: 8px;
    padding-top: 2px;
  }

  .kernel__desc {
    margin: 10px 0 0;
    font-size: 12px;
    line-height: 1.5;
    color: var(--text-secondary);
    max-width: 66ch;
  }

  .kernel__caps {
    display: flex;
    flex-wrap: wrap;
    gap: 5px;
    margin-top: 10px;
  }

  .kernel__notice {
    margin-top: 12px;
  }

  .kernel__setting {
    display: flex;
    align-items: center;
    gap: 16px;
    margin-top: 14px;
    padding-top: 13px;
    border-top: 1px solid var(--border);
  }

  .kernel__setting-main {
    flex: 1;
    min-width: 0;
  }

  .kernel__setting-title {
    font-size: 12.5px;
    font-weight: 550;
  }

  .kernel__setting-desc {
    margin-top: 2px;
    font-size: 11.5px;
    line-height: 1.45;
    color: var(--text-muted);
    max-width: 68ch;
  }

  .confirm {
    display: flex;
    gap: 10px;
    margin-top: 14px;
    padding: 12px;
    border-radius: var(--radius-md);
    background: var(--warning-soft);
    color: var(--warning);
  }

  .confirm__icon {
    flex: none;
    margin-top: 1px;
  }

  .confirm__main {
    flex: 1;
    min-width: 0;
  }

  .confirm__title {
    font-size: 12.5px;
    font-weight: 600;
  }

  .confirm__body {
    margin: 3px 0 10px;
    font-size: 11.5px;
    line-height: 1.5;
    color: var(--text-secondary);
  }

  .catalog .card__main {
    flex: 1;
    min-width: 0;
  }

  .field {
    display: flex;
    flex-direction: column;
    gap: 6px;
  }

  .field__label {
    font-size: 12px;
    font-weight: 550;
    color: var(--text-secondary);
  }

  .field__hint {
    margin: 6px 0 0;
    font-size: 11.5px;
    line-height: 1.45;
    color: var(--text-muted);
  }

  .setting-line {
    display: flex;
    align-items: center;
    gap: 16px;
    margin-top: 14px;
    padding-top: 13px;
    border-top: 1px solid var(--border);
  }

  .setting-line__main {
    flex: 1;
    min-width: 0;
  }

  .setting-line__title {
    font-size: 12.5px;
    font-weight: 550;
  }

  .setting-line__desc {
    margin-top: 2px;
    font-size: 11.5px;
    line-height: 1.45;
    color: var(--text-muted);
  }

  .catalog__divider {
    margin: 14px 0 4px;
  }

  .component-list {
    display: flex;
    flex-direction: column;
  }

  .component-row {
    display: flex;
    align-items: center;
    gap: 11px;
    padding: 11px 0;
  }

  .component-row + .component-row {
    border-top: 1px solid var(--border);
  }

  .component-row__icon {
    width: 30px;
    height: 30px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 9px;
    color: var(--accent);
    background: var(--accent-soft);
  }

  .component-row__main {
    flex: 1;
    min-width: 0;
  }

  .component-row__title {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 7px;
    font-size: 12.5px;
    font-weight: 550;
  }

  .component-row__desc {
    margin-top: 1px;
    font-size: 11.5px;
    color: var(--text-muted);
  }

  .component-row__control {
    flex: none;
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .loading-card {
    display: flex;
    flex-direction: column;
    gap: 10px;
    padding: 18px;
  }

  .skeleton {
    display: block;
    height: 12px;
    border-radius: 6px;
    background: linear-gradient(90deg, var(--bg-sunken), var(--bg-hover), var(--bg-sunken));
    background-size: 200% 100%;
    animation: shimmer 1.4s linear infinite;
  }

  .skeleton--title {
    width: 132px;
    height: 15px;
  }

  .skeleton--line {
    width: 100%;
  }

  .skeleton--short {
    width: 62%;
  }

  @keyframes shimmer {
    from {
      background-position: 200% 0;
    }
    to {
      background-position: -200% 0;
    }
  }

  @media (max-width: 640px) {
    .runtime__row,
    .kernel__head,
    .component-row {
      flex-wrap: wrap;
    }

    .kernel__action,
    .component-row__control {
      width: 100%;
      justify-content: flex-end;
    }
  }
</style>
