<script lang="ts">
  import EmptyState from './EmptyState.svelte';
  import Icon from './Icon.svelte';
  import PageHeader from './PageHeader.svelte';
  import { notify } from './bridge';
  import {
    downloads,
    activeDownloadCount,
    clearFinishedDownloads,
    humanSize,
    percentOf,
    revealDownload,
  } from './downloads.svelte';
  import { t } from './i18n.svelte';

  const hasFinished = $derived(
    downloads.items.some((item) => item.state === 'finished' || item.state === 'failed'),
  );

  function openFile(path: string): void {
    if (path) notify('ui.openPath', { path });
  }

  function filenameOf(item: { filename: string; path: string }): string {
    return item.filename || item.path.split('/').pop() || t('downloads.title');
  }
</script>

<div class="downloads-page">
  <div class="section">
    <PageHeader
      icon="download"
      title={t('downloads.pageTitle')}
      subtitle={t('downloads.pageLede')}
    />

    <div class="card">
      <div class="card__header">
        <div class="card__main">
          <div class="card__title">{t('downloads.list')}</div>
          <div class="card__subtitle">
            {#if activeDownloadCount() > 0}
              {t('downloads.active', { count: activeDownloadCount() })}
            {:else}
              {t('downloads.pageHint')}
            {/if}
          </div>
        </div>
        {#if hasFinished}
          <button class="button button--sm" onclick={clearFinishedDownloads}>
            {t('downloads.clear')}
          </button>
        {/if}
      </div>

      <div class="card__body">
        {#if downloads.items.length === 0}
          <EmptyState icon="download" title={t('downloads.empty')} body={t('downloads.emptyHint')} />
        {:else}
          <div class="download-list">
            {#each downloads.items as item (item.id)}
              <div class="download-row">
                <span
                  class="download-row__icon"
                  class:download-row__icon--error={item.state === 'failed'}
                >
                  <Icon name={item.state === 'failed' ? 'warning' : 'download'} size={15} />
                </span>
                <div class="download-row__main">
                  <div class="download-row__title" title={item.path}>{filenameOf(item)}</div>
                  {#if item.state === 'failed'}
                    <div class="download-row__status download-row__status--error">
                      {item.error || t('downloads.failed')}
                    </div>
                  {:else if item.state === 'finished'}
                    <div class="download-row__status">
                      {t('downloads.done')} · {humanSize(item.received)}
                    </div>
                  {:else}
                    <div class="bar"><span style:width="{percentOf(item)}%"></span></div>
                    <div class="download-row__status">
                      {percentOf(item)}%
                      {#if item.total > 0}
                        · {humanSize(item.received)} / {humanSize(item.total)}
                      {/if}
                    </div>
                  {/if}
                </div>
                <div class="download-row__control">
                  {#if item.state === 'finished'}
                    <button class="button button--sm" onclick={() => openFile(item.path)}>
                      <Icon name="external" size={12} />
                      {t('downloads.open')}
                    </button>
                    <button
                      class="icon-button"
                      title={t('downloads.reveal')}
                      aria-label={t('downloads.reveal')}
                      onclick={() => revealDownload(item)}
                    >
                      <Icon name="folder" size={14} />
                    </button>
                  {:else if item.state === 'failed'}
                    <span class="badge badge--danger">{t('downloads.failed')}</span>
                  {:else}
                    <span class="badge badge--accent">{percentOf(item)}%</span>
                  {/if}
                </div>
              </div>
            {/each}
          </div>
        {/if}
      </div>
    </div>
  </div>
</div>

<style>
  .downloads-page {
    height: 100%;
    overflow: auto;
    padding: 30px 34px 56px;
    background: var(--bg);
  }

  .card__main {
    flex: 1;
    min-width: 0;
  }

  .download-list {
    display: flex;
    flex-direction: column;
  }

  .download-row {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 12px 0;
  }

  .download-row + .download-row {
    border-top: 1px solid var(--border);
  }

  .download-row__icon {
    width: 34px;
    height: 34px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 10px;
    color: var(--accent);
    background: var(--accent-soft);
  }

  .download-row__icon--error {
    color: var(--danger);
    background: var(--danger-soft);
  }

  .download-row__main {
    flex: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 3px;
  }

  .download-row__title {
    font-size: var(--text-md);
    font-weight: var(--weight-medium);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .download-row__status {
    font-size: var(--text-xs);
    color: var(--text-muted);
  }

  .download-row__status--error {
    color: var(--danger);
  }

  .download-row__control {
    flex: none;
    display: flex;
    align-items: center;
    gap: 5px;
  }

  .bar {
    width: min(320px, 100%);
    height: 3px;
    border-radius: 2px;
    background: var(--bg-sunken);
    overflow: hidden;
  }

  .bar span {
    display: block;
    height: 100%;
    background: var(--accent);
    transition: width var(--normal) var(--ease);
  }

  @media (max-width: 640px) {
    .downloads-page {
      padding: 20px 18px 40px;
    }

    .download-row {
      flex-wrap: wrap;
    }

    .download-row__control {
      width: 100%;
      justify-content: flex-end;
    }
  }
</style>
