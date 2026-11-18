// Command dispatch, shared by the shell's menu shortcuts and the test harness.
//
// This used to live inline in App.svelte as a 390-line effect. Moving it here
// keeps the component readable and lets the dispatch rules be unit-tested
// without a browser. Everything that touches component state arrives through
// CommandContext; DOM-driven test scenarios stay in test-commands.ts.

export interface CommandContext {
  activeId: number;
  activeUrl: string;
  groups: Array<{ id: string }>;
  splitSecondary: number | null;
  siteCardOpen: boolean;
  addressEl: HTMLInputElement | null;
  newTab: () => void;
  closeTab: (id: number) => void;
  navigate: (url: string) => void;
  openSettings: (section?: string) => void;
  openFind: () => void;
  runFind: (forward: boolean) => void;
  closeFind: () => void;
  openFindWith: (text: string) => void;
  goBack: () => void;
  goForward: () => void;
  changeZoom: (delta: number) => void;
  openDownloads: () => void;
  openPalette: () => void;
  closePalette: () => void;
  requestReader: (summary: boolean) => void;
  closeReader: () => void;
  toggleSplit: () => void;
  closeSplit: () => void;
  setSplitRatio: (ratio: number) => void;
  focusSplitSecondary: () => void;
  openWatchDialog: () => void;
  openContainerPicker: () => void;
  openMiniWindow: () => void;
  createGroup: () => void;
  assignGroup: (tabId: number, groupId: string) => void;
  reopenClosedTab: () => void;
  toggleSiteCard: () => void;
  copyReport: () => void;
  saveReport: () => void;
  restartKernel: () => void;
  saveTimeline: () => void;
  openUrl: (url: string) => void;
  setFullscreen: (fullscreen: boolean) => void;
  notify: (method: string, params: Record<string, unknown>) => void;
  setSetting: (key: string, value: unknown) => void;
  /** DOM-driven scenarios, registered only in test mode. */
  runTestCommand?: (name: string) => boolean;
}

/** Prefix commands carry a payload after the colon. */
function prefixed(name: string, ctx: CommandContext): boolean {
  if (name.startsWith('settings:')) {
    ctx.openSettings(name.slice('settings:'.length));
    return true;
  }
  if (name.startsWith('open:')) {
    ctx.navigate(name.slice('open:'.length));
    return true;
  }
  if (name.startsWith('suggest:')) {
    const input = document.querySelector<HTMLInputElement>('.search input');
    if (input) {
      input.value = name.slice('suggest:'.length);
      input.dispatchEvent(new Event('input', { bubbles: true }));
      input.focus();
    }
    return true;
  }
  if (name.startsWith('find:')) {
    ctx.openFindWith(name.slice('find:'.length));
    return true;
  }
  return false;
}

/**
 * Runs one named command. Returns true when the command was recognised.
 * Unknown names fall through to the test harness (if registered) and are
 * otherwise ignored, exactly like the previous inline implementation.
 */
export function runCommand(name: string, ctx: CommandContext): boolean {
  if (prefixed(name, ctx)) return true;

  switch (name) {
    case 'newTab': ctx.newTab(); return true;
    case 'closeTab': ctx.closeTab(ctx.activeId); return true;
    case 'reload': ctx.navigate(ctx.activeUrl); return true;
    case 'focusAddress': ctx.addressEl?.focus(); ctx.addressEl?.select(); return true;
    case 'settings': ctx.openSettings(); return true;
    case 'fullscreen': ctx.setFullscreen(true); return true;
    case 'unfullscreen': ctx.setFullscreen(false); return true;
    case 'find': ctx.openFind(); return true;
    case 'findnext': ctx.runFind(true); return true;
    case 'findprev': ctx.runFind(false); return true;
    case 'findclose': ctx.closeFind(); return true;
    case 'devTools': ctx.notify('ui.devTools', { tab: ctx.activeId }); return true;
    case 'back': ctx.goBack(); return true;
    case 'forward': ctx.goForward(); return true;
    case 'zoomIn': ctx.changeZoom(10); return true;
    case 'zoomOut': ctx.changeZoom(-10); return true;
    case 'zoomReset': ctx.changeZoom(0); return true;
    case 'downloads': ctx.openDownloads(); return true;
    case 'palette': ctx.openPalette(); return true;
    case 'paletteclose': ctx.closePalette(); return true;
    case 'reader': ctx.requestReader(false); return true;
    case 'summary': ctx.requestReader(true); return true;
    case 'readerclose': ctx.closeReader(); return true;
    case 'split': ctx.toggleSplit(); return true;
    case 'splitfocus2': ctx.focusSplitSecondary(); return true;
    case 'splitclose': ctx.closeSplit(); return true;
    case 'splitratio': ctx.setSplitRatio(0.35); return true;
    case 'watchdialog': ctx.openWatchDialog(); return true;
    case 'containerdialog': ctx.openContainerPicker(); return true;
    case 'mini': ctx.openMiniWindow(); return true;
    case 'newgroup': ctx.createGroup(); return true;
    case 'reopentab': ctx.reopenClosedTab(); return true;
    case 'grouptab':
      if (ctx.groups[0]) ctx.assignGroup(ctx.activeId, ctx.groups[0].id);
      return true;
    case 'sitecard': ctx.toggleSiteCard(); return true;
    case 'copyreport': ctx.copyReport(); return true;
    case 'savereport': ctx.saveReport(); return true;
    case 'restartkernel': ctx.restartKernel(); return true;
    case 'savetimeline': ctx.saveTimeline(); return true;
    case 'setprovider':
      ctx.setSetting('dns.mode', 'provider');
      ctx.setSetting('dns.provider', 'quad9');
      return true;
    case 'setaccent':
      ctx.setSetting('ui.accent', '#e8590c');
      return true;
    case 'restartservice': ctx.notify('ui.restartKernel', {}); return true;
    case 'openfail': ctx.openUrl('https://this-host-does-not-exist.invalid/'); return true;
    default:
      return ctx.runTestCommand?.(name) ?? false;
  }
}