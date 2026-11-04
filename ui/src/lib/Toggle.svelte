<script lang="ts">
  interface Props {
    checked: boolean;
    label?: string;
    disabled?: boolean;
    onchange?: (value: boolean) => void;
  }

  let { checked, label = '', disabled = false, onchange }: Props = $props();
</script>

<button
  type="button"
  class="toggle"
  class:toggle--on={checked}
  role="switch"
  aria-checked={checked}
  aria-label={label}
  {disabled}
  style:opacity={disabled ? 0.45 : 1}
  onclick={() => onchange?.(!checked)}
>
  <span class="toggle__knob"></span>
</button>
