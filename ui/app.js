'use strict';
const $=id=>document.getElementById(id);
const english = {
 "Configura flussi": "Configure streams",
 "Connessione…": "Connecting…",
 "Sezioni": "Sections",
 "Flussi": "Streams",
 "Preferenze": "Settings",
 "In pausa": "Paused",
 "Mixer audio": "Audio mixer",
 "Il giusto spazio per ogni voce.": "Balance PC audio and voice chat.",
 "Centra": "Center",
 "Bilanciamento PC e Chat": "PC and chat balance",
 "Più PC": "More PC",
 "Centro": "Center",
 "Più Chat": "More Chat",
 "Regola con precisione": "Fine adjustment",
 "Regolazione precisa del ChatMix": "Fine ChatMix adjustment",
 "Sposta il ChatMix dell'uno per cento verso il PC": "Move ChatMix 1% toward PC",
 "Sposta il ChatMix dell'uno per cento verso il flusso Chat": "Move ChatMix 1% toward Chat",
 "ChatMix, volume, microfono e batterie anche sul display della base.": "The base display also shows ChatMix, volume, microphone and batteries.",
 "Schermata NovaMix sulla base disattivata. La rotella controlla il mixer.": "The NovaMix base display is off. The wheel still controls the mixer.",
 "Volumi": "Volume controls",
 "Volume cuffie": "Headset volume",
 "Il livello fisico non è ancora rilevato.": "Waiting for the base volume.",
 "Volume del mix": "Mix volume",
 "Regola il livello dei due flussi riprodotti da NovaMix.": "Adjust the combined level of the two streams.",
 "Microfono": "Microphone",
 "In attesa dello stato delle cuffie.": "Waiting for headset status.",
 "Non rilevato": "Unavailable",
 "Percorso audio": "Audio routing",
 "Scegli due uscite distinte per PC e Chat, poi indica le cuffie. Usa uscite virtuali o una presa digitale libera.": "Choose two separate outputs for PC and Chat, then choose your headset. Use virtual outputs or an unused digital output.",
 "Flusso PC": "PC stream",
 "Flusso Chat": "Chat stream",
 "Seleziona un’uscita": "Select an output",
 "Imposta questa uscita come predefinita in Windows dopo aver avviato i flussi.": "Set this as the Windows default output after starting the streams.",
 "Segnale PC": "PC signal",
 "Assegna questa uscita a tutte le app che vuoi nel flusso Chat, per esempio Discord, WhatsApp e Telegram. Usa le impostazioni audio dell’app o il Mixer volume di Windows.": "Assign this output to apps you want on the Chat stream, such as Discord, WhatsApp or Telegram. Use each app’s audio settings or Windows Volume mixer.",
 "Segnale Chat": "Chat signal",
 "Riproduci sulle cuffie": "Playback output",
 "Ricerca delle uscite di Windows…": "Looking for Windows audio outputs…",
 "Per tornare al percorso diretto, seleziona le cuffie in Windows e nelle app che avevi assegnato al flusso Chat.": "To return to direct playback, select the headset in Windows and in any apps you assigned to Chat.",
 "Fallo tuo.": "Settings",
 "Aspetto e controlli del mixer.": "Appearance and mixer controls.",
 "Aspetto": "Appearance",
 "Colore d’accento": "Accent color",
 "Un dettaglio, il tuo stile.": "Choose an accent for the mixer.",
 "Menta": "Mint",
 "Blu": "Blue",
 "Viola": "Purple",
 "Lingua / Language": "Language / Lingua",
 "Barra delle applicazioni": "System tray",
 "Personalizza l’indicatore delle cuffie vicino all’orologio.": "Customize the headset indicator next to the clock.",
 "Mostra la percentuale": "Show battery level as digits",
 "Visualizza la carica in cifre direttamente sulla barra. Passa il puntatore sull’icona per leggere la percentuale completa.": "See the charge as digits in the tray. Hover over the icon to see the full percentage.",
 "Colori della batteria": "Battery colors",
 "Bassa": "Low",
 "Media": "Medium",
 "Normale": "Normal",
 "Da normale a media": "Normal to medium",
 "Da media a bassa": "Medium to low",
 "La soglia bassa deve essere inferiore alla soglia media.": "The low threshold must be below the medium threshold.",
 "Avviso batteria": "Battery alert",
 "Ricevi una notifica di Windows quando la carica raggiunge o scende sotto la soglia.": "Get a Windows notification when charge reaches or falls below the threshold.",
 "Soglia di avviso": "Alert threshold",
 "Curva ChatMix più graduale": "Smoother ChatMix curve",
 "Passi più piccoli vicino allo zero e al centro. I pulsanti nel mixer regolano sempre l’1%.": "Smaller steps near zero and the center. The mixer buttons always adjust by 1%.",
 "Mostra overlay": "Show overlay",
 "Un riepilogo di ChatMix e volume vicino all’orologio.": "Show ChatMix and volume changes next to the clock.",
 "Usa il volume della tastiera": "Use keyboard volume controls",
 "La rotella e i tasti regolano il volume fisico delle cuffie.": "The keyboard wheel and volume keys adjust the headset’s physical volume.",
 "Schermata NovaMix sulla base": "NovaMix base display",
 "Mantiene visibili ChatMix, volume, microfono e batterie. Disattiva per tornare ai menu della base.": "Keeps ChatMix, volume, microphone and batteries visible. Turn it off to return to the base menus.",
 "Avviso microfono sulla base": "Microphone alert on the base",
 "Mostra un avviso quando la schermata NovaMix è disattivata.": "Show a mute alert when the NovaMix base display is off.",
 "I livelli delle app in Windows si applicano prima del bilanciamento dei due flussi.": "Windows app volumes apply before the two streams are mixed.",
 "Diagnostica": "Diagnostics",
 "Stato del mixer e ultimi eventi delle cuffie.": "Mixer status and recent headset events.",
 "Esporta report": "Export report",
 "Chiudi NovaMix": "Quit NovaMix",
 "Termina la riproduzione dei due flussi.": "Stop playback of both streams.",
 "Esci": "Quit",
 "0.20.8 · Due flussi": "0.20.8 · Dual stream",
 "· Anteprima": "· Preview",
 "Chiude la finestra; il mixer resta vicino all’orologio": "Close the window and keep the mixer running in the system tray",
 "Continua in background": "Keep running in background",
 "Riproduzione attiva.": "Playback is running.",
 "Apertura dei flussi…": "Starting streams…",
 "Uscite selezionate. Premi Avvia audio.": "Outputs selected. Select Start audio.",
 "Servono due uscite per i flussi e un’uscita per le cuffie.": "You need two stream outputs and a playback output.",
 "Seleziona tre uscite distinte.": "Select three different outputs.",
 "Ferma audio": "Stop audio",
 "Chiusura…": "Stopping…",
 "Annulla avvio": "Cancel startup",
 "Avvia audio": "Start audio",
 "USB collegata": "USB connected",
 "USB non disponibile": "USB unavailable",
 "Flussi attivi": "Streams active",
 "Batterie": "Batteries",
 "Cuffie": "Headset",
 "in carica": "charging",
 "* Valore richiesto: in attesa della conferma USB.": "* Waiting for the base to confirm the requested volume.",
 "Regola qui il volume della base; lettura in attesa.": "Adjust the base volume here. Waiting for a reading.",
 "Volume fisico confermato dalla base.": "The base has confirmed the physical volume.",
 "Muto": "Muted",
 "Attivo": "On",
 "Premi il tasto muto sulle cuffie per sincronizzare.": "Press the mute button on the headset to sync.",
 "Stato del tasto fisico sulle cuffie.": "Status of the physical mute button on the headset.",
 "Il mixer software è attivo, ma il controllo USB richiede una verifica. Puoi esportare la diagnostica dalle Preferenze.": "The mixer is running, but USB control needs attention. Export diagnostics from Settings.",
 "Muovi la rotella delle cuffie una volta: serve il volume iniziale per usare la tastiera.": "Move the headset wheel once so NovaMix can read the initial volume before using the keyboard controls.",
 "I tasti volume non sono stati acquisiti. Un altro programma potrebbe già gestirli.": "NovaMix could not register the volume keys. Another app may already be using them.",
 "Accesso al mixer non disponibile": "Mixer access unavailable",
 "Connessione a NovaMix interrotta. Riapri la finestra dall’icona vicino all’orologio.": "Lost the connection to NovaMix. Reopen the window from the tray icon.",
 "Diagnostica non disponibile": "Diagnostics unavailable",
 "Report salvato.": "Report saved.",
 "Apri il pannello da NovaMix per esportare il report.": "Open the NovaMix desktop window to export the report.",
 "Puoi chiudere con la X. NovaMix continua nell’area di notifica.": "Close the window to keep NovaMix running in the system tray.",
 "NovaMix è stato chiuso.": "NovaMix has quit.",
 "Puoi chiudere questa finestra.": "You can close this window.",
 "Ferma i flussi prima di cambiare le uscite.": "Stop the streams before changing outputs.",
 "Dispositivo non disponibile.": "Device unavailable.",
 "Valore fuori intervallo": "Value out of range",
 "Seleziona tre uscite distinte nella scheda Flussi.": "Select three different outputs in the Streams tab.",
 "Attendi la chiusura dei flussi.": "Wait for the streams to stop.",
 "Stato microfono non disponibile": "Microphone status unavailable",
 "Comando sconosciuto": "Unknown command",
 "Schermata NovaMix sulla base disattivata nelle Preferenze.": "The NovaMix base display is turned off in Settings.",
 "Invio del volume alla base non riuscito. Esporta la diagnostica.": "Could not send the volume to the base. Export diagnostics.",
 "Invio della schermata OLED non riuscito. Esporta la diagnostica.": "Could not update the base display. Export diagnostics.",
 "Impossibile salvare le preferenze in questa cartella.": "Could not save preferences in this folder.",
 "Impossibile salvare il report nella posizione scelta.": "Could not save the report in the selected location."
};

let language = 'it';
function tr(source) {
 return language === 'en' ? (english[source] ?? source) : source;
}
const staticText = [];
const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
for (let node; (node = walker.nextNode());) {
 const source = node.data;
 const original = source.trim();
 if (Object.hasOwn(english, original)) staticText.push(() => { node.data = source.replace(original, tr(original)); });
}
for (const element of document.querySelectorAll('[aria-label], [title]')) {
 for (const name of ['aria-label', 'title']) {
  const original = element.getAttribute(name);
  if (Object.hasOwn(english, original)) staticText.push(() => element.setAttribute(name, tr(original)));
 }
}
function applyLanguage(value) {
 const next = value ? 'en' : 'it';
 $('language').value = value ? '1' : '0';
 if (language === next) return;
 language = document.documentElement.lang = next;
 for (const translate of staticText) translate();
 deviceSignature = null;
}
const token=document.querySelector('meta[name=novamix-token]').content;
let state={},editing='',lastEdit=0,toastTimer,disconnected=false,embeddedReady=false,updating=false,writeRevision=0;
const accents=['mint','blue','violet'];
const sliders={};
function pending(id){const s=sliders[id];return !!s&&(s.busy||s.value!==null||s.timer!==null);}
function queueSlider(id,value){const s=sliders[id]??=( {value:null,busy:false,timer:null,last:0} );s.value=value;if(!s.timer&&!s.busy)s.timer=setTimeout(()=>flushSlider(id),Math.max(0,30-(Date.now()-s.last)));}
async function flushSlider(id){const s=sliders[id];if(!s)return;clearTimeout(s.timer);s.timer=null;if(s.busy||s.value===null)return;s.busy=true;const value=s.value;s.value=null;s.last=Date.now();await action(id,value);s.busy=false;if(s.value!==null)queueSlider(id,s.value);}
const headers={'X-NovaMix-Token':token};
const routeIds = ['route-pc', 'route-chat', 'route-out'];
const batteryColors = ['batteryLow', 'batteryMedium', 'batteryNormal'];
let batteryBandsDraft = false;
let deviceSignature = '', routeSaving = false, runSaving = false;

function renderRoutes(s) {
 const devices = s.devices || [];
 const signature = JSON.stringify(devices);
 if (signature !== deviceSignature) {
  deviceSignature = signature;
  for (const id of routeIds) {
   const option = (value, label) => Object.assign(document.createElement('option'),
                                                {value: String(value), textContent: label});
   $(id).replaceChildren(option(0, tr('Seleziona un’uscita')), ...devices.map(device => {
    const duplicate = devices.some(other => other.name === device.name && other.number !== device.number);
    return option(device.number, duplicate ? device.name + ' · ' + device.number : device.name);
   }));
  }
 }
 routeIds.forEach((id, index) => {
  if (!routeSaving) $(id).value = String(s.routes?.[index] ?? 0);
  $(id).disabled = Boolean(s.enabled || s.runningRequested || routeSaving || disconnected);
 });
 $('signal-pc').value = s.signal?.[0] ?? 0;
 $('signal-chat').value = s.signal?.[1] ?? 0;
 setText('route-status', s.enabled ? tr('Riproduzione attiva.') : s.runningRequested ? tr('Apertura dei flussi…')
   : s.routeReady ? tr('Uscite selezionate. Premi Avvia audio.')
   : devices.length < 3 ? tr('Servono due uscite per i flussi e un’uscita per le cuffie.')
   : tr('Seleziona tre uscite distinte.'));
}

for (const id of routeIds) {
 $(id).onchange = async () => {
  if (routeSaving) return;
  const value = Number($(id).value);
  routeSaving = true;
  renderRoutes(state);
  $('enabled').disabled = true;
  await action(id, value);
  routeSaving = false;
  await update();
 };
}
function toast(text){$('toast').textContent=text;$('toast').hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('toast').hidden=true,4500);}
async function action(name,value){
 ++writeRevision;
 try{const response=await fetch(`/api/action?name=${encodeURIComponent(name)}&value=${value}`,{method:'POST',headers});if(!response.ok)throw new Error(await response.text());return true;}
 catch(error){toast(tr(error.message));return false;}
 finally{++writeRevision;}
}
function setText(id,value){if($(id).textContent!==value)$(id).textContent=value;}
function percent(id,value){$(id).replaceChildren(document.createTextNode(String(value)),Object.assign(document.createElement('span'),{textContent:'%'}));}
function range(id,value,disabled){const el=$(id);el.disabled=disabled;if(editing!==id&&!pending(id)&&Date.now()-lastEdit>250)el.value=value;const n=(+el.value-(+el.min))/(+el.max-(+el.min))*100;el.style.setProperty('--fill',n+'%');}
function applyAccent(value){const accent=accents[value]||'mint';document.documentElement.dataset.accent=accent;for(const button of document.querySelectorAll('button[data-accent]'))button.setAttribute('aria-pressed',String(button.dataset.accent===accent));}
function setBalance(value){value=Math.max(-100,Math.min(100,value));$('balance').value=value;$('balance').style.setProperty('--fill',(value+100)/2+'%');percent('pc-value',value>0?100-value:100);percent('chat-value',value<0?100+value:100);lastEdit=Date.now();queueSlider('balance',value);}
function render(s){
 state=s;disconnected=false;
 applyLanguage(s.language);
 renderRoutes(s);
 for (const id of ['batteryPercent', 'batteryNotify'])
  if (!pending(id)) $(id).checked=Boolean(s[id]);
 for (const id of batteryColors) {
  if (!pending(id) && document.activeElement!==$(id) && Number.isInteger(s[id]))
   $(id).value='#'+s[id].toString(16).padStart(6,'0');
 }
 if (!pending('batteryThreshold') && document.activeElement!==$('batteryThreshold'))
  $('batteryThreshold').value=s.batteryThreshold ?? 25;
 const low=s.batteryLowLimit ?? 25, medium=s.batteryMediumLimit ?? 50;
 if (!batteryBandsDraft && !pending('batteryBands')) {
  if (document.activeElement!==$('batteryLowLimit')) $('batteryLowLimit').value=low;
  if (document.activeElement!==$('batteryMediumLimit')) $('batteryMediumLimit').value=medium;
 }
 setText('battery-low-range', low===0 ? '0%' : '0–'+low+'%');
 setText('battery-medium-range', medium===low+1 ? medium+'%' : (low+1)+'–'+medium+'%');
 setText('battery-normal-range', medium===99 ? '100%' : (medium+1)+'–100%');
 setText('base-mix-note',s.oledMix?tr('ChatMix, volume, microfono e batterie anche sul display della base.'):tr('Schermata NovaMix sulla base disattivata. La rotella controlla il mixer.'));
 if(!embeddedReady&&window.NovaMixDesktop){embeddedReady=true;window.NovaMixDesktop.ready();}
 if(!pending('accent'))applyAccent(s.accent);
 $('enabled').disabled=runSaving||routeSaving||Boolean(s.enabled&&!s.runningRequested);
 setText('enabled', s.enabled ? (s.runningRequested ? tr('Ferma audio') : tr('Chiusura…'))
   : s.runningRequested ? tr('Annulla avvio') : s.routeReady ? tr('Avvia audio') : tr('Configura flussi'));
 $('enabled').classList.toggle('on', !!s.enabled);
 $('dot').classList.toggle('on',!!s.hid);setText('connection',s.hid?tr('USB collegata'):tr('USB non disponibile'));
 $('status-pill').classList.toggle('on',!!s.enabled);setText('status-pill',s.enabled?tr('Flussi attivi'):tr('In pausa'));
 $('battery').hidden=false;setText('battery',`${tr('Batterie')} · ${tr('Cuffie')} ${s.battery<0?'—':s.battery+'%'}${s.charging===2?' · ' + tr('in carica'):''} · Base ${s.chargerBattery<0?'—':s.chargerBattery+'%'}`);$('preview-label').hidden=!s.preview;
 if(editing!=='balance'&&!pending('balance')&&Date.now()-lastEdit>250){percent('pc-value',s.game);percent('chat-value',s.chat);}
 range('balance',s.balance,!s.enabled);range('master',s.master,false);range('physical',Math.max(0,s.physicalDisplay),!s.enabled||!s.hid);
 for(const id of ['fine-pc','fine-chat','center'])$(id).disabled=!s.enabled;
 if(editing!=='master'&&!pending('master'))setText('master-value',s.master+'%');if(editing!=='physical'&&!pending('physical'))setText('physical-value',s.physicalDisplay<0?'—':s.physicalDisplay+'%'+(s.physicalPending?' *':''));
 setText('physical-help',s.physicalPending?tr('* Valore richiesto: in attesa della conferma USB.'):s.physical<0?tr('Regola qui il volume della base; lettura in attesa.'):tr('Volume fisico confermato dalla base.'));
 setText('mic-value',s.mic<0?tr('Non rilevato'):s.mic?tr('Muto'):tr('Attivo'));$('mic-value').classList.toggle('muted',s.mic===1);$('mic-value').classList.toggle('active',s.mic===0);
 setText('mic-help',s.mic<0?tr('Premi il tasto muto sulle cuffie per sincronizzare.'):tr('Stato del tasto fisico sulle cuffie.'));
 for(const id of ['overlay','keyboard','oled','oledMix','gentleMix'])$(id).checked=!!s[id];$('oled').disabled=s.mic<0;
 let message=[s.syncError, s.volumeSyncError].filter(Boolean).map(tr).join(' ');
 if(s.error)message=language==='en' ? 'Could not start or continue audio playback. Export diagnostics from Settings.' : s.error;
 else if(s.enabled&&s.hidError)message=tr('Il mixer software è attivo, ma il controllo USB richiede una verifica. Puoi esportare la diagnostica dalle Preferenze.');
 else if(s.keyboard&&!s.keyboardActive&&s.enabled)message=s.physical<0?tr('Muovi la rotella delle cuffie una volta: serve il volume iniziale per usare la tastiera.'):tr('I tasti volume non sono stati acquisiti. Un altro programma potrebbe già gestirli.');
 if (!message && s.notice === 'Impossibile salvare le preferenze in questa cartella.') message = s.notice;
 $('alert').hidden=!message;setText('alert',tr(message));
}
async function update(){if(updating)return;updating=true;const revision=writeRevision;try{const response=await fetch('/api/state',{headers});if(!response.ok)throw new Error(tr('Accesso al mixer non disponibile'));const next=await response.json();if(revision===writeRevision)render(next);}catch{if(!disconnected){disconnected=true;$('alert').textContent=tr('Connessione a NovaMix interrotta. Riapri la finestra dall’icona vicino all’orologio.');$('alert').hidden=false;for(const id of ['enabled','balance','master','physical','fine-pc','fine-chat','center'])$(id).disabled=true;for(const id of routeIds)$(id).disabled=true;}}finally{updating=false;}}
for(const button of document.querySelectorAll('.tab'))button.onclick=()=>{document.querySelectorAll('.tab').forEach(b=>{b.classList.toggle('selected',b===button);b.setAttribute('aria-current',b===button?'page':'false');});for(const id of ['mixer','streams','settings'])$(id).hidden=id!==button.dataset.panel;};
$('enabled').onclick=async()=>{
 if (!state.routeReady && !state.runningRequested) {
  for (const button of document.querySelectorAll('.tab')) {
   if (button.dataset.panel === 'streams') button.onclick();
  }
  return;
 }
 if (runSaving) return;
 runSaving = true;
 $('enabled').disabled = true;
 await action('enabled', state.runningRequested ? 0 : 1);
 runSaving = false;
 await update();
};
$('language').onchange = async event => {
 const select = event.target;
 select.disabled = true;
 await action('language', Number(select.value));
 select.disabled = false;
 await update();
};
$('center').onclick=()=>setBalance(0);
$('fine-pc').onclick=()=>setBalance(+$('balance').value-1);
$('fine-chat').onclick=()=>setBalance(+$('balance').value+1);
for(const button of document.querySelectorAll('button[data-accent]'))button.onclick=()=>{const value=accents.indexOf(button.dataset.accent);applyAccent(value);queueSlider('accent',value);};
for(const id of ['balance','master','physical']){
 $(id).oninput=e=>{editing=id;lastEdit=Date.now();const n=+e.target.value;if(id==='balance'){percent('pc-value',n>0?100-n:100);percent('chat-value',n<0?100+n:100);}else setText(id+'-value',n+'%');const percentValue=(n-(+e.target.min))/(+e.target.max-(+e.target.min))*100;e.target.style.setProperty('--fill',percentValue+'%');queueSlider(id,n);};
 $(id).onchange=()=>{flushSlider(id);editing='';lastEdit=Date.now();};$(id).onpointercancel=()=>{flushSlider(id);editing='';};
}
for(const id of ['overlay','keyboard','oled','oledMix','gentleMix'])$(id).onchange=async()=>{const checked=$(id).checked;$(id).disabled=true;await action(id,checked?1:0);$(id).disabled=id==='oled'&&state.mic<0;await update();};
for (const id of ['batteryPercent', 'batteryNotify'])
 $(id).onchange=()=>queueSlider(id,$(id).checked?1:0);
for (const id of batteryColors)
 $(id).oninput=()=>queueSlider(id,parseInt($(id).value.slice(1),16));
$('batteryThreshold').onchange=()=>{
 const input=$('batteryThreshold');
 if (input.reportValidity()) queueSlider(input.id,input.valueAsNumber);
};
for (const id of ['batteryLowLimit', 'batteryMediumLimit']) {
 $(id).oninput=()=>{batteryBandsDraft=true;};
 $(id).onchange=()=>{
  const low=$('batteryLowLimit'), medium=$('batteryMediumLimit');
  if (!$(id).reportValidity()) return;
  if (!low.validity.valid || !medium.validity.valid) return;
  const ordered=low.valueAsNumber<medium.valueAsNumber;
  $('battery-band-error').hidden=ordered;
  if (!ordered) return;
  batteryBandsDraft=false;
  queueSlider('batteryBands',low.valueAsNumber*100+medium.valueAsNumber);
 };
}
$('diagnostics').onclick=async()=>{try{const response=await fetch('/api/diagnostics',{headers});if(!response.ok)throw new Error(tr('Diagnostica non disponibile'));const text=await response.text();if(window.NovaMixDesktop){const result=await window.NovaMixDesktop.saveDiagnostics(text, language);if(result.saved)toast(tr('Report salvato.'));}else toast(tr('Apri il pannello da NovaMix per esportare il report.'));}catch(error){toast(tr(error.message));}};
$('hide').onclick=()=>{if(window.NovaMixDesktop)window.NovaMixDesktop.hide();else{window.close();toast(tr('Puoi chiudere con la X. NovaMix continua nell’area di notifica.'));}};
$('quit').onclick=async()=>{if(await action('quit',1)){clearInterval(poll);document.querySelector('main').replaceChildren(Object.assign(document.createElement('h1'),{textContent:tr('NovaMix è stato chiuso.')}),Object.assign(document.createElement('p'),{textContent:tr('Puoi chiudere questa finestra.')}));}};
update();const poll=setInterval(update,500);
