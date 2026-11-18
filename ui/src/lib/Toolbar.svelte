<script lang="ts">
  import AddressSuggest from './AddressSuggest.svelte';
  import Icon from './Icon.svelte';
  import { t } from './i18n.svelte';
  import { internalIcon, type Tab } from './tab-types';
  import type { AddressBadge } from './address-badges';
  import type { Suggestion } from './suggest';

  interface Props {
    active: Tab;
    addressValue: string;
    addressFocused: boolean;
    addressEl: HTMLInputElement | null;
    badges: AddressBadge[];
    suggestions: Suggestion[];
    suggestSelected: number;
    onsuggestpick: (item: Suggestion) => void;
    onsuggesthover: (index: number) => void;
    onaddresskeydown: (event: KeyboardEvent) => void;
    activeContainer: { name: string; color: string } | null;
    showHomeButton: boolean;
    homePage: string;
    showNetworkPending: boolean;
    applyState: string;
    applyTitle: string;
    appliedProxyPort: string;
    splitActive: boolean;
    showSplitButton: boolean;
    readerActive: boolean;
    activeBookmarked: boolean;
    downloadCount: number;
    downloadsActive: boolean;
    onback: () => void;
    onforward: () => void;
    onreload: () => void;
    onhome: () => void;
    onaddressinput: (value: string) => void;
    onaddressfocus: () => void;
    onaddressblur: () => void;
    onsubmit: (event: Event) => void;
    onsitecard: () => void;
    oncontainer: () => void;
    onapplynetwork: () => void;
    onpalette: () => void;
    onsplit: () => void;
    onreader: () => void;
    onbookmark: () => void;
    ondownloads: () => void;
    onsettings: () => void;
  }

  let {
    active,
    addressValue,
    addressFocused,
    addressEl,
    badges,
    suggestions,
    suggestSelected,
    onsuggestpick,
    onsuggesthover,
    onaddresskeydown,
    activeContainer,
    showHomeButton,
    homePage,
    showNetworkPending,
    applyState,
    applyTitle,
    appliedProxyPort,
    splitActive,
    showSplitButton,
    readerActive,
    activeBookmarked,
    downloadCount,
    downloadsActive,
    onback,
    onforward,
    onreload,
    onhome,
    onaddressinput,
    onaddressfocus,
    onaddressblur,
    onsubmit,
    onsitecard,
    oncontainer,
    onapplynetwork,
    onpalette,
    onsplit,
    onreader,
    onbookmark,
    ondownloads,
    onsettings,
  }: Props = $props();
</script>

<div class="toolbar">
  <div class="toolbar__group">
    <button class="icon-button" title={t('nav.back')} aria-label={t('nav.back')} disabled={!active.canBack} onclick={onback}>
      <Icon name="back" />
    </button>
    <button class="icon-button" title={t('nav.forward')} aria-label={t('nav.forward')} disabled={!active.canForward} onclick={onforward}>
      <Icon name="forward" />
    </button>
    <button class="icon-button" title={t('nav.reload')} aria-label={t('nav.reload')} onclick={onreload}>
      <Icon name="reload" />
    </button>
    {#if showHomeButton}
      <button class="icon-button" title={t('nav.home')} aria-label={t('nav.home')} onclick={onhome}>
        <Icon name="home" />
      </button>
    {/if}
  </div>

  <form class="address" onsubmit={onsubmit}>
    <span class="address__scheme">
      {#if active.page === 'web'}
        <button
          type="button"
          class="address__scheme-button"
          title={t('site.title')}
          aria-label={t('site.title')}
          onclick={onsitecard}
        >
          <Icon name="globe" size={12} />
        </button>
      {:else}
        <span class="address__scheme-icon">
          <Icon name={internalIcon(active.page)} size={12} />
        </span>
      {/if}
    </span>
    {#if activeContainer}
      <button
        type="button"
        class="address__container"
        style:background={activeContainer.color}
        title={`${t('containers.title')} · ${activeContainer.name}`}
        aria-label={`${t('containers.title')} · ${activeContainer.name}`}
        onclick={oncontainer}
      ></button>
    {/if}
    <input
      class="address__input"
      bind:this={addressEl}
      value={addressValue}
      oninput={(event) => onaddressinput((event.target as HTMLInputElement).value)}
      onfocus={onaddressfocus}
      onblur={onaddressblur}
      onkeydown={onaddresskeydown}
      placeholder={t('address.placeholder')}
      aria-label={t('address.placeholder')}
      role="combobox"
      aria-expanded={addressFocused && suggestions.length > 0}
      aria-controls="address-suggest-list"
      aria-activedescendant={suggestSelected >= 0 ? `address-suggest-${suggestSelected}` : undefined}
      spellcheck="false"
      autocomplete="off"
    />
    {#if !addressFocused && active.incognito}
      <span class="address__hint address__hint--private" title={t('tab.private')}>
        <Icon name="eye" size={11} />
        {t('tab.private')}
      </span>
    {/if}
    {#if !addressFocused && active.page === 'web'}
      {#each badges as badge (badge.key)}
        <span class="address__hint address__hint--{badge.tone}" title={badge.detail}>
          {badge.label}
        </span>
      {/each}
    {/if}
    {#if addressFocused}
      <AddressSuggest
        items={suggestions}
        selected={suggestSelected}
        onpick={onsuggestpick}
        onhover={onsuggesthover}
      />
    {/if}
  </form>

  <div class="toolbar__group">
    {#if showNetworkPending}
      <button
        class="network-pending"
        class:network-pending--applied={applyState === 'applied'}
        class:network-pending--error={applyState === 'error'}
        title={applyTitle}
        disabled={applyState === 'applying' || applyState === 'applied'}
        onclick={onapplynetwork}
      >
        <span class="dot"></span>
        {applyState === 'applying'
          ? t('network.applying')
          : applyState === 'applied'
            ? appliedProxyPort
              ? t('network.appliedWithPort', { port: appliedProxyPort })
              : t('network.applied')
            : applyState === 'error'
              ? t('network.applyFailed')
              : t('network.pending')}
      </button>
    {/if}
    <button class="icon-button" title={t('palette.shortcut')} aria-label={t('palette.shortcut')} onclick={onpalette}>
      <Icon name="command" />
    </button>
    {#if active.page === 'web'}
      {#if showSplitButton}
        <button
          class="icon-button"
          class:icon-button--active={splitActive}
          title={splitActive ? t('split.close') : t('split.open')}
          aria-label={splitActive ? t('split.close') : t('split.open')}
          onclick={onsplit}
        >
          <Icon name="columns" />
        </button>
      {/if}
      <button
        class="icon-button"
        class:icon-button--active={readerActive}
        title={t('reader.open')}
        aria-label={t('reader.open')}
        onclick={onreader}
      >
        <Icon name="book" />
      </button>
      <button
        class="icon-button"
        class:icon-button--active={activeBookmarked}
        title={activeBookmarked ? t('bookmarks.remove') : t('bookmarks.add')}
        aria-label={activeBookmarked ? t('bookmarks.remove') : t('bookmarks.add')}
        onclick={onbookmark}
      >
        <Icon name="star" />
      </button>
    {/if}
    <button
      class="icon-button"
      class:icon-button--active={downloadsActive}
      title={t('downloads.title')}
      aria-label={t('downloads.title')}
      onclick={ondownloads}
    >
      <Icon name="download" />
      {#if downloadCount > 0}
        <span class="icon-badge">{downloadCount}</span>
      {/if}
    </button>
    <button class="icon-button" title={t('nav.settings')} aria-label={t('nav.settings')} onclick={onsettings}>
      <Icon name="settings" />
    </button>
  </div>
</div>

