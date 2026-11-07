'use strict';

const { contextBridge, ipcRenderer } = require('electron');

// No generic IPC, Node, file-system or process access is exposed to the page.
contextBridge.exposeInMainWorld('NovaMixDesktop', Object.freeze({
  hide: () => ipcRenderer.send('novamix:hide'),
  ready: () => ipcRenderer.send('novamix:ready'),
  saveDiagnostics: (text, language) => ipcRenderer.invoke('novamix:save-diagnostics', text, language)
}));
