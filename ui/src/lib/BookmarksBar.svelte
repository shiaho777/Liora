<script lang="ts">
  import Icon from './Icon.svelte';
  import { t } from './i18n.svelte';
  import { countBookmarks, type BookmarkNode } from './bookmark-tree';

  interface Props {
    nodes: BookmarkNode[];
    onnavigate: (url: string) => void;
  }

  let { nodes, onnavigate }: Props = $props();

  let openId = $state<number | null>(null);
  let barEl = $state<HTMLDivElement | null>(null);

  const hasContent = $derived(nodes.length > 0);

  function close(): void {
    openId = null;
  }

  function pick(url: string): void {
    close();
    onnavigate(url);
  }

  function toggle(id: number): void {
    openId = openId === id ? null : id;
  }

  // Close the open menu when clicking anywhere outside the bar.
  function onWindowPointerdown(event: PointerEvent): void {
    if (openId === null) return;
    if (barEl && !barEl.contains(event.target as Node)) close();
  }

  function onWindowKeydown(event: KeyboardEvent): void {
    if (event.key === 'Escape') close();
  }
</script>

<svelte:window onpointerdown={onWindowPointerdown} onkeydown={onWindowKeydown} />

{#snippet menuItems(children: BookmarkNode[])}
  {#each children as child (child.id)}
    {#if child.type === 'folder'}
      <div class="bm-menu__sub">
        <button
          class="bm-menu__item bm-menu__item--folder"
          aria-haspopup="true"
          onclick={(event) => event.stopPropagation()}
        >
          <Icon name="folder" size={13} />
          <span class="bm-menu__title">{child.title}</span>
          <span class="bm-menu__count">{countBookmarks(child.children ?? [])}</span>
          <span class="bm-menu__arrow"><Icon name="chevron" size={11} /></span>
        </button>
        <div class="bm-menu bm-menu--nested" role="menu">
          {@render menuItems(child.children ?? [])}
        </div>
      </div>
    {:else if child.url}
      <button class="bm-menu__item" role="menuitem" title={child.url} onclick={() => pick(child.url!)}>
        <Icon name="star" size={13} />
        <span class="bm-menu__title">{child.title || child.url}</span>
      </button>
    {/if}
  {/each}
{/snippet}

{#if hasContent}
  <div class="bookmarksbar" bind:this={barEl}>
    <span class="bookmarksbar__label">
      <Icon name="star" size={11} />
      {t('bookmarks.bar')}
    </span>
    {#each nodes as node (node.id)}
      {#if node.type === 'folder'}
        <div class="bm-folder">
          <button
            class="bookmark-chip bm-folder__chip"
            class:bm-folder__chip--open={openId === node.id}
            aria-haspopup="true"
            aria-expanded={openId === node.id}
            onclick={() => toggle(node.id)}
          >
            <Icon name="folder" size={12} />
            {node.title}
            <Icon name="chevron" size={10} />
          </button>
          {#if openId === node.id}
            <div class="bm-menu" role="menu" aria-label={node.title}>
              {@render menuItems(node.children ?? [])}
            </div>
          {/if}
        </div>
      {:else if node.url}
        <button class="bookmark-chip" title={node.url} onclick={() => pick(node.url!)}>
          {node.title || node.url}
        </button>
      {/if}
    {/each}
  </div>
{/if}

<style>
  .bm-folder {
    position: relative;
    flex: none;
  }

  .bm-folder__chip--open {
    background: var(--bg-hover);
    color: var(--text);
  }

  .bm-menu {
    position: absolute;
    top: calc(100% + 4px);
    left: 0;
    z-index: 70;
    min-width: 220px;
    max-width: 320px;
    max-height: 380px;
    overflow-y: auto;
    padding: 5px;
    background: var(--surface-strong);
    border: 1px solid var(--border-strong);
    border-radius: var(--radius-md);
    box-shadow: var(--shadow-lg);
    animation: bm-in var(--fast) var(--ease-out);
  }

  @keyframes bm-in {
    from {
      opacity: 0;
      transform: translateY(-3px);
    }
    to {
      opacity: 1;
      transform: none;
    }
  }

  .bm-menu__item {
    display: flex;
    align-items: center;
    gap: 8px;
    width: 100%;
    padding: 6px 8px;
    border: 0;
    border-radius: var(--radius-sm);
    background: transparent;
    color: var(--text);
    font-size: var(--text-sm);
    text-align: left;
    cursor: default;
  }

  .bm-menu__item:hover {
    background: var(--bg-hover);
  }

  .bm-menu__title {
    flex: 1;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .bm-menu__count {
    flex: none;
    font-size: var(--text-2xs);
    color: var(--text-muted);
  }

  .bm-menu__arrow {
    flex: none;
    color: var(--text-muted);
    transform: rotate(0deg);
  }

  .bm-menu__sub {
    position: relative;
  }

  .bm-menu--nested {
    display: none;
    top: -5px;
    left: calc(100% + 2px);
  }

  .bm-menu__sub:hover > .bm-menu--nested,
  .bm-menu__sub:focus-within > .bm-menu--nested {
    display: block;
  }
</style>