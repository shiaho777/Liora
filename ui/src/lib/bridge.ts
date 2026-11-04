// JSON-RPC 2.0 bridge to Liora Core.
//
// The shell injects one of the transports below and calls
// window.__lioraDeliver(text) for every message coming from Core.
// In a normal browser a small in-memory backend is used instead, so the whole
// interface can be developed and reviewed without the native shell.

export interface Notification {
  method: string;
  params?: unknown;
}

type Pending = {
  resolve: (value: unknown) => void;
  reject: (error: RpcFailure) => void;
};

export class RpcFailure extends Error {
  code: number;
  data: unknown;

  constructor(code: number, message: string, data?: unknown) {
    super(message);
    this.code = code;
    this.data = data;
  }
}

interface Transport {
  name: string;
  send: (text: string) => void;
}

declare global {
  interface Window {
    __lioraDeliver?: (text: string) => void;
    webkit?: { messageHandlers?: Record<string, { postMessage: (value: unknown) => void }> };
    chrome?: { webview?: { postMessage: (value: unknown) => void } };
    LioraAndroid?: { postMessage: (value: unknown) => void };
    LioraBridge?: { postMessage: (value: unknown) => void };
  }
}

const listeners = new Set<(n: Notification) => void>();
const pending = new Map<number, Pending>();
let nextId = 1;
let transport: Transport | null = null;
let started = false;

function detectTransport(): Transport | null {
  if (window.webkit?.messageHandlers?.liora) {
    return { name: 'webkit', send: (text) => window.webkit!.messageHandlers!.liora.postMessage(text) };
  }
  if (window.chrome?.webview) {
    return { name: 'webview2', send: (text) => window.chrome!.webview!.postMessage(text) };
  }
  if (window.LioraAndroid) {
    return { name: 'android', send: (text) => window.LioraAndroid!.postMessage(text) };
  }
  if (window.LioraBridge) {
    return { name: 'custom', send: (text) => window.LioraBridge!.postMessage(text) };
  }
  return null;
}

function handleMessage(raw: string): void {
  let message: unknown;
  try {
    message = JSON.parse(raw);
  } catch {
    return;
  }
  if (Array.isArray(message)) {
    for (const item of message) handleMessage(JSON.stringify(item));
    return;
  }
  if (!message || typeof message !== 'object') return;
  const envelope = message as Record<string, unknown>;

  if (typeof envelope.id === 'number') {
    const entry = pending.get(envelope.id);
    if (!entry) return;
    pending.delete(envelope.id);
    const error = envelope.error as { code?: number; message?: string; data?: unknown } | undefined;
    if (error) {
      entry.reject(new RpcFailure(error.code ?? -32603, error.message ?? 'request failed', error.data));
    } else {
      entry.resolve(envelope.result ?? null);
    }
    return;
  }

  if (typeof envelope.method === 'string') {
    const notification: Notification = {
      method: envelope.method,
      params: envelope.params,
    };
    for (const listener of listeners) listener(notification);
  }
}

export async function start(): Promise<string> {
  if (started) return transport?.name ?? 'none';
  started = true;
  window.__lioraDeliver = handleMessage;

  transport = detectTransport();
  if (!transport) {
    // Development mode: the mock backend answers in-process.
    const { createMockBackend } = await import('./mock');
    transport = { name: 'mock', send: (text) => createMockBackend().handle(text) };
  }
  return transport.name;
}

export function transportName(): string {
  return transport?.name ?? 'none';
}

export function call<T = unknown>(method: string, params?: unknown): Promise<T> {
  if (!transport) return Promise.reject(new RpcFailure(-32000, 'bridge is not connected'));
  const id = nextId++;
  return new Promise<T>((resolve, reject) => {
    pending.set(id, { resolve: resolve as (value: unknown) => void, reject });
    transport!.send(JSON.stringify({ jsonrpc: '2.0', id, method, params: params ?? {} }));
  });
}

export function onNotification(listener: (n: Notification) => void): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

// Fire-and-forget message for the shell (window layout, navigation, ...).
// Methods starting with "ui." are handled by the shell, not by Core.
export function notify(method: string, params?: unknown): void {
  transport?.send(JSON.stringify({ jsonrpc: '2.0', method, params: params ?? {} }));
}

// Convenience wrappers used across the interface.

export function getSetting<T>(key: string, site?: string, tab?: number): Promise<T> {
  return call<Record<string, T>>('settings.get', { key, site, tab }).then((values) => values[key]);
}

export function setSetting(key: string, value: unknown, site?: string, tab?: number): Promise<void> {
  return call('settings.set', { key, value, site, tab }).then(() => undefined);
}

export function resetSetting(key: string, site?: string, tab?: number): Promise<void> {
  return call('settings.reset', { key, site, tab }).then(() => undefined);
}

export function describeSettings(site?: string): Promise<SettingsDescription> {
  return call('settings.describe', { site });
}

export interface SettingItem {
  key: string;
  default: unknown;
  value: unknown;
  scopes: string[];
  advanced: boolean;
  choices?: string[];
}

export interface SettingsDescription {
  items: SettingItem[];
  sites: Array<{ site: string; values: Record<string, unknown> }>;
  overrides?: Record<string, unknown>;
}
