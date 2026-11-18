<script lang="ts">
  import Icon from './Icon.svelte';
  import { call, notify } from './bridge';
  import { app, toast } from './state.svelte';
  import { buildKernelSwitches } from './kernel-switch-check';
  import { networkDelta } from './network-delta';
  import { buildDiagnosticReport, toReportTimeline } from './diagnostic-report-build';
  import { buildTimeline } from './diagnostics-timeline';
  import { t } from './i18n.svelte';

  interface Props {
    url: string;
    message: string;
    code: number;
    security?: { host: string; subject: string; valid: boolean; notAfter: number; https: boolean } | null;
    onretry?: () => void;
    ondirect?: () => void;
    onsettings?: () => void;
  }

  let { url, message, code, security = null, onretry, ondirect, onsettings }: Props = $props();

  interface Diagnostics {
    host: string;
    rule: { name: string; h3: string; ech: boolean; dns: string | null } | null;
    upstream: { id: string; protocol: string; address: string };
    addresses: string[];
    https: { alpn: string[]; echBytes: number } | null;
    resolved: boolean;
    elapsedMs: number;
  }

  interface AppliedSnapshot {
    at: number;
    provider: string;
    protocol: string;
    mode: string;
    proxy: string;
  }

  interface ResolverStatus {
    at: number;
    count: number;
  }

  interface DnsPlan {
    mode: string;
    endpoint?: { name?: string; protocol?: string } | null;
  }

  let diag = $state<Diagnostics | null>(null);
  let diagError = $state('');
  let applied = $state<AppliedSnapshot | null>(null);
  let appliedDelta = $state<Array<{ kind: string; from: string; to: string }>>([]);
  let resolverStatus = $state<ResolverStatus | null>(null);
  let kernelId = $state('');
  let kernelSwitches = $state<string[]>([]);
  let kernelLimits = $state<string[]>([]);
  let components = $state<string[]>([]);

  let host = $derived.by(() => {
    try {
      return new URL(url).hostname;
    } catch {
      return url;
    }
  });

  // Friendly explanation for the platform error codes users actually hit.
  const reason = $derived.by(() => {
    const codes: Record<number, string> = {
      [-1001]: t('error.reasonTimeout'),
      [-1003]: t('error.reasonHost'),
      [-1004]: t('error.reasonConnect'),
      [-1005]: t('error.reasonLost'),
      [-1009]: t('error.reasonOffline'),
      [-1200]: t('error.reasonTls'),
      [-1201]: t('error.reasonTlsDate'),
      [-1202]: t('error.reasonTlsUntrusted'),
    };
    return codes[code] ?? '';
  });

  const tlsFailure = $derived(code <= -1200 && code >= -1210);

  $effect(() => {
    void loadDiagnostics();
  });

  // A plain-text report that can be pasted into a bug report. The line
  // formatting lives in diagnostic-report-build.ts so the viewer can build a
  // refreshed report with the same shape.
  const report = $derived.by(() => {
    const generated = new Date();
    const tls =
      security?.https && security.host === host
        ? `subject=${security.subject} valid=${security.valid ? 'yes' : 'no'} expires=${
            security.notAfter > 0 ? new Date(security.notAfter).toISOString().slice(0, 10) : '-'
          }`
        : tlsFailure
          ? 'handshake failed'
          : 'not established';

    const timeline = buildTimeline({
      generatedAt: generated.toISOString(),
      reportApplied: applied
        ? {
            at: new Date(applied.at).toISOString(),
            provider: applied.provider,
            protocol: applied.protocol,
            proxy: applied.proxy,
          }
        : null,
      reportResolver: resolverStatus
        ? { at: new Date(resolverStatus.at).toISOString(), count: resolverStatus.count }
        : null,
      reportDeltaCount: appliedDelta.length,
      currentApplied: null,
      currentResolver: null,
      currentDeltaCount: 0,
      currentDeltaChanged: false,
    });

    return buildDiagnosticReport({
      generated,
      version: app.version,
      platform: app.platform,
      arch: app.arch,
      url,
      host,
      error: `${code}${reason ? ` - ${reason}` : ''}`,
      kernel: kernelId || 'unknown',
      switches: kernelSwitches,
      limits: kernelLimits,
      components,
      signature: app.runtime.codeSigning || 'unknown',
      cef: app.runtime.cefStatus || 'unknown',
      resolver: resolverStatus
        ? { at: new Date(resolverStatus.at).toISOString(), count: resolverStatus.count }
        : null,
      timeline: toReportTimeline(timeline),
      applied: applied
        ? {
            at: new Date(applied.at).toISOString(),
            provider: applied.provider,
            protocol: applied.protocol,
            proxy: applied.proxy,
          }
        : null,
      delta: appliedDelta,
      tls,
      rule: diag?.rule
        ? `${diag.rule.name} (h3=${diag.rule.h3}, ech=${diag.rule.ech})`
        : 'none',
      upstream: diag
        ? `${diag.upstream.protocol} ${diag.upstream.id} ${diag.upstream.address} ${diag.elapsedMs}ms`
        : undefined,
      addresses: diag?.addresses,
      https: diag ? (diag.https ?? { alpn: [], echBytes: 0 }) : undefined,
      diagnostics: diag ? undefined : diagError || 'unavailable',
    });
  });

  async function copyReport(): Promise<void> {    try {
      await navigator.clipboard.writeText(report);
      toast(t('error.copied'));
    } catch {
      // WKWebView can refuse the async clipboard API; the old selector trick
      // still works from a click handler.
      const area = document.createElement('textarea');
      area.value = report;
      area.style.position = 'fixed';
      area.style.opacity = '0';
      document.body.appendChild(area);
      area.select();
      const copied = document.execCommand('copy');
      area.remove();
      toast(copied ? t('error.copied') : t('error.copyFailed'));
    }
  }

  // The shell writes the file into the downloads folder and answers with
  // browser.diagnosticSaved, so no modal panel interrupts the user.
  function saveReport(): void {
    notify('ui.saveDiagnostic', { content: report });
  }

  // The shell's menu command path uses this for the verification hook.
  $effect(() => {
    const hooks = window as unknown as {
      __lioraCopyReport?: () => void;
      __lioraSaveReport?: () => void;
      __lioraReportText?: string;
    };
    hooks.__lioraCopyReport = () => {
      void copyReport();
    };
    hooks.__lioraSaveReport = () => saveReport();
    hooks.__lioraReportText = report;
  });

  async function loadDiagnostics(): Promise<void> {
    diag = null;
    diagError = '';
    kernelSwitches = [];
    kernelLimits = [];
    components = [];
    if (!host) return;

    try {
      const current = await call<{ id: string }>('kernels.current', { site: host });
      kernelId = current.id;
    } catch {
      kernelId = '';
    }

    // Switches and installed components explain rendering and signing
    // failures, which the DNS endpoint cannot see.
    const [setup, installed, appliedResult, planResult, resolverResult] = await Promise.allSettled([
      kernelId
        ? call<{
            switches?: string[];
            unsupported?: string[];
            dohTemplates?: string;
            dnsMode?: string;
            proxy?: string;
          }>('kernels.setup', { id: kernelId })
        : Promise.resolve(null),
      call<Array<{ id: string; version: string; current: boolean }>>('components.installed', {}),
      call<AppliedSnapshot>('net.lastApplied'),
      call<DnsPlan>('dns.plan'),
      call<ResolverStatus>('net.resolverStatus'),
    ]);
    if (setup.status === 'fulfilled' && setup.value) {
      // The effective flags, so the report records what the kernel actually
      // received rather than only the QUIC/SPKI subset.
      kernelSwitches = buildKernelSwitches(setup.value);
      kernelLimits = setup.value.unsupported ?? [];
    }
    if (installed.status === 'fulfilled') {
      components = installed.value.map(
        (item) => `${item.id}@${item.version}${item.current ? ' *' : ''}`,
      );
    }

    resolverStatus =
      resolverResult.status === 'fulfilled' &&
      resolverResult.value &&
      typeof resolverResult.value.at === 'number' &&
      resolverResult.value.count > 0
        ? resolverResult.value
        : null;

    applied =
      appliedResult.status === 'fulfilled' &&
      appliedResult.value &&
      typeof appliedResult.value.at === 'number' &&
      appliedResult.value.at > 0
        ? appliedResult.value
        : null;
    if (applied && planResult.status === 'fulfilled') {
      const currentProxy =
        setup.status === 'fulfilled' && setup.value ? (setup.value.proxy ?? '') : '';
      appliedDelta = networkDelta(
        {
          provider: applied.provider,
          protocol: applied.protocol,
          mode: applied.mode,
          proxy: applied.proxy,
        },
        {
          provider: planResult.value.endpoint?.name ?? '',
          protocol: planResult.value.endpoint?.protocol ?? '',
          mode: planResult.value.mode,
          proxy: currentProxy,
        },
      );
    } else {
      appliedDelta = [];
    }

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
</script>

<div class="errorpage">
  <div class="errorpage__card">
    <div class="errorpage__icon">
      <Icon name="globe" size={26} stroke={1.4} />
    </div>

    <h1 class="errorpage__title">{t('error.title')}</h1>
    <div class="errorpage__host mono">{host}</div>
    <p class="errorpage__message">{message || t('error.generic')}</p>

    <div class="errorpage__actions">
      <button class="button button--primary" onclick={() => onretry?.()}>
        <Icon name="refresh" size={13} />
        {t('error.retry')}
      </button>
      <button class="button button--ghost" onclick={() => ondirect?.()}>
        <Icon name="shield" size={13} />
        {t('error.direct')}
      </button>
    </div>

    <p class="errorpage__hint">{t('error.directHint')}</p>

    {#if reason}
      <p class="errorpage__reason">{reason}</p>
    {/if}

    <div class="diag">
      <div class="diag__row">
        <span class="diag__label">{t('error.connection')}</span>
        <span class="diag__value">
          {#if tlsFailure}
            <span class="badge badge--danger">{t('error.tlsFailed')}</span>
            {#if security?.subject && security.host === host}
              <span class="mono muted">{security.subject}</span>
            {/if}
          {:else if security?.https && security.host === host}
            <span class="badge" class:badge--success={security.valid} class:badge--danger={!security.valid}>
              {security.valid ? t('site.valid') : t('site.invalid')}
            </span>
          {:else if url.startsWith('https:')}
            <span class="badge badge--warning">{t('error.noTls')}</span>
          {:else}
            <span class="badge">{t('error.plainHttp')}</span>
          {/if}
        </span>
      </div>

      <div class="diag__row">
        <span class="diag__label">{t('error.resolution')}</span>
        <span class="diag__value">
          {#if diagError}
            <span class="muted">{diagError}</span>
          {:else if !diag}
            <span class="muted">{t('site.resolving')}</span>
          {:else}
            <span class="badge mono">{diag.upstream.protocol.toUpperCase()}</span>
            {#if diag.rule}
              <span class="badge badge--accent">{t('site.ruleMatched')}</span>
            {:else}
              <span class="badge">{t('error.noRule')}</span>
            {/if}
            {#if diag.resolved}
              <span class="badge badge--success">{t('error.resolved')}</span>
            {:else}
              <span class="badge badge--warning">{t('error.notResolved')}</span>
            {/if}
          {/if}
        </span>
      </div>

      {#if diag}
        <div class="diag__row">
          <span class="diag__label">{t('error.addresses')}</span>
          <span class="diag__value mono">
            {diag.addresses.length > 0 ? diag.addresses.slice(0, 4).join(', ') : t('site.noAddress')}
          </span>
        </div>
        <div class="diag__row">
          <span class="diag__label">{t('error.upstream')}</span>
          <span class="diag__value mono">{diag.upstream.id} · {diag.elapsedMs} ms</span>
        </div>
      {/if}
    </div>

    <details class="report">
      <summary>{t('error.report')}</summary>
      <pre>{report}</pre>
    </details>

    <button class="button button--ghost button--sm" onclick={copyReport}>
      <Icon name="download" size={12} />
      {t('error.copy')}
    </button>

    <button class="button button--ghost button--sm" onclick={saveReport}>
      <Icon name="folder" size={12} />
      {t('error.save')}
    </button>

    <div class="errorpage__footer">
      <span class="mono muted">NSURLError {code}</span>
      <button class="link" onclick={() => onsettings?.()}>{t('error.opensettings')}</button>
    </div>
  </div>
</div>

<style>
  .errorpage {
    height: 100%;
    display: grid;
    place-items: center;
    padding: 32px 20px;
    overflow: auto;
    background: var(--bg);
  }

  .errorpage__card {
    width: min(480px, 100%);
    display: flex;
    flex-direction: column;
    align-items: center;
    text-align: center;
    gap: 8px;
  }

  .errorpage__icon {
    width: 56px;
    height: 56px;
    border-radius: var(--radius-lg);
    display: grid;
    place-items: center;
    color: var(--text-muted);
    background: var(--bg-sunken);
    margin-bottom: 6px;
  }

  .errorpage__title {
    margin: 0;
    font-size: var(--text-xl);
    font-weight: var(--weight-semibold);
    letter-spacing: -0.01em;
  }

  .errorpage__host {
    color: var(--text-secondary);
    font-size: var(--text-sm);
  }

  .errorpage__message {
    margin: 2px 0 4px;
    font-size: var(--text-sm);
    color: var(--text-muted);
    line-height: 1.55;
    max-width: 44ch;
  }

  .errorpage__actions {
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
    justify-content: center;
    margin-top: 6px;
  }

  .errorpage__hint {
    margin: 4px 0 0;
    font-size: var(--text-xs);
    color: var(--text-muted);
    max-width: 46ch;
    line-height: 1.5;
  }

  .errorpage__footer {
    margin-top: 14px;
    display: flex;
    align-items: center;
    gap: 12px;
    font-size: var(--text-xs);
  }

  .errorpage__reason {
    margin: 2px 0 0;
    font-size: var(--text-sm);
    color: var(--warning);
  }

  .diag {
    margin-top: 14px;
    width: min(520px, 100%);
    border-top: 1px solid var(--border);
    padding-top: 10px;
    display: flex;
    flex-direction: column;
    gap: 6px;
    text-align: left;
  }

  .diag__row {
    display: flex;
    align-items: flex-start;
    gap: 10px;
    font-size: var(--text-xs);
  }

  .diag__label {
    width: 64px;
    flex: none;
    color: var(--text-muted);
  }

  .diag__value {
    flex: 1;
    min-width: 0;
    display: flex;
    gap: 5px;
    flex-wrap: wrap;
    align-items: center;
    word-break: break-all;
  }

  .report {
    margin-top: 12px;
    width: min(520px, 100%);
    text-align: left;
  }

  .report summary {
    cursor: pointer;
    font-size: var(--text-xs);
    color: var(--text-muted);
  }

  .report pre {
    margin: 6px 0 0;
    padding: 10px;
    border-radius: var(--radius-sm);
    background: var(--bg-sunken);
    color: var(--text-secondary);
    font-family: var(--font-mono);
    font-size: var(--text-xs);
    line-height: 1.55;
    max-height: 180px;
    overflow: auto;
    white-space: pre-wrap;
    word-break: break-all;
    user-select: text;
  }

  .link {
    color: var(--accent);
    font-size: var(--text-xs);
  }
</style>
