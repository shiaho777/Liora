<script lang="ts">
  import DiagnosticViewer from '../DiagnosticViewer.svelte';
  import PageHeader from '../PageHeader.svelte';
  import { t } from '../i18n.svelte';
</script>

<div class="section">
  <PageHeader icon="info" title={t('diag.title')} subtitle={t('diag.lede')} />

  <DiagnosticViewer />
</div>
