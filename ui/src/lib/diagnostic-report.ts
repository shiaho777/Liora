// Parser for the plain-text diagnostic report written by the failure page.
// The first line is "report: <version>"; a future version can extend the
// fields without breaking older readers.

export const SUPPORTED_REPORT_VERSION = 1;

export interface TlsSummary {
  /** "subject=..." form parsed from the TLS line, or a status sentence. */
  status: 'valid' | 'invalid' | 'failed' | 'absent' | 'unknown';
  subject?: string;
  expires?: string;
  raw: string;
}

export interface RuleSummary {
  matched: boolean;
  name?: string;
  h3?: string;
  ech?: boolean;
  raw: string;
}

export interface DiagnosticReport {
  version: number;
  generated?: string;
  appVersion?: string;
  platform?: string;
  url?: string;
  host?: string;
  errorCode?: number;
  errorReason?: string;
  kernel?: string;
  /** Chromium/Core switches handed to the kernel at start-up. */
  switches: string[];
  /** Capabilities the kernel explicitly cannot apply. */
  limits: string[];
  /** Installed components, "id@version" with "*" for the active version. */
  components: string[];
  /** How the application itself is code signed. */
  signature?: string;
  /** Whether the Chromium kernel started, failed, or was not selected. */
  cef?: string;
  tls: TlsSummary;
  rule: RuleSummary;
  upstream?: { protocol: string; id: string; address: string; elapsedMs?: number };
  addresses: string[];
  https?: { alpn: string[]; echBytes: number };
  notes?: string;
  /** Fields this version does not know about, kept for display. */
  extra: Record<string, string>;
  raw: string;
}

export type ParseResult =
  | { ok: true; report: DiagnosticReport }
  | { ok: false; error: string };

function parseError(value: string): { code?: number; reason?: string } {
  const match = /^\s*(-?\d+)\s*(?:-\s*(.*))?$/.exec(value);
  if (!match) return { reason: value };
  return { code: Number(match[1]), reason: match[2]?.trim() };
}

function splitList(value: string | undefined, separator: string): string[] {
  if (!value) return [];
  const trimmed = value.trim();
  if (!trimmed || trimmed.toLowerCase() === 'none') return [];
  return trimmed
    .split(separator)
    .map((item) => item.trim())
    .filter(Boolean);
}

function parseTls(value: string): TlsSummary {
  const lower = value.toLowerCase();
  if (lower.startsWith('subject=')) {
    const fields = new Map<string, string>();
    for (const token of value.split(/\s+/)) {
      const eq = token.indexOf('=');
      if (eq > 0) fields.set(token.slice(0, eq).toLowerCase(), token.slice(eq + 1));
    }
    const valid = fields.get('valid');
    return {
      status: valid === 'yes' ? 'valid' : valid === 'no' ? 'invalid' : 'unknown',
      subject: fields.get('subject'),
      expires: fields.get('expires'),
      raw: value,
    };
  }
  if (lower.includes('handshake failed')) return { status: 'failed', raw: value };
  if (lower.includes('not established')) return { status: 'absent', raw: value };
  return { status: 'unknown', raw: value };
}

function parseRule(value: string): RuleSummary {
  if (value.trim().toLowerCase() === 'none') return { matched: false, raw: value };
  const match = /^(.*?)\s*\(h3=([^,]+),\s*ech=(true|false)\)\s*$/.exec(value);
  if (!match) return { matched: true, name: value.trim(), raw: value };
  return { matched: true, name: match[1], h3: match[2], ech: match[3] === 'true', raw: value };
}

function parseUpstream(value: string): DiagnosticReport['upstream'] {
  const tokens = value.trim().split(/\s+/);
  if (tokens.length < 3) return undefined;
  const protocol = tokens[0];
  const id = tokens[1];
  const last = tokens[tokens.length - 1];
  const elapsed = /^(\d+)ms$/.exec(last);
  const address = tokens.slice(2, elapsed ? tokens.length - 1 : tokens.length).join(' ');
  return { protocol, id, address, elapsedMs: elapsed ? Number(elapsed[1]) : undefined };
}

function parseHttps(value: string): DiagnosticReport['https'] {
  const alpn = /alpn=([^\s]*)/.exec(value)?.[1] ?? '';
  const ech = /ech=(\d+)B/.exec(value)?.[1] ?? '0';
  return {
    alpn: alpn === '-' || alpn === '' ? [] : alpn.split(','),
    echBytes: Number(ech),
  };
}

export function parseDiagnosticReport(text: string): ParseResult {
  const lines = text.replace(/\r\n/g, '\n').split('\n');
  const raw = text;

  let index = 0;
  while (index < lines.length && lines[index].trim() === '') index += 1;
  if (index >= lines.length) return { ok: false, error: 'empty' };

  const header = /^report:\s*(\d+)\s*$/.exec(lines[index].trim());
  if (!header) return { ok: false, error: 'missing-version' };
  const version = Number(header[1]);
  if (version > SUPPORTED_REPORT_VERSION) return { ok: false, error: 'unsupported-version' };
  index += 1;

  // Optional human title line before the fields.
  if (index < lines.length && lines[index].trim() !== '' && !lines[index].includes(':')) index += 1;

  const fields = new Map<string, string>();
  for (; index < lines.length; index += 1) {
    const line = lines[index];
    const colon = line.indexOf(':');
    if (colon <= 0) continue;
    const key = line.slice(0, colon).trim().toLowerCase();
    const value = line.slice(colon + 1).trim();
    if (!fields.has(key)) fields.set(key, value);
  }

  const error = parseError(fields.get('error') ?? '');
  const report: DiagnosticReport = {
    version,
    generated: fields.get('generated'),
    appVersion: fields.get('version'),
    platform: fields.get('platform'),
    url: fields.get('url'),
    host: fields.get('host'),
    errorCode: error.code,
    errorReason: error.reason,
    kernel: fields.get('kernel'),
    switches: splitList(fields.get('switches'), ' '),
    limits: splitList(fields.get('limits'), ','),
    components: splitList(fields.get('components'), ','),
    signature: fields.get('signature'),
    cef: fields.get('cef'),
    tls: parseTls(fields.get('tls') ?? ''),
    rule: parseRule(fields.get('rule') ?? ''),
    upstream: parseUpstream(fields.get('upstream') ?? ''),
    addresses:
      (fields.get('addresses') ?? '').toLowerCase() === 'none'
        ? []
        : (fields.get('addresses') ?? '')
            .split(',')
            .map((value) => value.trim())
            .filter(Boolean),
    https: fields.has('https') ? parseHttps(fields.get('https') ?? '') : undefined,
    notes: fields.get('diagnostics'),
    extra: {},
    raw,
  };

  const known = new Set([
    'generated',
    'version',
    'platform',
    'url',
    'host',
    'error',
    'kernel',
    'switches',
    'limits',
    'components',
    'signature',
    'cef',
    'tls',
    'rule',
    'upstream',
    'addresses',
    'https',
    'diagnostics',
  ]);
  for (const [key, value] of fields) {
    if (!known.has(key)) report.extra[key] = value;
  }

  return { ok: true, report };
}
