<script lang="ts">
  // One loading surface for every panel. `rows` renders list-style skeletons;
  // the default renders a title plus two lines, matching a settings card.
  interface Props {
    rows?: number;
    label?: string;
  }

  let { rows = 0, label }: Props = $props();
</script>

<div class="loading-state" aria-busy="true" aria-label={label ?? 'Loading'}>
  {#if rows > 0}
    {#each Array(rows) as _, index (index)}
      <span class="skeleton-row"></span>
    {/each}
  {:else}
    <span class="skeleton skeleton--title"></span>
    <span class="skeleton skeleton--line"></span>
    <span class="skeleton skeleton--line skeleton--short"></span>
  {/if}
</div>

<style>
  .loading-state {
    display: flex;
    flex-direction: column;
    gap: 10px;
    padding: 6px 0;
  }
</style>