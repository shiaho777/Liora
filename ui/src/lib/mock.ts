// In-memory stand-in for Liora Core, used when the interface runs in a plain
// browser (development, screenshots, review). It mirrors the JSON shapes of the
// real RPC backend closely enough that no component needs a special case.

export interface MockBackend {
  handle: (text: string) => void;
}

type Handler = (params: any) => unknown;

interface Rule {
  id: string;
  name: string;
  enabled: boolean;
  preset?: string;
  domains: string[];
  h3: 'off' | 'prefer' | 'force';
  ech: boolean;
  echSource: string;
  alpn: string[];
  dns: string;
}

interface CustomDns {
  id: string;
  name: string;
  address: string;
  protocol: string;
}

interface MockBookmarkNode {
  id: number;
  type: 'folder' | 'url';
  title: string;
  url?: string;
  children?: MockBookmarkNode[];
}

function defaultBookmarkRoots(): MockBookmarkNode[] {
  return [
    {
      id: 1,
      type: 'folder',
      title: 'Bookmarks bar',
      children: [
        { id: 10, type: 'url', title: 'Svelte', url: 'https://svelte.dev' },
        { id: 11, type: 'url', title: 'Chromium', url: 'https://www.chromium.org' },
      ],
    },
    { id: 2, type: 'folder', title: 'Other bookmarks', children: [] },
    { id: 3, type: 'folder', title: 'Mobile bookmarks', children: [] },
  ];
}

function findBookmarkNode(nodes: MockBookmarkNode[], id: number): MockBookmarkNode | null {
  for (const node of nodes) {
    if (node.id === id) return node;
    const hit = findBookmarkNode(node.children ?? [], id);
    if (hit) return hit;
  }
  return null;
}

function findBookmarkParent(nodes: MockBookmarkNode[], id: number): MockBookmarkNode | null {
  for (const node of nodes) {
    if ((node.children ?? []).some((child) => child.id === id)) return node;
    const hit = findBookmarkParent(node.children ?? [], id);
    if (hit) return hit;
  }
  return null;
}

function flattenBookmarkUrls(nodes: MockBookmarkNode[]): Array<{ id: number; title: string; url: string }> {
  const out: Array<{ id: number; title: string; url: string }> = [];
  for (const node of nodes) {
    if (node.type === 'url' && node.url) out.push({ id: node.id, title: node.title, url: node.url });
    out.push(...flattenBookmarkUrls(node.children ?? []));
  }
  return out;
}

function isDescendant(node: MockBookmarkNode, id: number): boolean {
  if (node.id === id) return true;
  return (node.children ?? []).some((child) => isDescendant(child, id));
}

const SAMPLE_TIMELINE = `Liora diagnostics timeline
Exported: 2026-11-10T08:53:48.271Z
2026-11-10T08:53:35.565Z  resolver  run 2
2026-11-10T08:53:35.566Z  applied  Cloudflare Standard (doh) · http://127.0.0.1:49704
2026-11-10T08:53:42.703Z  generated
2026-11-10T08:53:45.438Z  current-resolver  run 3
`;

const SAMPLE_REPORT = `report: 1
Liora diagnostic report
Generated: 2026-11-10T07:35:44.966Z
Version: 0.1.0 (mock arm64)
URL: https://this-host-does-not-exist.invalid/
Host: this-host-does-not-exist.invalid
Error: -1005 - 连接在加载过程中中断。
Kernel: system
Switches: none
Limits: ech, forceQuic
Components: none
Signature: ad-hoc (no Team ID, io.liora.browser)
CEF: not selected
TLS: not established
Rule: none
Upstream: doh cloudflare https://cloudflare-dns.com/dns-query 339ms
Addresses: none
HTTPS: alpn=- ech=0B
`;

const PRESETS = [
  {
    id: 'x',
    name: { en: 'X (Twitter)', zh: 'X（Twitter）' },
    desc: { en: 'x.com, twitter.com and their media domains.', zh: 'x.com、twitter.com 及其媒体域名。' },
    domains: ['x.com', 'twitter.com', 'twimg.com', 't.co'],
    rule: { h3: 'prefer', ech: true, echSource: 'cloudflare-ech.com', alpn: ['h3', 'h2'] },
  },
  {
    id: 'discord',
    name: { en: 'Discord', zh: 'Discord' },
    desc: { en: 'Discord web client and invite links.', zh: 'Discord 网页版与邀请链接。' },
    domains: ['discord.com', 'discord.gg', 'discordapp.com', 'discordapp.net', 'discord.media'],
    rule: { h3: 'prefer', ech: true, echSource: 'cloudflare-ech.com', alpn: ['h3', 'h2'] },
  },
];

const PROVIDERS = [
  {
    id: 'adguard',
    name: 'AdGuard DNS',
    variants: [
      {
        id: 'adguard',
        label: { en: 'Default', zh: '默认' },
        desc: { en: 'Blocks ads, trackers and phishing.', zh: '拦截广告、跟踪器和钓鱼网站。' },
        categories: ['adblock', 'security', 'privacy'],
        protocols: ['doh', 'dot', 'doq'],
        doh: 'https://dns.adguard-dns.com/dns-query',
      },
      {
        id: 'adguard-family',
        label: { en: 'Family', zh: '家庭' },
        desc: { en: 'Default filtering plus adult content and safe search.', zh: '在默认过滤基础上拦截成人内容并强制安全搜索。' },
        categories: ['family', 'adblock'],
        protocols: ['doh', 'dot', 'doq'],
        doh: 'https://family.adguard-dns.com/dns-query',
      },
      {
        id: 'adguard-unfiltered',
        label: { en: 'Non-filtering', zh: '无过滤' },
        desc: { en: 'Encrypted DNS without any filtering.', zh: '仅加密，不做任何过滤。' },
        categories: ['unfiltered', 'privacy'],
        protocols: ['doh', 'dot', 'doq'],
        doh: 'https://unfiltered.adguard-dns.com/dns-query',
      },
    ],
  },
  {
    id: 'cloudflare',
    name: 'Cloudflare',
    variants: [
      {
        id: 'cloudflare',
        label: { en: 'Standard', zh: '标准' },
        desc: { en: 'Fast, no filtering, logs purged within 24 hours.', zh: '速度快，无过滤，日志 24 小时内清除。' },
        categories: ['unfiltered', 'privacy'],
        protocols: ['doh', 'dot'],
        doh: 'https://cloudflare-dns.com/dns-query',
      },
      {
        id: 'cloudflare-security',
        label: { en: 'Malware blocking', zh: '拦截恶意软件' },
        desc: { en: 'Blocks malware and phishing domains.', zh: '拦截恶意软件和钓鱼域名。' },
        categories: ['security'],
        protocols: ['doh', 'dot'],
        doh: 'https://security.cloudflare-dns.com/dns-query',
      },
      {
        id: 'cloudflare-family',
        label: { en: 'Family', zh: '家庭' },
        desc: { en: 'Blocks malware and adult content.', zh: '拦截恶意软件和成人内容。' },
        categories: ['family', 'security'],
        protocols: ['doh', 'dot'],
        doh: 'https://family.cloudflare-dns.com/dns-query',
      },
    ],
  },
  {
    id: 'quad9',
    name: 'Quad9',
    variants: [
      {
        id: 'quad9',
        label: { en: 'Secure', zh: '安全' },
        desc: { en: 'Blocks malicious domains. Non-profit, no personal data logged.', zh: '拦截恶意域名。非营利组织，不记录个人数据。' },
        categories: ['security', 'privacy'],
        protocols: ['doh', 'dot'],
        doh: 'https://dns.quad9.net/dns-query',
      },
      {
        id: 'quad9-unsecured',
        label: { en: 'Unsecured', zh: '无过滤' },
        desc: { en: 'No blocking.', zh: '不做任何拦截。' },
        categories: ['unfiltered', 'privacy'],
        protocols: ['doh', 'dot'],
        doh: 'https://dns10.quad9.net/dns-query',
      },
    ],
  },
  {
    id: 'google',
    name: 'Google Public DNS',
    variants: [
      {
        id: 'google',
        label: { en: 'Standard', zh: '标准' },
        desc: { en: 'Reliable global resolver without filtering.', zh: '全球可靠的解析服务，无过滤。' },
        categories: ['unfiltered'],
        protocols: ['doh', 'dot'],
        doh: 'https://dns.google/dns-query',
      },
    ],
  },
  {
    id: 'mullvad',
    name: 'Mullvad DNS',
    variants: [
      {
        id: 'mullvad',
        label: { en: 'Unfiltered', zh: '无过滤' },
        desc: { en: 'Privacy-focused, no logging.', zh: '注重隐私，不记录日志。' },
        categories: ['unfiltered', 'privacy'],
        protocols: ['doh', 'dot'],
        doh: 'https://dns.mullvad.net/dns-query',
      },
      {
        id: 'mullvad-adblock',
        label: { en: 'Ad blocking', zh: '广告拦截' },
        desc: { en: 'Blocks ads and trackers, no logging.', zh: '拦截广告和跟踪器，不记录日志。' },
        categories: ['adblock', 'privacy'],
        protocols: ['doh', 'dot'],
        doh: 'https://adblock.dns.mullvad.net/dns-query',
      },
    ],
  },
  {
    id: 'alidns',
    name: 'AliDNS',
    variants: [
      {
        id: 'alidns',
        label: { en: 'Standard', zh: '标准' },
        desc: { en: 'Fast in mainland China.', zh: '国内速度快。' },
        categories: ['china', 'unfiltered'],
        protocols: ['doh', 'dot', 'doq'],
        doh: 'https://dns.alidns.com/dns-query',
      },
    ],
  },
];

const CATEGORIES = [
  { id: 'adblock', name: { en: 'Ad blocking', zh: '广告拦截' } },
  { id: 'family', name: { en: 'Family protection', zh: '家庭保护' } },
  { id: 'security', name: { en: 'Malware protection', zh: '恶意网站拦截' } },
  { id: 'unfiltered', name: { en: 'No filtering', zh: '无过滤' } },
  { id: 'privacy', name: { en: 'Privacy first', zh: '隐私优先' } },
  { id: 'china', name: { en: 'Works in mainland China', zh: '国内可用' } },
];

const DEFAULTS: Record<string, unknown> = {
  'ui.mode': 'simple',
  'ui.theme': 'system',
  'ui.accent': '#3b6cf6',
  'ui.density': 'comfortable',
  'ui.toolbar': 'top',
  'ui.tabs': 'strip',
  'ui.language': 'auto',
  'ui.showHomeButton': false,
  'search.engine': 'duckduckgo',
  'search.suggest': true,
  'search.customEngines': [],
  'start.tiles': [],
  'home.page': 'liora://newtab',
  'startup.restore': true,
  'kernel.default': 'system',
  'dns.mode': 'auto',
  'dns.provider': 'cloudflare',
  'dns.protocol': 'doh',
  'dns.custom': [],
  'dns.customId': '',
  'net.rulesEnabled': true,
  'net.rules': [],
  'net.echOuter': 'cloudflare-ech.com',
  'privacy.thirdPartyCookies': 'block',
  'privacy.httpsUpgrade': true,
  'privacy.doNotTrack': false,
  'content.javascript': true,
  'content.images': true,
  'content.autoplay': 'block-audible',
  'content.zoom': 100,
  'content.userAgent': 'default',
  'downloads.dir': '',
  'downloads.ask': false,
  'components.autoUpdate': true,
  'components.channel': 'stable',
  'diagnostics.crashReports': false,
};

const CHOICES: Record<string, string[]> = {
  'ui.mode': ['simple', 'standard', 'advanced'],
  'ui.theme': ['system', 'light', 'dark'],
  'ui.density': ['compact', 'comfortable'],
  'ui.toolbar': ['top', 'bottom'],
  'ui.tabs': ['strip', 'vertical', 'hidden'],
  'dns.mode': ['auto', 'off', 'provider', 'custom'],
  'dns.protocol': ['doh', 'dot', 'doq'],
  'content.autoplay': ['allow', 'block-audible', 'block'],
  'content.userAgent': ['default', 'desktop', 'mobile'],
  'privacy.thirdPartyCookies': ['allow', 'block'],
  'components.channel': ['stable', 'beta'],
};

const ADVANCED = new Set([
  'dns.protocol',
  'dns.custom',
  'dns.customId',
  'net.rules',
  'net.echOuter',
  'privacy.doNotTrack',
  'content.javascript',
  'components.autoUpdate',
  'components.channel',
]);

const SITE_SCOPES = new Set([
  'kernel.default',
  'content.javascript',
  'content.images',
  'content.zoom',
  'content.userAgent',
  'content.autoplay',
  'privacy.httpsUpgrade',
  'privacy.thirdPartyCookies',
]);

let ruleSeq = 1;

function providerList() {
  return PROVIDERS;
}

function flatProviders() {
  const out: Array<Record<string, unknown>> = [];
  for (const provider of PROVIDERS) {
    for (const variant of provider.variants) out.push({ ...variant, provider: provider.name });
  }
  return out;
}

function normalizeHost(input: string): string {
  let value = input.trim().toLowerCase();
  const scheme = value.indexOf('://');
  if (scheme >= 0) value = value.slice(scheme + 3);
  value = value.split(/[/?#]/)[0];
  const at = value.lastIndexOf('@');
  if (at >= 0) value = value.slice(at + 1);
  return value;
}

function matches(rule: Rule, host: string): boolean {
  for (const pattern of rule.domains) {
    if (pattern.startsWith('*.')) {
      const base = pattern.slice(2);
      if (host.endsWith('.' + base)) return true;
    } else if (pattern.startsWith('=')) {
      if (host === pattern.slice(1)) return true;
    } else if (host === pattern || host.endsWith('.' + pattern)) {
      return true;
    }
  }
  return false;
}

export function createMockBackend(): MockBackend {
  const faviconCache: Record<string, string> = {};
  const userScripts: Array<{
    id: string;
    name: string;
    enabled: boolean;
    matches: string[];
    runAt: string;
    code: string;
    created: number;
    modified: number;
  }> = [];
  const matchPattern = (pattern: string, host: string): boolean => {
    if (pattern === '*') return true;
    if (pattern.startsWith('=')) return pattern.slice(1) === host;
    if (pattern.startsWith('*.')) {
      const parent = pattern.slice(2);
      return host.endsWith(parent) && host.length > parent.length && host[host.length - parent.length - 1] === '.';
    }
    return host === pattern || (host.endsWith(pattern) && host.length > pattern.length && host[host.length - pattern.length - 1] === '.');
  };
  const stored = localStorage.getItem('liora.mock.state');
  const state: {
    settings: Record<string, unknown>;
    sites: Record<string, Record<string, unknown>>;
    rules: Rule[];
    installed: Array<{ id: string; version: string; path: string; installedAt: number; current: boolean }>;
    bookmarks: MockBookmarkNode[];
    history: Array<{ url: string; title: string; visitCount: number; lastVisit: number }>;
    permissions: Array<{ origin: string; name: string; value: string; modified: number }>;
  } = stored
    ? JSON.parse(stored)
    : {
        settings: {},
        sites: {},
        rules: [],
        installed: [],
        bookmarks: defaultBookmarkRoots(),
        permissions: [
          { origin: 'https://meet.example', name: 'camera', value: 'allow', modified: Date.now() },
          { origin: 'https://record.example', name: 'microphone', value: 'block', modified: Date.now() },
        ],
        history: [
          { url: 'https://svelte.dev', title: 'Svelte', visitCount: 12, lastVisit: Date.now() },
          { url: 'https://developer.mozilla.org', title: 'MDN Web Docs', visitCount: 9, lastVisit: Date.now() - 86400000 },
          { url: 'https://www.chromium.org', title: 'Chromium', visitCount: 5, lastVisit: Date.now() - 172800000 },
          { url: 'https://github.com', title: 'GitHub', visitCount: 4, lastVisit: Date.now() - 259200000 },
        ],
      };
  state.bookmarks ??= defaultBookmarkRoots();
  // Migrate the old flat bookmark list into the bar root.
  if (state.bookmarks.length && !state.bookmarks.some((node) => node.type === 'folder')) {
    const flat = state.bookmarks as unknown as Array<{ id: number; title: string; url: string }>;
    state.bookmarks = defaultBookmarkRoots();
    const bar = state.bookmarks.find((node) => node.id === 1);
    if (bar) bar.children = flat.map((entry) => ({ ...entry, type: 'url' as const }));
  }
  state.permissions ??= [
    { origin: 'https://meet.example', name: 'camera', value: 'allow', modified: Date.now() },
    { origin: 'https://record.example', name: 'microphone', value: 'block', modified: Date.now() },
  ];
  state.history ??= [
    { url: 'https://svelte.dev', title: 'Svelte', visitCount: 12, lastVisit: Date.now() },
    { url: 'https://developer.mozilla.org', title: 'MDN Web Docs', visitCount: 9, lastVisit: Date.now() - 86400000 },
    { url: 'https://www.chromium.org', title: 'Chromium', visitCount: 5, lastVisit: Date.now() - 172800000 },
    { url: 'https://github.com', title: 'GitHub', visitCount: 4, lastVisit: Date.now() - 259200000 },
  ];

  if (state.rules.length === 0) {
    for (const preset of PRESETS) {
      state.rules.push({
        id: `rule-${ruleSeq++}`,
        name: preset.name.zh,
        enabled: true,
        preset: preset.id,
        domains: preset.domains,
        h3: preset.rule.h3 as Rule['h3'],
        ech: preset.rule.ech,
        echSource: preset.rule.echSource,
        alpn: preset.rule.alpn,
        dns: '',
      });
    }
    for (const rule of state.rules) {
      // Two presets ship enabled so the first run is useful without setup.
    }
  }

  const persist = () => {
    localStorage.setItem(
      'liora.mock.state',
      JSON.stringify({
        settings: state.settings,
        sites: state.sites,
        rules: state.rules,
        installed: state.installed,
        bookmarks: state.bookmarks,
        history: state.history,
        permissions: state.permissions,
      }),
    );
  };

  const get = (key: string, site?: string) => {
    if (site && state.sites[site] && key in state.sites[site]) return state.sites[site][key];
    return key in state.settings ? state.settings[key] : DEFAULTS[key];
  };
  const set = (key: string, value: unknown, site?: string) => {
    if (site) {
      state.sites[site] ??= {};
      state.sites[site][key] = value;
    } else if (JSON.stringify(value) === JSON.stringify(DEFAULTS[key])) {
      delete state.settings[key];
    } else {
      state.settings[key] = value;
    }
    persist();
  };

  const netPlan = () => {
    const mode = get('dns.mode') as string;
    const protocol = get('dns.protocol') as string;
    let endpoint: Record<string, unknown> | null = null;
    let planMode = 'automatic';
    let needsNet = false;

    if (mode === 'off') {
      planMode = 'system';
    } else if (mode === 'provider') {
      const id = get('dns.provider') as string;
      const variant = flatProviders().find((v) => v.id === id) ?? flatProviders()[0];
      endpoint = {
        id: variant.id,
        name: variant.provider,
        protocol,
        address: variant.doh,
        bootstrap: [],
      };
      planMode = 'secure';
      needsNet = protocol !== 'doh';
    } else if (mode === 'custom') {
      const custom = (get('dns.custom') as CustomDns[]) ?? [];
      const chosen = custom.find((c) => c.id === get('dns.customId')) ?? custom[0];
      if (chosen) {
        endpoint = { ...chosen, bootstrap: [] };
        planMode = 'secure';
        needsNet = chosen.protocol !== 'doh';
      }
    }

    const activeRules = (get('net.rulesEnabled') as boolean) ? state.rules.filter((r) => r.enabled) : [];
    const forceQuicOrigins: string[] = [];
    const warnings: string[] = [];
    for (const rule of activeRules) {
      if (rule.h3 !== 'force') continue;
      for (const pattern of rule.domains) {
        if (!pattern.startsWith('*.')) forceQuicOrigins.push(pattern.replace(/^=/, '') + ':443');
      }
      if (rule.domains.some((d) => !d.startsWith('='))) {
        warnings.push(`规则“${rule.name}”：强制 QUIC 只对列出的域名生效，子域依靠 HTTPS 记录优先走 h3。`);
      }
    }
    if (activeRules.length > 0) needsNet = true;

    return {
      dns: {
        mode: planMode,
        dohTemplates: endpoint && protocol === 'doh' ? endpoint.address : '',
        needsNet,
        endpoint,
        summary: endpoint ? `${endpoint.name} (${protocol.toUpperCase()})` : mode === 'off' ? '系统解析' : '自动',
      },
      needsNet,
      forceQuicOrigins,
      warnings,
      net: {
        version: 1,
        mode: planMode,
        upstream: endpoint,
        rules: activeRules.map((rule) => ({
          id: rule.id,
          patterns: rule.domains,
          h3: rule.h3,
          alpn: rule.alpn,
          ech: rule.ech,
          echSource: rule.ech ? rule.echSource || get('net.echOuter') : '',
          dns: null,
        })),
      },
    };
  };

  // Mirrors the shell: the snapshot changes only when the kernel is (re)loaded.
  let mockAppliedConfig = JSON.stringify(netPlan().net);
  let mockLastApplied: Record<string, unknown> | null = null;
  const storedResolver = localStorage.getItem('liora.mock.resolver');
  let mockResolver: Record<string, unknown> | null = storedResolver
    ? JSON.parse(storedResolver)
    : { at: Date.now(), count: 1 };

  let mockRecent = [
    {
      name: 'liora-timeline-sample.txt',
      path: '/mock/liora-timeline-sample.txt',
      size: 420,
      modified: Date.now() - 600000,
      kind: 'timeline',
    },
    {
      name: 'liora-diagnostic-sample.txt',
      path: '/mock/liora-diagnostic-sample.txt',
      size: 640,
      modified: Date.now() - 3600000,
      kind: 'report',
    },
  ];

  const methods: Record<string, Handler> = {
    'core.info': () => ({
      version: '0.1.0',
      profile: 'mock profile',
      platform: 'mock',
      arch: 'arm64',
      netNeedsNet: netPlan().needsNet,
      netSummary: netPlan().dns.summary,
    }),
    'core.ping': () => ({ ok: true, time: Date.now() }),
    'rpc.discover': () => ({ methods: Object.keys(methods), namespaces: {} }),

    'settings.describe': (p) => ({
      items: Object.keys(DEFAULTS).map((key) => ({
        key,
        default: DEFAULTS[key],
        value: get(key),
        scopes: SITE_SCOPES.has(key) ? ['global', 'site', 'tab'] : ['global'],
        advanced: ADVANCED.has(key),
        ...(CHOICES[key] ? { choices: CHOICES[key] } : {}),
      })),
      sites: Object.entries(state.sites).map(([site, values]) => ({ site, values })),
      ...(p.site ? { overrides: state.sites[p.site] ?? {} } : {}),
    }),
    'settings.get': (p) => {
      if (p.key) return { [p.key]: get(p.key, p.site) };
      const prefix = p.prefix ? `${p.prefix}.` : '';
      const out: Record<string, unknown> = {};
      for (const key of Object.keys(DEFAULTS)) if (key.startsWith(prefix)) out[key] = get(key, p.site);
      return out;
    },
    'settings.set': (p) => {
      if (p.values) Object.assign(state.settings, p.values);
      else set(p.key, p.value, p.site);
      persist();
      queueMicrotask(() => respond({ jsonrpc: '2.0', method: 'settings.changed', params: {} }));
      return { ok: true };
    },
    'settings.reset': (p) => {
      if (p.site && state.sites[p.site]) delete state.sites[p.site][p.key];
      else delete state.settings[p.key];
      persist();
      return { ok: true };
    },
    'settings.clearSite': (p) => {
      delete state.sites[p.site];
      persist();
      return { ok: true };
    },

    'userscript.list': () => userScripts,
    'userscript.add': (p) => {
      const script = {
        id: `script-${Date.now()}-${Math.floor(Math.random() * 1e6)}`,
        name: String(p.name ?? ''),
        enabled: p.enabled !== false,
        matches: Array.isArray(p.matches) ? p.matches : [],
        runAt: String(p.runAt ?? 'end'),
        code: String(p.code ?? ''),
        created: Date.now(),
        modified: Date.now(),
      };
      userScripts.push(script);
      return script;
    },
    'userscript.update': (p) => {
      const script = userScripts.find((s) => s.id === p.id);
      if (!script) throw new Error('unknown script');
      Object.assign(script, {
        name: String(p.name ?? script.name),
        matches: Array.isArray(p.matches) ? p.matches : script.matches,
        runAt: String(p.runAt ?? script.runAt),
        code: String(p.code ?? script.code),
        enabled: p.enabled !== false,
        modified: Date.now(),
      });
      return script;
    },
    'userscript.remove': (p) => {
      const index = userScripts.findIndex((s) => s.id === p.id);
      if (index >= 0) userScripts.splice(index, 1);
      return { ok: true };
    },
    'userscript.setEnabled': (p) => {
      const script = userScripts.find((s) => s.id === p.id);
      if (script) script.enabled = Boolean(p.enabled);
      return { ok: true };
    },
    'userscript.forHost': (p) => {
      const host = String(p.host ?? '');
      return userScripts.filter((s) => s.enabled && s.matches.some((m) => matchPattern(m, host)));
    },

    'dns.catalog': () => ({
      categories: CATEGORIES,
      providers: providerList(),
      custom: get('dns.custom'),
      active: netPlan().dns,
    }),
    'dns.validate': (p) => {
      const address = String(p.address ?? '');
      if (address.startsWith('http://')) return { valid: false, error: '明文 HTTP 不加密，请使用 https://' };
      if (address.startsWith('https://')) return { valid: true, normalized: address, protocol: 'doh', id: 'custom' };
      if (address.startsWith('tls://')) return { valid: true, normalized: address.slice(6), protocol: 'dot', id: 'custom' };
      if (address.startsWith('quic://')) return { valid: true, normalized: address.slice(7), protocol: 'doq', id: 'custom' };
      if (/^[a-z0-9.-]+(:\d+)?$/i.test(address)) return { valid: true, normalized: address, protocol: 'dot', id: 'custom' };
      return { valid: false, error: '无法识别的地址' };
    },
    'dns.custom.add': (p) => {
      const list = [...((get('dns.custom') as CustomDns[]) ?? [])];
      const protocol = String(p.address).startsWith('https://')
        ? 'doh'
        : String(p.address).startsWith('quic://')
          ? 'doq'
          : 'dot';
      const entry: CustomDns = {
        id: `custom-${Math.random().toString(16).slice(2, 10)}`,
        name: p.name || String(p.address),
        address: String(p.address),
        protocol,
      };
      list.push(entry);
      set('dns.custom', list);
      return entry;
    },
    'dns.custom.remove': (p) => {
      const list = ((get('dns.custom') as CustomDns[]) ?? []).filter((c) => c.id !== p.id);
      set('dns.custom', list);
      return { ok: true };
    },

    'rules.list': () => ({ rules: state.rules, enabled: get('net.rulesEnabled'), echOuter: get('net.echOuter') }),
    'rules.presets': () => PRESETS,
    'rules.add': (p) => {
      let rule: Rule;
      if (p.preset) {
        const preset = PRESETS.find((item) => item.id === p.preset)!;
        rule = {
          id: `rule-${ruleSeq++}`,
          name: preset.name.zh,
          enabled: true,
          preset: preset.id,
          domains: preset.domains,
          h3: preset.rule.h3 as Rule['h3'],
          ech: preset.rule.ech,
          echSource: preset.rule.echSource,
          alpn: preset.rule.alpn,
          dns: '',
        };
      } else {
        const domains = String(p.domains?.[0] ?? p.domain ?? '')
          .split(/[\s,]+/)
          .filter(Boolean)
          .map(normalizeHost);
        rule = {
          id: `rule-${ruleSeq++}`,
          name: p.name || domains[0] || '新规则',
          enabled: true,
          domains,
          h3: (p.h3 as Rule['h3']) ?? 'prefer',
          ech: p.ech ?? true,
          echSource: p.echSource ?? '',
          alpn: p.h3 === 'force' ? ['h3'] : ['h3', 'h2'],
          dns: p.dns ?? '',
        };
      }
      state.rules.push(rule);
      persist();
      queueMicrotask(() => respond({ jsonrpc: '2.0', method: 'settings.changed', params: {} }));
      return rule;
    },
    'rules.update': (p) => {
      const index = state.rules.findIndex((r) => r.id === p.id);
      if (index < 0) throw { code: -32002, message: '规则不存在' };
      state.rules[index] = { ...state.rules[index], ...p, domains: p.domains ?? state.rules[index].domains };
      persist();
      queueMicrotask(() => respond({ jsonrpc: '2.0', method: 'settings.changed', params: {} }));
      return state.rules[index];
    },
    'rules.remove': (p) => {
      state.rules = state.rules.filter((r) => r.id !== p.id);
      persist();
      queueMicrotask(() => respond({ jsonrpc: '2.0', method: 'settings.changed', params: {} }));
      return { ok: true };
    },
    'rules.enable': (p) => {
      const rule = state.rules.find((r) => r.id === p.id);
      if (rule) rule.enabled = p.enabled !== false;
      persist();
      queueMicrotask(() => respond({ jsonrpc: '2.0', method: 'settings.changed', params: {} }));
      return { ok: true };
    },
    'rules.match': (p) => {
      const host = normalizeHost(String(p.host ?? ''));
      const rule = state.rules.find((r) => r.enabled && matches(r, host));
      if (!rule) return { matched: false, host };
      return {
        matched: true,
        host,
        rule: rule.id,
        ruleName: rule.name,
        h3: rule.h3,
        ech: rule.ech,
        echSource: rule.ech ? rule.echSource || get('net.echOuter') : '',
        alpn: rule.alpn,
        dns: null,
      };
    },

    'net.plan': () => netPlan(),
    'net.lastApplied': () => mockLastApplied ?? {},
    'net.resolverStatus': () => mockResolver ?? {},
    'net.forHost': (p) => methods['rules.match'](p),
    'net.config': () => ({ config: netPlan().net, needsNet: netPlan().needsNet, warnings: [], kernels: [] }),
    'net.runtime': () => ({
      running: true,
      dohPort: 5399,
      proxyPort: 5400,
      spki: 'mock',
      appliedConfig: mockAppliedConfig,
    }),
    'dns.plan': () => netPlan().dns,

    'kernels.list': () => [
      {
        id: 'system',
        name: 'System WebView',
        version: '',
        component: '',
        caps: ['proxy'],
        available: true,
        netNeedsRestart: false,
      },
      {
        id: 'cef',
        name: 'Chromium (CEF)',
        version: '126.0',
        component: 'kernel-cef',
        // Mirrors KernelManager: no extension runtime exists yet.
        caps: ['secureDns', 'httpsRecords', 'ech', 'forceQuic', 'proxy', 'devtools', 'isolatedProfiles'],
        available: state.installed.some((i) => i.id === 'kernel-cef' && i.current),
        netNeedsRestart: true,
      },
      {
        id: 'kitesurf',
        name: 'Kitesurf (experimental)',
        version: '',
        component: '',
        // Mirrors KernelManager: DevTools over CDP and per-page isolates only.
        caps: ['devtools', 'isolatedProfiles'],
        available: false,
        netNeedsRestart: false,
      },
    ],
    'kernels.current': () => ({ id: get('kernel.default') }),
    'kernels.select': (p) => {
      set('kernel.default', p.id);
      return { id: p.id, restartRequired: p.id === 'cef' };
    },
    'kernels.setup': () => ({
      kernel: get('kernel.default'),
      dnsMode: 'secure',
      dohTemplates: netPlan().dns.dohTemplates,
      proxy: '',
      switches: [],
      unsupported: [],
      usesNet: netPlan().needsNet,
      netMissing: false,
    }),
    'components.catalog': () => [
      {
        id: 'kernel-cef',
        name: 'Chromium 内核 (CEF)',
        kind: 'kernel',
        description: '完整 Chromium 内核：ECH、强制 H3、MV3 扩展。',
        required: false,
        versions: [{ version: '126.0', minApp: '0.1.0', builds: [] }],
        installedVersion: state.installed.find((i) => i.id === 'kernel-cef')?.version,
      },
      {
        id: 'liora-net',
        name: 'liora-net 本地解析器',
        kind: 'net',
        description: 'DoT / DoQ、按域名分流与 HTTPS 记录注入。',
        required: false,
        versions: [{ version: '0.1.0', minApp: '0.1.0', builds: [] }],
        installedVersion: state.installed.find((i) => i.id === 'liora-net')?.version,
      },
    ],
    'components.updates': () => [],
    'components.installed': () => state.installed,

    'bookmarks.tree': () => ({
      bar: findBookmarkNode(state.bookmarks, 1),
      other: findBookmarkNode(state.bookmarks, 2),
      mobile: findBookmarkNode(state.bookmarks, 3),
    }),
    'bookmarks.forUrl': (p) => flattenBookmarkUrls(state.bookmarks).filter((entry) => entry.url === p.url),
    'bookmarks.search': (p) => {
      const query = String(p.query ?? '').toLowerCase();
      const limit = Number(p.limit ?? 50);
      return flattenBookmarkUrls(state.bookmarks)
        .filter((entry) => !query || entry.url.toLowerCase().includes(query) || entry.title.toLowerCase().includes(query))
        .slice(0, limit);
    },
    'bookmarks.add': (p) => {
      const parent = findBookmarkNode(state.bookmarks, Number(p.parent ?? 2)) ?? findBookmarkNode(state.bookmarks, 2);
      const entry: MockBookmarkNode = {
        id: 100 + flattenBookmarkUrls(state.bookmarks).length + Math.floor(Math.random() * 1000),
        type: 'url',
        title: p.title || p.url,
        url: p.url,
      };
      if (parent) (parent.children ??= []).push(entry);
      persist();
      return entry;
    },
    'bookmarks.addFolder': (p) => {
      const parent = findBookmarkNode(state.bookmarks, Number(p.parent ?? 2)) ?? findBookmarkNode(state.bookmarks, 2);
      const folder: MockBookmarkNode = {
        id: 500 + Math.floor(Math.random() * 100000),
        type: 'folder',
        title: p.title || 'Folder',
        children: [],
      };
      if (parent) (parent.children ??= []).push(folder);
      persist();
      return folder;
    },
    'bookmarks.update': (p) => {
      const node = findBookmarkNode(state.bookmarks, Number(p.id));
      if (!node) return { ok: false };
      if (typeof p.title === 'string') node.title = p.title;
      if (typeof p.url === 'string' && node.type === 'url') node.url = p.url;
      persist();
      return { ok: true };
    },
    'bookmarks.move': (p) => {
      const node = findBookmarkNode(state.bookmarks, Number(p.id));
      const target = findBookmarkNode(state.bookmarks, Number(p.parent));
      if (!node || !target || target.type !== 'folder') return { ok: false };
      if (node.type === 'folder' && isDescendant(node, target.id)) return { ok: false };
      const oldParent = findBookmarkParent(state.bookmarks, node.id);
      if (oldParent) oldParent.children = (oldParent.children ?? []).filter((child) => child.id !== node.id);
      (target.children ??= []).push(node);
      persist();
      return { ok: true };
    },
    'bookmarks.remove': (p) => {
      const parent = findBookmarkParent(state.bookmarks, Number(p.id));
      if (parent) parent.children = (parent.children ?? []).filter((child) => child.id !== Number(p.id));
      persist();
      return { ok: true };
    },
    'downloads.list': () => {
      const raw = localStorage.getItem('liora.mock.downloads');
      return raw ? JSON.parse(raw) : [];
    },
    'downloads.clear': () => {
      localStorage.removeItem('liora.mock.downloads');
      return { ok: true };
    },
    'session.load': () => {
      const raw = localStorage.getItem('liora.mock.session');
      return raw ? JSON.parse(raw) : { tabs: [], active: 0 };
    },
    'session.save': (p) => {
      localStorage.setItem('liora.mock.session', JSON.stringify({ tabs: p.tabs, active: p.active }));
      return { ok: true };
    },
    'session.clear': () => {
      localStorage.removeItem('liora.mock.session');
      return { ok: true };
    },
    'history.top': () => state.history.slice(0, 8),
    'history.suggest': (p) => {
      const input = String(p.input ?? '').toLowerCase();
      const limit = Number(p.limit ?? 6);
      if (!input) return [];
      return state.history
        .filter((item) => item.url.toLowerCase().includes(input) || item.title.toLowerCase().includes(input))
        .slice(0, limit);
    },
    'history.search': (p) => {
      const query = String(p.query ?? '').toLowerCase();
      const limit = Number(p.limit ?? 100);
      return state.history
        .filter((item) => !query || item.url.toLowerCase().includes(query) || item.title.toLowerCase().includes(query))
        .slice(0, limit);
    },
    'history.remove': (p) => {
      state.history = state.history.filter((item) => item.url !== p.url);
      persist();
      return { ok: true };
    },
    'history.clear': () => {
      state.history = [];
      persist();
      return { ok: true };
    },

    'favicon.set': (p) => {
      const host = String(p.host ?? '');
      const icon = String(p.icon ?? '');
      if (!host) throw new Error('host is required');
      if (!icon) delete faviconCache[host];
      else faviconCache[host] = icon;
      return { ok: true };
    },
    'favicon.get': (p) => ({ icon: faviconCache[String(p.host ?? '')] ?? '' }),
    'favicon.list': () =>
      Object.entries(faviconCache).map(([host, dataUrl]) => ({ host, dataUrl, updated: Date.now() })),

    'backup.list': () => [],
    'backup.now': () => ({ path: 'mock/backups/manual.liora' }),
    'backup.sections': () => ['settings', 'bookmarks', 'history', 'permissions'],

    'diagnostics.recent': () => mockRecent,
    'diagnostics.delete': (p) => {
      mockRecent = mockRecent.filter((entry) => entry.path !== p.path);
      return { ok: true };
    },
    'diagnostics.clear': (p) => {
      const keep = Math.max(0, Number(p.keep ?? 5));
      const deleted = Math.max(0, mockRecent.length - keep);
      mockRecent = mockRecent.slice(0, keep);
      return { deleted };
    },
    'migrate.detect': () => [
      {
        id: 'chrome/Default',
        browser: 'chrome',
        name: 'Google Chrome',
        profile: '个人',
        kind: 'chromium',
        path: '~/Library/Application Support/Google/Chrome/Default',
        bookmarks: true,
        history: true,
        needsAccess: false,
      },
      {
        id: 'firefox/abc.default',
        browser: 'firefox',
        name: 'Firefox',
        profile: 'default-release',
        kind: 'firefox',
        path: '~/Library/Application Support/Firefox/Profiles/abc.default',
        bookmarks: true,
        history: true,
        needsAccess: false,
      },
      {
        id: 'safari',
        browser: 'safari',
        name: 'Safari',
        profile: '',
        kind: 'safari',
        path: '~/Library/Safari',
        bookmarks: true,
        history: true,
        needsAccess: true,
      },
    ],
    'migrate.run': () => ({ ok: true, bookmarks: 42, history: 1200, warnings: [] }),
    'migrate.file': () => ({ ok: true, bookmarks: 10, history: 0, warnings: [] }),

    'permissions.list': (p) =>
      state.permissions.filter((entry) => !p.origin || entry.origin === p.origin),
    'permissions.set': (p) => {
      state.permissions = state.permissions.filter(
        (entry) => !(entry.origin === p.origin && entry.name === p.name),
      );
      if (p.value) {
        state.permissions.push({ origin: p.origin, name: p.name, value: p.value, modified: Date.now() });
      }
      persist();
      return { ok: true };
    },
  };

  const respond = (payload: Record<string, unknown>) => {
    const text = JSON.stringify(payload);
    queueMicrotask(() => window.__lioraDeliver?.(text));
  };

  return {
    handle(text: string) {
      let request: any;
      try {
        request = JSON.parse(text);
      } catch {
        respond({ jsonrpc: '2.0', id: null, error: { code: -32700, message: 'invalid JSON' } });
        return;
      }
      const method = String(request.method ?? '');
      // Shell-directed notifications: nothing to answer, but a failing URL can
      // be simulated so the error panel is reviewable in a browser.
      if (method.startsWith('ui.')) {
        if (method === 'ui.runtime') {
          respond({
            jsonrpc: '2.0',
            method: 'browser.runtime',
            params: {
              codeSigning: 'ad-hoc (no Team ID, io.liora.browser)',
              cefStatus: 'not selected',
            },
          });
          return;
        }
        if (method === 'ui.restartKernel') {
          const plan = netPlan();
          mockAppliedConfig = JSON.stringify(plan.net);
          mockResolver = {
            at: Date.now(),
            count: Number(mockResolver?.count ?? 0) + 1,
          };
          localStorage.setItem('liora.mock.resolver', JSON.stringify(mockResolver));
          const variant = flatProviders().find((item) => item.id === get('dns.provider'));
          mockLastApplied = {
            at: Date.now(),
            proxy: 'http://127.0.0.1:5400',
            provider: String(variant?.provider ?? 'Cloudflare'),
            protocol: String(variant ? plan.dns.endpoint?.protocol ?? 'doh' : 'doh'),
            mode: plan.dns.mode,
            summary: plan.dns.summary,
          };
          setTimeout(() => {
            respond({
              jsonrpc: '2.0',
              method: 'browser.kernelRestarted',
              params: {
                kernel: get('kernel.default'),
                switches: [],
                dnsMode: 'secure',
                dohTemplates: plan.dns.dohTemplates,
                proxy: 'http://127.0.0.1:5400',
              },
            });
          }, 400);
          return;
        }
        if (method === 'ui.trashDiagnostics') {
          const paths = Array.isArray(request.params?.paths) ? request.params.paths : [];
          const before = mockRecent.length;
          const set = new Set(paths.map((path: unknown) => String(path)));
          mockRecent = mockRecent.filter((entry) => !set.has(entry.path));
          setTimeout(() => {
            respond({
              jsonrpc: '2.0',
              method: 'browser.diagnosticsTrashed',
              params: { deleted: before - mockRecent.length, failed: 0 },
            });
          }, 300);
          return;
        }
        if (method === 'ui.trashDiagnostic') {
          const path = String(request.params?.path ?? '');
          const name =
            String(request.params?.name ?? '') || path.split('/').pop() || 'diagnostic.txt';
          mockRecent = mockRecent.filter((entry) => entry.path !== path);
          setTimeout(() => {
            respond({
              jsonrpc: '2.0',
              method: 'browser.diagnosticDeleted',
              params: { path, name },
            });
          }, 200);
          return;
        }
        if (method === 'ui.openDiagnostic') {
          const timeline = request.params?.purpose === 'timeline';
          respond({
            jsonrpc: '2.0',
            method: 'ui.filePicked',
            params: {
              purpose: timeline ? 'timeline' : 'diagnosticReport',
              path: String(request.params?.path ?? ''),
              content: timeline ? SAMPLE_TIMELINE : SAMPLE_REPORT,
            },
          });
          return;
        }
        if (method === 'ui.pickFile' && request.params?.purpose === 'diagnosticReport') {
          // The native shell reads the file; the mock answers with a sample so
          // the viewer can be reviewed in a plain browser.
          respond({
            jsonrpc: '2.0',
            method: 'ui.filePicked',
            params: {
              purpose: 'diagnosticReport',
              path: '/Users/you/Downloads/liora-diagnostic-20261110-153546.txt',
              content: SAMPLE_REPORT,
            },
          });
          return;
        }
        if (method === 'ui.navigate') {
          const params = request.params ?? {};
          const url = String(params.url ?? '');
          if (url.includes('fail')) {
            respond({
              jsonrpc: '2.0',
              method: 'browser.loadFailed',
              params: {
                tab: params.tab,
                url,
                code: -1003,
                message: '演示：无法找到服务器（mock 数据）',
              },
            });
          }
        }
        if (method === 'ui.navigate') {
          const params = request.params ?? {};
          const url = String(params.url ?? '');
          // A fake download so the panel can be reviewed without a native shell.
          if (url.endsWith('.bin') || url.includes('/download')) {
            const id = 'mock-download';
            const emit = (payload: Record<string, unknown>) =>
              respond({ jsonrpc: '2.0', method: 'browser.download', params: { id, path: '/Users/you/Downloads/sample.bin', filename: 'sample.bin', ...payload } });
            emit({ state: 'started', received: 0, total: 4_000_000 });
            setTimeout(() => emit({ state: 'progress', received: 1_600_000, total: 4_000_000 }), 400);
            setTimeout(() => {
              emit({ state: 'finished', received: 4_000_000, total: 4_000_000 });
              // Mirror the native shell, which stores terminal states in Core.
              localStorage.setItem(
                'liora.mock.downloads',
                JSON.stringify([
                  {
                    id,
                    filename: 'sample.bin',
                    path: '/Users/you/Downloads/sample.bin',
                    bytes: 4_000_000,
                    total: 4_000_000,
                    state: 'finished',
                    error: '',
                    created: Date.now(),
                  },
                ]),
              );
            }, 1200);
          }
        }
        return;
      }
      const handler = methods[method];
      if (!handler) {
        respond({
          jsonrpc: '2.0',
          id: request.id ?? null,
          error: { code: -32601, message: `unknown method: ${method}` },
        });
        return;
      }
      try {
        const result = handler(request.params ?? {});
        respond({ jsonrpc: '2.0', id: request.id ?? null, result: result ?? null });
      } catch (error: any) {
        respond({
          jsonrpc: '2.0',
          id: request.id ?? null,
          error: { code: error?.code ?? -32603, message: error?.message ?? 'request failed' },
        });
      }
    },
  };
}
