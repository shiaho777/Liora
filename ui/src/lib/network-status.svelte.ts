// Global view of whether the running kernel still uses an older network
// configuration. The DNS panel and the toolbar both read this, so leaving the
// panel does not hide a pending restart.
import { call } from './bridge';
import { configFingerprint, hasUnappliedChanges } from './kernel-switch-check';

export const networkStatus = $state({
  current: '',
  applied: '',
  loaded: false,
});

/** Re-reads both sides from Core. Safe to call often; it only issues two RPCs. */
export async function refreshNetworkStatus(): Promise<void> {
  const [runtime, config] = await Promise.allSettled([
    call<{ appliedConfig?: string }>('net.runtime'),
    call<{ config: unknown }>('net.config', {}),
  ]);
  if (runtime.status === 'fulfilled') networkStatus.applied = runtime.value.appliedConfig ?? '';
  if (config.status === 'fulfilled') {
    networkStatus.current = configFingerprint(config.value.config);
  }
  networkStatus.loaded = true;
}

export function networkPending(): boolean {
  return hasUnappliedChanges(networkStatus.current, networkStatus.applied);
}
