<script lang="ts">
  import Icon from './Icon.svelte';
  import { call, notify } from './bridge';
  import { app, toast } from './state.svelte';
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

  let diag = $state<Diagnostics | null>(null);
  let diagError = $state('');
  let kernelId = $state('');

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

  // A plain-text report that can be pasted into a bug report. Kept in one
  // place so the visible preview and the clipboard cannot drift apart.
  const report = $derived.by(() => {
    const lines = [
      'report: 1',
      'Liora diagnostic report',
      `Generated: ${new Date().toISOString()}`,
      `Version: ${app.version} (${app.platform} ${app.arch})`,
      `URL: ${url}`,
      `Host: ${host}`,
      `Error: ${code}${reason ? ` - ${reason}` : ''}`,
      `Kernel: ${kernelId || 'unknown'}`,
    ];

    if (security?.https && security.host === host) {
      const expires = security.notAfter > 0 ? new Date(security.notAfter).toISOString().slice(0, 10) : '-';
      lines.push(`TLS: subject=${security.subject} valid=${security.valid ? 'yes' : 'no'} expires=${expires}`);
    } else if (tlsFailure) {
      lines.push('TLS: handshake failed');
    } else {
      lines.push('TLS: not established');
    }

    lines.push(
      `Rule: ${diag?.rule ? `${diag.rule.name} (h3=${diag.rule.h3}, ech=${diag.rule.ech})` : 'none'}`,
    );
    if (diag) {
      lines.push(
        `Upstream: ${diag.upstream.protocol} ${diag.upstream.id} ${diag.upstream.address} ${diag.elapsedMs}ms`,
      );
      lines.push(`Addresses: ${diag.addresses.join(', ') || 'none'}`);
      lines.push(
        `HTTPS: alpn=${diag.https?.alpn.join(',') || '-'} ech=${diag.https?.echBytes ?? 0}B`,
      );
    } else {
      lines.push(`Diagnostics: ${diagError || 'unavailable'}`);
    }

    return lines.join('\n');
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
    if (!host) return;

    call<{ id: string }>('kernels.current', { site: host })
      .then((value) => (kernelId = value.id))
      .catch(() => (kernelId = ''));

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
    border-radius: 16px;
    display: grid;
    place-items: center;
    color: var(--text-muted);
    background: var(--bg-sunken);
    margin-bottom: 6px;
  }

  .errorpage__title {
    margin: 0;
    font-size: 17px;
    font-weight: 650;
    letter-spacing: -0.01em;
  }

  .errorpage__host {
    color: var(--text-secondary);
    font-size: 12.5px;
  }

  .errorpage__message {
    margin: 2px 0 4px;
    font-size: 12.5px;
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
    font-size: 11.5px;
    color: var(--text-muted);
    max-width: 46ch;
    line-height: 1.5;
  }

  .errorpage__footer {
    margin-top: 14px;
    display: flex;
    align-items: center;
    gap: 12px;
    font-size: 11.5px;
  }

  .errorpage__reason {
    margin: 2px 0 0;
    font-size: 12px;
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
    font-size: 11.5px;
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
    font-size: 11.5px;
    color: var(--text-muted);
  }

  .report pre {
    margin: 6px 0 0;
    padding: 10px;
    border-radius: var(--radius-sm);
    background: var(--bg-sunken);
    color: var(--text-secondary);
    font-family: var(--font-mono);
    font-size: 11px;
    line-height: 1.55;
    max-height: 180px;
    overflow: auto;
    white-space: pre-wrap;
    word-break: break-all;
    user-select: text;
  }

  .link {
    color: var(--accent);
    font-size: 11.5px;
  }
</style>
