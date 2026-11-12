<script lang="ts">
  import Icon from './Icon.svelte';
  import { readingMinutes, type ReaderData } from './reader';
  import { t } from './i18n.svelte';
  import { onMount } from 'svelte';

  interface Props {
    data: ReaderData;
    onclose: () => void;
  }

  let { data, onclose }: Props = $props();
  let scale = $state(1);

  onMount(() => {
    const saved = Number(localStorage.getItem('liora.reader.scale'));
    if (Number.isFinite(saved) && saved >= 0.85 && saved <= 1.35) scale = saved;
  });

  function adjust(delta: number): void {
    scale = Math.min(1.35, Math.max(0.85, Math.round((scale + delta) * 100) / 100));
    localStorage.setItem('liora.reader.scale', String(scale));
  }

  function onKeydown(event: KeyboardEvent): void {
    if (event.key === 'Escape') onclose();
  }
</script>

<svelte:window onkeydown={onKeydown} />

<div class="reader" style:--reader-scale={scale}>
  <div class="reader__bar">
    <button class="reader__back" onclick={onclose}>
      <Icon name="back" size={14} />
      {t('reader.close')}
    </button>
    <span class="reader__source">{data.site}</span>
    <div class="reader__tools">
      <button
        class="reader__tool"
        title={t('reader.smaller')}
        aria-label={t('reader.smaller')}
        disabled={scale <= 0.85}
        onclick={() => adjust(-0.1)}
      >
        A−
      </button>
      <span class="reader__scale">{Math.round(scale * 100)}%</span>
      <button
        class="reader__tool"
        title={t('reader.larger')}
        aria-label={t('reader.larger')}
        disabled={scale >= 1.35}
        onclick={() => adjust(0.1)}
      >
        A+
      </button>
    </div>
  </div>

  <div class="reader__scroll">
    <article class="reader__article" lang={data.lang || undefined}>
      <header class="reader__header">
        <div class="reader__site">{data.site}</div>
        <h1 class="reader__title">{data.title}</h1>
        {#if data.byline}
          <div class="reader__byline">{data.byline}</div>
        {/if}
        {#if data.excerpt}
          <p class="reader__excerpt">{data.excerpt}</p>
        {/if}
        <div class="reader__meta">
          <span>{t('reader.minutes', { count: readingMinutes(data) })}</span>
          <span>·</span>
          <span>{t('reader.local')}</span>
        </div>
      </header>

      <div class="reader__body">
        {#each data.blocks as block, index (index)}
          {#if block.type === 'h'}
            <svelte:element this={`h${block.level ?? 2}`} class="reader__heading">
              {block.text}
            </svelte:element>
          {:else if block.type === 'p'}
            <p class="reader__paragraph">{block.text}</p>
          {:else if block.type === 'quote'}
            <blockquote class="reader__quote">{block.text}</blockquote>
          {:else if block.type === 'pre'}
            <pre class="reader__pre">{block.text}</pre>
          {:else if block.type === 'li'}
            <div class="reader__list-item">
              <span class="reader__bullet"></span>
              <span>{block.text}</span>
            </div>
          {:else if block.type === 'img' && block.src}
            <figure class="reader__figure">
              <img src={block.src} alt={block.alt ?? ''} loading="lazy" />
              {#if block.alt}<figcaption>{block.alt}</figcaption>{/if}
            </figure>
          {/if}
        {/each}
      </div>

      <footer class="reader__footer">
        <span class="reader__mark">L</span>
        <span>{t('reader.footer')}</span>
      </footer>
    </article>
  </div>
</div>

<style>
  .reader {
    height: 100%;
    display: flex;
    flex-direction: column;
    background: var(--bg);
  }

  .reader__bar {
    flex: none;
    display: flex;
    align-items: center;
    gap: 12px;
    height: 44px;
    padding: 0 14px;
    background: var(--surface);
    border-bottom: 1px solid var(--border);
  }

  .reader__back {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    height: 26px;
    padding: 0 9px;
    border-radius: var(--radius-pill);
    font-size: 11.5px;
    font-weight: 550;
    color: var(--text-secondary);
    background: var(--bg-hover);
  }

  .reader__back:hover {
    background: var(--accent-soft);
    color: var(--accent-text);
  }

  .reader__source {
    flex: 1;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    text-align: center;
    font-size: 11px;
    color: var(--text-muted);
  }

  .reader__tools {
    flex: none;
    display: flex;
    align-items: center;
    gap: 4px;
  }

  .reader__tool {
    min-width: 28px;
    height: 24px;
    padding: 0 6px;
    border-radius: var(--radius-xs);
    font-size: 11px;
    font-weight: 600;
    color: var(--text-secondary);
  }

  .reader__tool:hover:not(:disabled) {
    background: var(--bg-hover);
    color: var(--text);
  }

  .reader__tool:disabled {
    opacity: 0.35;
  }

  .reader__scale {
    width: 34px;
    text-align: center;
    font-family: var(--font-mono);
    font-size: 9.5px;
    color: var(--text-muted);
  }

  .reader__scroll {
    flex: 1;
    min-height: 0;
    overflow: auto;
    scroll-behavior: smooth;
  }

  .reader__article {
    width: min(740px, calc(100% - 48px));
    margin: 0 auto;
    padding: 46px 0 80px;
    font-size: calc(17px * var(--reader-scale));
    line-height: 1.78;
    color: var(--text);
    font-family: "New York", "Iowan Old Style", "Palatino Linotype", Georgia, "Songti SC",
      "Noto Serif CJK SC", serif;
  }

  .reader__header {
    padding-bottom: 26px;
  }

  .reader__site {
    font-family: var(--font-sans);
    font-size: 10.5px;
    font-weight: 650;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--accent-text);
  }

  .reader__title {
    margin: 12px 0 0;
    font-size: 2.1em;
    line-height: 1.16;
    font-weight: 650;
    letter-spacing: -0.022em;
    color: var(--text);
  }

  .reader__byline {
    margin-top: 12px;
    font-family: var(--font-sans);
    font-size: 12px;
    color: var(--text-muted);
  }

  .reader__excerpt {
    margin: 16px 0 0;
    font-size: 1.02em;
    line-height: 1.6;
    color: var(--text-secondary);
  }

  .reader__meta {
    display: flex;
    flex-wrap: wrap;
    gap: 6px;
    margin-top: 16px;
    font-family: var(--font-sans);
    font-size: 10.5px;
    color: var(--text-muted);
  }

  .reader__body {
    border-top: 1px solid var(--border);
    padding-top: 26px;
  }

  .reader__body p,
  .reader__body .reader__heading,
  .reader__body .reader__quote,
  .reader__body .reader__pre,
  .reader__body .reader__list-item,
  .reader__body .reader__figure {
    margin: 0 0 1.15em;
  }

  .reader__paragraph {
    white-space: pre-wrap;
  }

  .reader__heading {
    margin-top: 1.6em;
    line-height: 1.25;
    font-weight: 650;
    letter-spacing: -0.012em;
  }

  .reader__heading:first-child {
    margin-top: 0;
  }

  h2.reader__heading {
    font-size: 1.42em;
  }

  h3.reader__heading {
    font-size: 1.18em;
  }

  .reader__quote {
    padding: 2px 0 2px 20px;
    border-left: 3px solid var(--accent);
    color: var(--text-secondary);
    font-style: italic;
  }

  .reader__pre {
    padding: 14px 16px;
    border-radius: var(--radius-sm);
    background: var(--bg-sunken);
    font-family: var(--font-mono);
    font-size: 0.74em;
    line-height: 1.55;
    overflow: auto;
  }

  .reader__list-item {
    display: flex;
    gap: 12px;
    padding-left: 8px;
  }

  .reader__bullet {
    flex: none;
    width: 5px;
    height: 5px;
    margin-top: 0.72em;
    border-radius: 50%;
    background: var(--accent);
  }

  .reader__figure {
    margin: 1.6em 0;
  }

  .reader__figure img {
    display: block;
    width: 100%;
    height: auto;
    border-radius: var(--radius-md);
    background: var(--bg-sunken);
  }

  .reader__figure figcaption {
    margin-top: 7px;
    font-family: var(--font-sans);
    font-size: 10.5px;
    line-height: 1.5;
    color: var(--text-muted);
    text-align: center;
  }

  .reader__footer {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-top: 44px;
    padding-top: 18px;
    border-top: 1px solid var(--border);
    font-family: var(--font-sans);
    font-size: 10.5px;
    color: var(--text-muted);
  }

  .reader__mark {
    width: 20px;
    height: 20px;
    display: grid;
    place-items: center;
    border-radius: 6px;
    background: var(--accent-soft);
    color: var(--accent);
    font-size: 10px;
    font-weight: 700;
  }

  @media (max-width: 640px) {
    .reader__article {
      width: calc(100% - 36px);
      padding-top: 30px;
    }

    .reader__source {
      display: none;
    }
  }
</style>
