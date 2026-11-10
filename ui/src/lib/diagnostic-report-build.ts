// Single source of truth for the plain-text diagnostic report, shared by the
// failure page and the viewer's "save refreshed report" action.
import type { DeltaLike } from './report-freshness';

export interface ReportSections {
  version: string;
  platform: string;
  arch: string;
  url: string;
  host: string;
  /** Error code plus an optional localized reason. */
  error: string;
  kernel: string;
  switches: string[];
  limits: string[];
  components: string[];
  signature: string;
  cef: string;
  applied: { at: string; provider: string; protocol: string; proxy: string } | null;
  delta: DeltaLike[];
  /** Text after "TLS:", e.g. "subject=... valid=no expires=..." */
  tls: string;
  /** Text after "Rule:", e.g. "x.com (h3=prefer, ech=true)" or "none". */
  rule: string;
  upstream?: string;
  addresses?: string[];
  https?: { alpn: string[]; echBytes: number };
  diagnostics?: string;
  generated?: Date;
}

export function buildDiagnosticReport(sections: ReportSections): string {
  const generated = (sections.generated ?? new Date()).toISOString();
  const lines = [
    'report: 1',
    'Liora diagnostic report',
    `Generated: ${generated}`,
    `Version: ${sections.version} (${sections.platform} ${sections.arch})`,
    `URL: ${sections.url}`,
    `Host: ${sections.host}`,
    `Error: ${sections.error}`,
    `Kernel: ${sections.kernel}`,
    `Switches: ${sections.switches.join(' ') || 'none'}`,
    `Limits: ${sections.limits.join(', ') || 'none'}`,
    `Components: ${sections.components.join(', ') || 'none'}`,
    `Signature: ${sections.signature}`,
    `CEF: ${sections.cef}`,
  ];

  if (sections.applied) {
    const protocol = sections.applied.protocol ? ` (${sections.applied.protocol})` : '';
    lines.push(
      `Applied: ${sections.applied.at} · ${sections.applied.provider || 'unknown'}${protocol} · proxy ${sections.applied.proxy || 'none'}`,
    );
    lines.push(
      `Delta: ${
        sections.delta.length > 0
          ? sections.delta
              .map((item) => `${item.kind} ${item.from || 'none'} -> ${item.to || 'none'}`)
              .join(' · ')
          : 'none'
      }`,
    );
  } else {
    lines.push('Applied: none');
    lines.push('Delta: none');
  }

  lines.push(`TLS: ${sections.tls}`);
  lines.push(`Rule: ${sections.rule}`);

  if (sections.upstream) {
    lines.push(`Upstream: ${sections.upstream}`);
    lines.push(`Addresses: ${(sections.addresses ?? []).join(', ') || 'none'}`);
    const https = sections.https ?? { alpn: [], echBytes: 0 };
    lines.push(`HTTPS: alpn=${https.alpn.join(',') || '-'} ech=${https.echBytes}B`);
  } else if (sections.diagnostics) {
    lines.push(`Diagnostics: ${sections.diagnostics}`);
  }

  return lines.join('\n');
}
