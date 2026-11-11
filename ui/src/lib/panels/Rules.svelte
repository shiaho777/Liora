<script lang="ts">
  import Icon from '../Icon.svelte';
  import Segmented from '../Segmented.svelte';
  import Toggle from '../Toggle.svelte';
  import { call } from '../bridge';
  import { toast } from '../state.svelte';
  import { set } from '../settings-store.svelte';
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
  let probeResult = $state<{
    matched: boolean;
    ruleName?: string;
    h3?: string;
    ech?: boolean;
  } | null>(null);

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
    return mode === 'force'
      ? t('rules.h3.force')
      : mode === 'off'
        ? t('rules.h3.off')
        : t('rules.h3.prefer');
  }

  let hasForce = $derived(rules.some((rule) => rule.enabled && rule.h3 === 'force'));
  let hasEch = $derived(rules.some((rule) => rule.enabled && rule.ech));
</script>

<div class="rules">
  <section class="card">
    <div class="card__header">
      <div class="card__main">
        <div class="card__title">{t('rules.title')}</div>
        <div class="card__subtitle">{t('rules.lede')}</div>
      </div>
      <Toggle checked={enabled} label={t('rules.enabled')} onchange={toggleGlobal} />
    </div>

    <div class="card__body">
      {#if loading}
        <div class="rules-loading" aria-busy="true">
          <span class="skeleton skeleton--line"></span>
          <span class="skeleton skeleton--line skeleton--short"></span>
        </div>
      {:else if rules.length === 0}
        {#if !adding}
          <div class="empty-state rules-empty">
            <span class="empty-state__icon"><Icon name="shield" size={19} /></span>
            <div class="empty-state__title">{t('rules.empty')}</div>
            <p class="empty-state__body">{t('rules.emptyHint')}</p>
            <div class="rules-empty__actions">
              <button class="button button--primary button--sm" onclick={() => (adding = true)}>
                <Icon name="plus" size={13} />
                {t('rules.add')}
              </button>
              {#if presets.length > 0}
                <span class="rules-empty__or">{t('rules.orPreset')}</span>
                {#each presets as preset (preset.id)}
                  <button class="chip chip--preset" onclick={() => addPreset(preset)}>
                    <Icon name="plus" size={10} />
                    {localized(preset.name)}
                  </button>
                {/each}
              {/if}
            </div>
          </div>
        {/if}
      {:else}
        <div class="rules__toolbar">
          <span class="rules__count">{t('rules.count', { count: rules.length })}</span>
          <button class="button button--sm" onclick={() => (adding = !adding)}>
            <Icon name={adding ? 'close' : 'plus'} size={12} />
            {adding ? t('rules.cancel') : t('rules.add')}
          </button>
        </div>

        <div class="rule-list">
          {#each rules as rule (rule.id)}
            <div class="rule" class:rule--disabled={!rule.enabled || !enabled}>
              <span class="rule__dot" class:rule__dot--on={rule.enabled && enabled}></span>
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
                <button
                  class="icon-button"
                  title={t('rules.delete')}
                  aria-label={t('rules.delete')}
                  onclick={() => remove(rule)}
                >
                  <Icon name="trash" size={14} />
                </button>
              </div>
            </div>
          {/each}
        </div>
      {/if}

      {#if adding}
        <div class="editor">
          <div class="editor__head">
            <div class="editor__title">{t('rules.editorTitle')}</div>
            <button
              class="icon-button"
              title={t('rules.cancel')}
              aria-label={t('rules.cancel')}
              onclick={() => (adding = false)}
            >
              <Icon name="close" size={13} />
            </button>
          </div>

          <label class="field">
            <span class="field__label">{t('rules.domainLabel')}</span>
            <input
              class="input input--mono"
              bind:value={domain}
              placeholder={t('rules.domain.placeholder')}
              spellcheck="false"
            />
          </label>

          <div class="editor__grid">
            <div class="field">
              <span class="field__label">{t('rules.h3')}</span>
              <Segmented
                value={h3}
                options={h3Options}
                ariaLabel={t('rules.h3')}
                onchange={(next) => (h3 = next)}
              />
            </div>
            <div class="field">
              <span class="field__label">{t('rules.ech')}</span>
              <div class="editor__switch">
                <Toggle checked={ech} label={t('rules.ech')} onchange={(next) => (ech = next)} />
                <span>{ech ? t('rules.echOn') : t('rules.echOff')}</span>
              </div>
            </div>
          </div>

          {#if presets.length > 0}
            <div class="editor__presets">
              <span class="editor__presets-label">{t('rules.orPreset')}</span>
              {#each presets as preset (preset.id)}
                <button class="chip chip--preset" onclick={() => addPreset(preset)}>
                  <Icon name="plus" size={10} />
                  {localized(preset.name)}
                </button>
              {/each}
            </div>
          {/if}

          {#if addError}
            <div class="callout callout--danger">
              <span class="callout__icon"><Icon name="warning" size={14} /></span>
              <span>{addError}</span>
            </div>
          {/if}

          <div class="editor__actions">
            <button class="button button--ghost button--sm" onclick={() => (adding = false)}>
              {t('rules.cancel')}
            </button>
            <button class="button button--primary button--sm" onclick={addCustom}>
              {t('rules.add')}
            </button>
          </div>
        </div>
      {/if}

      {#if hasForce}
        <div class="callout callout--warning rules__notice">
          <span class="callout__icon"><Icon name="warning" size={14} /></span>
          <span>{t('rules.forceWarning')}</span>
        </div>
      {/if}

      {#if hasEch}
        <div class="callout rules__notice">
          <span class="callout__icon"><Icon name="info" size={14} /></span>
          <span>{t('rules.echWarning')}</span>
        </div>
      {/if}
    </div>
  </section>

  <section class="card test">
    <div class="test__head">
      <span class="test__icon"><Icon name="search" size={15} /></span>
      <div class="test__main">
        <div class="card__title">{t('rules.test')}</div>
        <div class="card__subtitle">{t('rules.testPlaceholder')}</div>
      </div>
      <div class="test__form">
        <input
          class="input input--mono"
          bind:value={probe}
          placeholder="x.com"
          spellcheck="false"
          onkeydown={(event) => event.key === 'Enter' && runProbe()}
        />
        <button class="button" onclick={runProbe}>{t('rules.check')}</button>
      </div>
    </div>

    {#if probeResult}
      <div class="test__result">
        <div class="callout" class:callout--accent={probeResult.matched}>
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
      </div>
    {/if}
  </section>
</div>

<style>
  .rules {
    display: flex;
    flex-direction: column;
    gap: 14px;
  }

  .card__main {
    flex: 1;
    min-width: 0;
  }

  .rules-empty {
    gap: 6px;
    padding: 24px 18px;
  }

  .rules-empty__actions {
    display: flex;
    align-items: center;
    justify-content: center;
    flex-wrap: wrap;
    gap: 6px;
    margin-top: 8px;
  }

  .rules-empty__or {
    margin: 0 3px;
    font-size: 11px;
    color: var(--text-muted);
  }

  .chip--preset {
    display: inline-flex;
    align-items: center;
    gap: 4px;
  }

  .rules__toolbar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    margin-bottom: 10px;
  }

  .rules__count {
    font-size: 11px;
    font-weight: 550;
    letter-spacing: 0.02em;
    text-transform: uppercase;
    color: var(--text-muted);
  }

  .rule-list {
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    overflow: hidden;
  }

  .rule {
    padding: 12px 13px;
    border: none;
    border-radius: 0;
    background: var(--surface);
  }

  .rule + .rule {
    border-top: 1px solid var(--border);
  }

  .rule__dot {
    width: 7px;
    height: 7px;
    flex: none;
    border-radius: 50%;
    background: var(--border-strong);
  }

  .rule__dot--on {
    background: var(--success);
  }

  .rule__name {
    font-size: 12.5px;
    font-weight: 550;
  }

  .rule__domains {
    font-size: 10.5px;
  }

  .rule__meta {
    margin-top: 6px;
  }

  .rules__notice {
    margin-top: 12px;
  }

  .editor {
    display: flex;
    flex-direction: column;
    gap: 12px;
    margin-top: 12px;
    padding: 13px;
    border-radius: var(--radius-sm);
    border: 1px solid var(--border);
    background: var(--bg-sunken);
  }

  .editor__head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 10px;
  }

  .editor__title {
    font-size: 12.5px;
    font-weight: 600;
  }

  .editor__grid {
    display: flex;
    align-items: flex-end;
    gap: 24px;
    flex-wrap: wrap;
  }

  .field {
    display: flex;
    flex-direction: column;
    gap: 6px;
  }

  .field__label {
    font-size: 11px;
    font-weight: 550;
    color: var(--text-muted);
  }

  .editor__switch {
    display: flex;
    align-items: center;
    gap: 8px;
    height: 32px;
    font-size: 12px;
    color: var(--text-secondary);
  }

  .editor__presets {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 5px;
  }

  .editor__presets-label {
    margin-right: 2px;
    font-size: 11px;
    color: var(--text-muted);
  }

  .editor__actions {
    display: flex;
    justify-content: flex-end;
    gap: 6px;
  }

  .test__head {
    display: flex;
    align-items: center;
    gap: 11px;
    padding: 14px 18px;
  }

  .test__icon {
    width: 32px;
    height: 32px;
    flex: none;
    display: grid;
    place-items: center;
    border-radius: 9px;
    color: var(--text-secondary);
    background: var(--bg-sunken);
  }

  .test__main {
    flex: 1;
    min-width: 0;
  }

  .test__form {
    flex: none;
    display: flex;
    gap: 6px;
  }

  .test__form .input {
    width: 190px;
  }

  .test__result {
    padding: 0 18px 14px;
  }

  .test__result .callout {
    margin: 0;
  }

  .rules-loading {
    display: flex;
    flex-direction: column;
    gap: 9px;
    padding: 6px 0;
  }

  .skeleton {
    display: block;
    height: 12px;
    border-radius: 6px;
    background: linear-gradient(90deg, var(--bg-sunken), var(--bg-hover), var(--bg-sunken));
    background-size: 200% 100%;
    animation: shimmer 1.4s linear infinite;
  }

  .skeleton--line {
    width: 100%;
  }

  .skeleton--short {
    width: 58%;
  }

  @keyframes shimmer {
    from {
      background-position: 200% 0;
    }
    to {
      background-position: -200% 0;
    }
  }

  @media (max-width: 720px) {
    .test__head {
      flex-wrap: wrap;
    }

    .test__form {
      width: 100%;
    }

    .test__form .input {
      flex: 1;
      width: auto;
    }
  }
</style>
