<script lang="ts">
  import { onMount } from 'svelte';
  import Icon from './Icon.svelte';
  import { t } from './i18n.svelte';

  interface PaletteTab {
    id: number;
    title: string;
    url: string;
    page: string;
  }

  interface PaletteCommand {
    id: string;
    label: string;
    icon: string;
    hint?: string;
  }

  interface Props {
    tabs: PaletteTab[];
    activeId: number;
    commands: PaletteCommand[];
    onclose: () => void;
    onopentab: (id: number) => void;
    oncommand: (id: string) => void;
    onquery: (query: string) => void;
  }

  let { tabs, activeId, commands, onclose, onopentab, oncommand, onquery }: Props = $props();

  interface Result {
    kind: 'tab' | 'command' | 'search';
    key: string;
    label: string;
    hint: string;
    icon: string;
    run: () => void;
  }

  let query = $state('');
  let selected = $state(0);
  let inputEl = $state<HTMLInputElement | null>(null);

  const matches = $derived.by(() => {
    const text = query.trim().toLowerCase();
    const results: Result[] = [];

    for (const tab of tabs) {
      const haystack = `${tab.title} ${tab.url}`.toLowerCase();
      if (text && !haystack.includes(text)) continue;
      results.push({
        kind: 'tab',
        key: `tab:${tab.id}`,
        label: tab.title || tab.url,
        hint: tab.url,
        icon: tab.page === 'web' ? 'globe' : tab.page === 'newtab' ? 'home' : 'info',
        run: () => onopentab(tab.id),
      });
      if (results.length >= 7) break;
    }

    for (const command of commands) {
      if (text && !command.label.toLowerCase().includes(text)) continue;
      results.push({
        kind: 'command',
        key: `cmd:${command.id}`,
        label: command.label,
        hint: command.hint ?? '',
        icon: command.icon,
        run: () => oncommand(command.id),
      });
      if (results.length >= 13) break;
    }

    if (query.trim()) {
      results.push({
        kind: 'search',
        key: 'search',
        label: t('palette.searchWeb', { query: query.trim() }),
        hint: t('palette.open'),
        icon: 'search',
        run: () => onquery(query.trim()),
      });
    }

    return results;
  });

  $effect(() => {
    if (selected >= matches.length) selected = Math.max(0, matches.length - 1);
  });

  onMount(() => {
    inputEl?.focus();
  });

  function move(delta: number): void {
    if (matches.length === 0) return;
    selected = (selected + delta + matches.length) % matches.length;
  }

  function run(index: number): void {
    const result = matches[index];
    if (result) result.run();
  }

  function onKeydown(event: KeyboardEvent): void {
    if (event.key === 'ArrowDown') {
      event.preventDefault();
      move(1);
    } else if (event.key === 'ArrowUp') {
      event.preventDefault();
      move(-1);
    } else if (event.key === 'Enter') {
      event.preventDefault();
      run(selected);
    } else if (event.key === 'Escape') {
      event.preventDefault();
      onclose();
    }
  }
</script>

<!-- svelte-ignore a11y_click_events_have_key_events -->
<div class="palette-backdrop" role="presentation" onclick={onclose}>
  <div
    class="palette"
    role="dialog"
    tabindex="-1"
    aria-modal="true"
    aria-label={t('palette.title')}
    onclick={(event) => event.stopPropagation()}
  >
    <div class="palette__input">
      <span class="palette__search"><Icon name="search" size={17} /></span>
      <input
        bind:this={inputEl}
        bind:value={query}
        placeholder={t('palette.placeholder')}
        aria-label={t('palette.placeholder')}
        spellcheck="false"
        autocomplete="off"
        oninput={() => (selected = 0)}
        onkeydown={onKeydown}
      />
      <kbd>esc</kbd>
    </div>

    <div class="palette__results" role="listbox" aria-label={t('palette.title')}>
      {#if matches.length === 0}
        <div class="palette__empty">{t('palette.empty')}</div>
      {:else}
        {#each matches as result, index (result.key)}
          {#if index === 0 || matches[index - 1].kind !== result.kind}
            <div class="palette__section">
              {result.kind === 'tab'
                ? t('palette.tabs')
                : result.kind === 'command'
                  ? t('palette.commands')
                  : t('palette.search')}
            </div>
          {/if}
          <button
            class="palette__item"
            class:palette__item--active={index === selected}
            class:palette__item--search={result.kind === 'search'}
            role="option"
            aria-selected={index === selected}
            onmouseenter={() => (selected = index)}
            onclick={() => run(index)}
          >
            <span class="palette__icon"><Icon name={result.icon} size={14} /></span>
            <span class="palette__label">{result.label}</span>
            {#if result.hint}
              <span
                class="palette__hint"
                class:palette__hint--url={result.kind === 'tab'}
              >
                {result.hint}
              </span>
            {/if}
            {#if index === selected}
              <kbd class="palette__enter">↵</kbd>
            {/if}
          </button>
        {/each}
      {/if}
    </div>

    <div class="palette__foot">
      <span><kbd>↑</kbd><kbd>↓</kbd> {t('palette.navigate')}</span>
      <span><kbd>↵</kbd> {t('palette.open')}</span>
      <span><kbd>esc</kbd> {t('palette.close')}</span>
    </div>
  </div>
</div>

<style>
  .palette-backdrop {
    position: fixed;
    inset: 0;
    z-index: 80;
    display: flex;
    justify-content: center;
    align-items: flex-start;
    padding: 12vh 20px 20px;
    background: rgba(8, 10, 14, 0.46);
    backdrop-filter: blur(5px);
    animation: palette-fade 120ms var(--ease);
  }

  .palette {
    width: min(620px, 100%);
    max-height: min(560px, 74vh);
    display: flex;
    flex-direction: column;
    border-radius: 15px;
    border: 1px solid var(--border-strong);
    background: var(--surface-strong);
    box-shadow: var(--shadow-lg);
    overflow: hidden;
    animation: palette-in 160ms var(--ease-out);
  }

  .palette__input {
    flex: none;
    display: flex;
    align-items: center;
    gap: 10px;
    height: 54px;
    padding: 0 14px;
    border-bottom: 1px solid var(--border);
  }

  .palette__search {
    width: 30px;
    height: 30px;
    display: grid;
    place-items: center;
    border-radius: 9px;
    color: var(--accent);
    background: var(--accent-soft);
  }

  .palette__input input {
    flex: 1;
    min-width: 0;
    border: none;
    outline: none;
    background: transparent;
    font-size: 15px;
    color: var(--text);
  }

  .palette__input input::placeholder {
    color: var(--text-muted);
  }

  kbd {
    font-family: var(--font-mono);
    font-size: 10px;
    color: var(--text-muted);
    padding: 1px 5px;
    border-radius: 4px;
    border: 1px solid var(--border);
    background: var(--bg-sunken);
  }

  .palette__results {
    flex: 1;
    min-height: 0;
    overflow-y: auto;
    padding: 7px;
  }


  .palette__section {
    padding: 9px 9px 4px;
    font-size: 9.5px;
    font-weight: 650;
    letter-spacing: 0.05em;
    text-transform: uppercase;
    color: var(--text-muted);
  }

  .palette__item {
    display: flex;
    align-items: center;
    gap: 9px;
    width: 100%;
    height: 37px;
    padding: 0 9px;
    border-radius: var(--radius-sm);
    text-align: left;
    color: var(--text-secondary);
    transition:
      background var(--fast) var(--ease),
      color var(--fast) var(--ease);
  }

  .palette__item--active {
    background: var(--accent-soft);
    color: var(--text);
  }

  .palette__icon {
    width: 24px;
    height: 24px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 7px;
    color: var(--text-muted);
    background: var(--bg-sunken);
  }

  .palette__item--active .palette__icon {
    color: var(--accent);
    background: var(--surface);
  }

  .palette__label {
    flex: 1;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    font-size: 12.5px;
    font-weight: 520;
  }

  .palette__hint {
    flex: none;
    max-width: 46%;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    font-size: 10.5px;
    color: var(--text-muted);
  }

  .palette__hint--url {
    font-family: var(--font-mono);
    font-size: 9.5px;
  }

  .palette__enter {
    flex: none;
    margin-left: 2px;
    color: var(--accent-text);
    border-color: transparent;
    background: var(--surface);
  }

  .palette__empty {
    padding: 26px 12px;
    text-align: center;
    font-size: 12px;
    color: var(--text-muted);
  }

  .palette__foot {
    flex: none;
    display: flex;
    align-items: center;
    gap: 16px;
    padding: 8px 13px;
    border-top: 1px solid var(--border);
    background: var(--bg-sunken);
    font-size: 10px;
    color: var(--text-muted);
  }

  .palette__foot span {
    display: inline-flex;
    align-items: center;
    gap: 3px;
  }

  @keyframes palette-fade {
    from {
      opacity: 0;
    }
    to {
      opacity: 1;
    }
  }

  @keyframes palette-in {
    from {
      opacity: 0;
      transform: translateY(-5px) scale(0.985);
    }
    to {
      opacity: 1;
      transform: none;
    }
  }

  @media (prefers-reduced-motion: reduce) {
    .palette-backdrop,
    .palette {
      animation: none;
    }
  }
</style>
