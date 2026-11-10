<script lang="ts">
  import DiagnosticViewer from './DiagnosticViewer.svelte';
  import { t } from './i18n.svelte';
</script>

<div class="page">
  <header class="page__header">
    <h1 class="page__title">{t('diag.title')}</h1>
    <p class="page__lede">{t('diag.lede')}</p>
  </header>
  <DiagnosticViewer />
</div>

<style>
  .page {
    height: 100%;
    overflow: auto;
    padding: 22px 24px 48px;
    background: var(--bg);
  }

  .page__header {
    margin-bottom: 16px;
    max-width: 760px;
  }

  .page__title {
    margin: 0;
    font-size: 18px;
    font-weight: 650;
    letter-spacing: -0.015em;
  }

  .page__lede {
    margin: 4px 0 0;
    font-size: 12.5px;
    color: var(--text-muted);
    max-width: 62ch;
  }
</style>
