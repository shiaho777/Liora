import { describe, expect, it } from 'vitest';
import { cefReadiness } from './kernel-signing';

describe('cefReadiness', () => {
  it('flags an ad-hoc build even when CEF has not been started yet', () => {
    const state = cefReadiness('ad-hoc (no Team ID, liora)', 'not selected');
    expect(state.level).toBe('warning');
    expect(state.reason).toBe('ad-hoc');
  });

  it('reports a Developer ID build as ready', () => {
    const state = cefReadiness('Developer ID (TEAM123456 / io.liora.browser)', 'not selected');
    expect(state.level).toBe('ok');
    expect(state.reason).toBe('signed');
  });

  it('prefers a concrete CEF failure over the signature summary', () => {
    const state = cefReadiness(
      'Developer ID (TEAM123456 / io.liora.browser)',
      'failed: Signature validation of current process failed (-67030)',
    );
    expect(state.level).toBe('danger');
    expect(state.reason).toBe('failed');
  });

  it('does not guess when neither value is known', () => {
    expect(cefReadiness('', '')).toEqual({ level: 'unknown', reason: 'unknown' });
    expect(cefReadiness('unknown', 'unknown')).toEqual({
      level: 'unknown',
      reason: 'unknown',
    });
  });

  it('treats unsigned variants as ad-hoc', () => {
    expect(cefReadiness('unsigned (no Team ID)', 'not selected').reason).toBe('ad-hoc');
  });
});
