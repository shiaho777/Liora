<script lang="ts">
  import Icon from './Icon.svelte';
  import { app, toast } from './state.svelte';
  import { call, notify, onNotification } from './bridge';
  import { parseDiagnosticReport, type DiagnosticReport } from './diagnostic-report';
  import { explainLimit, explainSwitch } from './kernel-explanations';
  import {
    reportFreshness,
    type CurrentApplied,
    type ReportFreshness,
  } from './report-freshness';
  import {
    buildKernelSwitches,
    checkDohTemplate,
    checkForcedOrigins,
    checkProxy,
    staleItems,
    type DohTemplateCheck,
    type ForcedOriginsCheck,
    type KernelSetupLike,
    type ProxyCheck,
    type ProxyRuntimeLike,
    type StaleKind,
  } from './kernel-switch-check';
  import { i18n, t } from './i18n.svelte';
  import { networkDelta } from './network-delta';
  import { buildDiagnosticReport } from './diagnostic-report-build';

  let report = $state<DiagnosticReport | null>(null);
  let error = $state('');
  let fileName = $state('');
  let pasted = $state('');
  let pasteOpen = $state(false);
  let ruleCheck = $state<ForcedOriginsCheck | null>(null);
  let rulesError = $state('');
  let dohCheck = $state<DohTemplateCheck | null>(null);
  let dohError = $state('');
  let proxyCheck = $state<ProxyCheck | null>(null);
  let proxyError = $state('');
  let freshness = $state<ReportFreshness | null>(null);
  let savingRefresh = $state(false);
  let restarting = $state(false);
  // After a restart the report's historical switches are replaced by the ones
  // the kernel actually received, while the raw text stays untouched.
  let liveSwitches = $state<string[] | null>(null);

  const displaySwitches = $derived(liveSwitches ?? report?.switches ?? []);
  const stale = $derived(staleItems(ruleCheck, dohCheck, proxyCheck));
  const errored = $derived(
    [
      rulesError ? 'rules' : '',
      dohError ? 'dns' : '',
      proxyError ? 'proxy' : '',
    ].filter(Boolean) as StaleKind[],
  );
  const configured = $derived(
    Boolean(
      ruleCheck?.reported.length ||
        ruleCheck?.expected.length ||
        dohCheck?.reported ||
        dohCheck?.expected ||
        proxyCheck?.reported ||
        proxyCheck?.expected,
    ),
  );

  function deltaLabel(kind: string): string {
    if (kind === 'provider') return t('dns.deltaProvider');
    if (kind === 'protocol') return t('dns.deltaProtocol');
    if (kind === 'mode') return t('dns.deltaMode');
    if (kind === 'proxy') return t('dns.deltaProxy');
    return kind || t('diag.delta');
  }

  function staleLabel(kind: StaleKind): string {
    if (kind === 'rules') return t('restart.rules');
    if (kind === 'dns') return t('restart.dns');
    return t('restart.proxy');
  }

  // Turns generic switch/limit copy into statements about this report.
  const context = $derived({
    ruleName: report?.rule.matched ? report.rule.name : undefined,
    upstream: report?.upstream?.id,
  });

  // A report can arrive from the file panel, from the test hook, or from a
  // pasted text block. All of them go through the same parser.
  $effect(() => {
    return onNotification((message) => {
      if (message.method === 'browser.kernelRestarted') {
        const params = (message.params ?? {}) as {
          switches?: string[];
          dohMode?: string;
          dnsMode?: string;
          dohTemplates?: string;
          proxy?: string;
        };
        if (!report) return;
        liveSwitches = buildKernelSwitches(params);
        restarting = false;
        toast(t('restart.done'));
        void loadCrossChecks(report, liveSwitches);
        return;
      }
      if (message.method !== 'ui.filePicked') return;
      const params = (message.params ?? {}) as {
        purpose?: string;
        path?: string;
        content?: string;
        error?: string;
      };
      if (params.purpose !== 'diagnosticReport') return;
      fileName = (params.path ?? '').split('/').pop() ?? '';
      if (params.error) {
        report = null;
        error = params.error;
        return;
      }
      accept(params.content ?? '');
    });
  });

  function accept(text: string): void {
    const result = parseDiagnosticReport(text);
    if (!result.ok) {
      report = null;
      error = result.error;
      return;
    }
    report = result.report;
    error = '';
    ruleCheck = null;
    rulesError = '';
    dohCheck = null;
    dohError = '';
    proxyCheck = null;
    proxyError = '';
    freshness = null;
    restarting = false;
    liveSwitches = null;
    void loadCrossChecks(result.report);
  }

  // A running kernel keeps the switches it started with, so both the forced
  // origins and the DoH endpoint can be stale. Compare against what the
  // current rules and DNS settings would produce now.
  async function loadCrossChecks(
    parsed: DiagnosticReport,
    switchList: string[] = parsed.switches,
  ): Promise<void> {
    if (!parsed.switchesKnown) return;
    const kernel = parsed.kernel && parsed.kernel !== 'unknown' ? parsed.kernel : '';
    const [rules, plan, setup, runtime, appliedResult] = await Promise.allSettled([
      call<{
        rules: Array<{ name: string; enabled: boolean; domains: string[]; h3: string }>;
        enabled: boolean;
      }>('rules.list'),
      call<{
        mode: string;
        dohTemplates: string;
        endpoint?: { name?: string; id?: string; protocol?: string } | null;
      }>('dns.plan'),
      kernel ? call<KernelSetupLike>('kernels.setup', { id: kernel }) : Promise.resolve(null),
      call<ProxyRuntimeLike>('net.runtime'),
      call<CurrentApplied>('net.lastApplied'),
    ]);
    if (report?.raw !== parsed.raw) return;

    if (rules.status === 'fulfilled') {
      ruleCheck = checkForcedOrigins(switchList, rules.value.rules, rules.value.enabled !== false);
    } else {
      rulesError = t('stale.unavailable');
    }

    const setupValue =
      setup.status === 'fulfilled' && setup.value ? (setup.value as KernelSetupLike) : null;
    if (plan.status === 'fulfilled') {
      dohCheck = checkDohTemplate(switchList, plan.value, setupValue);
    } else if (setupValue) {
      dohCheck = checkDohTemplate(
        switchList,
        { mode: setupValue.dnsMode, dohTemplates: setupValue.dohTemplates, endpoint: null },
        setupValue,
      );
    } else {
      dohError = t('dnsStale.unavailable');
    }

    if (runtime.status === 'fulfilled') {
      proxyCheck = checkProxy(switchList, setupValue, runtime.value);
    } else if (setupValue) {
      proxyCheck = checkProxy(switchList, setupValue, null);
    } else {
      proxyError = t('proxyStale.unavailable');
    }

    // The report is a snapshot: compare its applied state and delta with what
    // is active now, so an old report cannot masquerade as current.
    const currentApplied =
      appliedResult.status === 'fulfilled' &&
      appliedResult.value &&
      typeof appliedResult.value.at === 'number' &&
      appliedResult.value.at > 0
        ? appliedResult.value
        : null;
    if (parsed.appliedKnown && plan.status === 'fulfilled') {
      freshness = reportFreshness(
        parsed.applied
          ? {
              at: parsed.applied.at,
              provider: parsed.applied.provider,
              protocol: parsed.applied.protocol,
              proxy: parsed.applied.proxy,
            }
          : null,
        parsed.delta,
        currentApplied,
        {
          provider: plan.value.endpoint?.name ?? '',
          protocol: plan.value.endpoint?.protocol ?? '',
          mode: plan.value.mode,
          proxy: setupValue?.proxy ?? '',
        },
      );
    } else {
      freshness = null;
    }
  }

  function tlsTextOf(value: DiagnosticReport): string {
    const tls = value.tls;
    if (tls.status === 'valid' || tls.status === 'invalid') {
      return `subject=${tls.subject ?? ''} valid=${tls.status === 'valid' ? 'yes' : 'no'} expires=${tls.expires ?? '-'}`;
    }
    if (tls.status === 'failed') return 'handshake failed';
    if (tls.status === 'absent') return 'not established';
    return tls.raw || 'not established';
  }

  // Rebuilds the report with the current network state while keeping the
  // failure context (URL, error, TLS, DNS result) from the saved snapshot.
  async function saveRefreshedReport(): Promise<void> {
    if (!report || savingRefresh) return;
    savingRefresh = true;
    try {
      const kernel = report.kernel ?? '';
      const [setup, installed, appliedResult, plan, resolverResult] =
        await Promise.allSettled([
        kernel
          ? call<KernelSetupLike & { switches?: string[]; unsupported?: string[] }>(
              'kernels.setup',
              { id: kernel },
            )
          : Promise.resolve(null),
        call<Array<{ id: string; version: string; current: boolean }>>('components.installed', {}),
        call<CurrentApplied>('net.lastApplied'),
        call<{ mode: string; endpoint?: { name?: string; protocol?: string } | null }>(
          'dns.plan',
        ),
        call<{ at: number; count: number }>('net.resolverStatus'),
      ]);

      const setupValue = setup.status === 'fulfilled' && setup.value ? setup.value : null;
      const currentApplied =
        appliedResult.status === 'fulfilled' &&
        appliedResult.value &&
        typeof appliedResult.value.at === 'number' &&
        appliedResult.value.at > 0
          ? appliedResult.value
          : null;
      const planValue = plan.status === 'fulfilled' ? plan.value : null;
      const resolverValue =
        resolverResult.status === 'fulfilled' &&
        resolverResult.value &&
        typeof resolverResult.value.at === 'number' &&
        resolverResult.value.count > 0
          ? resolverResult.value
          : null;
      const currentDelta = currentApplied
        ? networkDelta(
            {
              provider: currentApplied.provider,
              protocol: currentApplied.protocol,
              mode: currentApplied.mode,
              proxy: currentApplied.proxy,
            },
            {
              provider: planValue?.endpoint?.name ?? '',
              protocol: planValue?.endpoint?.protocol ?? '',
              mode: planValue?.mode ?? '',
              proxy: setupValue?.proxy ?? '',
            },
          )
        : [];

      const components =
        installed.status === 'fulfilled'
          ? installed.value.map(
              (item) => `${item.id}@${item.version}${item.current ? ' *' : ''}`,
            )
          : [];

      const content = buildDiagnosticReport({
        version: app.version,
        platform: app.platform,
        arch: app.arch,
        url: report.url ?? '',
        host: report.host ?? '',
        error: `${report.errorCode ?? 0}${report.errorReason ? ` - ${report.errorReason}` : ''}`,
        kernel: report.kernel ?? 'unknown',
        switches: setupValue ? buildKernelSwitches(setupValue) : [],
        limits: setupValue?.unsupported ?? [],
        components,
        signature: app.runtime.codeSigning || 'unknown',
        cef: app.runtime.cefStatus || 'unknown',
        resolver: resolverValue
          ? {
              at: new Date(resolverValue.at).toISOString(),
              count: resolverValue.count,
            }
          : null,
        applied: currentApplied
          ? {
              at: new Date(currentApplied.at as number).toISOString(),
              provider: currentApplied.provider ?? '',
              protocol: currentApplied.protocol ?? '',
              proxy: currentApplied.proxy ?? '',
            }
          : null,
        delta: currentDelta,
        tls: tlsTextOf(report),
        rule: report.rule.matched
          ? `${report.rule.name} (h3=${report.rule.h3 ?? 'prefer'}, ech=${report.rule.ech ?? false})`
          : 'none',
        upstream: report.upstream
          ? `${report.upstream.protocol} ${report.upstream.id} ${report.upstream.address}${
              report.upstream.elapsedMs !== undefined ? ` ${report.upstream.elapsedMs}ms` : ''
            }`
          : undefined,
        addresses: report.addresses,
        https: report.https ?? undefined,
        diagnostics: report.notes,
      });

      notify('ui.saveDiagnostic', { content });
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    } finally {
      savingRefresh = false;
    }
  }

  function formatTime(value: string): string {
    const date = new Date(value);
    return Number.isNaN(date.getTime()) ? value : date.toLocaleString();
  }

  function portOf(value: string): string {
    try {
      return new URL(value).port;
    } catch {
      return /:(\d+)\/?$/.exec(value)?.[1] ?? value;
    }
  }

  function appliedText(value: { provider?: string; protocol?: string; proxy?: string } | null): string {
    if (!value) return t('dns.deltaNone');
    const provider = value.provider || '—';
    const protocol = value.protocol ? ` (${value.protocol})` : '';
    const proxy = value.proxy ? ` · ${value.proxy}` : '';
    return `${provider}${protocol}${proxy}`;
  }

  function deltaText(items: Array<{ kind: string; from: string; to: string }>): string {
    if (items.length === 0) return t('dns.deltaNone');
    return items
      .map(
        (item) =>
          `${deltaLabel(item.kind)} ${item.from || t('dns.deltaNone')} → ${item.to || t('dns.deltaNone')}`,
      )
      .join(' · ');
  }

  function restartKernel(): void {
    if (!report || restarting) return;
    restarting = true;
    notify('ui.restartKernel', { kernel: report.kernel ?? '' });
    // Re-enable the button if the shell never answers, rather than leaving the
    // viewer stuck on "restarting".
    setTimeout(() => {
      restarting = false;
    }, 15000);
  }

  $effect(() => {
    (window as unknown as { __lioraRestartKernel?: () => void }).__lioraRestartKernel = () =>
      restartKernel();
  });

  function pickFile(): void {
    if (app.transport === 'mock') {
      // The mock backend answers ui.pickFile with a sample report.
      notify('ui.pickFile', { purpose: 'diagnosticReport' });
      return;
    }
    notify('ui.pickFile', { purpose: 'diagnosticReport' });
  }

  function parsePasted(): void {
    if (!pasted.trim()) return;
    fileName = '';
    accept(pasted);
    if (report) {
      pasteOpen = false;
      toast(t('diag.loaded'));
    }
  }

  function errorText(): string {
    if (error === 'missing-version') return t('diag.missingVersion');
    if (error === 'unsupported-version') return t('diag.unsupportedVersion');
    if (error === 'empty') return t('diag.empty');
    return error;
  }

  function tlsLabel(status: DiagnosticReport['tls']['status']): string {
    if (status === 'valid') return t('site.valid');
    if (status === 'invalid') return t('site.invalid');
    if (status === 'failed') return t('error.tlsFailed');
    if (status === 'absent') return t('error.noTls');
    return t('diag.unknown');
  }

  function tlsClass(status: DiagnosticReport['tls']['status']): string {
    if (status === 'valid') return 'badge--success';
    if (status === 'invalid' || status === 'failed') return 'badge--danger';
    if (status === 'absent') return 'badge--warning';
    return '';
  }

  function when(value: string | undefined): string {
    if (!value) return '';
    const parsed = new Date(value);
    return Number.isNaN(parsed.getTime()) ? value : parsed.toLocaleString();
  }
</script>

<div class="diag">
  {#if !report}
    <div class="diag__empty">
      <div class="diag__empty-icon"><Icon name="info" size={22} stroke={1.4} /></div>
      <div class="diag__empty-title">{t('diag.title')}</div>
      <p class="diag__empty-text">{t('diag.lede')}</p>

      <div class="cluster" style="justify-content: center">
        <button class="button button--primary" onclick={pickFile}>
          <Icon name="folder" size={13} />
          {t('diag.open')}
        </button>
        <button class="button button--ghost" onclick={() => (pasteOpen = !pasteOpen)}>
          {t('diag.paste')}
        </button>
      </div>

      {#if error}
        <div class="callout callout--danger" style:max-width="520px; margin-top:14px">
          <span class="callout__icon"><Icon name="warning" size={14} /></span>
          <span>{errorText()}</span>
        </div>
      {/if}
    </div>
  {:else}
    <header class="diag__header">
      <div>
        <div class="diag__title">{report.host || report.url || fileName || t('diag.title')}</div>
        <div class="diag__subtitle mono">
          {#if fileName}{fileName}{/if}
          {#if report.generated} · {when(report.generated)}{/if}
        </div>
      </div>
      <div class="cluster">
        <span class="badge">{t('diag.version')} {report.version}</span>
        {#if report.errorCode !== undefined}
          <span class="badge badge--danger mono">{report.errorCode}</span>
        {/if}
        <button class="button button--ghost button--sm" onclick={pickFile}>
          <Icon name="folder" size={12} />
          {t('diag.openAnother')}
        </button>
      </div>
    </header>

    {#if report.errorReason}
      <div class="callout callout--warning">
        <span class="callout__icon"><Icon name="warning" size={14} /></span>
        <span>{report.errorReason}</span>
      </div>
    {/if}

    <section class="diag__section">
      <div class="diag__label">{t('diag.page')}</div>
      <div class="diag__row">
        <span class="diag__key">URL</span>
        <span class="diag__value mono">{report.url ?? '—'}</span>
      </div>
      <div class="diag__row">
        <span class="diag__key">{t('site.kernel')}</span>
        <span class="diag__value mono">{report.kernel ?? '—'}</span>
      </div>
      {#if report.appVersion}
        <div class="diag__row">
          <span class="diag__key">{t('about.version')}</span>
          <span class="diag__value mono">{report.appVersion}</span>
        </div>
      {/if}
    </section>

    <section class="diag__section">
      <div class="diag__label">{t('diag.runtime')}</div>
      {#if report.resolverKnown}
        <div class="diag__row">
          <span class="diag__key">{t('diag.resolver')}</span>
          <span class="diag__value">
            {#if report.resolver}
              {t('dns.resolverRestarted', {
                time: formatTime(report.resolver.at),
                count: report.resolver.count ?? 0,
              })}
            {:else}
              <span class="muted">{t('dns.deltaNone')}</span>
            {/if}
          </span>
        </div>
      {/if}
      <div class="diag__row">
        <span class="diag__key">{t('diag.signature')}</span>
        <span class="diag__value">
          {#if report.signature}
            <span
              class="badge"
              class:badge--warning={/adhoc|unsigned/i.test(report.signature)}
              class:badge--success={/developer id/i.test(report.signature)}
            >
              {report.signature}
            </span>
          {:else}
            <span class="muted">{t('diag.none')}</span>
          {/if}
        </span>
      </div>
      <div class="diag__row">
        <span class="diag__key">CEF</span>
        <span class="diag__value">
          {#if report.cef}
            <span
              class="badge"
              class:badge--danger={report.cef.startsWith('failed')}
              class:badge--success={report.cef === 'ready'}
            >
              {report.cef}
            </span>
          {:else}
            <span class="muted">{t('diag.none')}</span>
          {/if}
        </span>
      </div>
      <div class="diag__row">
        <span class="diag__key">{t('diag.switches')}</span>
        <span class="diag__value diag__value--stack">
          {#if displaySwitches.length > 0}
            {#each displaySwitches as item (item)}
              {@const explanation = explainSwitch(item, i18n.locale, context)}
              <span class="flag">
                <span class="badge mono">{item}</span>
                <span class="flag__copy">
                  <span class="flag__title">{explanation.title}</span>
                  <span class="flag__detail">{explanation.detail}</span>
                </span>
              </span>
            {/each}
          {:else}
            <span class="muted">{t('diag.none')}</span>
          {/if}
        </span>
      </div>
      {#if !report.switchesKnown}
        <div class="diag__note">{t('stale.unknown')}</div>
      {:else}
        {#if stale.length > 0}
          <div class="callout callout--warning" style="margin-top: 2px">
            <span class="callout__icon"><Icon name="warning" size={14} /></span>
            <span class="flag__copy">
              <span class="flag__title">{t('restart.title')}</span>
              <span class="flag__detail">{t('restart.body')}</span>
              {#each stale as item (item.kind)}
                <span class="flag__detail">
                  <strong>{staleLabel(item.kind)}</strong>
                </span>
                <span class="flag__detail mono">
                  {t('dnsStale.reported')}: {item.reported || t('restart.none')}
                </span>
                <span class="flag__detail mono">
                  {t('dnsStale.expected')}: {item.expected || t('restart.none')}
                </span>
                {#if item.extra.length > 0}
                  <span class="flag__detail mono">
                    {t('stale.extra')}: {item.extra.join(', ')}
                  </span>
                {/if}
                {#if item.missing.length > 0}
                  <span class="flag__detail mono">
                    {t('stale.missing')}: {item.missing.join(', ')}
                  </span>
                {/if}
              {/each}
              <span class="cluster" style="margin-top: 4px">
                <button
                  class="button button--primary button--sm"
                  disabled={restarting}
                  onclick={restartKernel}
                >
                  <Icon name="refresh" size={12} />
                  {restarting ? t('restart.working') : t('restart.action')}
                </button>
              </span>
            </span>
          </div>
        {:else if configured && errored.length === 0 && !proxyCheck?.portOnlyChange}
          <div class="diag__note diag__note--ok">{t('restart.match')}</div>
        {/if}
        {#if proxyCheck?.portOnlyChange && !proxyCheck.stale}
          <div class="diag__note">
            {t('proxyStale.resolverRestarted', {
              from: portOf(proxyCheck.reported),
              to: portOf(proxyCheck.expected),
            })}
          </div>
        {/if}
        {#if errored.length > 0}
          <div class="diag__note">
            {t('restart.partial', { items: errored.map(staleLabel).join('、') })}
          </div>
        {/if}
      {/if}
      <div class="diag__row">
        <span class="diag__key">{t('diag.limits')}</span>
        <span class="diag__value diag__value--stack">
          {#if report.limits.length > 0}
            {#each report.limits as item (item)}
              {@const explanation = explainLimit(item, i18n.locale, context)}
              <span class="flag">
                <span class="badge badge--warning mono">{item}</span>
                <span class="flag__copy">
                  <span class="flag__title">{explanation.title}</span>
                  <span class="flag__detail">{explanation.detail}</span>
                </span>
              </span>
            {/each}
          {:else}
            <span class="muted">{t('diag.none')}</span>
          {/if}
        </span>
      </div>
      {#if liveSwitches}
        <div class="diag__note diag__note--ok">{t('restart.refreshed')}</div>
      {/if}
      <div class="diag__row">
        <span class="diag__key">{t('diag.components')}</span>
        <span class="diag__value">
          {#if report.components.length > 0}
            {#each report.components as item (item)}
              <span class="badge mono">{item}</span>
            {/each}
          {:else}
            <span class="muted">{t('diag.none')}</span>
          {/if}
        </span>
      </div>
    </section>

    {#if report.appliedKnown}
      <section class="diag__section">
        <div class="diag__label">{t('diag.applied')}</div>
        {#if report.applied}
          <div class="diag__row">
            <span class="diag__key">{t('diag.appliedAt')}</span>
            <span class="diag__value mono">{report.applied.at || '—'}</span>
          </div>
          <div class="diag__row">
            <span class="diag__key">{t('dns.deltaProvider')}</span>
            <span class="diag__value">
              {report.applied.provider || '—'}{#if report.applied.protocol}{' ('}{report.applied.protocol}){/if}
            </span>
          </div>
          <div class="diag__row">
            <span class="diag__key">{t('dns.deltaProxy')}</span>
            <span class="diag__value mono">{report.applied.proxy || t('dns.deltaNone')}</span>
          </div>
        {:else}
          <div class="diag__note">{t('dns.deltaNone')}</div>
        {/if}
        {#if report.delta.length > 0}
          <div class="diag__label" style:margin-top="6px">{t('diag.delta')}</div>
          {#each report.delta as item, index (item.kind + index)}
            <div class="diag__row">
              <span class="diag__key">{deltaLabel(item.kind)}</span>
              <span class="diag__value mono">
                {item.from || t('dns.deltaNone')} → {item.to || t('dns.deltaNone')}
              </span>
            </div>
          {/each}
        {/if}
        {#if freshness?.changed}
          <div class="callout callout--warning" style="margin-top: 8px">
            <span class="callout__icon"><Icon name="warning" size={14} /></span>
            <span class="flag__copy">
              <span class="flag__title">{t('diag.nowDiffTitle')}</span>
              <span class="flag__detail">{t('diag.nowDiffBody')}</span>
              {#if freshness.appliedChanged}
                <span class="flag__detail">
                  {t('diag.reportApplied')}: {appliedText(freshness.reportApplied)}
                </span>
                <span class="flag__detail">
                  {t('diag.currentApplied')}: {appliedText(freshness.currentApplied)}
                </span>
              {/if}
              {#if freshness.deltaChanged}
                <span class="flag__detail">
                  {t('diag.reportDelta')}: {deltaText(freshness.reportDelta)}
                </span>
                <span class="flag__detail">
                  {t('diag.currentDelta')}: {deltaText(freshness.currentDelta)}
                </span>
              {/if}
              <span class="cluster" style="margin-top: 4px">
                <button
                  class="button button--primary button--sm"
                  disabled={savingRefresh}
                  onclick={saveRefreshedReport}
                >
                  <Icon name="download" size={12} />
                  {savingRefresh ? t('diag.refreshing') : t('diag.refreshReport')}
                </button>
              </span>
              <span class="flag__detail muted">{t('diag.refreshHint')}</span>
            </span>
          </div>
        {/if}
      </section>
    {/if}

    <section class="diag__section">
      <div class="diag__label">{t('error.connection')}</div>
      <div class="diag__row">
        <span class="diag__key">TLS</span>
        <span class="diag__value">
          <span class="badge {tlsClass(report.tls.status)}">{tlsLabel(report.tls.status)}</span>
          {#if report.tls.subject}
            <span class="mono muted">{report.tls.subject}</span>
          {/if}
          {#if report.tls.expires}
            <span class="muted">{t('site.expires')}: {report.tls.expires}</span>
          {/if}
        </span>
      </div>
    </section>

    <section class="diag__section">
      <div class="diag__label">{t('error.resolution')}</div>
      <div class="diag__row">
        <span class="diag__key">{t('site.network')}</span>
        <span class="diag__value">
          {#if report.rule.matched}
            <span class="badge badge--accent">{t('site.ruleMatched')}</span>
            <span>{report.rule.name}</span>
            <span class="badge">H3 {report.rule.h3 ?? '—'}</span>
            {#if report.rule.ech}<span class="badge">ECH</span>{/if}
          {:else}
            <span class="badge">{t('error.noRule')}</span>
          {/if}
        </span>
      </div>
      {#if report.upstream}
        <div class="diag__row">
          <span class="diag__key">{t('error.upstream')}</span>
          <span class="diag__value">
            <span class="badge mono">{report.upstream.protocol.toUpperCase()}</span>
            <span class="mono">{report.upstream.id} · {report.upstream.address}</span>
            {#if report.upstream.elapsedMs !== undefined}
              <span class="muted">{report.upstream.elapsedMs} ms</span>
            {/if}
          </span>
        </div>
      {/if}
      <div class="diag__row">
        <span class="diag__key">{t('error.addresses')}</span>
        <span class="diag__value mono">
          {report.addresses.length > 0 ? report.addresses.join(', ') : t('site.noAddress')}
        </span>
      </div>
      {#if report.https}
        <div class="diag__row">
          <span class="diag__key">HTTPS</span>
          <span class="diag__value">
            <span class="badge">ALPN {report.https.alpn.join('/') || '—'}</span>
            <span class="badge" class:badge--accent={report.https.echBytes > 0}>
              ECH {report.https.echBytes}B
            </span>
          </span>
        </div>
      {/if}
    </section>

    {#if report.notes}
      <section class="diag__section">
        <div class="diag__label">{t('diag.notes')}</div>
        <div class="diag__row">
          <span class="diag__value">{report.notes}</span>
        </div>
      </section>
    {/if}

    {#if Object.keys(report.extra).length > 0}
      <section class="diag__section">
        <div class="diag__label">{t('diag.extra')}</div>
        {#each Object.entries(report.extra) as [key, value] (key)}
          <div class="diag__row">
            <span class="diag__key mono">{key}</span>
            <span class="diag__value mono">{value}</span>
          </div>
        {/each}
      </section>
    {/if}

    <details class="diag__raw">
      <summary>{t('diag.raw')}</summary>
      <pre>{report.raw}</pre>
    </details>
  {/if}

  {#if pasteOpen}
    <div class="diag__paste">
      <textarea
        class="input input--mono"
        bind:value={pasted}
        placeholder={t('diag.pasteHint')}
        rows="8"
        spellcheck="false"
      ></textarea>
      <div class="cluster" style:margin-top="8px">
        <button class="button button--primary button--sm" onclick={parsePasted}>{t('diag.parse')}</button>
        <button class="button button--ghost button--sm" onclick={() => (pasteOpen = false)}>✕</button>
      </div>
    </div>
  {/if}
</div>

<style>
  .diag {
    display: flex;
    flex-direction: column;
    gap: 12px;
    max-width: 760px;
  }

  .diag__empty {
    padding: 40px 20px;
    display: flex;
    flex-direction: column;
    align-items: center;
    text-align: center;
    gap: 8px;
    border: 1px dashed var(--border-strong);
    border-radius: var(--radius-md);
    background: var(--surface);
  }

  .diag__empty-icon {
    width: 48px;
    height: 48px;
    border-radius: 14px;
    display: grid;
    place-items: center;
    background: var(--bg-sunken);
    color: var(--text-muted);
    margin-bottom: 4px;
  }

  .diag__empty-title {
    font-size: 14px;
    font-weight: 600;
  }

  .diag__empty-text {
    margin: 0 0 8px;
    max-width: 46ch;
    font-size: 12.5px;
    line-height: 1.55;
    color: var(--text-muted);
  }

  .diag__header {
    display: flex;
    align-items: flex-start;
    gap: 12px;
    flex-wrap: wrap;
  }

  .diag__title {
    font-size: 15px;
    font-weight: 650;
    letter-spacing: -0.01em;
  }

  .diag__subtitle {
    margin-top: 2px;
    font-size: 11.5px;
    color: var(--text-muted);
    word-break: break-all;
  }

  .diag__section {
    padding: 12px 14px;
    border: 1px solid var(--border);
    border-radius: var(--radius-md);
    background: var(--surface);
    display: flex;
    flex-direction: column;
    gap: 7px;
  }

  .diag__label {
    font-size: 11.5px;
    font-weight: 600;
    color: var(--text-secondary);
  }

  .diag__row {
    display: flex;
    align-items: flex-start;
    gap: 10px;
    font-size: 12px;
  }

  .diag__key {
    width: 72px;
    flex: none;
    color: var(--text-muted);
  }

  .diag__value {
    flex: 1;
    min-width: 0;
    display: flex;
    align-items: center;
    gap: 6px;
    flex-wrap: wrap;
    word-break: break-all;
  }

  .diag__value--stack {
    flex-direction: column;
    align-items: stretch;
    gap: 8px;
  }

  .flag {
    display: flex;
    flex-direction: column;
    gap: 4px;
    padding: 8px 10px;
    border-radius: var(--radius-sm);
    background: var(--bg-sunken);
  }

  .flag > .badge {
    align-self: flex-start;
    max-width: 100%;
    height: auto;
    padding: 2px 7px;
    white-space: normal;
    word-break: break-all;
  }

  .flag__copy {
    display: flex;
    flex-direction: column;
    gap: 1px;
  }

  .flag__title {
    font-size: 11.5px;
    font-weight: 600;
    color: var(--text-secondary);
  }

  .flag__detail {
    font-size: 11.5px;
    line-height: 1.5;
    color: var(--text-muted);
  }

  .diag__note {
    font-size: 11.5px;
    line-height: 1.5;
    color: var(--text-muted);
  }

  .diag__note--ok {
    color: var(--success);
  }

  .diag__raw {
    font-size: 11.5px;
  }

  .diag__raw summary {
    cursor: pointer;
    color: var(--text-muted);
  }

  .diag__raw pre {
    margin: 8px 0 0;
    padding: 12px;
    border-radius: var(--radius-sm);
    background: var(--bg-sunken);
    font-family: var(--font-mono);
    font-size: 11px;
    line-height: 1.6;
    white-space: pre-wrap;
    word-break: break-all;
    max-height: 320px;
    overflow: auto;
    user-select: text;
  }

  .diag__paste {
    padding: 12px;
    border: 1px solid var(--border);
    border-radius: var(--radius-md);
    background: var(--surface);
  }

  .diag__paste textarea {
    height: auto;
    padding: 10px;
    resize: vertical;
  }
</style>
