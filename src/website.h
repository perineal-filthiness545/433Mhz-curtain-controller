const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>433MHz Analyzer</title>
  <style>
    body  { font-family:monospace; background:#111; color:#0f0; padding:16px; max-width:1100px; }
    h2,h3 { margin:0 0 8px; }
    hr    { border-color:#333; margin:14px 0; }
    .row  { display:flex; gap:8px; margin:4px 0; align-items:center; flex-wrap:wrap; }
    label { color:#888; font-size:0.85em; }
    input[type=text], input[type=number], select {
      background:#222; border:1px solid #444; color:#0f0;
      font-family:monospace; padding:3px 6px; border-radius:2px; }

    button { background:#0a0; border:none; color:#fff; padding:5px 14px;
             cursor:pointer; font-family:monospace; font-size:1em; border-radius:2px; }
    button:hover   { background:#0d0; }
    button.red     { background:#600; } button.red:hover   { background:#900; }
    button.stop    { background:#660; } button.stop:hover  { background:#990; }
    button.blue    { background:#036; } button.blue:hover  { background:#058; }
    button:disabled{ background:#333; color:#555; cursor:default; }

    /* status bar */
    #capBar { display:flex; align-items:center; gap:10px; margin:8px 0; flex-wrap:wrap; }
    #dot { display:inline-block; width:10px; height:10px; border-radius:50%;
           background:#f00; opacity:0; }
    #dot.blink { animation:blink 0.7s infinite; }
    @keyframes blink { 0%,100%{opacity:1} 50%{opacity:0} }
    #rssiMeter { display:inline-block; height:10px; width:80px;
                 background:#222; border:1px solid #333; vertical-align:middle;
                 position:relative; overflow:hidden; }
    #rssiBar   { position:absolute; left:0; top:0; height:100%;
                 background:#0a0; transition:width 0.3s; }

    /* wizard */
    details#wizard { background:#0a0a0a; border:1px solid #333; padding:10px 14px;
                     border-radius:3px; margin:10px 0; }
    details#wizard > summary { cursor:pointer; color:#0af; font-weight:bold; user-select:none; }
    .btn-chip { display:inline-flex; align-items:center; gap:4px; background:#003;
                border:1px solid #036; padding:2px 8px; border-radius:12px;
                margin:2px; font-size:0.88em; }
    .btn-chip .rm { cursor:pointer; color:#888; } .btn-chip .rm:hover { color:#f44; }

    /* wizard overlay */
    #wizOverlay { display:none; position:fixed; top:0; left:0; width:100%; height:100%;
                  background:rgba(0,0,0,0.92); z-index:100;
                  align-items:center; justify-content:center; }
    #wizOverlay.show { display:flex; }
    #wizBox { background:#111; border:2px solid #0af; border-radius:8px;
              padding:28px 44px; text-align:center; min-width:360px; max-width:540px; width:90%; }
    #wizTitle  { color:#555; font-size:0.82em; text-transform:uppercase;
                 letter-spacing:2px; margin-bottom:10px; }
    #wizStepBar { font-size:0.8em; margin-bottom:14px; min-height:1.2em; }
    .wiz-step-done { color:#333; }
    .wiz-step-cur  { color:#0af; font-weight:bold; border-bottom:1px solid #0af; }
    .wiz-step-todo { color:#222; }
    #wizBtnName { font-size:2.4em; color:#0f0; font-weight:bold;
                  margin:8px 0; letter-spacing:2px; }
    #wizSubtext { font-size:1.05em; margin:6px 0 10px; min-height:1.4em; }
    #wizRound   { color:#555; font-size:0.82em; margin:4px 0 10px; min-height:1.1em; }

    .pulse-wrap { height:56px; display:flex; align-items:center; justify-content:center;
                  margin:4px 0; }
    .pulse-ring { width:48px; height:48px; border-radius:50%; border:3px solid #0f0;
                  animation:pulse-anim 1.1s ease-out infinite; }
    @keyframes pulse-anim {
      0%   { transform:scale(0.75); opacity:1; }
      100% { transform:scale(1.5);  opacity:0; }
    }

    #wizCapList { text-align:left; margin:8px 0; }
    .wiz-cap-row { display:flex; align-items:center; gap:8px; padding:3px 0;
                   border-bottom:1px solid #1a1a1a; font-size:0.78em; }
    .wiz-cap-num { color:#555; min-width:32px; }
    .wiz-cap-seq { flex:1; overflow:hidden; white-space:nowrap; letter-spacing:0.4px; }
    .wiz-cap-meta{ color:#555; white-space:nowrap; }
    .wiz-match-bar { height:4px; border-radius:2px; margin:8px 0; background:#1a1a1a; }
    .wiz-match-fill { height:100%; border-radius:2px; transition:width 0.5s; }

    #wizResult  { margin:10px 0; font-size:1.05em; font-weight:bold; min-height:1.4em; }
    .match-good { color:#0f0; }
    .match-warn { color:#fa0; }
    .match-bad  { color:#f44; }
    #wizBtns    { display:flex; gap:8px; justify-content:center; flex-wrap:wrap;
                  margin-top:16px; }

    /* filter bar */
    #filterBar { display:flex; gap:14px; align-items:center; margin:8px 0;
                 font-size:0.88em; color:#888; flex-wrap:wrap; }

    /* packets */
    .pkt { background:#0a0a0a; border:1px solid #333; padding:8px 10px;
           margin:6px 0; border-radius:3px; }
    .pkt-header { display:flex; gap:8px; align-items:baseline; margin-bottom:4px;
                  cursor:pointer; flex-wrap:wrap; }
    .pkt-idx  { color:#555; min-width:28px; }
    .pkt-n    { color:#555; font-size:0.85em; }
    .pkt-lbl  { padding:1px 8px; border-radius:10px; background:#036;
                color:#0af; font-size:0.82em; font-weight:bold; }
    .pkt-info { font-size:0.88em; }
    .proto-ok { color:#0af; font-weight:bold; }
    .proto-q  { color:#fa0; }

    .bars { overflow-x:auto; white-space:nowrap; background:#050505;
            border:1px solid #222; padding:4px; min-height:32px; }
    .pb { display:inline-block; height:24px; vertical-align:middle; }
    .ps { background:#003300; }
    .pl { background:#000044; }
    .pg { background:#332200; }

    .seq { font-size:0.78em; letter-spacing:0.5px; overflow-x:auto; white-space:nowrap;
           background:#050505; border:1px solid #1a1a1a; padding:3px 6px;
           border-radius:2px; margin:4px 0; }
    .sc { color:#0a0; }
    .lc { color:#46f; }
    .gc { color:#a60; }

    .ptbl { font-size:0.82em; color:#888; border-collapse:collapse; margin:4px 0; }
    .ptbl td { padding:1px 12px 1px 0; }
    .ptbl .v { color:#0f0; }

    .bmc-row { font-size:0.82em; margin:4px 0; display:flex; gap:8px; align-items:baseline;
               flex-wrap:wrap; }
    .bmc-hex { color:#0f0; letter-spacing:1.5px; font-weight:bold; }
    .bmc-meta{ color:#444; }

    #bmcComp { background:#0a0a0a; border:1px solid #333; padding:8px 12px;
               margin:6px 0; border-radius:3px; }
    .cmp-tbl { font-size:0.8em; font-family:monospace; border-collapse:collapse; }
    .cmp-tbl td { padding:2px 7px; text-align:center; }
    .cmp-tbl .hdr { color:#444; }
    .cmp-tbl .lbl { color:#0af; text-align:left; white-space:nowrap; padding-right:12px; }
    .cmp-same { color:#444; }
    .cmp-diff { color:#ff0; font-weight:bold; }
    .cmp-ok   { color:#0a0; }
    .cmp-chk  { color:#fa0; }

    .raw { color:#555; font-size:0.75em; margin-top:4px;
           word-break:break-all; display:none; }
    .raw.open { display:block; }

    /* TX replay */
    #txSection { margin-top:4px; }
    #replayLine { color:#888; font-size:0.82em; min-height:1.1em; margin-top:4px; }

    #statusLine { color:#888; font-size:0.82em; min-height:1.2em; margin-top:4px; }
  </style>
</head>
<body>
  <h2>433MHz Analyzer</h2>

  <!-- Capture status bar -->
  <div id="capBar">
    <span id="dot"></span>
    <span id="capStatus" style="color:#555;font-size:0.88em"></span>
    <span id="rssiMeter"><span id="rssiBar" style="width:0%"></span></span>
    <span id="rssiVal" style="color:#555;font-size:0.9em">RSSI —</span>
    <span id="edgesVal" style="color:#555;font-size:0.82em"></span>
  </div>

  <!-- Button capture wizard -->
  <details id="wizard">
    <summary>&#9654; Button Capture Wizard</summary>
    <div style="margin-top:10px">
      <div class="row" style="margin-bottom:8px">
        <input id="newBtnName" type="text" placeholder="Button name (e.g. OPEN)" style="width:180px"
               onkeydown="if(event.key==='Enter')addBtn()">
        <button onclick="addBtn()">+ Add</button>
        <span style="color:#555;font-size:0.85em;margin-left:6px">Duration per capture:
          <select id="capDur">
            <option value="3000">3s</option>
            <option value="5000" selected>5s</option>
            <option value="10000">10s</option>
          </select>
        </span>
      </div>
      <div id="chipList" style="margin-bottom:10px">
        <span style="color:#555;font-size:0.85em">No buttons yet — type a name and click + Add</span>
      </div>
      <div id="capBtnRow" class="row" style="margin-bottom:8px"></div>
      <button id="btnWiz" class="blue" onclick="startWizard()" disabled>
        &#9654; Start Capture Wizard
      </button>
      <span style="color:#555;font-size:0.82em;margin-left:8px">
        Waits for each press, captures 3&#215;, checks consistency
      </span>
    </div>
  </details>

  <!-- Filter bar + download -->
  <div id="filterBar">
    <label><input type="checkbox" id="chkNoise" onchange="applyFilter()" checked>
      Hide noise (n&lt;50)</label>
    <span id="filterInfo" style="color:#444"></span>
    <button class="blue" style="font-size:0.82em;padding:3px 10px;margin-left:auto"
            onclick="downloadCaptures()">&#8659; Download JSON</button>
  </div>

  <!-- Quick single-capture buttons (populated when button names are added) -->
  <div id="quickCapRow" class="row" style="margin:8px 0"></div>

  <div id="bmcComp"></div>
  <div id="pkts"></div>

  <!-- TX Replay — always visible -->
  <div id="txSection">
    <hr>
    <h3>TX Replay <span style="color:#444;font-size:0.75em;font-weight:normal">via CC1101 @ 433.92 MHz</span></h3>
    <div id="txBtns" class="row">
      <span style="color:#555;font-size:0.85em">No labeled captures yet — add a button name above, capture it, then replay</span>
    </div>
    <div id="replayLine"></div>
    <div style="margin-top:8px;display:flex;gap:8px;align-items:center;flex-wrap:wrap">
      <label style="color:#555;font-size:0.82em">Noise filter (min pulses):
        <input type="number" id="filterN" value="50" min="0" max="500"
               style="width:60px;margin-left:4px"
               onchange="setFilter(this.value)">
      </label>
      <span style="color:#444;font-size:0.78em">0 = accept all ≥10 pulses</span>
    </div>
  </div>

  <!-- Wizard overlay -->
  <div id="wizOverlay">
    <div id="wizBox">
      <div id="wizTitle">Button Capture Wizard</div>
      <div id="wizStepBar"></div>
      <div id="wizBtnName"></div>
      <div id="wizSubtext" style="color:#0f0">Press the button now</div>
      <div id="wizWaitWrap" class="pulse-wrap">
        <div class="pulse-ring"></div>
      </div>
      <div id="wizRound"></div>
      <div id="wizCapList"></div>
      <div class="wiz-match-bar" id="wizMatchBar" style="display:none">
        <div class="wiz-match-fill" id="wizMatchFill" style="width:0%"></div>
      </div>
      <div id="wizResult"></div>
      <div id="wizBtns">
        <button class="red" onclick="wizCancel()">Cancel</button>
      </div>
    </div>
  </div>

  <div id="statusLine"></div>

  <!-- Home Assistant / MQTT -->
  <details id="mqttSection" style="background:#0a0a0a;border:1px solid #333;padding:10px 14px;border-radius:3px;margin:10px 0">
    <summary style="cursor:pointer;color:#0af;font-weight:bold;user-select:none">&#9654; Home Assistant / MQTT</summary>
    <div style="margin-top:10px">
      <div class="row">
        <label style="color:#888;font-size:0.85em">Device name:
          <input id="mqttName" type="text" placeholder="Garage Door" style="width:160px;margin-left:4px">
        </label>
        <span style="color:#444;font-size:0.78em">(shown in Home Assistant)</span>
      </div>
      <div class="row" style="margin-top:6px">
        <label style="color:#888;font-size:0.85em">Broker:
          <input id="mqttHost" type="text" placeholder="192.168.x.x" style="width:160px;margin-left:4px">
        </label>
        <label style="color:#888;font-size:0.85em">Port:
          <input id="mqttPort" type="number" value="1883" style="width:70px;margin-left:4px">
        </label>
      </div>
      <div class="row" style="margin-top:6px">
        <label style="color:#888;font-size:0.85em">User:
          <input id="mqttUser" type="text" style="width:120px;margin-left:4px">
        </label>
        <label style="color:#888;font-size:0.85em">Pass:
          <input id="mqttPass" type="password" style="width:120px;margin-left:4px">
        </label>
      </div>
      <div class="row" style="margin-top:8px">
        <button onclick="saveMQTT()">Save &amp; Connect</button>
        <button class="blue" style="font-size:0.85em;padding:4px 10px" onclick="rediscover()">Republish Discovery</button>
        <span id="mqttStatus" style="color:#555;font-size:0.85em;margin-left:6px">Loading…</span>
      </div>
      <div id="mqttInfo" style="color:#444;font-size:0.78em;margin-top:8px"></div>
      <div style="color:#444;font-size:0.78em;margin-top:6px">
        HA auto-discovers a <b style="color:#555">Garage Door cover</b> entity (requires saved OPEN + CLOSE labels)
        and individual <b style="color:#555">Button</b> entities for every saved signal.
        Needs Mosquitto (or other MQTT broker) accessible from this device.
      </div>
    </div>
  </details>

<script>
  // ── State ─────────────────────────────────────────────────────────
  let scanning  = false;
  let pollTimer = null;
  let prevJson  = '';
  let btnNames  = [];
  let allPkts   = [];

  function setStatus(msg) { document.getElementById('statusLine').textContent = msg; }

  // ── RSSI ──────────────────────────────────────────────────────────
  function updateRSSI(v) {
    const el  = document.getElementById('rssiVal');
    const bar = document.getElementById('rssiBar');
    const pct = Math.max(0, Math.min(100, (v + 110) * 2));
    const col = v > -60 ? '#0f0' : v > -80 ? '#fa0' : '#555';
    el.textContent = 'RSSI ' + v.toFixed(1) + ' dBm';
    el.style.color = col;
    bar.style.width = pct + '%';
    bar.style.background = col;
  }

  function getCapDur() { return parseInt(document.getElementById('capDur').value); }

  function setCapturing(on) {
    scanning = on;
    document.getElementById('dot').className = on ? 'blink' : '';
    document.getElementById('capStatus').textContent = on ? 'Capturing…' : '';
    if (on) {
      if (!pollTimer) pollTimer = setInterval(poll, 300);
    } else {
      if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
      fetchPackets();
    }
  }

  // ── Poll ──────────────────────────────────────────────────────────
  async function poll() {
    const s = await fetch('/status').then(r=>r.json()).catch(()=>null);
    if (!s) return;
    updateRSSI(s.rssi);
    if (s.edges !== undefined)
      document.getElementById('edgesVal').textContent = 'edges ' + s.edges;
    if (!s.capturing && scanning) setCapturing(false);
    if (s.count > 0) fetchPackets();
  }

  async function fetchPackets() {
    const j = await fetch('/packets').then(r=>r.text()).catch(()=>'[]');
    if (j === prevJson) return;
    prevJson = j;
    allPkts = JSON.parse(j);
    applyFilter();
  }

  // ── Download ──────────────────────────────────────────────────────
  function downloadCaptures() {
    if (!allPkts.length) { setStatus('Nothing to download yet'); return; }
    const ts  = new Date().toISOString().replace(/[:.]/g,'-').slice(0,19);
    const j   = JSON.stringify(allPkts, null, 2);
    const a   = document.createElement('a');
    a.href    = 'data:application/json,' + encodeURIComponent(j);
    a.download = 'captures_' + ts + '.json';
    document.body.appendChild(a); a.click(); document.body.removeChild(a);
  }

  // ── Wizard chip list ──────────────────────────────────────────────
  function addBtn() {
    const inp  = document.getElementById('newBtnName');
    const name = inp.value.trim().toUpperCase().replace(/[^A-Z0-9_]/g,'');
    if (!name || btnNames.includes(name)) { inp.select(); return; }
    btnNames.push(name);
    inp.value = '';
    renderBtnList();
  }
  function removeBtn(name) {
    btnNames = btnNames.filter(b => b !== name);
    renderBtnList();
  }
  function renderBtnList() {
    document.getElementById('btnWiz').disabled = btnNames.length < 1;
    const chipDiv  = document.getElementById('chipList');
    const capRow   = document.getElementById('capBtnRow');
    const quickRow = document.getElementById('quickCapRow');
    if (!btnNames.length) {
      chipDiv.innerHTML  = '<span style="color:#555;font-size:0.85em">No buttons yet — type a name and click + Add</span>';
      capRow.innerHTML   = '';
      quickRow.innerHTML = '';
      return;
    }
    chipDiv.innerHTML = btnNames.map(n =>
      `<span class="btn-chip">${n}<span class="rm" onclick="removeBtn('${n}')">&times;</span></span>`
    ).join('');
    const capBtns = btnNames.map(n =>
      `<button class="blue" style="font-size:0.88em;padding:4px 10px"
        onclick="captureSingle('${n}')">&#9654; Capture ${n}</button>`
    ).join('');
    capRow.innerHTML   = capBtns;
    quickRow.innerHTML = capBtns;
  }

  async function captureSingle(name) {
    const t = getCapDur();
    await fetch('/capture?btn=' + encodeURIComponent(name) + '&t=' + t);
    setCapturing(true);
  }

  // ── Wizard state machine ──────────────────────────────────────────
  const WIZ_REPS = 3;
  let wizBtns    = [];
  let wizIdx     = 0;
  let wizCaps    = {};
  let wizAllCaps = [];
  let wizPhase   = 'idle';
  let wizPollTmr = null;

  function startWizard() {
    if (!btnNames.length) return;
    wizBtns    = [...btnNames];
    wizIdx     = 0;
    wizCaps    = {};
    wizAllCaps = [];
    wizBtns.forEach(n => wizCaps[n] = []);
    document.getElementById('wizOverlay').classList.add('show');
    wizStartButton();
  }

  function wizStartButton() {
    if (wizIdx >= wizBtns.length) { wizFinish(); return; }
    const name  = wizBtns[wizIdx];
    wizCaps[name] = [];
    document.getElementById('wizCapList').innerHTML  = '';
    document.getElementById('wizResult').innerHTML   = '';
    document.getElementById('wizMatchBar').style.display = 'none';
    wizNextCapture();
  }

  function wizNextCapture() {
    const name  = wizBtns[wizIdx];
    const round = wizCaps[name].length + 1;
    wizPhase    = 'waiting';

    wizSetStepBar();
    document.getElementById('wizBtnName').textContent  = name;
    document.getElementById('wizRound').textContent    = 'Capture ' + round + ' of ' + WIZ_REPS;
    document.getElementById('wizSubtext').textContent  = 'Press the button now';
    document.getElementById('wizSubtext').style.color  = '#0f0';
    document.getElementById('wizWaitWrap').style.visibility = 'visible';
    document.getElementById('wizBtns').innerHTML =
      '<button class="red" onclick="wizCancel()">Cancel</button>';

    fetch('/capture?btn=' + encodeURIComponent(name) + '&t=60000');

    wizClearPoll();
    wizPollTmr = setInterval(wizPoll, 200);
  }

  async function wizPoll() {
    if (wizPhase !== 'waiting') return;
    const s = await fetch('/status').then(r=>r.json()).catch(()=>null);
    if (!s) return;

    if (!s.capturing && s.count === 0) {
      wizPhase = 'error';
      wizClearPoll();
      document.getElementById('wizWaitWrap').style.visibility = 'hidden';
      document.getElementById('wizSubtext').textContent = 'No signal received — try again?';
      document.getElementById('wizSubtext').style.color = '#f80';
      document.getElementById('wizBtns').innerHTML =
        '<button onclick="wizNextCapture()">Retry</button>' +
        '<button class="red" style="margin-left:6px" onclick="wizCancel()">Cancel</button>';
      return;
    }

    if (s.count >= 1) {
      wizPhase = 'stopping';
      wizClearPoll();
      document.getElementById('wizWaitWrap').style.visibility = 'hidden';
      document.getElementById('wizSubtext').textContent = 'Signal received — stabilizing…';
      document.getElementById('wizSubtext').style.color = '#0af';

      setTimeout(async () => {
        await fetch('/stop');
        const pkts = await fetch('/packets').then(r=>r.json()).catch(()=>[]);
        wizGotPacket(pkts.length ? pkts[0] : null);
      }, 1000);
    }
  }

  function wizGotPacket(pkt) {
    const name = wizBtns[wizIdx];
    if (!pkt) {
      document.getElementById('wizSubtext').textContent = 'No valid packet — retry?';
      document.getElementById('wizSubtext').style.color = '#f80';
      document.getElementById('wizBtns').innerHTML =
        '<button onclick="wizNextCapture()">Retry</button>' +
        '<button class="red" style="margin-left:6px" onclick="wizCancel()">Cancel</button>';
      return;
    }

    wizCaps[name].push(pkt);
    wizAllCaps.push(pkt);
    wizRenderCapList(name);

    const count = wizCaps[name].length;
    if (count < WIZ_REPS) {
      document.getElementById('wizSubtext').textContent =
        'Got it! Press again (' + (count+1) + '/' + WIZ_REPS + ')';
      document.getElementById('wizSubtext').style.color = '#0af';
      document.getElementById('wizRound').textContent = 'Capture ' + (count+1) + ' of ' + WIZ_REPS;
      wizPhase = 'between';
      setTimeout(wizNextCapture, 1400);
    } else {
      wizAnalyze(name);
    }
  }

  function wizRenderCapList(name) {
    const caps = wizCaps[name];
    document.getElementById('wizCapList').innerHTML = caps.map((p, i) => {
      const seq   = wizSeqStr(p);
      const short = seq.slice(0, 64);
      const html  = short
        .replace(/S/g, '<span style="color:#0a0">S</span>')
        .replace(/L/g, '<span style="color:#46f">L</span>');
      const m = getMeta(p);
      const meta = m ? 'T=' + m.ts + ' ' + m.reps + 'rep' : 'n=' + p.n;
      return `<div class="wiz-cap-row">
        <span class="wiz-cap-num">cap${i+1}</span>
        <span class="wiz-cap-seq">${html}</span>
        <span class="wiz-cap-meta">${meta}</span>
      </div>`;
    }).join('');
  }

  function wizSeqStr(pkt) {
    const ts = pkt.ts || 0;
    if (!ts) return '';
    const thr = ts * 1.5;
    const raw = pkt.p.map(v => {
      if (v > 3500) return '|';
      if (v < 100 || v > 2000) return '';
      return v >= thr ? 'L' : 'S';
    }).join('');
    const parts = raw.split('|').filter(s => s.length > 10);
    return parts.length ? parts[0] : raw.replace(/\|/g, '');
  }

  function seqSim(a, b) {
    const len = Math.min(a.length, b.length);
    if (!len) return 0;
    let m = 0;
    for (let i = 0; i < len; i++) if (a[i] === b[i]) m++;
    return m / Math.max(a.length, b.length);
  }

  function wizAnalyze(name) {
    wizPhase = 'analyzing';
    document.getElementById('wizSubtext').textContent = 'Analyzing…';
    document.getElementById('wizSubtext').style.color = '#888';

    setTimeout(() => {
      const caps = wizCaps[name];
      const seqs = caps.map(wizSeqStr);

      let total = 0, pairs = 0;
      for (let i = 0; i < seqs.length - 1; i++)
        for (let j = i+1; j < seqs.length; j++) { total += seqSim(seqs[i], seqs[j]); pairs++; }
      const avg = pairs ? total / pairs : 0;
      const pct = Math.round(avg * 100);

      const isLast = wizIdx >= wizBtns.length - 1;
      const nextName = isLast ? null : wizBtns[wizIdx + 1];

      const bar  = document.getElementById('wizMatchBar');
      const fill = document.getElementById('wizMatchFill');
      bar.style.display = 'block';
      fill.style.background = avg >= 0.90 ? '#0f0' : avg >= 0.75 ? '#fa0' : '#f44';
      setTimeout(() => { fill.style.width = pct + '%'; }, 30);

      let cls, icon, verdict;
      if      (avg >= 0.90) { cls = 'match-good'; icon = '&#10003;'; verdict = 'Fixed code — safe to replay'; }
      else if (avg >= 0.75) { cls = 'match-warn'; icon = '&#9888;';  verdict = 'Partial match — rolling code suspected'; }
      else                  { cls = 'match-bad';  icon = '&#10007;'; verdict = 'No match — rolling code'; }

      document.getElementById('wizSubtext').textContent = '';
      document.getElementById('wizResult').innerHTML =
        `<span class="${cls}">${icon} ${pct}% match (${WIZ_REPS} captures)</span>` +
        `<div style="color:#555;font-size:0.82em;margin-top:4px">${verdict}</div>`;

      let btns = '';
      if (avg >= 0.75) {
        btns += nextName
          ? `<button onclick="wizAdvance()">Next: ${nextName} &rarr;</button>`
          : `<button onclick="wizAdvance()">Done &#10003;</button>`;
      } else {
        btns += `<button onclick="wizRetry()">&#8634; Retry ${name}</button>`;
        btns += nextName
          ? `<button class="stop" onclick="wizAdvance()">Skip &rarr;</button>`
          : `<button class="stop" onclick="wizAdvance()">Finish anyway</button>`;
      }
      btns += '<button class="red" onclick="wizCancel()">Cancel</button>';
      document.getElementById('wizBtns').innerHTML = btns;
      wizPhase = 'result';
    }, 500);
  }

  function wizAdvance() {
    wizIdx++;
    document.getElementById('wizCapList').innerHTML = '';
    document.getElementById('wizResult').innerHTML  = '';
    document.getElementById('wizMatchBar').style.display = 'none';
    document.getElementById('wizMatchFill').style.width  = '0%';
    wizStartButton();
  }

  function wizRetry() {
    const name = wizBtns[wizIdx];
    const old = wizCaps[name] || [];
    wizAllCaps = wizAllCaps.filter(p => !old.includes(p));
    wizCaps[name] = [];
    document.getElementById('wizCapList').innerHTML = '';
    document.getElementById('wizResult').innerHTML  = '';
    document.getElementById('wizMatchBar').style.display = 'none';
    document.getElementById('wizMatchFill').style.width  = '0%';
    wizNextCapture();
  }

  function wizFinish() {
    document.getElementById('wizOverlay').classList.remove('show');
    allPkts = wizAllCaps;
    applyFilter();
    setStatus('Wizard complete — ' + wizAllCaps.length + ' capture(s). Use ⬇ Download JSON to save.');
  }

  function wizCancel() {
    wizClearPoll();
    fetch('/stop').catch(()=>{});
    wizPhase = 'idle';
    document.getElementById('wizOverlay').classList.remove('show');
  }

  function wizClearPoll() {
    if (wizPollTmr) { clearInterval(wizPollTmr); wizPollTmr = null; }
  }

  function wizSetStepBar() {
    document.getElementById('wizStepBar').innerHTML = wizBtns.map((n, i) =>
      `<span class="${i < wizIdx ? 'wiz-step-done' : i === wizIdx ? 'wiz-step-cur' : 'wiz-step-todo'}">${n}</span>`
    ).join(' &rsaquo; ');
  }

  // ── Analysis helpers ──────────────────────────────────────────────
  function getMeta(pkt) {
    let ts = pkt.ts || 0, tl = pkt.tl || 0;
    if (!ts) {
      const ng = pkt.p.filter(v => v < 2000 && v > 50);
      if (ng.length < 8) return null;
      const s = [...ng].sort((a,b)=>a-b);
      let mg=0, si=1;
      for (let i=1;i<s.length;i++) if(s[i]-s[i-1]>mg){mg=s[i]-s[i-1];si=i;}
      const sh=s.slice(0,si), ln=s.slice(si);
      if (sh.length<4||ln.length<2) return null;
      ts = Math.round(sh.reduce((a,b)=>a+b,0)/sh.length);
      tl = Math.round(ln.reduce((a,b)=>a+b,0)/ln.length);
    }
    if (!ts) return null;
    const ratio = tl ? tl/ts : 0;
    let proto;
    if      (ratio < 1.3)  proto = '? ratio<1.3';
    else if (ratio < 2.5)  proto = 'Manchester / biphase';
    else if (ratio < 4.5)  proto = 'OOK / EV1527';
    else                   proto = '? ratio>4.5';
    return { ts, tl, ratio: ratio.toFixed(2), proto, reps: pkt.reps||0, lbl: pkt.lbl||'' };
  }

  function buildSeq(p, ts, max) {
    if (!ts) return '';
    const thr = ts * 1.5;
    return p.slice(0, max||350).map(v =>
      v > 3500 && v < 8000 ? '|' : v > 2000 ? '·' : v >= thr ? 'L' : 'S'
    ).join('');
  }

  function seqHtml(seq) {
    return seq
      .replace(/S/g, '<span class="sc">S</span>')
      .replace(/L/g, '<span class="lc">L</span>')
      .replace(/\|/g,'<span class="gc">|</span>')
      .replace(/·/g, '<span style="color:#332200">·</span>');
  }

  function pairStats(p, ts) {
    const thr = ts * 1.5;
    const dg  = p.filter(v => v < 2000);
    let ss=0, sl=0, ls=0, ll=0;
    for (let i=0; i+1<dg.length; i+=2) {
      const h=dg[i]>=thr, l=dg[i+1]>=thr;
      if (!h&&!l) ss++; else if (!h&&l) sl++;
      else if (h&&!l) ls++; else ll++;
    }
    return {ss,sl,ls,ll};
  }

  // ── BMC comparison table ──────────────────────────────────────────
  function renderBMCCompare(pkts) {
    const best = {};
    pkts.forEach(p => {
      if (!p.bmc || !p.lbl) return;
      const prev = best[p.lbl];
      if (!prev || (p.bn||0) > (prev.bn||0) || (p.reps||0) > (prev.reps||0)) best[p.lbl] = p;
    });
    const labels = Object.keys(best);
    if (labels.length < 2) { document.getElementById('bmcComp').innerHTML=''; return; }

    const maxB = Math.max(...labels.map(l => Math.ceil((best[l].bn||0)/8)));
    if (!maxB) { document.getElementById('bmcComp').innerHTML=''; return; }

    function getBytes(pkt) {
      const h = pkt.bmc||'';
      return Array.from({length:maxB}, (_,i) =>
        h.length > i*2+1 ? parseInt(h.slice(i*2,i*2+2),16) : null);
    }
    const ba = {}; labels.forEach(l => ba[l] = getBytes(best[l]));

    let h = '<div style="color:#0af;font-weight:bold;margin-bottom:8px;font-size:0.9em">';
    h += '&#9679; BMC Decoded — Byte Comparison</div>';
    h += '<div style="overflow-x:auto"><table class="cmp-tbl">';

    h += '<tr><td class="hdr lbl">Byte</td>';
    for (let b=0;b<maxB;b++) h += `<td class="hdr">${b.toString().padStart(2,'0')}</td>`;
    h += '</tr>';

    labels.forEach(l => {
      h += `<tr><td class="lbl">${l}</td>`;
      ba[l].forEach((v,b) => {
        const vals = labels.map(ll => ba[ll][b]);
        const same = vals.every(x => x===vals[0] && x!==null);
        const hex  = v!==null ? v.toString(16).toUpperCase().padStart(2,'0') : '--';
        h += `<td class="${same?'cmp-same':'cmp-diff'}">${hex}</td>`;
      });
      h += '</tr>';
    });

    h += '<tr><td class="hdr lbl">same?</td>';
    for (let b=0;b<maxB;b++) {
      const vals = labels.map(l => ba[l][b]);
      const same = vals.every(v => v===vals[0] && v!==null);
      h += `<td>${same?'<span class="cmp-ok">&#10003;</span>':'<span class="cmp-chk">&#10007;</span>'}</td>`;
    }
    h += '</tr></table></div>';

    h += '<div style="color:#444;font-size:0.78em;margin-top:6px">';
    labels.forEach(l => h += `${l} sof=${best[l].sof||0} bits=${best[l].bn||0}&nbsp;&nbsp;`);
    h += '</div>';

    document.getElementById('bmcComp').innerHTML = h;
  }

  // ── TX Replay (BMC only — only labeled captures with decoded BMC) ──
  function renderTXReplay(pkts) {
    const bmcLabels   = [...new Set(pkts.filter(p => p.lbl && (p.bn||0) >= 8).map(p => p.lbl))];
    const savedLabels = [...new Set(pkts.filter(p => p.lbl && p.saved).map(p => p.lbl))];
    const btns = document.getElementById('txBtns');
    if (!bmcLabels.length) {
      btns.innerHTML = '<span style="color:#555;font-size:0.85em">No labeled captures yet — add a button name above, capture it, then replay</span>';
      return;
    }
    btns.innerHTML = bmcLabels.map(lbl => {
      const isSaved = savedLabels.includes(lbl);
      const play = `<button class="blue" onclick="bmcReplay('${lbl}')" title="Transmit via CC1101">&#9654;&nbsp;${lbl}</button>`;
      const sv   = isSaved
        ? `<button class="red" style="font-size:0.82em;padding:3px 8px" onclick="delSig('${lbl}')" title="Remove from flash">&#9733;&nbsp;Del</button>`
        : `<button style="font-size:0.82em;padding:3px 8px" onclick="saveSig('${lbl}')" title="Save to flash">&#9734;&nbsp;Save</button>`;
      return `<span style="display:inline-flex;gap:4px;margin:2px;align-items:center">${play}${sv}</span>`;
    }).join('');
  }

  function bmcReplay(lbl) {
    document.getElementById('replayLine').textContent = 'Sending ' + lbl + ' (BMC)…';
    fetch('/replay?lbl=' + encodeURIComponent(lbl))
      .then(r => r.text())
      .then(t => document.getElementById('replayLine').textContent = lbl + ' BMC: ' + t)
      .catch(() => document.getElementById('replayLine').textContent = lbl + ' BMC: error');
  }

  function saveSig(lbl) {
    document.getElementById('replayLine').textContent = 'Saving ' + lbl + '…';
    fetch('/savesig?lbl=' + encodeURIComponent(lbl))
      .then(r => r.text())
      .then(t => { document.getElementById('replayLine').textContent = lbl + ' saved (' + t + ')'; setTimeout(fetchPackets, 300); })
      .catch(() => { document.getElementById('replayLine').textContent = lbl + ': save error'; });
  }

  function delSig(lbl) {
    document.getElementById('replayLine').textContent = 'Deleting ' + lbl + '…';
    fetch('/delsig?lbl=' + encodeURIComponent(lbl))
      .then(r => r.text())
      .then(t => { document.getElementById('replayLine').textContent = lbl + ' deleted (' + t + ')'; setTimeout(fetchPackets, 300); })
      .catch(() => { document.getElementById('replayLine').textContent = lbl + ': delete error'; });
  }

  function setFilter(n) {
    fetch('/setfilter?n=' + encodeURIComponent(n))
      .then(r => r.text())
      .then(t => document.getElementById('replayLine').textContent = 'Noise filter: ' + t)
      .catch(() => document.getElementById('replayLine').textContent = 'setFilter: error');
  }

  // ── Filter + render ───────────────────────────────────────────────
  function applyFilter() {
    const hide = document.getElementById('chkNoise').checked;
    const vis  = hide ? allPkts.filter(p=>p.n>=50) : allPkts;
    const hid  = allPkts.length - vis.length;
    document.getElementById('filterInfo').textContent = allPkts.length
      ? (hid ? vis.length+' shown, '+hid+' noise hidden' : vis.length+' packet(s)') : '';
    renderBMCCompare(vis);
    renderPkts(vis);
    renderTXReplay(allPkts);
  }

  function renderPkts(pkts) {
    const div = document.getElementById('pkts');
    if (!pkts.length) {
      div.innerHTML = allPkts.length
        ? '<span style="color:#555">All packets filtered — uncheck "Hide noise" to show</span>'
        : '<span style="color:#555">No packets captured yet — use the wizard above</span>';
      return;
    }
    div.innerHTML = pkts.map((pkt, i) => {
      const m   = getMeta(pkt);
      const maxW = Math.min(Math.max(...pkt.p), 5500);
      const sc   = 90 / maxW;

      const bars = pkt.p.slice(0,300).map(w => {
        const px = Math.max(1, Math.round(w*sc));
        let cls = 'ps';
        if (w>3500&&w<8000) cls='pg';
        else if (m && w >= m.ts*1.5) cls='pl';
        return `<span class="pb ${cls}" style="width:${px}px" title="${w}µs"></span>`;
      }).join('');

      const lblHtml = (m&&m.lbl) ? `<span class="pkt-lbl">${m.lbl}</span> ` : '';
      let infoHtml;
      if (m) {
        const pc = m.proto.includes('Manchester') ? 'proto-ok' : 'proto-q';
        infoHtml = `${lblHtml}<span class="${pc}">${m.proto}</span>`
          + ` &nbsp;T=${m.ts}&nbsp;2T=${m.tl}&nbsp;ratio=${m.ratio}`
          + ` &nbsp;<span style="color:#0c0;font-weight:bold">${m.reps} rep${m.reps!==1?'s':''}</span>`;
      } else {
        infoHtml = `${lblHtml}<span class="proto-q">? insufficient data</span>`;
      }

      const sq    = m ? buildSeq(pkt.p, m.ts, 400) : '';
      const sqHtml = sq ? `<div class="seq">${seqHtml(sq)}</div>` : '';

      let pairHtml = '';
      if (m && m.ts) {
        const ps  = pairStats(pkt.p, m.ts);
        const tot = ps.ss+ps.sl+ps.ls+ps.ll;
        if (tot>0) pairHtml = `<table class="ptbl"><tr>`
          +`<td>SS:</td><td class="v">${ps.ss}</td>`
          +`<td>SL:</td><td class="v">${ps.sl}</td>`
          +`<td>LS:</td><td class="v">${ps.ls}</td>`
          +`<td>LL:</td><td class="v">${ps.ll}</td>`
          +`<td style="color:#444">(${tot} pairs)</td>`
          +`</tr></table>`;
      }

      let bmcHtml = '';
      if (pkt.bmc && pkt.bn) {
        const bytes = pkt.bmc.match(/.{1,2}/g) || [];
        const highlighted = bytes.map((b, idx) => {
          const v = parseInt(b, 16);
          const diff = allPkts.some(q =>
            q.lbl && q.lbl !== pkt.lbl && q.bmc &&
            q.bmc.length > idx*2+1 &&
            parseInt(q.bmc.slice(idx*2, idx*2+2), 16) !== v
          );
          return `<span style="color:${diff?'#ff0':'#0f0'}">${b}</span>`;
        }).join(' ');
        bmcHtml = `<div class="bmc-row">
          <span class="bmc-meta">BMC</span>
          <span class="bmc-hex">${highlighted}</span>
          <span class="bmc-meta">${pkt.bn}b&nbsp;sof=${pkt.sof}</span>
        </div>`;
      }

      return `<div class="pkt">
        <div class="pkt-header" onclick="toggleRaw(${i})">
          <span class="pkt-idx">#${i+1}</span>
          <span class="pkt-n">n=${pkt.n}</span>
          <span class="pkt-info">${infoHtml}</span>
        </div>
        <div class="bars">${bars}</div>
        ${bmcHtml}
        ${sqHtml}
        ${pairHtml}
        <div class="raw" id="raw${i}">${pkt.p.join(', ')}</div>
      </div>`;
    }).join('');
  }

  function toggleRaw(i) { document.getElementById('raw'+i).classList.toggle('open'); }

  // ── MQTT config ───────────────────────────────────────────────────
  async function fetchMQTT() {
    const c = await fetch('/mqttcfg').then(r=>r.json()).catch(()=>null);
    if (!c) return;
    document.getElementById('mqttHost').value = c.host || '';
    document.getElementById('mqttPort').value = c.port || 1883;
    document.getElementById('mqttUser').value = c.user || '';
    const st = document.getElementById('mqttStatus');
    if (c.connected) {
      st.textContent = '● Connected'; st.style.color = '#0f0';
    } else {
      st.textContent = c.host ? '○ Disconnected' : 'Not configured'; st.style.color = '#888';
    }
    if (c.name !== undefined) document.getElementById('mqttName').value = c.name;
    if (c.devId) {
      document.getElementById('mqttInfo').innerHTML =
        'Device ID: <span style="color:#0af">' + c.devId + '</span>' +
        ' &nbsp;|&nbsp; HA name: <b style="color:#0af">' + (c.name||'?') + '</b>' +
        '<br>Cover topic: <span style="color:#888">home/433mhz/' + c.devId + '/cover/cmd</span>' +
        ' &nbsp;|&nbsp; Button: <span style="color:#888">home/433mhz/' + c.devId + '/press/LABEL</span>';
    }
  }

  async function saveMQTT() {
    const fd = new FormData();
    fd.append('name', document.getElementById('mqttName').value.trim() || 'Garage Door');
    fd.append('host', document.getElementById('mqttHost').value.trim());
    fd.append('port', document.getElementById('mqttPort').value || '1883');
    fd.append('user', document.getElementById('mqttUser').value);
    fd.append('pass', document.getElementById('mqttPass').value);
    const t = await fetch('/mqttcfg', {method:'POST', body:fd}).then(r=>r.text()).catch(()=>'error');
    document.getElementById('mqttStatus').textContent = t;
    setTimeout(fetchMQTT, 2500);
  }

  async function rediscover() {
    const t = await fetch('/mqttdiscover').then(r=>r.text()).catch(()=>'error');
    document.getElementById('mqttStatus').textContent = 'Discovery: ' + t;
  }

  // ── Init ──────────────────────────────────────────────────────────
  fetch('/status').then(r=>r.json()).then(s => {
    updateRSSI(s.rssi);
    if (s.capturing) setCapturing(true);
    else if (s.count>0) fetchPackets();
  }).catch(()=>{});
  fetchMQTT();
</script>
</body>
</html>
)rawliteral";
