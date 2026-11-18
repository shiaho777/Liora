<script lang="ts">
  import { flip } from 'svelte/animate';
  import { fly } from 'svelte/transition';
  import Icon from './Icon.svelte';
  import { t } from './i18n.svelte';
  import { internalIcon, type Tab } from './tab-types';

  // Respect the user's motion preference; the global CSS reduces duration too,
  // but skipping the JS animation entirely avoids layout work.
  const reduceMotion =
    typeof window !== 'undefined' &&
    window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  const tabMotion = reduceMotion ? { duration: 0 } : { duration: 180 };

  interface Props {
    tabs: Tab[];
    activeId: number;
    groupColor: (groupId?: string) => string;
    containerColor: (id?: string) => string;
    containerName: (id?: string) => string;
    onselect: (id: number) => void;
    onclose: (id: number) => void;
    onnew: () => void;
  }

  let { tabs, activeId, groupColor, containerColor, containerName, onselect, onclose, onnew }: Props = $props();

  let railEl = $state<HTMLDivElement | null>(null);

  // A tab opened beyond the visible strip must not stay out of reach.
  $effect(() => {
    void activeId;
    void tabs.length;
    const node = railEl?.querySelector<HTMLElement>('.tab--active');
    node?.scrollIntoView({ block: 'nearest', inline: 'nearest' });
  });

  // Arrow keys move between tabs, matching native tablist behaviour.
  function onRailKeydown(event: KeyboardEvent): void {
    if (event.key !== 'ArrowRight' && event.key !== 'ArrowLeft') return;
    const index = tabs.findIndex((tab) => tab.id === activeId);
    if (index < 0) return;
    event.preventDefault();
    const delta = event.key === 'ArrowRight' ? 1 : -1;
    const next = tabs[(index + delta + tabs.length) % tabs.length];
    onselect(next.id);
    railEl?.querySelectorAll<HTMLElement>('.tab')[(index + delta + tabs.length) % tabs.length]?.focus();
  }
</script>

<div class="tabstrip">
  <!-- svelte-ignore a11y_interactive_supports_focus -->
  <!-- Focus lives on the active tab (roving tabindex), not on the container. -->
  <div
    class="tabstrip__rail"
    bind:this={railEl}
    role="tablist"
    aria-label={t('tab.list')}
    onkeydown={onRailKeydown}
  >
    {#each tabs as tab (tab.id)}
      <div
        class="tab"
        class:tab--active={tab.id === activeId}
        class:tab--grouped={Boolean(tab.groupId)}
        style:--group-color={groupColor(tab.groupId)}
        animate:flip={tabMotion}
        in:fly={{ x: -12, duration: tabMotion.duration }}
        role="tab"
        tabindex={tab.id === activeId ? 0 : -1}
        aria-selected={tab.id === activeId}
        onclick={() => onselect(tab.id)}
        onkeydown={(event) => {
          if (event.key === 'Enter' || event.key === ' ') {
            event.preventDefault();
            onselect(tab.id);
          }
        }}
        onauxclick={(event) => {
          // Middle-click closes the tab, the universal browser convention.
          if (event.button === 1) {
            event.preventDefault();
            onclose(tab.id);
          }
        }}
        ondblclick={() => onnew()}
        title={tab.title}
      >
        <span class="tab__favicon">
          {#if tab.page === 'web' && tab.favicon}
            <img src={tab.favicon} alt="" />
          {:else if tab.page === 'web'}
            <Icon name="globe" size={10} />
          {:else if tab.page === 'newtab'}
            L
          {:else}
            <Icon name={internalIcon(tab.page)} size={10} />
          {/if}
        </span>
        {#if tab.containerId}
          <span
            class="tab__container"
            style:background={containerColor(tab.containerId)}
            title={containerName(tab.containerId)}
          ></span>
        {/if}
        <span class="tab__title">{tab.title}</span>
        <button
          class="tab__close"
          title={t('tab.close')}
          aria-label={t('tab.close')}
          tabindex="-1"
          onclick={(event) => {
            event.stopPropagation();
            onclose(tab.id);
          }}
        >
          <Icon name="close" size={10} />
        </button>
      </div>
    {/each}
  </div>
  <button class="icon-button" title={t('tab.new')} aria-label={t('tab.new')} onclick={() => onnew()}>
    <Icon name="plus" />
  </button>
</div>