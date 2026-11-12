<script lang="ts">
  import type { Snippet } from 'svelte';
  import Icon from './Icon.svelte';

  interface Props {
    icon: string;
    title: string;
    subtitle?: string;
    actions?: Snippet;
  }

  let { icon, title, subtitle = '', actions }: Props = $props();
</script>

<header class="page-head">
  <span class="page-head__icon"><Icon name={icon} size={19} /></span>
  <div class="page-head__main">
    <h1 class="page-head__title">{title}</h1>
    {#if subtitle}<p class="page-head__subtitle">{subtitle}</p>{/if}
  </div>
  {#if actions}
    <div class="page-head__actions">{@render actions()}</div>
  {/if}
</header>

<style>
  .page-head {
    display: flex;
    align-items: flex-start;
    gap: 13px;
    padding-bottom: 18px;
    border-bottom: 1px solid var(--border);
  }

  .page-head__icon {
    width: 42px;
    height: 42px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 12px;
    color: var(--accent);
    background: var(--accent-soft);
    box-shadow: inset 0 0 0 1px rgba(59, 108, 246, 0.08);
  }

  .page-head__main {
    flex: 1;
    min-width: 0;
    padding-top: 1px;
  }

  .page-head__title {
    margin: 0;
    font-size: 22px;
    font-weight: 700;
    letter-spacing: -0.02em;
    line-height: 1.25;
  }

  .page-head__subtitle {
    margin: 5px 0 0;
    max-width: 66ch;
    font-size: 13px;
    line-height: 1.55;
    color: var(--text-muted);
  }

  .page-head__actions {
    flex: none;
    display: flex;
    align-items: center;
    gap: 8px;
    padding-top: 2px;
  }

  @media (max-width: 640px) {
    .page-head {
      flex-wrap: wrap;
    }

    .page-head__actions {
      width: 100%;
      justify-content: flex-end;
    }
  }
</style>
