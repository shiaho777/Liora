// Focus containment for modal surfaces. While a dialog is open, Tab must cycle
// within it instead of escaping to the content behind. One helper keeps the
// behaviour identical across the palette, site card, container picker and
// watch dialog.

const FOCUSABLE =
  'a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])';

function focusableWithin(container: HTMLElement): HTMLElement[] {
  return [...container.querySelectorAll<HTMLElement>(FOCUSABLE)].filter(
    (el) => el.offsetParent !== null || el === document.activeElement,
  );
}

/**
 * Traps Tab/Shift+Tab inside `container`. Returns a cleanup function.
 * Call it when the dialog mounts; invoke the cleanup when it unmounts.
 */
export function trapFocus(container: HTMLElement): () => void {
  function onKeydown(event: KeyboardEvent): void {
    if (event.key !== 'Tab') return;
    const items = focusableWithin(container);
    if (items.length === 0) {
      event.preventDefault();
      container.focus();
      return;
    }
    const first = items[0];
    const last = items[items.length - 1];
    const active = document.activeElement as HTMLElement | null;

    if (event.shiftKey) {
      if (active === first || active === container || !container.contains(active)) {
        event.preventDefault();
        last.focus();
      }
    } else if (active === last || !container.contains(active)) {
      event.preventDefault();
      first.focus();
    }
  }

  container.addEventListener('keydown', onKeydown);
  return () => container.removeEventListener('keydown', onKeydown);
}