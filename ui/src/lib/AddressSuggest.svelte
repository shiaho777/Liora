<script lang="ts">
  import Icon from './Icon.svelte';
  import { t } from './i18n.svelte';
  import type { Suggestion } from './suggest';

  interface Props {
    items: Suggestion[];
    selected: number;
    onpick: (item: Suggestion) => void;
    onhover: (index: number) => void;
  }

  let { items, selected, onpick, onhover }: Props = $props();

  function iconFor(kind: Suggestion['kind']): string {
    if (kind === 'bookmark') return 'star';
    if (kind === 'history') return 'history';
    if (kind === 'url') return 'globe';
    return 'search';
  }

  function labelFor(item: Suggestion): string {
    if (item.kind === 'search') return t('suggest.searchWith', { engine: item.detail });
    if (item.kind === 'url') return t('suggest.openUrl');
    return item.title;
  }
</script>

{#if items.length > 0}
  <div class="address-suggest" id="address-suggest-list" role="listbox" aria-label={t('suggest.list')}>
    {#each items as item, index (item.kind + item.value)}
      <button
        type="button"
        class="address-suggest__row"
        class:address-suggest__row--selected={index === selected}
        role="option"
        aria-selected={index === selected}
        id="address-suggest-{index}"
        onmousedown={(event) => {
          // Keep focus in the input; blur would close the list before click.
          event.preventDefault();
          onpick(item);
        }}
        onmouseenter={() => onhover(index)}
      >
        <span class="address-suggest__icon address-suggest__icon--{item.kind}">
          <Icon name={iconFor(item.kind)} size={13} />
        </span>
        <span class="address-suggest__title">{labelFor(item)}</span>
        {#if item.kind !== 'search' && item.detail}
          <span class="address-suggest__detail">{item.detail}</span>
        {/if}
        {#if item.kind === 'bookmark'}
          <span class="address-suggest__tag">{t('suggest.bookmarked')}</span>
        {/if}
      </button>
    {/each}
    <div class="address-suggest__footer">{t('suggest.hint')}</div>
  </div>
{/if}

<style>
  .address-suggest {
    position: absolute;
    top: calc(100% + 6px);
    left: 0;
    right: 0;
    z-index: 60;
    background: var(--surface-raised);
    border: 1px solid var(--border);
    border-radius: var(--radius-lg);
    box-shadow: var(--shadow-lg);
    padding: 5px;
    animation: suggest-in var(--fast) var(--ease-out);
  }

  @keyframes suggest-in {
    from {
      opacity: 0;
      transform: translateY(-4px);
    }
    to {
      opacity: 1;
      transform: none;
    }
  }

  .address-suggest__row {
    display: flex;
    align-items: center;
    gap: 9px;
    width: 100%;
    padding: 7px 9px;
    border: 0;
    border-radius: var(--radius-md);
    background: transparent;
    color: var(--text);
    font-size: var(--text-sm);
    text-align: left;
    cursor: default;
  }

  .address-suggest__row--selected {
    background: var(--accent-soft);
  }

  .address-suggest__icon {
    flex: none;
    width: 22px;
    height: 22px;
    display: grid;
    place-items: center;
    border-radius: 7px;
    color: var(--text-muted);
    background: var(--bg-sunken);
  }

  .address-suggest__icon--bookmark {
    color: var(--accent);
    background: var(--accent-soft);
  }

  .address-suggest__icon--search {
    color: var(--text-muted);
  }

  .address-suggest__title {
    flex: 1;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .address-suggest__detail {
    flex: none;
    max-width: 34%;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    color: var(--text-muted);
    font-size: var(--text-xs);
  }

  .address-suggest__tag {
    flex: none;
    font-size: var(--text-xs);
    color: var(--accent);
  }

  .address-suggest__footer {
    padding: 5px 9px 3px;
    font-size: var(--text-xs);
    color: var(--text-muted);
    border-top: 1px solid var(--border);
    margin-top: 4px;
  }
</style>