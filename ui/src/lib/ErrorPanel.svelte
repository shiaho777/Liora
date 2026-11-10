<script lang="ts">
  import Icon from './Icon.svelte';
  import { t } from './i18n.svelte';

  interface Props {
    url: string;
    message: string;
    code: number;
    onretry?: () => void;
    ondirect?: () => void;
    onsettings?: () => void;
  }

  let { url, message, code, onretry, ondirect, onsettings }: Props = $props();

  let host = $derived.by(() => {
    try {
      return new URL(url).hostname;
    } catch {
      return url;
    }
  });
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

  .link {
    color: var(--accent);
    font-size: 11.5px;
  }
</style>
