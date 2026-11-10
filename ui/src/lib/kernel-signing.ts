// Explains whether the Chromium kernel can run with the way this build is
// signed. Chromium validates helper processes against the app's Team ID, so an
// ad-hoc build fails with -67030 even though the component is installed.

export type CefReadinessLevel = 'ok' | 'warning' | 'danger' | 'unknown';
export type CefReadinessReason = 'signed' | 'ad-hoc' | 'failed' | 'ready' | 'unknown';

export interface CefReadiness {
  level: CefReadinessLevel;
  reason: CefReadinessReason;
}

/**
 * `cefStatus` is what the shell reported at launch: "ready", "not selected",
 * or "failed: ...". A failure is the most specific signal, so it wins over the
 * signature heuristics.
 */
/** True when an action on this kernel should ask for confirmation first. */
export function cefNeedsConfirmation(kernelId: string, readiness: CefReadiness): boolean {
  return kernelId === 'cef' && readiness.level !== 'ok';
}

export function cefReadiness(codeSigning: string, cefStatus: string): CefReadiness {
  const status = (cefStatus ?? '').trim();
  if (/^failed/i.test(status)) return { level: 'danger', reason: 'failed' };
  // A ready CEF process is proof that the current signature is sufficient,
  // e.g. when single-process compatibility mode is enabled.
  if (/^ready/i.test(status)) return { level: 'ok', reason: 'ready' };

  const signature = (codeSigning ?? '').trim();
  if (/ad-?hoc|unsigned/i.test(signature)) return { level: 'warning', reason: 'ad-hoc' };
  if (/developer id/i.test(signature)) return { level: 'ok', reason: 'signed' };
  return { level: 'unknown', reason: 'unknown' };
}
