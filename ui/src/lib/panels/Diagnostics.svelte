<script lang="ts">
  import DiagnosticViewer from '../DiagnosticViewer.svelte';
  import { t } from '../i18n.svelte';
</script>

<div class="section">
  <header class="settings__header">
    <h1 class="settings__title">{t('diag.title')}</h1>
    <p class="settings__lede">{t('diag.lede')}</p>
  </header>

  <DiagnosticViewer />
</div>
