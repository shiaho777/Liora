// Shared dismiss helper: every overlay fades/scales out before it unmounts,
// so closing feels like the inverse of opening instead of a hard cut.
// Kept free of Svelte so the timing rule can be unit-tested.

/** Exit duration; mirrors --fast so open/close feel symmetric. */
export const DISMISS_MS = 120;

/**
 * Adds `className` to the element, then runs `done` after the exit animation.
 * Under prefers-reduced-motion the callback runs immediately — the global
 * reduced-motion rule would skip the animation anyway, and delaying the
 * unmount would just feel laggy.
 */
export function dismissWith(
  el: HTMLElement | null,
  className: string,
  done: () => void,
  ms: number = DISMISS_MS,
): void {
  const reduced =
    typeof matchMedia === 'function' && matchMedia('(prefers-reduced-motion: reduce)').matches;
  if (!el || reduced || ms <= 0) {
    done();
    return;
  }
  el.classList.add(className);
  setTimeout(done, ms);
}