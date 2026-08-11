    // --- WebSocket ---
    let ws = null;
    let adminToken = '';
    let currentPath = '/';
    let filesHistory = [];
    let termHistory = [];
    let histIdx = -1;

    function connect(password) {
      adminToken = password;
      document.getElementById('login-msg').textContent = 'Connecting...';
      ws = new WebSocket('ws://192.168.4.1/ws');
      ws.onopen = () => {
        ws.send(JSON.stringify({type: 'auth', token: password}));
      };
      ws.onmessage = (e) => {
        let msg;
        try { msg = JSON.parse(e.data); } catch { return; }
        onMessage(msg);
      };
      ws.onclose = () => {
        document.getElementById('login-overlay').style.display = 'flex';
        document.getElementById('login-msg').textContent = 'Connection lost. Reconnect.';
      };
      ws.onerror = () => {
        document.getElementById('login-msg').textContent = 'WebSocket error';
      };
    }

    function onMessage(msg) {
      switch (msg.type) {
        case 'auth':
          if (msg.data === true) {
            document.getElementById('login-overlay').style.display = 'none';
            // request settings and a file listing
            ws.send(JSON.stringify({type: 'settings', action: 'get'}));
            ws.send(JSON.stringify({type: 'cmd', cmd: 'ls /'}));
          } else {
            document.getElementById('login-msg').textContent = 'Access denied';
          }
          break;
        case 'sysinfo': updateSysInfo(msg.data); break;
        case 'terminal': appendTerminal(msg.data, false); break;
        case 'files': renderFiles(msg.data, msg.path); break;
        case 'networks': renderNetworks(msg.data); break;
        case 'settings': fillSettings(msg.data); adminToken = msg.data.adminPass || adminToken; break;
        case 'weather': if (window.weatherResult) window.weatherResult(msg.data); break;
        case 'error':
          appendTerminal('Error: ' + (msg.data || 'unknown'), false);
          break;
      }
    }

    function sendCmd(cmd) {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({type: 'cmd', cmd: cmd}));
      }
    }

    // --- Login ---
    document.getElementById('login-btn').addEventListener('click', () => {
      const p = document.getElementById('login-pass').value;
      connect(p);
    });
    document.getElementById('login-pass').addEventListener('keydown', (e) => {
      if (e.key === 'Enter') document.getElementById('login-btn').click();
    });

    // --- Desktop & wallpaper ---
    fetch('/wallpaper.jpg')
      .then(r => { if (r.status === 200) document.body.style.backgroundImage = "url('/wallpaper.jpg?t=" + Date.now() + "')"; })
      .catch(() => {});

    // --- Start menu ---
    const startMenu = document.getElementById('start-menu');
    document.getElementById('start-btn').addEventListener('click', (e) => {
      e.stopPropagation();
      startMenu.style.display = startMenu.style.display === 'block' ? 'none' : 'block';
    });
    document.addEventListener('click', (e) => {
      if (!startMenu.contains(e.target) && e.target.id !== 'start-btn') startMenu.style.display = 'none';
    });
    document.querySelectorAll('.menu-item').forEach(el => {
      el.addEventListener('click', () => {
        openWindow(el.dataset.app);
        startMenu.style.display = 'none';
      });
    });
    document.getElementById('menu-search').addEventListener('input', (e) => {
      const q = e.target.value.toLowerCase();
      document.querySelectorAll('.menu-item').forEach(el => {
        el.style.display = el.textContent.toLowerCase().includes(q) ? '' : 'none';
      });
    });

    // --- Clock ---
    setInterval(() => {
      const d = new Date();
      document.getElementById('clock').textContent = d.toLocaleTimeString([], {hour:'2-digit', minute:'2-digit'});
    }, 1000);

    // --- Window manager ---
    let zTop = 100;
    const winPos = { system: {x:60,y:40}, terminal: {x:90,y:70}, files: {x:120,y:100}, wifi: {x:150,y:130}, settings: {x:180,y:160}, attacks: {x:210,y:190}, portal: {x:260,y:220}, calc: {x:310,y:250}, note: {x:340,y:280}, guide: {x:370,y:310}, viewer: {x:400,y:340}, network: {x:430,y:370}, about: {x:460,y:400}, clock: {x:490,y:430}, task: {x:520,y:460}, game: {x:550,y:490}, snake: {x:580,y:520}, tictactoe: {x:610,y:550}, minesweeper: {x:640,y:580}, weather: {x:670,y:610} };
    const apps = {
      system: { title: 'System Status', w: 360, h: 400, html: systemHTML() },
      terminal: { title: 'Terminal', w: 560, h: 380, html: terminalHTML() },
      files: { title: 'File Manager', w: 520, h: 420, html: filesHTML() },
      wifi: { title: 'Wi-Fi Scanner', w: 560, h: 420, html: wifiHTML() },
      settings: { title: 'Settings', w: 420, h: 360, html: settingsHTML() },
      attacks: { title: 'Attacks', w: 520, h: 480, html: attacksHTML() },
      portal: { title: 'Captive Portal', w: 480, h: 420, html: portalHTML() },
      calc: { title: 'Calculator', w: 280, h: 380, html: calcHTML() },
      note: { title: 'Notepad', w: 420, h: 320, html: noteHTML() },
      guide: { title: 'Guide', w: 460, h: 420, html: guideHTML() },
      viewer: { title: 'File Viewer', w: 520, h: 420, html: viewerHTML() },
      network: { title: 'Network', w: 460, h: 420, html: networkHTML() },
      about: { title: 'About Icy OS', w: 360, h: 380, html: aboutHTML() },
      clock: { title: 'Clock', w: 260, h: 120, html: clockHTML() },
      task: { title: 'Task Manager', w: 400, h: 320, html: taskHTML() },
      game: { title: 'Pong', w: 520, h: 400, html: gameHTML() },
      snake: { title: 'Snake', w: 360, h: 420, html: snakeHTML() },
      tictactoe: { title: 'Tic-Tac-Toe', w: 320, h: 380, html: tictactoeHTML() },
      minesweeper: { title: 'Minesweeper', w: 360, h: 480, html: minesweeperHTML() },
      weather: { title: 'Weather', w: 340, h: 240, html: weatherHTML() }
    };
    const openWins = {};

    function openWindow(app) {
      if (openWins[app]) { focusWindow(app); return; }
      const a = apps[app];
      const pos = winPos[app];
      const id = 'win-' + app;
      const div = document.createElement('div');
      div.className = 'window';
      div.id = id;
      div.style.width = a.w + 'px';
      div.style.height = a.h + 'px';
      div.style.left = pos.x + 'px';
      div.style.top = pos.y + 'px';
      div.style.zIndex = ++zTop;
      div.innerHTML = `
        <div class="titlebar" data-app="${app}">
          <span class="win-title">${a.title}</span>
          <div class="win-btns">
            <button class="min" data-app="${app}">_</button>
            <button class="max" data-app="${app}">&#x25A1;</button>
            <button class="close" data-app="${app}">&#x2715;</button>
          </div>
        </div>
        <div class="win-content">${a.html}</div>
      `;
      document.getElementById('desktop').appendChild(div);
      openWins[app] = div;
      addTaskIcon(app, a.title);
      initWindowBehavior(div, app);
      focusWindow(app);
      if (app === 'wifi') ws.send(JSON.stringify({type:'scanner', action:'subscribe'}));
    }

    function closeWindow(app) {
      const div = openWins[app];
      if (!div) return;
      div.remove();
      delete openWins[app];
      if (app === 'wifi') ws.send(JSON.stringify({type:'scanner', action:'unsubscribe'}));
      const ic = document.getElementById('task-' + app);
      if (ic) ic.remove();
    }

    function focusWindow(app) {
      const div = openWins[app];
      if (!div) return;
      if (div.classList.contains('minimized')) {
        div.classList.remove('minimized');
      }
      div.style.zIndex = ++zTop;
      document.querySelectorAll('.window').forEach(w => w.classList.remove('active'));
      div.classList.add('active');
      document.querySelectorAll('.task-icon').forEach(i => i.classList.remove('active'));
      const ic = document.getElementById('task-' + app);
      if (ic) ic.classList.add('active');
    }

    function addTaskIcon(app, title) {
      const bar = document.getElementById('task-icons');
      const ic = document.createElement('div');
      ic.className = 'task-icon active';
      ic.id = 'task-' + app;
      ic.textContent = title;
      ic.addEventListener('click', () => {
        const div = openWins[app];
        if (div && !div.classList.contains('minimized')) {
          if (div.style.zIndex == zTop) {
            div.classList.add('minimized');
            ic.classList.remove('active');
          } else {
            focusWindow(app);
          }
        } else {
          focusWindow(app);
        }
      });
      bar.appendChild(ic);
    }

    function initWindowBehavior(div, app) {
      const title = div.querySelector('.titlebar');
      let dragging = false, ox = 0, oy = 0, sx = 0, sy = 0;
      const desktop = document.getElementById('desktop');

      function getClient(e) { return e.touches ? e.touches[0] : e; }
      function clamp() {
        const dw = desktop.clientWidth, dh = desktop.clientHeight;
        let l = parseInt(div.style.left || 0), t = parseInt(div.style.top || 0);
        let w = div.offsetWidth, h = div.offsetHeight;
        if (l + w > dw) l = Math.max(0, dw - w);
        if (t + h > dh) t = Math.max(0, dh - h);
        if (l < 0) l = 0;
        if (t < 0) t = 0;
        div.style.left = l + 'px';
        div.style.top = t + 'px';
      }

      function startDrag(e) {
        dragging = true;
        focusWindow(app);
        const c = getClient(e);
        ox = c.clientX; oy = c.clientY;
        sx = div.offsetLeft; sy = div.offsetTop;
      }
      function moveDrag(e) {
        if (!dragging) return;
        e.preventDefault();
        const c = getClient(e);
        let l = sx + c.clientX - ox;
        let t = sy + c.clientY - oy;
        const dw = desktop.clientWidth, dh = desktop.clientHeight;
        const w = div.offsetWidth, h = div.offsetHeight;
        l = Math.max(0, Math.min(l, dw - w));
        t = Math.max(0, Math.min(t, dh - h));
        div.style.left = l + 'px';
        div.style.top = t + 'px';
      }
      function endDrag() { dragging = false; clamp(); }

      title.addEventListener('mousedown', startDrag);
      title.addEventListener('touchstart', startDrag, {passive: false});
      document.addEventListener('mousemove', moveDrag);
      document.addEventListener('touchmove', moveDrag, {passive: false});
      document.addEventListener('mouseup', endDrag);
      document.addEventListener('touchend', endDrag);

      div.addEventListener('mousedown', () => focusWindow(app));
      div.addEventListener('touchstart', () => focusWindow(app), {passive: true});

      div.querySelector('.close').addEventListener('click', () => closeWindow(app));
      div.querySelector('.min').addEventListener('click', () => {
        div.classList.add('minimized');
        document.getElementById('task-' + app).classList.remove('active');
      });
      div.querySelector('.max').addEventListener('click', () => {
        if (div.dataset.max === '1') {
          div.style.width = apps[app].w + 'px';
          div.style.height = apps[app].h + 'px';
          div.style.top = winPos[app].y + 'px';
          div.style.left = winPos[app].x + 'px';
          div.dataset.max = '0';
        } else {
          div.style.width = '100%';
          div.style.height = 'calc(100% - 44px)';
          div.style.top = '0';
          div.style.left = '0';
          div.dataset.max = '1';
        }
      });
      clamp();

      if (app === 'terminal') initTerminal(div);
      if (app === 'files') initFiles(div);
      if (app === 'wifi') initWifi(div);
      if (app === 'settings') initSettings(div);
      if (app === 'attacks') initAttacks(div);
      if (app === 'portal') initPortal(div);
      if (app === 'calc') initCalc(div);
      if (app === 'note') initNote(div);
      if (app === 'viewer') initViewer(div);
      if (app === 'network') initNetwork(div);
      if (app === 'about') initAbout(div);
      if (app === 'clock') initClock(div);
      if (app === 'task') initTask(div);
      if (app === 'game') initGame(div);
      if (app === 'snake') initSnake(div);
      if (app === 'tictactoe') initTicTacToe(div);
      if (app === 'minesweeper') initMinesweeper(div);
      if (app === 'weather') initWeather(div);
    }

    // --- App HTML ---
    function systemHTML() {
      return `
        <h3 style="margin-bottom:10px">System Status</h3>
        <div class="meter"><label><span>Free Heap</span><span id="heap-pct">0%</span></label><div class="bar-outer"><div id="heap-bar" class="bar-inner" style="width:0%"></div></div></div>
        <div class="meter"><label><span>Uptime</span><span id="uptime-val">0s</span></label></div>
        <div class="meter"><label><span>WebSocket Clients</span><span id="clients-val">0</span></label></div>
        <div class="meter"><label><span>AP Stations</span><span id="stations-val">0</span></label></div>
        <div class="meter"><label><span>Best Nearby RSSI</span><span id="rssi-val">--</span></label></div>
        <div class="meter"><label><span>SD Card</span><span id="sd-val">--</span></label></div>
        <div class="meter"><label><span>SD Total</span><span id="sd-total">--</span></label></div>
        <div class="meter"><label><span>SD Used</span><span id="sd-used">--</span></label></div>
        <div class="meter"><label><span>SD Free</span><span id="sd-free">--</span></label></div>
        <div class="meter"><label><span>Attack</span><span id="attack-val">idle</span></label></div>
        <div class="meter"><label><span>Attack Pkts</span><span id="attack-pkts-val">0</span></label></div>
        <div class="meter"><label><span>Attack Ch</span><span id="attack-ch-val">0</span></label></div>
      `;
    }

    function terminalHTML() {
      return `
        <div id="terminal-output"></div>
        <div id="terminal-input">
          <input type="text" id="term-cmd" list="cmd-suggestions" placeholder="Type a command..." autocomplete="off">
          <button class="std" id="term-send">Send</button>
        </div>
        <datalist id="cmd-suggestions"></datalist>
      `;
    }

    function filesHTML() {
      return `
        <div style="margin-bottom:10px;display:flex;gap:8px;align-items:center;flex-wrap:wrap">
          <button class="std" id="files-back" title="Back">←</button>
          <button class="std" id="files-up" title="Up to parent">↑</button>
          <button class="std" id="files-refresh" title="Refresh">↻</button>
          <input type="file" id="files-file">
          <button class="std" id="files-upload-btn">Upload</button>
          <button class="std" id="files-mkdir" style="background:#2b3039;color:#e0e6ed">New Folder</button>
          <button class="std" id="files-touch" style="background:#2b3039;color:#e0e6ed">New File</button>
          <span id="files-up-status" style="font-size:12px;color:#9aa3ad"></span>
        </div>
        <div class="meter" style="margin-bottom:10px"><label><span>SD Used</span><span id="files-sd-pct">0%</span></label><div class="bar-outer"><div id="files-sd-bar" class="bar-inner" style="width:0%"></div></div></div>
        <div class="breadcrumb" id="files-bc">/ <span>/</span></div>
        <table><thead><tr><th>Name</th><th>Type</th><th>Size</th><th>Actions</th></tr></thead><tbody id="files-list"></tbody></table>
      `;
    }

    function wifiHTML() {
      return `
        <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:10px">
          <span style="font-weight:bold">Nearby Networks (auto-refresh)</span>
          <button class="std" id="wifi-now">Scan now</button>
        </div>
        <table><thead><tr><th>SSID</th><th>RSSI</th><th>Channel</th><th>Auth</th></tr></thead><tbody id="wifi-list"></tbody></table>
      `;
    }

    function settingsHTML() {
      return `
        <div style="max-height:320px;overflow:auto;padding-right:4px">
          <div class="field"><label>AP SSID</label><input id="set-ssid"></div>
          <div class="field"><label>AP Password</label><input id="set-pass" type="password"></div>
          <div class="field"><label>AP Channel</label><input id="set-channel" type="number" min="1" max="13"></div>
          <div class="field"><label>Admin Password</label><input id="set-admin" type="password"></div>
          <div class="field"><label>Buzzer GPIO</label><input id="set-buzz" type="number"></div>
          <div class="field"><label>STA SSID (internet)</label><input id="set-stassid" placeholder="leave blank to disable"></div>
          <div class="field"><label>STA Password</label><input id="set-stapass" type="password" placeholder="Wi-Fi password"></div>
          <div class="field"><label>NTP Server</label><input id="set-ntp" placeholder="pool.ntp.org"></div>
          <div class="field"><label>NTP Offset (hours)</label><input id="set-offset" type="number" min="-12" max="14"></div>
          <div class="field"><label>Wallpaper</label><input type="file" id="set-wall" accept="image/*"></div>
        </div>
        <button class="std" id="set-wall-btn" style="margin-top:5px;margin-bottom:10px">Set Wallpaper</button>
        <div style="display:flex;gap:8px;flex-wrap:wrap">
          <button class="std" id="set-save">Save</button>
          <button class="std" id="set-reboot" style="background:#ff6b6b">Reboot</button>
        </div>
        <p id="set-msg" style="margin-top:10px;color:#9aa3ad;font-size:12px"></p>
      `;
    }

    function attacksHTML() {
      return `
        <div style="margin-bottom:10px">
          <label style="font-size:12px;color:#9aa3ad">Attack type</label>
          <select id="atk-type" style="width:100%;margin-top:4px">
            <option value="deauth">Deauth target</option>
            <option value="deauthall">Deauth all</option>
            <option value="beacon">Beacon spam</option>
            <option value="probeflood">Probe flood</option>
            <option value="ssidspam">SSID spam</option>
            <option value="fakeap">Fake AP</option>
            <option value="karma">Karma</option>
            <option value="randomssid">Random SSIDs</option>
            <option value="authflood">Auth flood</option>
            <option value="assocflood">Assoc flood</option>
            <option value="pmkid">PMKID (experimental)</option>
            <option value="csa">CSA</option>
            <option value="quiet">Quiet</option>
          </select>
        </div>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:10px">
          <div class="field"><label>SSID(s)</label><input id="atk-ssid" placeholder="Free WiFi,Guest" value="Free WiFi"></div>
          <div class="field"><label>Channel</label><input id="atk-ch" type="number" value="6" min="1" max="13"></div>
          <div class="field"><label>Target AP index</label><input id="atk-idx" type="number" value="" placeholder="0..N or blank"></div>
          <div class="field"><label>BSSID / MAC</label><input id="atk-bssid" placeholder="aa:bb:cc:dd:ee:ff"></div>
          <div class="field"><label>Password</label><input id="atk-pass" type="password" placeholder="optional"></div>
          <div class="field"><label>Count</label><input id="atk-count" type="number" value="8" min="1" max="64"></div>
        </div>
        <div style="display:flex;gap:12px;align-items:center;margin-bottom:12px">
          <label><input id="atk-cycle" type="checkbox"> Cycle list</label>
          <label><input id="atk-hidden" type="checkbox"> Hidden</label>
          <label><input id="atk-hop" type="checkbox"> Hop channels</label>
        </div>
        <div style="display:flex;gap:8px;flex-wrap:wrap;margin-bottom:12px">
          <button class="std" id="atk-start" style="background:#ff6b6b">Start</button>
          <button class="std" id="atk-stop" style="background:#2b3039;color:#e0e6ed">Stop</button>
        </div>
        <div id="atk-status" style="background:#0f1115;border:1px solid #2b3039;border-radius:5px;padding:10px;font-family:monospace;font-size:12px">
          Status: idle
        </div>
      `;
    }

    function initAttacks(div) {
      const type = div.querySelector('#atk-type');
      const ssid = div.querySelector('#atk-ssid');
      const ch = div.querySelector('#atk-ch');
      const idx = div.querySelector('#atk-idx');
      const bssid = div.querySelector('#atk-bssid');
      const pass = div.querySelector('#atk-pass');
      const count = div.querySelector('#atk-count');
      const cycle = div.querySelector('#atk-cycle');
      const hidden = div.querySelector('#atk-hidden');
      const hop = div.querySelector('#atk-hop');
      const status = div.querySelector('#atk-status');

      function setStatus() {
        const s = window.lastAttack || { attack: 'idle', attack_pkts: 0, attack_ch: 0 };
        status.textContent = `Status: ${s.attack} | Pkts: ${s.attack_pkts} | Ch: ${s.attack_ch}`;
      }
      setInterval(setStatus, 1000);

      div.querySelector('#atk-start').addEventListener('click', () => {
        let cmd = `attack -t ${type.value} -ch ${ch.value}`;
        if (ssid.value.trim()) cmd += ` -s ${ssid.value}`;
        if (bssid.value.trim()) cmd += ` -b ${bssid.value}`;
        if (idx.value !== '') cmd += ` -a ${idx.value}`;
        if (pass.value.trim()) cmd += ` -p ${pass.value}`;
        if (count.value) cmd += ` -c ${count.value}`;
        if (cycle.checked) cmd += ' -l';
        if (hidden.checked) cmd += ' -h';
        if (hop.checked) cmd += ' -hop';
        sendCmd(cmd);
      });

      div.querySelector('#atk-stop').addEventListener('click', () => sendCmd('stopscan'));
    }

    function portalHTML() {
      return `
        <p style="font-size:12px;color:#9aa3ad;margin-bottom:10px">Only MAC/IP is logged. No credentials are captured.</p>
        <div class="field"><label>Portal HTML</label><textarea id="portal-html" rows="6" placeholder="Paste custom HTML or leave empty for default"></textarea></div>
        <div style="display:flex;gap:8px;flex-wrap:wrap;margin-bottom:12px">
          <button class="std" id="portal-save">Save to SD</button>
          <button class="std" id="portal-start" style="background:#ff6b6b">Start Portal</button>
          <button class="std" id="portal-stop" style="background:#2b3039;color:#e0e6ed">Stop Portal</button>
        </div>
        <p id="portal-status" style="font-size:12px;color:#9aa3ad"></p>
      `;
    }

    function initPortal(div) {
      const html = div.querySelector('#portal-html');
      const status = div.querySelector('#portal-status');
      div.querySelector('#portal-save').addEventListener('click', () => {
        if (!ws) { status.textContent = 'Not connected'; return; }
        ws.send(JSON.stringify({type:'portal', action:'save', html: html.value || ' '}));
        status.textContent = 'Saving...';
      });
      div.querySelector('#portal-start').addEventListener('click', () => sendCmd('portal start'));
      div.querySelector('#portal-stop').addEventListener('click', () => sendCmd('portal stop'));
    }

    function calcHTML() {
      return `
        <input id="calc-display" style="width:100%;margin-bottom:8px;background:#0f1115;color:#36d13c;border:1px solid #36d13c;padding:8px;font-family:monospace;text-align:right" readonly>
        <div style="display:grid;grid-template-columns:repeat(4,1fr);gap:6px;margin-bottom:12px">
          <button class="std" onclick="calcKey('C')">C</button><button class="std" onclick="calcKey('/')">/</button><button class="std" onclick="calcKey('*')">*</button><button class="std" onclick="calcKey('DEL')">DEL</button>
          <button class="std" onclick="calcKey('7')">7</button><button class="std" onclick="calcKey('8')">8</button><button class="std" onclick="calcKey('9')">9</button><button class="std" onclick="calcKey('-')">-</button>
          <button class="std" onclick="calcKey('4')">4</button><button class="std" onclick="calcKey('5')">5</button><button class="std" onclick="calcKey('6')">6</button><button class="std" onclick="calcKey('+')">+</button>
          <button class="std" onclick="calcKey('1')">1</button><button class="std" onclick="calcKey('2')">2</button><button class="std" onclick="calcKey('3')">3</button><button class="std" onclick="calcKey('=')">=</button>
          <button class="std" onclick="calcKey('0')" style="grid-column:span 2">0</button><button class="std" onclick="calcKey('.')">.</button><button class="std" onclick="calcKey('^')">^</button>
        </div>
        <div style="border-top:1px solid #2b3039;padding-top:12px">
          <div style="font-size:13px;color:#9aa3ad;margin-bottom:6px">Unit Converter</div>
          <div style="display:flex;gap:6px;align-items:center;flex-wrap:wrap">
            <select id="conv-cat" style="background:#0f1115;border:1px solid #2b3039;color:#e0e6ed;padding:6px;border-radius:5px"><option value="length">Length</option><option value="weight">Weight</option><option value="data">Data</option></select>
            <input id="conv-val" type="number" style="width:80px;background:#0f1115;border:1px solid #2b3039;color:#e0e6ed;padding:6px;border-radius:5px" value="1">
            <select id="conv-from" style="background:#0f1115;border:1px solid #2b3039;color:#e0e6ed;padding:6px;border-radius:5px"></select>
            <span>→</span>
            <select id="conv-to" style="background:#0f1115;border:1px solid #2b3039;color:#e0e6ed;padding:6px;border-radius:5px"></select>
            <button class="std" onclick="calcConvert()">Convert</button>
          </div>
          <div id="conv-res" style="margin-top:8px;font-family:monospace;color:#36d13c"></div>
        </div>
      `;
    }

    function noteHTML() {
      return `
        <div style="display:flex;gap:8px;margin-bottom:8px">
          <button class="std" id="note-load">Load notes.txt</button>
          <button class="std" id="note-save">Save notes.txt</button>
          <span id="note-status" style="font-size:12px;color:#9aa3ad"></span>
        </div>
        <textarea id="note-area" style="width:100%;height:200px;background:#0f1115;color:#e0e6ed;border:1px solid #36d13c;padding:8px;font-family:monospace" placeholder="Type notes here..."></textarea>
      `;
    }

    function guideHTML() {
      return `
        <div style="font-size:13px;line-height:1.5;overflow:auto;height:100%">
          <h4>Icy OS Guide</h4>
          <p>Welcome to the Icy OS Web OS. Use the Start menu to open apps.</p>
          <h5>Terminal</h5>
          <p>Linux-style commands: cd, pwd, ls, cat, head, tail, wc, find, grep, file, cp, mv, rm, mkdir, rmdir, touch, df, du, free, uptime, whoami, hostname, ifconfig, iw, neofetch, ps, env, echo, clear, history, help, man.</p>
          <h5>Wi-Fi Attacks</h5>
          <p>attack -t deauth|deauthall|beacon|probe|probeflood|ssidspam|fakeap|karma|randomssid|authflood|assocflood|pmkid|sae|csa|quiet|badmsg|sleep</p>
          <h5>File Manager</h5>
          <p>Upload, open, download, delete, rename, and create files/folders on the SD card.</p>
          <h5>Captive Portal</h5>
          <p>Start/Stop a benign portal from the Portal app. Only client IP is logged.</p>
          <h5>Settings</h5>
          <p>Change AP SSID, password, admin password, and buzzer GPIO. Reboot to apply.</p>
        </div>
      `;
    }

    function initCalc(div) {
      let expr = '';
      window.calcKey = (k) => {
        const disp = div.querySelector('#calc-display');
        if (k === 'C') expr = '';
        else if (k === 'DEL') expr = expr.slice(0, -1);
        else if (k === '=') {
          try { expr = String(Function('return ' + expr)()); }
          catch (e) { expr = 'Err'; }
        }
        else if (k === '^') expr += '**';
        else expr += k;
        disp.value = expr;
      };
      const units = {
        length: {m:1, km:1000, cm:0.01, mm:0.001, in:0.0254, ft:0.3048, yd:0.9144, mi:1609.34},
        weight: {kg:1, g:0.001, mg:0.000001, lb:0.453592, oz:0.0283495},
        data: {B:1/1024/1024, KB:1/1024, MB:1, GB:1024, TB:1024*1024}
      };
      const from = div.querySelector('#conv-from'), to = div.querySelector('#conv-to'), catSel = div.querySelector('#conv-cat');
      const build = (cat) => { from.innerHTML = ''; to.innerHTML = ''; Object.keys(units[cat]).forEach(u => { from.add(new Option(u, u)); to.add(new Option(u, u)); }); };
      catSel.addEventListener('change', () => build(catSel.value));
      build(catSel.value);
      const res = div.querySelector('#conv-res');
      window.calcConvert = () => {
        const v = parseFloat(div.querySelector('#conv-val').value);
        if (isNaN(v)) { res.textContent = 'Enter a number'; return; }
        const cat = catSel.value;
        const f = units[cat][from.value], t = units[cat][to.value];
        const out = v * f / t;
        res.textContent = v + ' ' + from.value + ' = ' + (out < 0.001 ? out.toExponential(3) : parseFloat(out.toPrecision(6))) + ' ' + to.value;
      };
    }

    function weatherHTML() {
      return `
        <div style="display:flex;gap:8px;margin-bottom:10px;align-items:center;flex-wrap:wrap">
          <input id="weather-city" value="Mumbai" style="flex:1;background:#0f1115;border:1px solid #2b3039;color:#e0e6ed;padding:6px;border-radius:5px">
          <button class="std" id="weather-go">Check</button>
        </div>
        <div id="weather-out" style="white-space:pre-wrap;font-family:monospace;font-size:13px;color:#e0e6ed;min-height:60px;background:#0f1115;border:1px solid #2b3039;padding:10px;border-radius:6px">Requires STA internet. Enter a city and press Check.</div>
      `;
    }
    function initWeather(div) {
      const out = div.querySelector('#weather-out');
      const go = div.querySelector('#weather-go');
      const city = div.querySelector('#weather-city');
      go.addEventListener('click', () => { out.textContent = 'Fetching...'; sendCmd('weather ' + city.value.trim()); });
      window.weatherResult = (text) => { out.textContent = text; };
    }

    function initNote(div) {
      const area = div.querySelector('#note-area');
      const status = div.querySelector('#note-status');
      div.querySelector('#note-load').addEventListener('click', async () => {
        try {
          const r = await fetch('/files?token=' + encodeURIComponent(adminToken) + '&path=/notes.txt');
          const t = await r.text();
          area.value = t;
          status.textContent = 'Loaded';
        } catch (e) { status.textContent = 'No notes yet'; }
      });
      div.querySelector('#note-save').addEventListener('click', async () => {
        try {
          const form = new FormData();
          const blob = new Blob([area.value], {type: 'text/plain'});
          form.append('file', blob, 'notes.txt');
          const r = await fetch('/fs/upload?token=' + encodeURIComponent(adminToken), {
            method: 'POST', body: form
          });
          const t = await r.text();
          status.textContent = (r.ok ? 'Saved: ' : 'Error: ') + t;
        } catch (e) { status.textContent = 'Save failed'; }
      });
    }

    function viewerHTML() {
      return `
        <div id="viewer-wrap" style="height:100%;display:flex;flex-direction:column">
          <div style="display:flex;gap:8px;margin-bottom:8px;align-items:center;flex-wrap:wrap">
            <button class="std" id="viewer-reload">Reload</button>
            <button class="std" id="viewer-zoom-in">+</button>
            <button class="std" id="viewer-zoom-out">-</button>
            <button class="std" id="viewer-rotate">⟳</button>
            <button class="std" id="viewer-setbg" style="background:#2b3039;color:#e0e6ed;display:none">Set as BG</button>
            <span id="viewer-path" style="font-size:12px;color:#9aa3ad;word-break:break-all"></span>
          </div>
          <div id="viewer-content" style="flex:1;overflow:auto;background:#0f1115;border:1px solid #36d13c;padding:8px;display:flex;align-items:center;justify-content:center"></div>
        </div>
      `;
    }

    function initViewer(div) {
      const content = div.querySelector('#viewer-content');
      const pathEl = div.querySelector('#viewer-path');
      const reload = div.querySelector('#viewer-reload');
      const setbg = div.querySelector('#viewer-setbg');
      setbg.addEventListener('click', () => sendCmd('setwall ' + window.viewerPath));
      const load = () => {
        const p = window.viewerPath || '/';
        pathEl.textContent = p;
        const ext = p.split('.').pop().toLowerCase();
        const img = ['jpg','jpeg','png','svg','gif','bmp','ico','webp'];
        const txt = ['txt','json','csv','html','js','css','ino','cpp','c','h','py','md','log','xml','yml','yaml','ini','cfg'];
        const url = '/files?token=' + encodeURIComponent(adminToken) + '&path=' + encodeURIComponent(p);
        content.innerHTML = '';
        setbg.style.display = img.includes(ext) ? 'inline-block' : 'none';
        if (img.includes(ext)) {
          const imgEl = document.createElement('img');
          imgEl.id = 'viewer-img';
          imgEl.src = url;
          imgEl.style.maxWidth = '100%';
          imgEl.style.transition = 'transform 0.2s ease';
          imgEl.style.transformOrigin = 'center center';
          content.appendChild(imgEl);
        } else if (txt.includes(ext)) {
          fetch(url).then(r => r.text()).then(t => {
            const ta = document.createElement('textarea');
            ta.value = t;
            ta.style.width = '100%';
            ta.style.height = '100%';
            ta.style.background = 'transparent';
            ta.style.color = '#e0e6ed';
            ta.style.border = 'none';
            ta.style.fontFamily = 'monospace';
            ta.readOnly = true;
            content.appendChild(ta);
          }).catch(e => { content.textContent = 'Error: ' + e; });
        } else {
          content.innerHTML = '<p>Binary/unknown file. <a href="' + url + '" download style="color:#36d13c">Download</a></p>';
        }
      };
      reload.addEventListener('click', load);
      let scale = 1, rot = 0;
      const apply = () => { const i = content.querySelector('#viewer-img'); if (i) i.style.transform = 'scale(' + scale + ') rotate(' + rot + 'deg)'; };
      div.querySelector('#viewer-zoom-in').addEventListener('click', () => { scale = Math.min(scale + 0.25, 4); apply(); });
      div.querySelector('#viewer-zoom-out').addEventListener('click', () => { scale = Math.max(scale - 0.25, 0.25); apply(); });
      div.querySelector('#viewer-rotate').addEventListener('click', () => { rot = (rot + 90) % 360; apply(); });
      load();
    }

    function networkHTML() {
      return `
        <div style="display:flex;gap:8px;margin-bottom:10px">
          <button class="std" id="net-scan">Scan</button>
          <button class="std" id="net-refresh" style="background:#2b3039;color:#e0e6ed">Refresh</button>
        </div>
        <div id="net-info" style="font-size:13px;margin-bottom:10px"></div>
        <table id="net-table"><thead><tr><th>SSID</th><th>RSSI</th><th>Ch</th><th>Auth</th></tr></thead><tbody id="net-list"></tbody></table>
      `;
    }

    function aboutHTML() {
      return `
        <div style="text-align:center;margin-bottom:16px">
          <h2 style="color:#36d13c;margin:0">Icy OS</h2>
          <p style="font-size:12px;color:#9aa3ad">portable into microcontroller</p>
        </div>
        <div class="meter"><label><span>Version</span><span id="about-ver">1.0.0</span></label></div>
        <div class="meter"><label><span>Uptime</span><span id="about-uptime">--</span></label></div>
        <div class="meter"><label><span>Free Heap</span><span id="about-heap">--</span></label></div>
        <div class="meter"><label><span>SD Total</span><span id="about-sd-total">--</span></label></div>
        <div class="meter"><label><span>SD Used</span><span id="about-sd-used">--</span></label></div>
        <div class="meter"><label><span>SD Free</span><span id="about-sd-free">--</span></label></div>
        <div class="meter"><label><span>AP Stations</span><span id="about-stations">--</span></label></div>
      `;
    }

    function clockHTML() {
      return `
        <div style="text-align:center;padding:10px">
          <div id="clock-time" style="font-size:42px;font-weight:bold;color:#36d13c;font-family:monospace">00:00:00</div>
          <div id="clock-date" style="font-size:14px;color:#9aa3ad;margin-top:6px">--</div>
        </div>
      `;
    }

    function taskHTML() {
      return `
        <table>
          <thead><tr><th>Process</th><th>State</th><th>Detail</th></tr></thead>
          <tbody>
            <tr><td>Attack</td><td id="task-attack-state">idle</td><td id="task-attack-detail">--</td></tr>
            <tr><td>Wi-Fi Scan</td><td id="task-scan-state">idle</td><td id="task-scan-detail">--</td></tr>
            <tr><td>Captive Portal</td><td id="task-portal-state">off</td><td id="task-portal-detail">--</td></tr>
            <tr><td>WebSocket</td><td id="task-ws-state">--</td><td id="task-ws-detail">--</td></tr>
            <tr><td>Heap Free</td><td id="task-heap">--</td><td id="task-uptime">--</td></tr>
          </tbody>
        </table>
      `;
    }

    function initNetwork(div) {
      div.querySelector('#net-scan').addEventListener('click', () => sendCmd('scanall'));
      div.querySelector('#net-refresh').addEventListener('click', () => {
        const d = window.lastSysInfo || {};
        div.querySelector('#net-info').innerHTML = `<b>AP:</b> ${d.ap_ssid || 'Icy-OS'}<br><b>IP:</b> ${d.ap_ip || '--'}<br><b>MAC:</b> ${d.ap_mac || '--'}<br><b>Stations:</b> ${d.stations ?? 0}`;
      });
    }

    function initAbout(div) { /* data comes from sysinfo */ }
    function initClock(div) {
      const t = div.querySelector('#clock-time');
      const d = div.querySelector('#clock-date');
      const update = () => {
        const now = new Date();
        t.textContent = now.toLocaleTimeString('en-GB', {hour12:false});
        d.textContent = now.toLocaleDateString('en-GB', {weekday:'long', day:'numeric', month:'long', year:'numeric'});
      };
      update();
      div._clock = setInterval(update, 1000);
    }
    function initTask(div) { /* data comes from sysinfo */ }

    function gameHTML() {
      return `
        <div style="display:flex;gap:8px;margin-bottom:10px;align-items:center">
          <span>Score: <span id="game-score">0 - 0</span></span>
          <button class="std" id="game-start">Start / Pause</button>
          <span style="font-size:12px;color:#9aa3ad">Use ↑/↓ or W/S keys</span>
        </div>
        <canvas id="game-canvas" width="480" height="300" style="background:#0f1115;border:1px solid #36d13c;border-radius:6px;display:block;margin:0 auto"></canvas>
      `;
    }

    function initGame(div) {
      const canvas = div.querySelector('#game-canvas');
      const ctx = canvas.getContext('2d');
      const scoreEl = div.querySelector('#game-score');
      let running = false, paused = false;
      let ball = {x:240,y:150,dx:3,dy:2}, paddleH=60, pw=10;
      let p1={y:120,score:0}, p2={y:120,score:0};
      let keys={};
      const loop = () => {
        if (!running || paused) { draw(); if (running) requestAnimationFrame(loop); return; }
        if (keys['w'] || keys['ArrowUp']) p1.y -= 6;
        if (keys['s'] || keys['ArrowDown']) p1.y += 6;
        p2.y += (ball.y - (p2.y + paddleH/2)) * 0.08;
        p1.y = Math.max(0, Math.min(300 - paddleH, p1.y));
        p2.y = Math.max(0, Math.min(300 - paddleH, p2.y));
        ball.x += ball.dx; ball.y += ball.dy;
        if (ball.y <= 0 || ball.y >= 300) ball.dy = -ball.dy;
        let p = ball.dx < 0 ? p1 : p2;
        let px = ball.dx < 0 ? 20 : 460 - pw;
        if (ball.x >= px && ball.x <= px + pw && ball.y >= p.y && ball.y <= p.y + paddleH) {
          ball.dx = -ball.dx * 1.05;
          ball.dy += (Math.random() - 0.5) * 2;
        }
        if (ball.x < 0) { p2.score++; resetBall(); }
        if (ball.x > 480) { p1.score++; resetBall(); }
        scoreEl.textContent = p1.score + ' - ' + p2.score;
        draw();
        requestAnimationFrame(loop);
      };
      const resetBall = () => { ball = {x:240,y:150,dx:(Math.random()>0.5?3:-3)*(1+Math.random()*0.2),dy:(Math.random()-0.5)*4}; };
      const draw = () => {
        ctx.fillStyle = '#0f1115'; ctx.fillRect(0,0,480,300);
        ctx.strokeStyle = '#36d13c'; ctx.beginPath(); ctx.moveTo(240,0); ctx.lineTo(240,300); ctx.stroke();
        ctx.fillStyle = '#36d13c'; ctx.fillRect(20, p1.y, pw, paddleH); ctx.fillRect(460 - pw, p2.y, pw, paddleH);
        ctx.fillRect(ball.x - 4, ball.y - 4, 8, 8);
      };
      resetBall(); draw();
      div.querySelector('#game-start').addEventListener('click', () => {
        if (!running) { running = true; paused = false; loop(); }
        else { paused = !paused; if (!paused) loop(); }
      });
      window.addEventListener('keydown', e => { keys[e.key] = true; });
      window.addEventListener('keyup', e => { keys[e.key] = false; });
    }

    function renderNetworks(list) {
      const tbody = document.getElementById('net-list');
      if (!tbody) return;
      tbody.innerHTML = '';
      list.forEach(n => {
        const tr = document.createElement('tr');
        tr.innerHTML = `<td>${n.ssid || '(hidden)'}</td><td>${n.rssi} dBm</td><td>${n.channel}</td><td>${n.auth}</td>`;
        tbody.appendChild(tr);
      });
    }

    // --- App logic ---
    function updateSysInfo(d) {
      if (!d) return;
      try {
      const used = d.heap_total - d.heap_free;
      const pct = d.heap_total ? Math.round(used / d.heap_total * 100) : 0;
      const set = (id, v) => { const el = document.getElementById(id); if (el) el.textContent = v; };
      const setw = (id, v) => { const el = document.getElementById(id); if (el) el.style.width = v; };
      set('heap-pct', pct + '%');
      setw('heap-bar', pct + '%');
      set('uptime-val', formatUptime(d.uptime));
      set('clients-val', d.clients);
      set('stations-val', d.stations);
      set('rssi-val', d.rssi + ' dBm');
      set('sd-val', d.sd ? 'OK' : 'Error');
      set('sd-total', formatBytes((d.sd_total ?? 0) * 1024));
      set('sd-used',  formatBytes((d.sd_used  ?? 0) * 1024));
      set('sd-free',  formatBytes((d.sd_free  ?? 0) * 1024));
      const usedPct = d.sd_total ? Math.round((d.sd_used / d.sd_total) * 100) : 0;
      const fsb = document.getElementById('files-sd-bar');
      const fsp = document.getElementById('files-sd-pct');
      if (fsb) fsb.style.width = usedPct + '%';
      if (fsp) fsp.textContent = usedPct + '%';
      set('attack-val', d.attack || 'idle');
      set('attack-pkts-val', d.attack_pkts ?? 0);
      set('attack-ch-val', d.attack_ch ?? 0);
      window.lastSysInfo = d;
      if (d.attack) window.lastAttack = d;

      set('heap-status', 'Heap: ' + Math.round(d.heap_free/1024) + 'K');
      set('stations-status', 'Stations: ' + d.stations);
      const sd = document.getElementById('sd-status');
      if (sd) { sd.textContent = 'SD: ' + (d.sd ? 'OK' : 'ERR'); sd.className = d.sd ? 'good' : 'bad'; }

      // About app
      set('about-uptime', formatUptime(d.uptime));
      set('about-heap', Math.round(d.heap_free/1024) + ' KB');
      set('about-sd-total', formatBytes((d.sd_total ?? 0) * 1024));
      set('about-sd-used',  formatBytes((d.sd_used  ?? 0) * 1024));
      set('about-sd-free',  formatBytes((d.sd_free  ?? 0) * 1024));
      set('about-stations', d.stations);

      // Task Manager
      set('task-attack-state', d.attack && d.attack !== 'idle' ? 'running' : 'idle');
      set('task-attack-detail', (d.attack || 'idle') + ' ch' + (d.attack_ch ?? 0) + ' pkts ' + (d.attack_pkts ?? 0));
      set('task-scan-state', d.rssi > -100 ? 'ready' : 'none');
      set('task-scan-detail', 'best rssi ' + (d.rssi ?? '--') + ' dBm');
      set('task-portal-state', d.portal ? 'running' : 'off');
      set('task-portal-detail', d.portal ? 'http://192.168.4.1/' : '--');
      set('task-ws-state', 'clients');
      set('task-ws-detail', d.clients ?? 0);
      set('task-heap', Math.round(d.heap_free/1024) + ' KB');
      set('task-uptime', formatUptime(d.uptime));
      } catch (e) { console.warn('sysinfo update error', e); }
    }

    function formatUptime(s) {
      const d = Math.floor(s/86400), h = Math.floor((s%86400)/3600), m = Math.floor((s%3600)/60);
      return (d?d+'d ':'') + (h?h+'h ':'') + m + 'm ' + (s%60)+'s';
    }

    function appendTerminal(text, isPrompt) {
      const out = document.getElementById('terminal-output');
      if (!out) return;
      const p = document.createElement('div');
      p.textContent = (isPrompt ? 'root@esp32-os:~# ' : '') + text;
      p.style.color = isPrompt ? '#36d13c' : '#e0e6ed';
      if (!isPrompt) p.style.marginLeft = '12px';
      out.appendChild(p);
      out.scrollTop = out.scrollHeight;
    }

    const COMMANDS = ['help','man','ls','cd','pwd','cat','head','tail','wc','find','grep','file','cp','mv','rm','mkdir','rmdir','touch','readfile','df','du','free','uptime','whoami','hostname','ifconfig','iw','neofetch','ps','env','echo','clear','history','sysinfo','scanall','list -a','select -a','clearlist -a','clients','wardrive','arpscan','pingscan','portscan','stopscan','wifi scan','beep','ota','reboot','settings get','settings set','portal start','portal stop','attack -t deauth','attack -t deauthall','attack -t beacon','attack -t probe','attack -t probeflood','attack -t ssidspam','attack -t fakeap','attack -t karma','attack -t randomssid','attack -t authflood','attack -t assocflood','attack -t pmkid','attack -t csa','attack -t quiet','attack -t badmsg','attack -t sleep'];

    function initTerminal(div) {
      const out = div.querySelector('#terminal-output');
      const inp = div.querySelector('#term-cmd');
      const dl = document.getElementById('cmd-suggestions');
      if (dl) {
        COMMANDS.forEach(c => { const o = document.createElement('option'); o.value = c; dl.appendChild(o); });
      }
      const send = () => {
        const v = inp.value.trim();
        if (!v) return;
        if (v === 'clear') {
          out.innerHTML = '';
          inp.value = '';
          return;
        }
        if (v === 'history') {
          appendTerminal('Command history:', false);
          termHistory.forEach((h, i) => appendTerminal(i + ': ' + h, false));
          inp.value = '';
          return;
        }
        termHistory.push(v);
        histIdx = termHistory.length;
        appendTerminal(v, true);
        sendCmd(v);
        inp.value = '';
      };
      div.querySelector('#term-send').addEventListener('click', send);
      inp.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') send();
        else if (e.key === 'ArrowUp') {
          if (histIdx > 0) { histIdx--; inp.value = termHistory[histIdx]; }
        } else if (e.key === 'ArrowDown') {
          if (histIdx < termHistory.length - 1) { histIdx++; inp.value = termHistory[histIdx]; }
          else { histIdx = termHistory.length; inp.value = ''; }
        }
      });
    }

    function renderFiles(list, path) {
      if (currentPath !== (path || '/')) filesHistory.push(currentPath);
      currentPath = path || '/';
      const tbody = document.getElementById('files-list');
      if (!tbody) return;
      tbody.innerHTML = '';
      const parts = currentPath.split('/').filter(Boolean);
      let cp = '';
      let bc = '<span data-path="/">/</span>';
      parts.forEach(s => { cp += '/' + s; bc += ' / <span data-path="' + cp + '">' + s + '</span>'; });
      document.getElementById('files-bc').innerHTML = bc;
      document.querySelectorAll('#files-bc span').forEach(sp => {
        if (sp.dataset.path) sp.addEventListener('click', () => sendCmd('ls ' + sp.dataset.path));
      });
      list.forEach(f => {
        const tr = document.createElement('tr');
        const full = (currentPath === '/' ? '' : currentPath) + '/' + f.name;
        const ext = f.name.split('.').pop().toLowerCase();
        const img = ['jpg','jpeg','png','svg','gif','bmp','ico','webp'].includes(ext);
        let actions = '';
        if (!f.dir) {
          actions += `<button class="std" style="padding:2px 6px;font-size:11px" onclick="window.viewerPath='${full}'; openWindow('viewer')">Open</button> `;
          if (img) actions += `<button class="std" style="padding:2px 6px;font-size:11px;background:#2b3039;color:#e0e6ed" onclick="sendCmd('setwall ${full}')">Set BG</button> `;
          actions += `<a href="/files?token=${encodeURIComponent(adminToken)}&path=${encodeURIComponent(full)}" download style="text-decoration:none"><button class="std" style="padding:2px 6px;font-size:11px">DL</button></a> `;
          actions += `<button class="std" style="padding:2px 6px;font-size:11px" onclick="sendCmd('rm ${full}')">Del</button> `;
          actions += `<button class="std" style="padding:2px 6px;font-size:11px" onclick="const n=prompt('Rename to','${f.name}'); if(n) sendCmd('mv ${full} ' + (currentPath==='/' ? '' : currentPath) + '/' + n)">Ren</button>`;
        }
        tr.innerHTML = `<td>${f.name}</td><td>${f.dir ? 'dir' : 'file'}</td><td>${f.dir ? '-' : formatBytes(f.size)}</td><td>${actions}</td>`;
        if (f.dir) {
          tr.style.cursor = 'pointer';
          tr.addEventListener('dblclick', () => {
            const np = (currentPath === '/' ? '' : currentPath) + '/' + f.name;
            sendCmd('ls ' + np);
          });
        }
        tbody.appendChild(tr);
      });
    }

    function initFiles(div) {
      const btn = div.querySelector('#files-upload-btn');
      const input = div.querySelector('#files-file');
      const status = div.querySelector('#files-up-status');
      btn.addEventListener('click', async () => {
        const file = input.files[0];
        if (!file) { status.textContent = 'No file selected'; return; }
        const form = new FormData();
        form.append('file', file, file.name);
        status.textContent = 'Uploading...';
        try {
          const resp = await fetch('http://192.168.4.1/fs/upload?token=' + encodeURIComponent(adminToken), {
            method: 'POST',
            body: form
          });
          const text = await resp.text();
          status.textContent = (resp.ok ? 'OK: ' : 'Error: ') + text;
          if (resp.ok) {
            input.value = '';
            sendCmd('ls ' + currentPath);
            if (file.name.toLowerCase() === 'wallpaper.jpg') {
              document.body.style.backgroundImage = "url('/wallpaper.jpg?t=" + Date.now() + "')";
            }
          }
        } catch (e) {
          status.textContent = 'Upload failed: ' + e;
        }
      });
      div.querySelector('#files-mkdir').addEventListener('click', () => {
        const n = prompt('Folder name?');
        if (n) sendCmd('mkdir ' + (currentPath === '/' ? '' : currentPath) + '/' + n);
      });
      div.querySelector('#files-touch').addEventListener('click', () => {
        const n = prompt('File name?');
        if (n) sendCmd('touch ' + (currentPath === '/' ? '' : currentPath) + '/' + n);
      });
      div.querySelector('#files-back').addEventListener('click', () => {
        if (filesHistory && filesHistory.length) {
          const p = filesHistory.pop();
          sendCmd('ls ' + p);
        }
      });
      div.querySelector('#files-up').addEventListener('click', () => {
        const p = currentPath === '/' ? '/' : currentPath.substring(0, currentPath.lastIndexOf('/')) || '/';
        sendCmd('ls ' + p);
      });
      div.querySelector('#files-refresh').addEventListener('click', () => {
        sendCmd('ls ' + currentPath);
      });
      sendCmd('ls /');
    }

    function formatBytes(b) {
      if (b < 1024) return b + ' B';
      if (b < 1048576) return (b/1024).toFixed(1) + ' KB';
      return (b/1048576).toFixed(1) + ' MB';
    }

    function renderNetworks(list) {
      const tbody = document.getElementById('wifi-list');
      if (!tbody) return;
      tbody.innerHTML = '';
      list.forEach(n => {
        const tr = document.createElement('tr');
        const pct = Math.min(100, Math.max(0, (n.rssi + 90) * 100 / 60));
        const color = n.rssi > -60 ? '#51cf66' : n.rssi > -75 ? '#ffd43b' : '#ff6b6b';
        tr.innerHTML = `
          <td>${n.ssid || '<hidden>'}</td>
          <td><div class="bar-outer"><div class="bar-inner" style="width:${pct}%;background:${color}"></div></div> ${n.rssi}</td>
          <td>${n.channel}</td>
          <td>${n.auth}</td>
        `;
        tbody.appendChild(tr);
      });
    }

    function initWifi(div) {
      div.querySelector('#wifi-now').addEventListener('click', () => sendCmd('wifi scan'));
    }

    function fillSettings(d) {
      const div = document.getElementById('win-settings');
      if (!div) return;
      const set = (id, v) => { const el = document.getElementById(id); if (el) el.value = (v === undefined || v === null) ? '' : v; };
      set('set-ssid', d.ssid);
      set('set-pass', d.password);
      set('set-channel', d.channel);
      set('set-admin', d.adminPass);
      set('set-buzz', d.buzzerGPIO);
      set('set-stassid', d.staSSID);
      set('set-stapass', d.staPassword);
      set('set-ntp', d.ntpServer);
      set('set-offset', d.ntpOffset);
      const msg = document.getElementById('set-msg');
      if (msg) msg.textContent = 'STA: ' + (d.staStatus || 'unknown') + (d.staIP ? ' ' + d.staIP : '');
    }

    function initSettings(div) {
      ws.send(JSON.stringify({type:'settings', action:'get'}));
      div.querySelector('#set-save').addEventListener('click', () => {
        const payload = {
          ssid: document.getElementById('set-ssid').value,
          password: document.getElementById('set-pass').value,
          channel: parseInt(document.getElementById('set-channel').value, 10),
          adminPass: document.getElementById('set-admin').value,
          buzzerGPIO: parseInt(document.getElementById('set-buzz').value, 10),
          staSSID: document.getElementById('set-stassid').value,
          staPassword: document.getElementById('set-stapass').value,
          ntpServer: document.getElementById('set-ntp').value,
          ntpOffset: parseInt(document.getElementById('set-offset').value, 10)
        };
        ws.send(JSON.stringify({type:'settings', action:'set', value: payload}));
        document.getElementById('set-msg').textContent = 'Saving...';
      });
      div.querySelector('#set-reboot').addEventListener('click', () => sendCmd('reboot'));
      div.querySelector('#set-wall-btn').addEventListener('click', async () => {
        const input = div.querySelector('#set-wall');
        const file = input.files[0];
        const msg = document.getElementById('set-msg');
        if (!file) { msg.textContent = 'No image selected'; return; }
        const form = new FormData();
        form.append('file', file, 'wallpaper.jpg');
        msg.textContent = 'Uploading wallpaper...';
        try {
          const resp = await fetch('http://192.168.4.1/fs/upload?token=' + encodeURIComponent(adminToken), { method: 'POST', body: form });
          const text = await resp.text();
          msg.textContent = (resp.ok ? 'Wallpaper set. ' : 'Error: ') + text;
          if (resp.ok) {
            input.value = '';
            document.body.style.backgroundImage = "url('/wallpaper.jpg?t=" + Date.now() + "')";
          }
        } catch (e) {
          msg.textContent = 'Wallpaper upload failed: ' + e;
        }
      });
    }

    // --- Toasts ---
    function toast(msg, type = 'info') {
      const box = document.createElement('div');
      box.className = 'toast' + (type === 'error' ? ' toast-error' : '');
      box.textContent = msg;
      document.body.appendChild(box);
      setTimeout(() => {
        box.style.opacity = '0';
        setTimeout(() => box.remove(), 300);
      }, 3000);
    }

    // --- Virtual keyboard ---
    let activeInput = null;
    let shiftOn = false;
    const kbLayout = [
      ['`','1','2','3','4','5','6','7','8','9','0','-','=','Backspace'],
      ['q','w','e','r','t','y','u','i','o','p','[',']','\\'],
      ['a','s','d','f','g','h','j','k','l',';','\'','Enter'],
      ['z','x','c','v','b','n','m',',','.','/','Shift','Space']
    ];
    function kbInsert(text) {
      if (!activeInput) return;
      if (text === 'Backspace') {
        activeInput.value = activeInput.value.slice(0, -1);
      } else if (text === 'Enter') {
        if (activeInput.id === 'term-cmd') {
          const v = activeInput.value.trim();
          if (v) sendCmd(v);
          activeInput.value = '';
        } else if (activeInput.tagName === 'TEXTAREA') {
          activeInput.value += '\n';
        } else {
          activeInput.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', bubbles: true}));
        }
      } else if (text === 'Space') {
        activeInput.value += ' ';
      } else if (text === 'Shift') {
        shiftOn = !shiftOn;
      } else {
        activeInput.value += shiftOn ? text.toUpperCase() : text;
      }
      activeInput.focus();
    }
    // --- Snake ---
    function snakeHTML() {
      return `
        <div style="display:flex;gap:8px;margin-bottom:10px;align-items:center;flex-wrap:wrap">
          <span>Score: <span id="snake-score">0</span></span>
          <span id="snake-status" style="font-size:13px;color:#9aa3ad"></span>
          <button class="std" id="snake-start">Start</button>
          <span style="font-size:12px;color:#9aa3ad">Arrows or swipe</span>
        </div>
        <canvas id="snake-canvas" width="300" height="300" style="background:#0f1115;border:1px solid #36d13c;border-radius:6px;display:block;margin:0 auto"></canvas>
      `;
    }
    function initSnake(div) {
      const canvas = div.querySelector('#snake-canvas'), ctx = canvas.getContext('2d');
      const scoreEl = div.querySelector('#snake-score'), status = div.querySelector('#snake-status');
      let running = false, paused = false, s, food, dir, nextDir, score, timer;
      const reset = () => { s = [{x:10,y:10}]; dir={x:1,y:0}; nextDir={x:1,y:0}; food={x:15,y:15}; score=0; scoreEl.textContent=0; status.textContent=''; draw(); };
      const spawnFood = () => { do { food={x:Math.floor(Math.random()*15),y:Math.floor(Math.random()*15)}; } while (s.some(p=>p.x===food.x&&p.y===food.y)); };
      const draw = () => {
        ctx.fillStyle='#0f1115'; ctx.fillRect(0,0,300,300);
        ctx.fillStyle='#36d13c'; s.forEach(p=>ctx.fillRect(p.x*20,p.y*20,18,18));
        ctx.fillStyle='#ff6b6b'; ctx.fillRect(food.x*20,food.y*20,18,18);
      };
      const step = () => {
        if (!running) return;
        if (paused) { timer = setTimeout(step, 120); return; }
        dir = nextDir;
        const head = {x: s[0].x+dir.x, y: s[0].y+dir.y};
        if (head.x<0||head.x>=15||head.y<0||head.y>=15 || s.some(p=>p.x===head.x&&p.y===head.y)) {
          running=false; status.textContent='Game over!'; status.style.color='#ff6b6b'; clearTimeout(timer); return;
        }
        s.unshift(head);
        if (head.x===food.x && head.y===food.y) { score+=10; scoreEl.textContent=score; spawnFood(); }
        else s.pop();
        draw();
        timer = setTimeout(step, 120);
      };
      div.querySelector('#snake-start').addEventListener('click', () => {
        if (!running) { running=true; paused=false; step(); }
        else { paused=!paused; if (!paused) step(); }
      });
      window.addEventListener('keydown', e => { if (!running||paused) return; if (e.key==='ArrowUp'&&dir.y===0) nextDir={x:0,y:-1}; if (e.key==='ArrowDown'&&dir.y===0) nextDir={x:0,y:1}; if (e.key==='ArrowLeft'&&dir.x===0) nextDir={x:-1,y:0}; if (e.key==='ArrowRight'&&dir.x===0) nextDir={x:1,y:0}; });
      let sx,sy;
      canvas.addEventListener('touchstart', e => { const t=e.touches[0]; sx=t.clientX; sy=t.clientY; }, {passive:true});
      canvas.addEventListener('touchend', e => { if (!running||paused) return; const t=e.changedTouches[0]; const dx=t.clientX-sx, dy=t.clientY-sy; if (Math.abs(dx)>Math.abs(dy)) { if (dx>20&&dir.x===0) nextDir={x:1,y:0}; if (dx<-20&&dir.x===0) nextDir={x:-1,y:0}; } else { if (dy>20&&dir.y===0) nextDir={x:0,y:1}; if (dy<-20&&dir.y===0) nextDir={x:0,y:-1}; } }, {passive:true});
      reset();
    }

    // --- Tic-Tac-Toe ---
    function tictactoeHTML() {
      return `
        <div style="display:flex;gap:8px;margin-bottom:10px;align-items:center">
          <span id="ttt-status">Your turn (X)</span>
          <button class="std" id="ttt-reset">Reset</button>
        </div>
        <div id="ttt-grid" style="display:grid;grid-template-columns:repeat(3,80px);gap:6px;justify-content:center"></div>
      `;
    }
    function initTicTacToe(div) {
      const grid = div.querySelector('#ttt-grid'), status = div.querySelector('#ttt-status');
      let board = Array(9).fill(''), player='X', gameOver=false;
      const wins = [[0,1,2],[3,4,5],[6,7,8],[0,3,6],[1,4,7],[2,5,8],[0,4,8],[2,4,6]];
      const check = (who) => wins.some(w=>w.every(i=>board[i]===who));
      const ai = () => {
        if (gameOver || player !== 'O') return;
        const empty = board.map((v,i)=>v?-1:i).filter(i=>i!==-1);
        const best = empty.find(i=>{ board[i]='O'; const w=check('O'); board[i]=''; return w; });
        const block = empty.find(i=>{ board[i]='X'; const w=check('X'); board[i]=''; return w; });
        const i = best!==undefined ? best : block!==undefined ? block : empty[Math.floor(Math.random()*empty.length)];
        if (i!==undefined) {
          board[i]='O';
          if (check('O')) { status.textContent='O wins!'; status.style.color='#ff6b6b'; gameOver=true; }
          else if (!board.includes('')) { status.textContent='Draw'; status.style.color='#9aa3ad'; gameOver=true; }
          else { player='X'; status.textContent='Your turn (X)'; status.style.color='#e0e6ed'; }
          render();
        }
      };
      const render = () => {
        grid.innerHTML='';
        board.forEach((v,i)=>{
          const b=document.createElement('button'); b.className='std'; b.style.width='80px'; b.style.height='80px'; b.style.fontSize='28px'; b.textContent=v;
          b.addEventListener('click', () => {
            if (v||gameOver||player!=='X') return;
            board[i]='X';
            if (check('X')) { status.textContent='You win!'; status.style.color='#36d13c'; gameOver=true; render(); return; }
            if (!board.includes('')) { status.textContent='Draw'; status.style.color='#9aa3ad'; gameOver=true; render(); return; }
            player='O'; status.textContent='Computer thinking...'; setTimeout(ai, 300);
            render();
          });
          grid.appendChild(b);
        });
      };
      div.querySelector('#ttt-reset').addEventListener('click', () => { board=Array(9).fill(''); player='X'; gameOver=false; status.textContent='Your turn (X)'; status.style.color='#e0e6ed'; render(); });
      render();
    }

    // --- Minesweeper ---
    function minesweeperHTML() {
      return `
        <div style="display:flex;gap:8px;margin-bottom:10px;align-items:center;flex-wrap:wrap">
          <span>Flags: <span id="ms-mines">10</span></span>
          <span id="ms-status" style="font-size:13px"></span>
          <button class="std" id="ms-reset">New</button>
        </div>
        <div id="ms-grid" style="display:grid;grid-template-columns:repeat(8,34px);gap:3px;justify-content:center"></div>
      `;
    }
    function initMinesweeper(div) {
      const grid = div.querySelector('#ms-grid'), status = div.querySelector('#ms-status'), minesEl = div.querySelector('#ms-mines');
      const W=8, H=8, M=10; let cells=[], revealed=[], flagged=[], over=false;
      const reset = () => {
        cells=Array(H).fill(0).map(()=>Array(W).fill(0)); revealed=Array(H).fill(0).map(()=>Array(W).fill(false)); flagged=Array(H).fill(0).map(()=>Array(W).fill(false)); over=false; status.textContent=''; status.style.color='#e0e6ed';
        let m=0; while (m<M) { const r=Math.floor(Math.random()*H), c=Math.floor(Math.random()*W); if (!cells[r][c]) { cells[r][c]=-1; m++; } }
        for (let r=0;r<H;r++) for (let c=0;c<W;c++) if (cells[r][c]!==-1) {
          let n=0; for (let dr=-1;dr<=1;dr++) for (let dc=-1;dc<=1;dc++) { const nr=r+dr,nc=c+dc; if (nr>=0&&nr<H&&nc>=0&&nc<W&&cells[nr][nc]===-1) n++; }
          cells[r][c]=n;
        }
        minesEl.textContent=M; render();
      };
      const reveal = (r,c) => {
        if (r<0||r>=H||c<0||c>=W||revealed[r][c]||flagged[r][c]) return;
        revealed[r][c]=true;
        if (cells[r][c]===-1) { over=true; status.textContent='Game over'; status.style.color='#ff6b6b'; return; }
        if (cells[r][c]===0) for (let dr=-1;dr<=1;dr++) for (let dc=-1;dc<=1;dc++) reveal(r+dr,c+dc);
      };
      const checkWin = () => { for (let r=0;r<H;r++) for (let c=0;c<W;c++) if (cells[r][c]!==-1 && !revealed[r][c]) return false; return true; };
      const render = () => {
        grid.innerHTML='';
        for (let r=0;r<H;r++) for (let c=0;c<W;c++) {
          const b=document.createElement('button'); b.className='std'; b.style.width='34px'; b.style.height='34px'; b.style.fontSize='12px'; b.style.padding='0';
          if (flagged[r][c]) { b.textContent='🚩'; b.style.background='#2b3039'; }
          else if (revealed[r][c]) {
            if (cells[r][c]===-1) { b.textContent='💣'; b.style.background='#ff6b6b'; }
            else { b.textContent=cells[r][c]||''; b.style.background='#0f1115'; }
          }
          const doClick = () => { if (over||flagged[r][c]) return; if (flagged[r][c]) { flagged[r][c]=false; minesEl.textContent=+minesEl.textContent+1; render(); return; } reveal(r,c); if (!over && checkWin()) { over=true; status.textContent='You win!'; status.style.color='#36d13c'; } render(); };
          b.addEventListener('click', doClick);
          b.addEventListener('contextmenu', e => { e.preventDefault(); if (over||revealed[r][c]) return; flagged[r][c]=!flagged[r][c]; minesEl.textContent=flagged[r][c] ? +minesEl.textContent-1 : +minesEl.textContent+1; render(); });
          let long;
          b.addEventListener('touchstart', () => { long=setTimeout(() => { if (over||revealed[r][c]) return; flagged[r][c]=!flagged[r][c]; minesEl.textContent=flagged[r][c] ? +minesEl.textContent-1 : +minesEl.textContent+1; render(); }, 500); });
          b.addEventListener('touchend', () => clearTimeout(long));
          grid.appendChild(b);
        }
      };
      div.querySelector('#ms-reset').addEventListener('click', reset);
      reset();
    }

    function initKeyboard() {
      const kb = document.createElement('div');
      kb.id = 'virtual-keyboard';
      kb.style.display = 'none';
      let html = '<div id="kb-close">x</div>';
      kbLayout.forEach(row => {
        html += '<div class="kb-row">';
        row.forEach(k => {
          const label = k === 'Backspace' ? '⌫' : k === 'Enter' ? '↵' : k === 'Shift' ? '⇧' : k === 'Space' ? '␣' : k;
          html += `<button type="button" class="kb-key" data-key="${k}">${label}</button>`;
        });
        html += '</div>';
      });
      kb.innerHTML = html;
      document.body.appendChild(kb);
      kb.addEventListener('click', e => {
        if (e.target.id === 'kb-close') { kb.style.display = 'none'; return; }
        if (e.target.classList.contains('kb-key')) kbInsert(e.target.dataset.key);
      });
      window.addEventListener('focusin', e => {
        if (['INPUT','TEXTAREA'].includes(e.target.tagName)) {
          activeInput = e.target;
          kb.style.display = 'flex';
        }
      });
      window.addEventListener('click', e => {
        if (e.target.id === 'kb-close' || e.target.closest && e.target.closest('#virtual-keyboard')) return;
        if (['INPUT','TEXTAREA'].includes(e.target.tagName)) return;
        if (kb.style.display === 'flex') kb.style.display = 'none';
      });
    }

    initKeyboard();

    if ('serviceWorker' in navigator) {
      navigator.serviceWorker.register('service-worker.js').catch(() => {});
    }
