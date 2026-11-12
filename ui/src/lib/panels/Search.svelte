<script lang="ts">
  import PageHeader from '../PageHeader.svelte';
  import Toggle from '../Toggle.svelte';
  import { value, set } from '../settings-store.svelte';
  import { t } from '../i18n.svelte';

  const engines = [
    { id: 'duckduckgo', name: 'DuckDuckGo' },
    { id: 'google', name: 'Google' },
    { id: 'bing', name: 'Bing' },
    { id: 'brave', name: 'Brave Search' },
    { id: 'startpage', name: 'Startpage' },
    { id: 'baidu', name: '百度' },
    { id: 'sogou', name: '搜狗' },
  ];
</script>

<div class="section">
  <PageHeader icon="search" title={t('search.title')} subtitle={t('search.lede')} />

  <div class="card">
    <div class="card__body">
      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('search.engine')}</div>
          <div class="row__desc">{t('search.engine.desc')}</div>
        </div>
        <div class="row__control">
          <select
            class="select"
            value={value<string>('search.engine')}
            onchange={(event) => set('search.engine', (event.target as HTMLSelectElement).value)}
          >
            {#each engines as engine (engine.id)}
              <option value={engine.id}>{engine.name}</option>
            {/each}
          </select>
        </div>
      </div>

      <div class="row">
        <div class="row__main">
          <div class="row__title">{t('search.suggest')}</div>
          <div class="row__desc">{t('search.suggest.desc')}</div>
        </div>
        <div class="row__control">
          <Toggle
            checked={value<boolean>('search.suggest')}
            label={t('search.suggest')}
            onchange={(next) => set('search.suggest', next)}
          />
        </div>
      </div>
    </div>
  </div>
</div>
