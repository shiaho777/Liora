// DOM-driven test scenarios for the shell's LIORA_TEST_COMMAND harness.
//
// These deliberately click real buttons by their visible text, because they
// exercise the same paths a user would. That also makes them fragile when
// copy changes — which is exactly why they live here, registered only when
// the shell is in test mode, instead of inside the production command path.

function clickButtonByText(text: string, exact = false): void {
  const button = [...document.querySelectorAll('button')].find((item) =>
    exact ? item.textContent?.trim() === text : item.textContent?.includes(text),
  );
  button?.click();
}

function clickProviderCard(name: string): void {
  const card = [...document.querySelectorAll<HTMLElement>('.provider')].find((item) =>
    item.textContent?.includes(name),
  );
  card?.click();
}

function clickCefCardButton(label: string, exact = false): void {
  const card = [...document.querySelectorAll<HTMLElement>('.card')].find((item) =>
    item.textContent?.includes('Chromium (CEF)'),
  );
  [...(card?.querySelectorAll<HTMLElement>('button') ?? [])]
    .find((button) => (exact ? button.textContent?.trim() === label : button.textContent?.includes(label)))
    ?.click();
}

function clickConfirmButton(label: string): void {
  [...document.querySelectorAll<HTMLElement>('.confirm button')]
    .find((button) => button.textContent?.includes(label))
    ?.click();
}

function clickNavItem(text: string, exact = false): void {
  const item = [...document.querySelectorAll<HTMLElement>('.nav-item')].find((node) =>
    exact ? node.textContent?.trim() === text : node.textContent?.includes(text),
  );
  item?.click();
}

/** Runs a test scenario. Returns true when the name was recognised. */
export function runTestScenario(name: string): boolean {
  switch (name) {
    case 'splitdrag': {
      const splitNode = document.querySelector<HTMLElement>('.split');
      const divider = document.querySelector<HTMLElement>('.split__divider');
      if (!splitNode || !divider) return true;
      const splitRect = splitNode.getBoundingClientRect();
      const dividerRect = divider.getBoundingClientRect();
      const options = {
        bubbles: true,
        pointerId: 1,
        clientY: dividerRect.top + 10,
      } as PointerEventInit;
      divider.dispatchEvent(
        new PointerEvent('pointerdown', { ...options, clientX: dividerRect.left + 2 }),
      );
      const x = splitRect.left + splitRect.width * 0.35;
      window.dispatchEvent(new PointerEvent('pointermove', { ...options, clientX: x }));
      window.dispatchEvent(new PointerEvent('pointerup', { ...options, clientX: x }));
      return true;
    }
    case 'watchsubmit':
      document.querySelector<HTMLElement>('.watch-dialog .button--primary')?.click();
      return true;
    case 'addresstype': {
      // Type into the real address input so the suggestion pipeline runs.
      const input = document.querySelector<HTMLInputElement>('.address__input');
      if (!input) return false;
      input.focus();
      const setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')?.set;
      setter?.call(input, 'example');
      input.dispatchEvent(new Event('input', { bubbles: true }));
      setTimeout(() => {
        const rows = [...document.querySelectorAll('.address-suggest__row')].map((row) =>
          row.textContent?.trim(),
        );
        (window as unknown as { __lioraSuggestDump?: string }).__lioraSuggestDump = JSON.stringify({
          focused: document.activeElement === input,
          value: input.value,
          rows,
        });
      }, 600);
      return true;
    }
    case 'homeclick':
      [...document.querySelectorAll<HTMLElement>('.toolbar button')]
        .find((button) => button.getAttribute('aria-label') === '主页' || button.getAttribute('title') === '主页')
        ?.click();
      return true;
    case 'sitejs':
      document.querySelector<HTMLElement>('.sitecard .toggle[aria-label="JavaScript"]')?.click();
      return true;
    case 'usnew':
      clickButtonByText('新建脚本');
      return true;
    case 'ussave': {
      const setVal = (el: HTMLInputElement | HTMLTextAreaElement | null, value: string) => {
        if (!el) return;
        el.value = value;
        el.dispatchEvent(new Event('input', { bubbles: true }));
      };
      setVal(document.querySelector<HTMLInputElement>('.editor input[type="text"]'), 'Test script');
      const areas = document.querySelectorAll<HTMLTextAreaElement>('.editor textarea');
      setVal(areas[0] ?? null, 'example.com');
      setVal(areas[1] ?? null, "document.title='VIA-UI';");
      setTimeout(() => clickButtonByText('保存'), 300);
      return true;
    }
    case 'usimport': {
      const source = [
        '// ==UserScript==',
        '// @name         Example Retitler',
        '// @match        https://example.com/*',
        '// @run-at       document-end',
        '// @grant        none',
        '// ==/UserScript==',
        "document.title = 'IMPORTED-OK';",
      ].join('\n');
      const input = document.querySelector<HTMLInputElement>('.file-hidden');
      if (!input) return true;
      const file = new File([source], 'test.user.js', { type: 'text/javascript' });
      const transfer = new DataTransfer();
      transfer.items.add(file);
      input.files = transfer.files;
      input.dispatchEvent(new Event('change', { bubbles: true }));
      return true;
    }
    case 'tabmiddle': {
      // Middle-click the second tab (first web tab) to close it.
      const tab = document.querySelectorAll<HTMLElement>('.tab')[1];
      tab?.dispatchEvent(new MouseEvent('auxclick', { bubbles: true, button: 1 }));
      return true;
    }
    case 'tabarrow': {
      const rail = document.querySelector<HTMLElement>('.tabstrip__rail');
      rail?.dispatchEvent(new KeyboardEvent('keydown', { bubbles: true, key: 'ArrowRight' }));
      return true;
    }
    case 'addressnav': {
      const input = document.querySelector<HTMLInputElement>('.address__input');
      if (!input) return true;
      input.value = 'https://www.iana.org';
      input.dispatchEvent(new Event('input', { bubbles: true }));
      input.closest('form')?.dispatchEvent(new Event('submit', { bubbles: true, cancelable: true }));
      return true;
    }
    case 'railsearch': {
      const input = document.querySelector<HTMLInputElement>('.tabrail__search input');
      if (!input) return true;
      input.value = 'wikipedia';
      input.dispatchEvent(new Event('input', { bubbles: true }));
      return true;
    }
    case 'trapcheck': {
      // Press Tab several times inside the open dialog and report whether focus
      // stayed contained. The trap listens on the dialog container.
      const dialog = document.querySelector<HTMLElement>('[aria-modal="true"]');
      if (!dialog) {
        (window as unknown as { __lioraTrap?: string }).__lioraTrap = 'no-dialog';
        return true;
      }
      for (let i = 0; i < 6; i++) {
        dialog.dispatchEvent(new KeyboardEvent('keydown', { key: 'Tab', bubbles: true }));
      }
      const contained = dialog.contains(document.activeElement);
      (window as unknown as { __lioraTrap?: string }).__lioraTrap = contained ? 'TRAP-OK' : 'TRAP-FAIL';
      return true;
    }
    case 'dnschange':
      clickButtonByText('选择提供商', true);
      setTimeout(() => clickProviderCard('Quad9'), 600);
      return true;
    case 'dnschangeback':
      clickButtonByText('选择提供商', true);
      setTimeout(() => clickProviderCard('Cloudflare'), 600);
      return true;
    case 'restartnetwork':
      clickButtonByText('重启');
      return true;
    case 'downloadcancel': {
      const control = document.querySelector('.download-row__control');
      const button = control?.querySelector<HTMLButtonElement>('.icon-button');
      button?.click();
      return true;
    }
    case 'dldiag': {
      // Read after a tick so Svelte has flushed any pending state change.
      setTimeout(() => {
        const rows = document.querySelectorAll('.download-row');
        const control = document.querySelector('.download-row__control');
        (window as unknown as { __lioraDlDump?: string }).__lioraDlDump = JSON.stringify({
          rows: rows.length,
          controlHtml: control?.innerHTML.slice(0, 200) ?? 'none',
          states: [...rows].map((row) => row.querySelector('.download-row__status')?.textContent?.trim()),
        });
      }, 400);
      return true;
    }
    case 'downloadretry': {
      const row = document.querySelector('.download-row');
      row?.querySelector<HTMLButtonElement>('.download-row__control .button')?.click();
      return true;
    }
    case 'revealtoast':
      document.querySelector<HTMLElement>('.toast__action[data-kind="folder"]')?.click();
      return true;
    case 'openfiletoast':
      document.querySelector<HTMLElement>('.toast__action[data-kind="file"]')?.click();
      return true;
    case 'applyfrombadge':
      document.querySelector<HTMLElement>('.network-pending')?.click();
      return true;
    case 'saverefreshed':
      clickButtonByText('保存当前状态');
      return true;
    case 'openrecent':
      document.querySelector<HTMLElement>('.recent__item')?.click();
      return true;
    case 'deleterecent':
      document.querySelector<HTMLElement>('.recent__delete')?.click();
      return true;
    case 'clearrecent':
      clickButtonByText('清理旧文件');
      return true;
    case 'openrecenttimeline':
      document.querySelector<HTMLElement>('.recent__item[data-kind="timeline"]')?.click();
      return true;
    case 'tab1':
      document.querySelectorAll<HTMLElement>('.tab')[0]?.click();
      return true;
    case 'tab2':
      document.querySelectorAll<HTMLElement>('.tab')[1]?.click();
      return true;
    case 'tab3':
      document.querySelectorAll<HTMLElement>('.tab')[2]?.click();
      return true;
    case 'refreshcatalog':
      document.querySelector<HTMLElement>('.catalog-refresh')?.click();
      return true;
    case 'rulesadd':
      clickButtonByText('添加规则', true);
      return true;
    case 'rulespreset':
      clickButtonByText('Twitter');
      return true;
    case 'updatecomponent':
      document.querySelector<HTMLElement>('.component-update')?.click();
      return true;
    case 'kitesurfconnect':
      clickButtonByText('连接引擎', true);
      return true;
    case 'kitesurfselect':
      clickButtonByText('设为默认', true);
      return true;
    case 'cefdownload':
      clickCefCardButton('下载');
      return true;
    case 'cefselect':
      clickCefCardButton('默认内核', true);
      return true;
    case 'cefkeep':
      clickConfirmButton('保持系统 WebView');
      return true;
    case 'cefconfirmselect':
      clickConfirmButton('仍要选择');
      return true;
    case 'cefconfirm':
      clickConfirmButton('仍要下载');
      return true;
    case 'cefcancel':
      clickConfirmButton('取消');
      return true;
    case 'opendns':
      clickNavItem('DNS 与直连');
      return true;
    case 'leavepage':
      clickNavItem('外观', true);
      return true;
    case 'opendnsrestart':
      document.querySelector<HTMLElement>('.network-pending')?.click();
      setTimeout(() => clickButtonByText('重启'), 1500);
      return true;
    case 'advanced':
      document.querySelector<HTMLElement>('.settings__footer .toggle')?.click();
      return true;
    case 'settingsdone':
      document.querySelector<HTMLElement>('.settings__done')?.click();
      return true;
    case 'scrollsettings': {
      const main = document.querySelector<HTMLElement>('.settings__main');
      if (main) main.scrollTop = main.scrollHeight;
      return true;
    }
    case 'scrollrules':
      document.querySelector('.rules')?.scrollIntoView({ block: 'start' });
      return true;
    case 'pagedump': {
      const errors = (window as unknown as { __lioraErrors?: string[] }).__lioraErrors ?? [];
      const content = document.querySelector('.content');
      const badge = document.querySelector<HTMLElement>('.network-pending');
      (window as unknown as { __lioraPageText?: string }).__lioraPageText = [
        document.body.innerText,
        '-- favicons --',
        JSON.stringify(
          [...document.querySelectorAll('.tab')].map((item) => ({
            title: item.querySelector('.tab__title')?.textContent?.trim() ?? '',
            icon: item.querySelector('.tab__favicon img') ? 'image' : 'placeholder',
          })),
        ),
        '-- toolbar buttons --',
        JSON.stringify(
          [...document.querySelectorAll('.toolbar button')].map(
            (item) => item.getAttribute('aria-label') || item.getAttribute('title') || '(icon)',
          ),
        ),
        '-- active nav --',
        document.querySelector('.nav-item--active')?.textContent?.trim() ?? 'none',
        '-- nav states --',
        JSON.stringify(
          [...document.querySelectorAll('.nav-item')].map((item) => ({
            text: item.textContent?.trim(),
            active: item.classList.contains('nav-item--active'),
            background: getComputedStyle(item).backgroundColor,
          })),
        ),
        '-- pane title --',
        document.querySelector('.settings__title')?.textContent?.trim() ?? 'none',
        '-- section --',
        (() => {
          const el = document.querySelector('.settings__main .section');
          if (!el) return 'none';
          const style = getComputedStyle(el);
          const rect = el.getBoundingClientRect();
          return JSON.stringify({
            opacity: style.opacity,
            transform: style.transform,
            animation: style.animationName,
            display: style.display,
            top: rect.top,
            height: rect.height,
            scrollTop: document.querySelector('.settings__main')?.scrollTop,
          });
        })(),
        '-- split --',
        (() => {
          const el = document.querySelector('.split');
          if (!el) return 'none';
          const style = getComputedStyle(el);
          return JSON.stringify({
            inline: el.getAttribute('style'),
            columns: style.gridTemplateColumns,
            width: el.getBoundingClientRect().width,
          });
        })(),
        '-- active icons --',
        JSON.stringify(
          [...document.querySelectorAll('.toolbar .icon-button--active')].map(
            (item) => item.getAttribute('aria-label') || item.getAttribute('title'),
          ),
        ),
        '-- badge --',
        badge
          ? JSON.stringify({ text: badge.textContent?.trim(), title: badge.title })
          : 'none',
        '-- content --',
        content?.innerHTML.slice(0, 700) ?? 'no content element',
        '-- focus trap --',
        (window as unknown as { __lioraTrap?: string }).__lioraTrap ?? 'not-run',
        '-- errors --',
        JSON.stringify(errors),
      ].join('\n');
      return true;
    }
    default:
      return false;
  }
}