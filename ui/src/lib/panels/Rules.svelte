<script lang="ts">
  import Icon from '../Icon.svelte';
  import Segmented from '../Segmented.svelte';
  import Toggle from '../Toggle.svelte';
  import { call } from '../bridge';
  import { app, toast } from '../state.svelte';
  import { set, value } from '../settings-store.svelte';
  import { localized, t } from '../i18n.svelte';

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

  interface Preset {
    id: string;
    name: Record<string, string>;
    desc: Record<string, string>;
  }

  let rules = $state<Rule[]>([]);
  let presets = $state<Preset[]>([]);
  let enabled = $state(true);
  let loading = $state(true);
  let adding = $state(false);

  let domain = $state('');
  let h3 = $state('prefer');
  let ech = $state(true);
  let dnsOverride = $state('');
  let addError = $state('');

  let probe = $state('');
  let probeResult = $state<{ matched: boolean; ruleName?: string; h3?: string; ech?: boolean } | null>(null);

  async function load(): Promise<void> {
    loading = true;
    try {
      const list = await call<{ rules: Rule[]; enabled: boolean }>('rules.list');
      rules = list.rules;
      enabled = list.enabled !== false;
      presets = await call<Preset[]>('rules.presets');
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    } finally {
      loading = false;
    }
  }

  $effect(() => {
    void load();
  });

  async function toggleGlobal(next: boolean): Promise<void> {
    enabled = next;
    await set('net.rulesEnabled', next);
  }

  async function toggleRule(rule: Rule, next: boolean): Promise<void> {
    rule.enabled = next;
    try {
      await call('rules.enable', { id: rule.id, enabled: next });
    } catch (error: any) {
      rule.enabled = !next;
      toast(String(error?.message ?? error), 'error');
    }
  }

  async function addPreset(preset: Preset): Promise<void> {
    try {
      const rule = await call<Rule>('rules.add', { preset: preset.id });
      rules = [...rules, rule];
      adding = false;
      toast(t('settings.changed'));
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    }
  }

  async function addCustom(): Promise<void> {
    addError = '';
    if (!domain.trim()) return;
    try {
      const rule = await call<Rule>('rules.add', {
        name: domain.trim(),
        domains: [domain.trim()],
        h3,
        ech,
        dns: dnsOverride,
      });
      rules = [...rules, rule];
      domain = '';
      adding = false;
      toast(t('settings.changed'));
      await load();
    } catch (error: any) {
      addError = String(error?.message ?? error);
    }
  }

  async function remove(rule: Rule): Promise<void> {
    try {
      await call('rules.remove', { id: rule.id });
      rules = rules.filter((item) => item.id !== rule.id);
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    }
  }

  async function runProbe(): Promise<void> {
    if (!probe.trim()) return;
    try {
      probeResult = await call('rules.match', { host: probe.trim() });
    } catch (error: any) {
      toast(String(error?.message ?? error), 'error');
    }
  }

  const h3Options = $derived([
    { value: 'off', label: t('rules.h3.off') },
    { value: 'prefer', label: t('rules.h3.prefer') },
    { value: 'force', label: t('rules.h3.force') },
  ]);

  function h3Label(mode: string): string {
    return mode === 'force' ? t('rules.h3.force') : mode === 'off' ? t('rules.h3.off') : t('rules.h3.prefer');
  }

  let hasForce = $derived(rules.some((rule) => rule.enabled && rule.h3 === 'force'));
  let hasEch = $derived(rules.some((rule) => rule.enabled && rule.ech));
</script>

<div class="section">
  <div class="card">
    <div class="card__header">
      <div class="row__main">
        <div class="card__title">{t('rules.title')}</div>
        <div class="card__subtitle">{t('rules.lede')}</div>
      </div>
      <Toggle checked={enabled} label={t('rules.enabled')} onchange={toggleGlobal} />
    </div>

    <div class="card__body">
      {#if loading}
        <div class="empty">…</div>
      {:else if rules.length === 0}
        <div class="empty">
          {t('rules.empty')}<br />
          <span style:font-size="12px">{t('rules.emptyHint')}</span>
        </div>
      {:else}
        <div class="stack">
          {#each rules as rule (rule.id)}
            <div class="rule" class:rule--disabled={!rule.enabled || !enabled}>
              <div class="rule__main">
                <div class="rule__name">
                  {rule.name}
                  {#if rule.preset}
                    <span class="badge">{t('rules.preset')}</span>
                  {/if}
                </div>
                <div class="rule__domains">{rule.domains.join(' · ')}</div>
                <div class="rule__meta">
                  <span class="badge" class:badge--accent={rule.h3 === 'force'}>
                    H3 {h3Label(rule.h3)}
                  </span>
                  {#if rule.ech}
                    <span class="badge badge--accent">ECH</span>
                  {/if}
                  {#if rule.dns}
                    <span class="badge mono">{rule.dns}</span>
                  {/if}
                </div>
              </div>
              <div class="row__control">
                <Toggle
                  checked={rule.enabled}
                  label={rule.name}
                  onchange={(next) => toggleRule(rule, next)}
                />
                <button class="icon-button" title={t('rules.delete')} onclick={() => remove(rule)}>
                  <Icon name="trash" size={14} />
                </button>
              </div>
            </div>
          {/each}
        </div>
      {/if}

      <div class="cluster" style:margin-top="12px">
        <button class="button button--primary" onclick={() => (adding = !adding)}>
          <Icon name="plus" size={13} />
          {t('rules.add')}
        </button>
        {#each presets as preset (preset.id)}
          <button class="chip" onclick={() => addPreset(preset)}>
            + {localized(preset.name)}
          </button>
        {/each}
      </div>

      {#if adding}
        <div class="addform card">
          <div class="card__body">
            <div class="stack">
              <input
                class="input input--mono"
                bind:value={domain}
                placeholder={t('rules.domain.placeholder')}
                aria-label={t('rules.domain')}
              />

              <div class="cluster">
                <span class="muted" style:font-size="12px">{t('rules.h3')}</span>
                <Segmented value={h3} options={h3Options} ariaLabel={t('rules.h3')} onchange={(v) => (h3 = v)} />
                <span class="muted" style:font-size="12px; margin-left:8px">{t('rules.ech')}</span>
                <Toggle checked={ech} label={t('rules.ech')} onchange={(v) => (ech = v)} />
              </div>

              {#if addError}
                <div class="callout callout--danger">
                  <span class="callout__icon"><Icon name="warning" size={14} /></span>
                  <span>{addError}</span>
                </div>
              {/if}

              <div class="cluster">
                <button class="button button--primary" onclick={addCustom}>{t('rules.add')}</button>
                <button class="button button--ghost" onclick={() => (adding = false)}>✕</button>
              </div>
            </div>
          </div>
        </div>
      {/if}

      {#if hasForce}
        <div class="callout callout--warning" style:margin-top="12px">
          <span class="callout__icon"><Icon name="warning" size={14} /></span>
          <span>{t('rules.forceWarning')}</span>
        </div>
      {/if}

      {#if hasEch}
        <div class="callout" style:margin-top="8px">
          <span class="callout__icon"><Icon name="info" size={14} /></span>
          <span>{t('rules.echWarning')}</span>
        </div>
      {/if}
    </div>
  </div>

  <div class="card">
    <div class="card__header">
      <div>
        <div class="card__title">{t('rules.test')}</div>
        <div class="card__subtitle">{t('rules.testPlaceholder')}</div>
      </div>
    </div>
    <div class="card__body">
      <div class="cluster">
        <input
          class="input input--mono"
          bind:value={probe}
          placeholder="x.com"
          style:flex="1"
          onkeydown={(event) => event.key === 'Enter' && runProbe()}
        />
        <button class="button" onclick={runProbe}>{t('rules.test')}</button>
      </div>

      {#if probeResult}
        <div class="callout" class:callout--accent={probeResult.matched} style:margin-top="10px">
          <span class="callout__icon">
            <Icon name={probeResult.matched ? 'check' : 'info'} size={14} />
          </span>
          <span>
            {#if probeResult.matched}
              {t('rules.matched')}: <strong>{probeResult.ruleName}</strong>
              · H3 {h3Label(probeResult.h3 ?? 'prefer')}
              {#if probeResult.ech}· ECH{/if}
            {:else}
              {t('rules.noMatch')}
            {/if}
          </span>
        </div>
      {/if}
    </div>
  </div>
</div>

<style>
  .addform {
    margin-top: 12px;
    box-shadow: none;
    background: var(--bg-sunken);
  }
</style>
