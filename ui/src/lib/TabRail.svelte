<script lang="ts">
  import { fly } from 'svelte/transition';
  import Icon from './Icon.svelte';
  import { t } from './i18n.svelte';
  import { internalIcon, type GroupedTabs, type Tab, type TabGroup } from './tab-types';

  const reduceMotion =
    typeof window !== 'undefined' &&
    window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  const tabMotion = reduceMotion ? { duration: 0 } : { duration: 180 };

  interface Props {
    tabs: Tab[];
    activeId: number;
    groups: TabGroup[];
    groupedTabs: GroupedTabs[];
    ungroupedTabs: Tab[];
    groupColor: (groupId?: string) => string;
    groupName: (groupId?: string) => string;
    containerColor: (id?: string) => string;
    containerName: (id?: string) => string;
    onselect: (id: number) => void;
    onclose: (id: number) => void;
    onnew: () => void;
    oncreategroup: () => void;
    onassigngroup: (tabId: number, groupId: string) => void;
    onungroup: (tabId: number) => void;
    onclosegroup: (groupId: string) => void;
    oncyclecolor: (group: TabGroup) => void;
    ontogglecollapse: (group: TabGroup) => void;
    onrenamegroup: (groupId: string, name: string) => void;
  }

  let {
    tabs,
    activeId,
    groups,
    groupedTabs,
    ungroupedTabs,
    groupColor,
    groupName,
    containerColor,
    containerName,
    onselect,
    onclose,
    onnew,
    oncreategroup,
    onassigngroup,
    onungroup,
    onclosegroup,
    oncyclecolor,
    ontogglecollapse,
    onrenamegroup,
  }: Props = $props();

  // Local UI state: search query and drag/rename bookkeeping. Data mutations
  // go back to the parent through callbacks.
  let tabQuery = $state('');
  let draggingTab = $state(0);
  let dragOverGroup = $state('');
  let renamingGroup = $state('');
  let groupNameDraft = $state('');
  let renameEl = $state<HTMLInputElement | null>(null);

  const visibleTabs = $derived.by(() => {
    const query = tabQuery.trim().toLowerCase();
    if (!query) return tabs;
    return tabs.filter((tab) => `${tab.title} ${tab.url}`.toLowerCase().includes(query));
  });

  function beginRename(group: TabGroup): void {
    renamingGroup = group.id;
    groupNameDraft = group.name;
    requestAnimationFrame(() => renameEl?.select());
  }

  function commitRename(): void {
    const name = groupNameDraft.trim();
    if (renamingGroup && name) onrenamegroup(renamingGroup, name);
    renamingGroup = '';
  }

  function dropOnGroup(event: DragEvent, groupId: string | null): void {
    event.preventDefault();
    const id = Number(event.dataTransfer?.getData('text/liora-tab'));
    if (id) {
      if (groupId) onassigngroup(id, groupId);
      else onungroup(id);
    }
    dragOverGroup = '';
    draggingTab = 0;
  }
</script>

{#snippet verticalTab(tab: Tab, showGroup = false)}
  <div
    class="tabrow"
    class:tabrow--active={tab.id === activeId}
    class:tabrow--dragging={draggingTab === tab.id}
    in:fly={{ y: -8, duration: tabMotion.duration }}
    role="tab"
    tabindex={tab.id === activeId ? 0 : -1}
    aria-selected={tab.id === activeId}
    draggable="true"
    onclick={() => onselect(tab.id)}
    onkeydown={(event) => {
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        onselect(tab.id);
      }
    }}
    onauxclick={(event) => {
      if (event.button === 1) {
        event.preventDefault();
        onclose(tab.id);
      }
    }}
    ondragstart={(event) => {
      draggingTab = tab.id;
      event.dataTransfer?.setData('text/liora-tab', String(tab.id));
      if (event.dataTransfer) event.dataTransfer.effectAllowed = 'move';
    }}
    ondragend={() => {
      draggingTab = 0;
      dragOverGroup = '';
    }}
    title={tab.groupId ? `${tab.url} · ${groupName(tab.groupId)}` : tab.url}
  >
    <span class="tabrow__favicon">
      {#if tab.page === 'web' && tab.favicon}
        <img src={tab.favicon} alt="" />
      {:else if tab.page === 'web'}
        <Icon name="globe" size={11} />
      {:else if tab.page === 'newtab'}
        L
      {:else}
        <Icon name={internalIcon(tab.page)} size={11} />
      {/if}
    </span>
    {#if tab.containerId}
      <span
        class="tabrow__container"
        style:background={containerColor(tab.containerId)}
        title={containerName(tab.containerId)}
      ></span>
    {/if}
    {#if tab.incognito}
      <span class="tabrow__private" title={t('tab.private')} aria-label={t('tab.private')}>
        <Icon name="eye" size={10} />
      </span>
    {/if}
    <span class="tabrow__title">{tab.title}</span>
    {#if showGroup && tab.groupId}
      <span
        class="tabrow__group"
        style:background={groupColor(tab.groupId)}
        title={groupName(tab.groupId)}
      ></span>
    {/if}
    <button
      class="tabrow__close"
      title={t('tab.close')}
      aria-label={t('tab.close')}
      tabindex="-1"
      onclick={(event) => {
        event.stopPropagation();
        onclose(tab.id);
      }}
    >
      <Icon name="close" size={9} />
    </button>
  </div>
{/snippet}

<aside class="tabrail" aria-label={t('tabs.label')}>
  <div class="tabrail__head">
    <span class="tabrail__label">
      {t('tabs.label')}
      <span class="tabrail__count">{tabs.length}</span>
    </span>
    <button class="icon-button" title={t('groups.new')} aria-label={t('groups.new')} onclick={oncreategroup}>
      <Icon name="folder" />
    </button>
    <button
      class="icon-button"
      title={t('tab.new')}
      aria-label={t('tab.new')}
      onclick={() => {
        tabQuery = '';
        onnew();
      }}
    >
      <Icon name="plus" />
    </button>
  </div>

  <label class="tabrail__search">
    <Icon name="search" size={12} />
    <input bind:value={tabQuery} placeholder={t('tabs.search')} aria-label={t('tabs.search')} spellcheck="false" />
    {#if tabQuery}
      <button class="tabrail__clear" title={t('tabs.clear')} aria-label={t('tabs.clear')} onclick={() => (tabQuery = '')}>
        <Icon name="close" size={9} />
      </button>
    {/if}
  </label>

  <div class="tabrail__list">
    {#if tabQuery.trim()}
      {#if visibleTabs.length === 0}
        <div class="tabrail__empty">{t('tabs.none')}</div>
      {:else}
        {#each visibleTabs as tab (tab.id)}
          {@render verticalTab(tab, true)}
        {/each}
      {/if}
    {:else}
      {#each groupedTabs as group (group.id)}
        <div class="tabgroup">
          <div
            class="tabgroup__head"
            class:tabgroup__head--active={group.tabs.some((tab) => tab.id === activeId)}
            class:tabgroup__head--drop={dragOverGroup === group.id}
            role="button"
            tabindex="0"
            onclick={() => ontogglecollapse(group)}
            onkeydown={(event) => {
              if (event.key === 'Enter' || event.key === ' ') {
                event.preventDefault();
                ontogglecollapse(group);
              }
            }}
            ondragover={(event) => {
              event.preventDefault();
              dragOverGroup = group.id;
            }}
            ondragleave={() => {
              if (dragOverGroup === group.id) dragOverGroup = '';
            }}
            ondrop={(event) => dropOnGroup(event, group.id)}
          >
            <span class="tabgroup__chevron" class:tabgroup__chevron--open={!group.collapsed}>
              <Icon name="chevron" size={9} />
            </span>
            <button
              class="tabgroup__dot"
              style:background={group.color}
              title={t('groups.color')}
              aria-label={t('groups.color')}
              onclick={(event) => {
                event.stopPropagation();
                oncyclecolor(group);
              }}
            ></button>
            {#if renamingGroup === group.id}
              <input
                class="tabgroup__rename"
                bind:this={renameEl}
                bind:value={groupNameDraft}
                onclick={(event) => event.stopPropagation()}
                onblur={commitRename}
                onkeydown={(event) => {
                  if (event.key === 'Enter') {
                    event.preventDefault();
                    commitRename();
                  } else if (event.key === 'Escape') {
                    event.preventDefault();
                    renamingGroup = '';
                  }
                }}
              />
            {:else}
              <span class="tabgroup__name">{group.name}</span>
            {/if}
            <span class="tabgroup__count">{group.tabs.length}</span>
            <button
              class="tabgroup__action"
              title={t('groups.rename')}
              aria-label={t('groups.rename')}
              onclick={(event) => {
                event.stopPropagation();
                beginRename(group);
              }}
            >
              <Icon name="pencil" size={11} />
            </button>
            <button
              class="tabgroup__action"
              title={t('groups.close')}
              aria-label={t('groups.close')}
              onclick={(event) => {
                event.stopPropagation();
                onclosegroup(group.id);
              }}
            >
              <Icon name="close" size={10} />
            </button>
          </div>
          {#if !group.collapsed}
            {#each group.tabs as tab (tab.id)}
              {@render verticalTab(tab, false)}
            {/each}
          {/if}
        </div>
      {/each}

      {#if groups.length > 0}
        <div
          class="tabgroup__head tabgroup__head--plain"
          class:tabgroup__head--drop={dragOverGroup === 'none'}
          role="presentation"
          title={t('groups.dropHint')}
          ondragover={(event) => {
            event.preventDefault();
            dragOverGroup = 'none';
          }}
          ondragleave={() => {
            if (dragOverGroup === 'none') dragOverGroup = '';
          }}
          ondrop={(event) => dropOnGroup(event, null)}
        >
          <span class="tabgroup__name">{t('groups.ungrouped')}</span>
          <span class="tabgroup__count">{ungroupedTabs.length}</span>
        </div>
      {/if}

      {#each ungroupedTabs as tab (tab.id)}
        {@render verticalTab(tab, false)}
      {/each}
    {/if}
  </div>

  <div class="tabrail__foot">{t('tabs.count', { count: tabs.length })}</div>
</aside>