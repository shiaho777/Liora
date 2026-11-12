<script lang="ts">
  // A single icon set keeps stroke weight and sizing consistent.
  interface Props {
    name: string;
    size?: number;
    stroke?: number;
  }

  let { name, size = 16, stroke = 1.7 }: Props = $props();

  const paths: Record<string, string> = {
    back: 'M10 3 5 8l5 5M5 8h6.5A4.5 4.5 0 0 1 16 12.5V13',
    forward: 'M6 3l5 5-5 5M11 8H4.5A4.5 4.5 0 0 0 0 12.5V13',
    reload: 'M13.5 8a5.5 5.5 0 1 1-1.7-4M13.5 2v3h-3',
    home: 'M2.5 7 8 2.5 13.5 7v6.5h-4v-4h-3v4h-4z',
    plus: 'M8 3v10M3 8h10',
    close: 'M4 4l8 8M12 4l-8 8',
    settings:
      'M8 10.2a2.2 2.2 0 1 0 0-4.4 2.2 2.2 0 0 0 0 4.4zM12.9 9.7l1 .7-1 1.7-1.1-.4a3.9 3.9 0 0 1-1 .6l-.2 1.2H8.4l-.2-1.2a3.9 3.9 0 0 1-1-.6l-1.1.4-1-1.7 1-.7a3.9 3.9 0 0 1 0-1.2l-1-.7 1-1.7 1.1.4a3.9 3.9 0 0 1 1-.6l.2-1.2h2.2l.2 1.2a3.9 3.9 0 0 1 1 .6l1.1-.4 1 1.7-1 .7a3.9 3.9 0 0 1 0 1.2z',
    search: 'M7.2 12a4.8 4.8 0 1 0 0-9.6 4.8 4.8 0 0 0 0 9.6zM10.8 10.8 14 14',
    shield: 'M8 2 13 4v4.2c0 3-2.1 5.1-5 5.8-2.9-.7-5-2.8-5-5.8V4z',
    globe: 'M8 14A6 6 0 1 0 8 2a6 6 0 0 0 0 12zM2.2 8h11.6M8 2c1.6 1.7 2.4 3.7 2.4 6S9.6 12.3 8 14c-1.6-1.7-2.4-3.7-2.4-6S6.4 3.7 8 2z',
    history: 'M8 14A6 6 0 1 0 4.2 2.2M2 2v3.2h3.2M8 4.8V8l2.4 1.6',
    download: 'M8 2v8M5 7l3 3 3-3M2.5 13.5h11',
    puzzle: 'M6 2.5h4v1.8a1.4 1.4 0 0 0 2.8 0V2.5h.7v4h1.8a1.4 1.4 0 0 1 0 2.8h-1.8v4.2H9.8a1.4 1.4 0 0 1-2.8 0H4.5V9.3H2.7a1.4 1.4 0 0 1 0-2.8H4.5v-4H6z',
    check: 'M3 8.5 6.2 11.5 13 4.5',
    warning: 'M8 2.5 14.5 13.5h-13zM8 6.5v3.2M8 11.6v.6',
    info: 'M8 14A6 6 0 1 0 8 2a6 6 0 0 0 0 12zM8 7.2v4M8 5v.6',
    chevron: 'M6 4l4 4-4 4',
    trash: 'M3 4.5h10M6 4.5V3h4v1.5M4.5 4.5l.6 9h5.8l.6-9',
    refresh: 'M13.5 8a5.5 5.5 0 1 1-1.7-4M13.5 2v3h-3',
    external: 'M9 3h4v4M13 3 7.5 8.5M11 9.5V13H3V5h3.5',
    folder: 'M2 4.5h4l1.2 1.6H14V13H2z',
    star: 'M8 2.5 9.8 6l3.7.5-2.7 2.6.7 3.7L8 11l-3.5 1.8.7-3.7L2.5 6.5 6.2 6z',
    sliders: 'M3 5h10M3 11h10M6 3v4M10 9v4',
    server: 'M2.5 4.5h11v3h-11zM2.5 9.5h11v3h-11zM5 6h.5M5 11h.5',
    pencil: 'M3 13l.6-2.6 7.2-7.2 2 2-7.2 7.2L3 13zM9.6 4.4l2 2',
    book: 'M8 4.2C6.6 3.2 4.7 3.2 2.8 4v8.4c1.9-.8 3.8-.8 5.2.2 1.4-1 3.3-1 5.2-.2V4c-1.9-.8-3.8-.8-5.2.2zM8 4.2v8.4',
  };
</script>

<svg
  width={size}
  height={size}
  viewBox="0 0 16 16"
  fill="none"
  stroke="currentColor"
  stroke-width={stroke}
  stroke-linecap="round"
  stroke-linejoin="round"
  aria-hidden="true"
>
  {#if name === 'settings'}
    <path d={paths.settings} />
  {:else}
    <path d={paths[name] ?? paths.info} />
  {/if}
</svg>
