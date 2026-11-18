import { afterEach, describe, expect, it, vi } from 'vitest';
import { DISMISS_MS, dismissWith } from './dismiss';

afterEach(() => {
  vi.useRealTimers();
  vi.unstubAllGlobals();
});

/** Minimal element stub: the helper only touches classList.add. */
function stubElement(): { el: HTMLElement; added: string[] } {
  const added: string[] = [];
  const el = { classList: { add: (name: string) => added.push(name) } } as unknown as HTMLElement;
  return { el, added };
}

describe('dismissWith', () => {
  it('adds the exit class and closes after the animation', () => {
    vi.useFakeTimers();
    vi.stubGlobal('matchMedia', () => ({ matches: false }));
    const { el, added } = stubElement();
    const done = vi.fn();
    dismissWith(el, 'overlay-out', done);
    expect(added).toEqual(['overlay-out']);
    expect(done).not.toHaveBeenCalled();
    vi.advanceTimersByTime(DISMISS_MS);
    expect(done).toHaveBeenCalledTimes(1);
  });

  it('closes immediately without an element', () => {
    const done = vi.fn();
    dismissWith(null, 'overlay-out', done);
    expect(done).toHaveBeenCalledTimes(1);
  });

  it('closes immediately under reduced motion', () => {
    vi.stubGlobal('matchMedia', () => ({ matches: true }));
    const { el, added } = stubElement();
    const done = vi.fn();
    dismissWith(el, 'overlay-out', done);
    expect(done).toHaveBeenCalledTimes(1);
    expect(added).toEqual([]);
  });
});