<script lang="ts">
  import Icon from './Icon.svelte';

  // One empty-state surface for every panel: icon, title, explanation and an
  // optional call to action. Replaces the ad-hoc `.empty` divs so "nothing
  // here yet" looks intentional rather than unfinished.
  interface Props {
    icon: string;
    title: string;
    body?: string;
    compact?: boolean;
    actionLabel?: string;
    onaction?: () => void;
  }

  let { icon, title, body, compact = false, actionLabel, onaction }: Props = $props();
</script>

<div class="empty-state" class:empty-state--compact={compact}>
  <span class="empty-state__icon"><Icon name={icon} size={19} /></span>
  <div class="empty-state__title">{title}</div>
  {#if body}
    <p class="empty-state__body">{body}</p>
  {/if}
  {#if actionLabel && onaction}
    <button class="button button--primary button--sm empty-state__action" onclick={onaction}>
      {actionLabel}
    </button>
  {/if}
</div>

<style>
  .empty-state__action {
    margin-top: 10px;
  }
</style>