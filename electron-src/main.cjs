'use strict';

const path = require('node:path');
const fs = require('node:fs/promises');
const MAX_REPORT_BYTES = 2 * 1024 * 1024;

function parseLaunch(args) {
  const options = {};
  for (let i = 0; i < args.length; i++) {
    const name = args[i];
    if (name === '--preview' && !options.preview) options.preview = true;
    else if ((name === '--url' || name === '--parent') && !(name in options) && i + 1 < args.length) options[name] = args[++i];
    else throw new Error('Parametri di avvio non validi. Avvia NovaMix.exe.');
  }
  const match = /^http:\/\/127\.0\.0\.1:([1-9][0-9]{0,4})\/ui\?token=([a-fA-F0-9]{64})$/.exec(options['--url'] || '');
  if (!match || match[0] !== options['--url'] || Number(match[1]) > 65535) throw new Error('Indirizzo del motore NovaMix non valido.');
  const parent = options['--parent'] || '';
  if (!/^[1-9][0-9]{0,9}$/.test(parent) || parent.trim() !== parent || Number(parent) > 0xffffffff) throw new Error('Processo del motore NovaMix non valido.');
  const url = new URL(options['--url']);
  return Object.freeze({ url: url.href, origin: url.origin, parent: Number(parent), preview: !!options.preview });
}

function mayRequest(url, resourceType, launch) {
  try {
    const parsed = new URL(url);
    if (parsed.origin !== launch.origin || parsed.protocol !== 'http:' || parsed.username || parsed.password || parsed.hash) return false;
    if (resourceType === 'mainFrame') return parsed.href === launch.url;
    if (resourceType === 'subFrame' || resourceType === 'webSocket') return false;
    return true;
  } catch { return false; }
}

function validReport(text) {
  return typeof text === 'string' && text.length > 0 && Buffer.byteLength(text, 'utf8') <= MAX_REPORT_BYTES && !text.includes('\0');
}

function parentAlive(pid) {
  try { process.kill(pid, 0); return true; }
  catch (error) { return error.code === 'EPERM'; }
}

function trustedSender(event, window, url) {
  return !!window && !window.isDestroyed() && event.sender === window.webContents &&
    event.senderFrame === window.webContents.mainFrame && event.senderFrame?.url === url &&
    window.webContents.getURL() === url;
}

function startupErrorText(error) {
  const text = [error?.name || 'Error', error?.code || '', error?.message || 'Avvio interfaccia fallito.'].join(' ');
  return text.replace(/https?:\/\/\S+/gi, '[URL rimossa]').replace(/\b[0-9a-f]{64}\b/gi, '[token rimosso]').slice(0, 8192);
}

function logStartupFailure(error) {
  try {
    const file = path.resolve(path.dirname(process.execPath), '..', 'ui-startup-error.txt');
    require('node:fs').writeFileSync(file, `${new Date().toISOString()}\n${startupErrorText(error)}\n`, 'utf8');
  } catch { /* Startup diagnostics must never prevent shutdown. */ }
}

function selfTest() {
  const assert = require('node:assert/strict');
  const token = 'a'.repeat(64), address = `http://127.0.0.1:45678/ui?token=${token}`;
  const launch = parseLaunch(['--parent', '1234', '--preview', '--url', address]);
  assert.equal(launch.url, address); assert.equal(launch.parent, 1234); assert.equal(launch.preview, true);
  for (const url of [address + '#fragment', address + '&other=1', address.replace('127.0.0.1', 'localhost'), address.replace('127.0.0.1', 'evil.test'), address.replace('http:', 'https:'), address.replace(':45678', ':65536'), address.replace('127.0.0.1', 'user@127.0.0.1'), address.replace('/ui?', '/other?'), address.slice(0, -1), address + '\n']) {
    assert.throws(() => parseLaunch(['--url', url, '--parent', '1234']), undefined, url);
  }
  for (const args of [['--url', address], ['--url', address, '--parent', '0'], ['--url', address, '--parent', '-1'], ['--url', address, '--parent', '4294967296'], ['--url', address, '--parent', '1234', '--parent', '4567'], ['--url', address, '--parent', '1234', '--remote-debugging-port', '9222']]) assert.throws(() => parseLaunch(args));
  assert.equal(mayRequest(address, 'mainFrame', launch), true);
  assert.equal(mayRequest(`${launch.origin}/app.js`, 'script', launch), true);
  assert.equal(mayRequest(`${launch.origin}/api/state`, 'xhr', launch), true);
  for (const [url, kind] of [[`${launch.origin}/api/state`, 'mainFrame'], [address, 'subFrame'], ['https://example.com/', 'script'], ['file:///C:/Windows/win.ini', 'other'], [address.replace(':45678', ':45679'), 'xhr'], [address + '#fragment', 'mainFrame'], [address.replace('http:', 'ws:'), 'webSocket'], [address.replace('127.0.0.1', 'user@127.0.0.1'), 'xhr']]) assert.equal(mayRequest(url, kind, launch), false);
  assert.equal(validReport('NovaMix 0.20\nAudio: 1'), true);
  for (const invalid of [null, 7, {}, '', 'bad\0text', 'x'.repeat(MAX_REPORT_BYTES + 1), 'è'.repeat(MAX_REPORT_BYTES)]) assert.equal(validReport(invalid), false);
  const frame = { url: address }, contents = { mainFrame: frame, getURL: () => address };
  const window = { isDestroyed: () => false, webContents: contents };
  assert.equal(trustedSender({ sender: contents, senderFrame: frame }, window, address), true);
  assert.equal(trustedSender({ sender: contents, senderFrame: { url: address } }, window, address), false);
  assert.equal(trustedSender({ sender: {}, senderFrame: frame }, window, address), false);
  assert.equal(trustedSender({ sender: contents, senderFrame: frame }, window, address + '#bad'), false);
  assert.equal(parentAlive(process.pid), true);
  const sanitized = startupErrorText({ name: 'Error', code: 'ERR_FAILED', message: `Cannot load ${address}; token ${token}` });
  assert.equal(sanitized.includes(token), false); assert.equal(sanitized.includes('http://'), false);
  assert.equal(sanitized.includes('ERR_FAILED'), true);
  console.log('NovaMix Electron: launcher, origin/network, report limits and IPC sender checks passed.');
}

async function startElectron() {
  const { app, BrowserWindow, Menu, nativeTheme, ipcMain, dialog, session } = require('electron');
  app.setName('NovaMix');
  app.setAppUserModelId('NovaMix.Desktop');
  app.enableSandbox();
  let launch;
  try { launch = parseLaunch(process.argv.slice(process.defaultApp ? 2 : 1)); }
  catch (error) { logStartupFailure(error); dialog.showErrorBox('NovaMix', error.message); app.exit(1); return; }
  if (launch.parent === process.pid || !parentAlive(launch.parent)) {
    logStartupFailure({ code: 'PARENT_NOT_AVAILABLE', message: 'Il processo del motore NovaMix non risulta disponibile.' });
    app.exit(1); return;
  }
  const packageRoot = path.resolve(path.dirname(process.execPath), '..');
  const profile = path.join(packageRoot, launch.preview ? 'UiData-preview' : 'UiData');
  require('node:fs').mkdirSync(profile, { recursive: true });
  app.setPath('userData', profile);
  app.setPath('sessionData', profile);

  let window, parentTimer, savingReport = false;
  app.on('window-all-closed', () => app.quit());
  app.on('before-quit', () => clearInterval(parentTimer));
  await app.whenReady();
  nativeTheme.themeSource = 'dark';
  Menu.setApplicationMenu(null);
  const localSession = session.fromPartition('persist:novamix', { cache: false });
  localSession.setPermissionRequestHandler((_contents, _permission, callback) => callback(false));
  localSession.setPermissionCheckHandler(() => false);
  localSession.setDevicePermissionHandler(() => false);
  localSession.on('will-download', event => event.preventDefault());
  localSession.webRequest.onBeforeRequest((details, callback) => callback({ cancel: !mayRequest(details.url, details.resourceType, launch) }));

  const icon = path.join(packageRoot, 'assets', 'NovaMix.ico');
  window = new BrowserWindow({
    width: 820, height: 900, minWidth: 680, minHeight: 740,
    title: launch.preview ? 'NovaMix — Anteprima' : 'NovaMix',
    icon, backgroundColor: '#111315', show: false, autoHideMenuBar: true,
    webPreferences: {
      session: localSession, preload: path.join(__dirname, 'preload.cjs'),
      contextIsolation: true, sandbox: true, nodeIntegration: false,
      nodeIntegrationInWorker: false, nodeIntegrationInSubFrames: false,
      webSecurity: true, allowRunningInsecureContent: false,
      webviewTag: false, spellcheck: false, devTools: false
    }
  });
  window.setMenuBarVisibility(false);
  const contents = window.webContents;
  contents.setWindowOpenHandler(() => ({ action: 'deny' }));
  contents.on('will-attach-webview', event => event.preventDefault());
  contents.on('will-navigate', (event, url) => { if ((event.url || url) !== launch.url) event.preventDefault(); });
  contents.on('will-frame-navigate', event => { if (!event.isMainFrame || event.url !== launch.url) event.preventDefault(); });
  contents.on('will-redirect', (event, url) => { if ((event.url || url) !== launch.url) event.preventDefault(); });
  contents.on('did-navigate-in-page', (_event, url, isMainFrame) => { if (isMainFrame && url !== launch.url) app.quit(); });
  contents.on('page-title-updated', event => event.preventDefault());
  contents.on('render-process-gone', (_event, details) => {
    logStartupFailure({ code: 'RENDERER_EXIT', message: `Motore grafico terminato: ${details.reason}, codice ${details.exitCode}.` });
    app.quit();
  });
  contents.on('will-prevent-unload', event => event.preventDefault());
  window.once('ready-to-show', () => { if (!window.isDestroyed()) window.show(); });
  window.on('closed', () => { window = null; });

  ipcMain.on('novamix:hide', event => { if (trustedSender(event, window, launch.url)) app.quit(); });
  ipcMain.on('novamix:ready', event => { if (trustedSender(event, window, launch.url) && !window.isVisible()) window.show(); });
  ipcMain.handle('novamix:save-diagnostics', async (event, text, language) => {
    if (!trustedSender(event, window, launch.url) || !validReport(text)) throw new Error('Report diagnostico non valido.');
    const english = language === 'en';
    if (savingReport) return { saved: false };
    savingReport = true;
    try {
      const result = await dialog.showSaveDialog(window, {
        title: english ? 'Save NovaMix diagnostics' : 'Salva diagnostica NovaMix',
        buttonLabel: english ? 'Save report' : 'Salva report',
        defaultPath: english ? 'novamix-diagnostics.txt' : 'diagnostica-novamix.txt',
        filters: [{ name: english ? 'Text report' : 'Report di testo', extensions: ['txt'] }],
        properties: ['showOverwriteConfirmation']
      });
      if (result.canceled || !result.filePath) return { saved: false };
      // The path comes exclusively from the native save dialog, never from IPC.
      await fs.writeFile(result.filePath, text, { encoding: 'utf8' });
      return { saved: true };
    } catch { throw new Error('Impossibile salvare il report nella posizione scelta.'); }
    finally { savingReport = false; }
  });

  parentTimer = setInterval(() => { if (!parentAlive(launch.parent)) app.quit(); }, 1500);
  parentTimer.unref();
  try { await window.loadURL(launch.url); }
  catch (error) {
    logStartupFailure(error);
    if (parentAlive(launch.parent)) dialog.showErrorBox('NovaMix', 'Impossibile aprire il pannello. Riaprilo dall’icona NovaMix vicino all’orologio.');
    app.quit();
  }
}

if (require.main === module) {
  if (!process.versions.electron && process.argv.includes('--shell-self-test')) selfTest();
  else if (process.versions.electron) startElectron().catch(error => { logStartupFailure(error); require('electron').app.exit(1); });
  else throw new Error('Avvia NovaMix.exe per aprire il pannello.');
}
module.exports = { parseLaunch, mayRequest, validReport, trustedSender };
