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
  'content.adblock': true,
  'downloads.dir': '',
  'downloads.ask': false,
  'extensions.enabled': true,
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
  'extensions.enabled',
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
  'content.adblock',
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
  const stored = localStorage.getItem('liora.mock.state');
  const state: {
    settings: Record<string, unknown>;
    sites: Record<string, Record<string, unknown>>;
    rules: Rule[];
    installed: Array<{ id: string; version: string; path: string; installedAt: number; current: boolean }>;
    bookmarks: Array<{ id: number; title: string; url: string }>;
    history: Array<{ url: string; title: string; visitCount: number; lastVisit: number }>;
    permissions: Array<{ origin: string; name: string; value: string; modified: number }>;
  } = stored
    ? JSON.parse(stored)
    : {
        settings: {},
        sites: {},
        rules: [],
        installed: [],
        bookmarks: [
          { id: 10, title: 'Svelte', url: 'https://svelte.dev' },
          { id: 11, title: 'Chromium', url: 'https://www.chromium.org' },
        ],
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
  state.bookmarks ??= [
    { id: 10, title: 'Svelte', url: 'https://svelte.dev' },
    { id: 11, title: 'Chromium', url: 'https://www.chromium.org' },
  ];
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

  const get = (key: string) => (key in state.settings ? state.settings[key] : DEFAULTS[key]);
  const set = (key: string, value: unknown) => {
    if (JSON.stringify(value) === JSON.stringify(DEFAULTS[key])) delete state.settings[key];
    else state.settings[key] = value;
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

    'settings.describe': () => ({
      items: Object.keys(DEFAULTS).map((key) => ({
        key,
        default: DEFAULTS[key],
        value: get(key),
        scopes: SITE_SCOPES.has(key) ? ['global', 'site', 'tab'] : ['global'],
        advanced: ADVANCED.has(key),
        ...(CHOICES[key] ? { choices: CHOICES[key] } : {}),
      })),
      sites: Object.entries(state.sites).map(([site, values]) => ({ site, values })),
    }),
    'settings.get': (p) => {
      if (p.key) return { [p.key]: get(p.key) };
      const prefix = p.prefix ? `${p.prefix}.` : '';
      const out: Record<string, unknown> = {};
      for (const key of Object.keys(DEFAULTS)) if (key.startsWith(prefix)) out[key] = get(key);
      return out;
    },
    'settings.set': (p) => {
      if (p.values) Object.assign(state.settings, p.values);
      else set(p.key, p.value);
      persist();
      return { ok: true };
    },
    'settings.reset': (p) => {
      delete state.settings[p.key];
      persist();
      return { ok: true };
    },
    'settings.clearSite': (p) => {
      delete state.sites[p.site];
      persist();
      return { ok: true };
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
      return rule;
    },
    'rules.update': (p) => {
      const index = state.rules.findIndex((r) => r.id === p.id);
      if (index < 0) throw { code: -32002, message: '规则不存在' };
      state.rules[index] = { ...state.rules[index], ...p, domains: p.domains ?? state.rules[index].domains };
      persist();
      return state.rules[index];
    },
    'rules.remove': (p) => {
      state.rules = state.rules.filter((r) => r.id !== p.id);
      persist();
      return { ok: true };
    },
    'rules.enable': (p) => {
      const rule = state.rules.find((r) => r.id === p.id);
      if (rule) rule.enabled = p.enabled !== false;
      persist();
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
    'net.forHost': (p) => methods['rules.match'](p),
    'net.runtime': () => ({ running: true, dohPort: 5399, proxyPort: 5400, spki: 'mock' }),
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
        caps: ['secureDns', 'httpsRecords', 'ech', 'forceQuic', 'proxy', 'extensions', 'devtools', 'isolatedProfiles'],
        available: state.installed.some((i) => i.id === 'kernel-cef' && i.current),
        netNeedsRestart: true,
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
      bar: {
        id: 1,
        type: 'folder',
        title: '书签栏',
        children: state.bookmarks.map((entry) => ({ ...entry, type: 'url' })),
      },
      other: { id: 2, type: 'folder', title: '其他书签', children: [] },
      mobile: { id: 3, type: 'folder', title: '移动端书签', children: [] },
    }),
    'bookmarks.forUrl': (p) => state.bookmarks.filter((entry) => entry.url === p.url),
    'bookmarks.add': (p) => {
      const entry = { id: 100 + state.bookmarks.length, title: p.title || p.url, url: p.url };
      state.bookmarks.push(entry);
      persist();
      return entry;
    },
    'bookmarks.remove': (p) => {
      state.bookmarks = state.bookmarks.filter((entry) => entry.id !== p.id);
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
    'history.suggest': () => [],
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

    'backup.list': () => [],
    'backup.now': () => ({ path: 'mock/backups/manual.liora' }),
    'backup.sections': () => ['settings', 'bookmarks', 'history', 'permissions'],

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
        if (method === 'ui.restartKernel') {
          const plan = netPlan();
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
