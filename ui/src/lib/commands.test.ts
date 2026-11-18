import { describe, expect, it, vi } from 'vitest';
import { runCommand, type CommandContext } from './commands';

function makeContext(overrides: Partial<CommandContext> = {}): CommandContext {
  const base: CommandContext = {
    activeId: 7,
    activeUrl: 'https://example.com/',
    groups: [{ id: 'g1' }],
    splitSecondary: 9,
    siteCardOpen: false,
    addressEl: null,
    newTab: vi.fn(),
    closeTab: vi.fn(),
    navigate: vi.fn(),
    openSettings: vi.fn(),
    openFind: vi.fn(),
    runFind: vi.fn(),
    closeFind: vi.fn(),
    openFindWith: vi.fn(),
    goBack: vi.fn(),
    goForward: vi.fn(),
    changeZoom: vi.fn(),
    openDownloads: vi.fn(),
    openPalette: vi.fn(),
    closePalette: vi.fn(),
    requestReader: vi.fn(),
    closeReader: vi.fn(),
    toggleSplit: vi.fn(),
    closeSplit: vi.fn(),
    setSplitRatio: vi.fn(),
    focusSplitSecondary: vi.fn(),
    openWatchDialog: vi.fn(),
    openContainerPicker: vi.fn(),
    openMiniWindow: vi.fn(),
    createGroup: vi.fn(),
    assignGroup: vi.fn(),
    reopenClosedTab: vi.fn(),
    toggleSiteCard: vi.fn(),
    copyReport: vi.fn(),
    saveReport: vi.fn(),
    restartKernel: vi.fn(),
    saveTimeline: vi.fn(),
    openUrl: vi.fn(),
    setFullscreen: vi.fn(),
    notify: vi.fn(),
    setSetting: vi.fn(),
  };
  return { ...base, ...overrides };
}

describe('runCommand', () => {
  it('routes prefix commands with their payload', () => {
    const ctx = makeContext();
    expect(runCommand('settings:dns', ctx)).toBe(true);
    expect(ctx.openSettings).toHaveBeenCalledWith('dns');
    expect(runCommand('open:https://x.com/', ctx)).toBe(true);
    expect(ctx.navigate).toHaveBeenCalledWith('https://x.com/');
    expect(runCommand('find:needle', ctx)).toBe(true);
    expect(ctx.openFindWith).toHaveBeenCalledWith('needle');
  });

  it('acts on the active tab', () => {
    const ctx = makeContext();
    runCommand('closeTab', ctx);
    expect(ctx.closeTab).toHaveBeenCalledWith(7);
    runCommand('reload', ctx);
    expect(ctx.navigate).toHaveBeenCalledWith('https://example.com/');
    runCommand('devTools', ctx);
    expect(ctx.notify).toHaveBeenCalledWith('ui.devTools', { tab: 7 });
  });

  it('maps zoom commands to deltas', () => {
    const ctx = makeContext();
    runCommand('zoomIn', ctx);
    runCommand('zoomOut', ctx);
    runCommand('zoomReset', ctx);
    expect(ctx.changeZoom).toHaveBeenNthCalledWith(1, 10);
    expect(ctx.changeZoom).toHaveBeenNthCalledWith(2, -10);
    expect(ctx.changeZoom).toHaveBeenNthCalledWith(3, 0);
  });

  it('distinguishes reader from summary', () => {
    const ctx = makeContext();
    runCommand('reader', ctx);
    runCommand('summary', ctx);
    expect(ctx.requestReader).toHaveBeenNthCalledWith(1, false);
    expect(ctx.requestReader).toHaveBeenNthCalledWith(2, true);
  });

  it('assigns the first group only when one exists', () => {
    const ctx = makeContext();
    runCommand('grouptab', ctx);
    expect(ctx.assignGroup).toHaveBeenCalledWith(7, 'g1');

    const empty = makeContext({ groups: [] });
    runCommand('grouptab', empty);
    expect(empty.assignGroup).not.toHaveBeenCalled();
  });

  it('writes both provider settings for setprovider', () => {
    const ctx = makeContext();
    runCommand('setprovider', ctx);
    expect(ctx.setSetting).toHaveBeenCalledWith('dns.mode', 'provider');
    expect(ctx.setSetting).toHaveBeenCalledWith('dns.provider', 'quad9');
  });

  it('opens the failure probe URL for openfail', () => {
    const ctx = makeContext();
    runCommand('openfail', ctx);
    expect(ctx.openUrl).toHaveBeenCalledWith('https://this-host-does-not-exist.invalid/');
  });

  it('falls through to the test harness for unknown names', () => {
    const harness = vi.fn().mockReturnValue(true);
    const ctx = makeContext({ runTestCommand: harness });
    expect(runCommand('pagedump', ctx)).toBe(true);
    expect(harness).toHaveBeenCalledWith('pagedump');
  });

  it('reports unknown names as unhandled without a harness', () => {
    const ctx = makeContext();
    expect(runCommand('pagedump', ctx)).toBe(false);
    expect(runCommand('totally-unknown', ctx)).toBe(false);
  });

  it('does not swallow known commands into the harness', () => {
    const harness = vi.fn();
    const ctx = makeContext({ runTestCommand: harness });
    runCommand('newTab', ctx);
    expect(ctx.newTab).toHaveBeenCalled();
    expect(harness).not.toHaveBeenCalled();
  });
});