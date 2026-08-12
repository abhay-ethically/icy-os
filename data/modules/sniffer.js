(function() {
  window.icyModules = window.icyModules || {};
  window.icyModules.sniffer = {
    name: 'Sniffer',
    icon: '🦈',
    html: `
      <div style="display:flex;gap:8px;margin-bottom:10px;align-items:center;flex-wrap:wrap">
        <label>Ch</label>
        <input id="sniff-ch" type="number" min="1" max="13" value="1" style="width:60px;background:#0f1115;border:1px solid #2b3039;color:#e0e6ed;padding:6px;border-radius:5px">
        <label>Sec</label>
        <input id="sniff-sec" type="number" min="5" max="300" value="30" style="width:70px;background:#0f1115;border:1px solid #2b3039;color:#e0e6ed;padding:6px;border-radius:5px">
        <button class="std" id="sniff-go" style="background:var(--accent, #36d13c);color:#000">Start</button>
        <button class="std" id="sniff-stop" style="background:#2b3039;color:#e0e6ed">Stop</button>
        <button class="std" id="sniff-refresh">Refresh</button>
      </div>
      <div id="sniff-status" style="font-size:13px;color:#9aa3ad;margin-bottom:10px">Ready. Captures saved to /captures.</div>
      <div class="table-wrap"><table id="sniff-table" style="display:none"><thead><tr><th>Capture</th><th>Size</th><th>Actions</th></tr></thead><tbody id="sniff-list"></tbody></table></div>
      <div id="sniff-empty" style="color:#9aa3ad;font-size:13px">No captures yet.</div>
    `,
    init: function(div, ctx) {
      const ch = div.querySelector('#sniff-ch');
      const sec = div.querySelector('#sniff-sec');
      const status = div.querySelector('#sniff-status');
      const list = div.querySelector('#sniff-list');
      const table = div.querySelector('#sniff-table');
      const empty = div.querySelector('#sniff-empty');

      function formatBytes(b) {
        if (b < 1024) return b + ' B';
        if (b < 1048576) return (b/1024).toFixed(1) + ' KB';
        return (b/1048576).toFixed(1) + ' MB';
      }

      function renderFiles(msg) {
        if (!msg.data || msg.path !== '/captures') return;
        const files = msg.data.filter(f => f.type === 'file' && f.name.endsWith('.pcap'));
        if (files.length === 0) {
          table.style.display = 'none';
          empty.style.display = 'block';
        } else {
          table.style.display = 'table';
          empty.style.display = 'none';
          list.innerHTML = files.map(f => {
            const url = '/files?path=/captures/' + encodeURIComponent(f.name) + '&token=' + encodeURIComponent(ctx.adminToken || '');
            return `<tr><td>${f.name}</td><td>${formatBytes(f.size)}</td><td><a href="${url}" download class="std" style="padding:4px 8px;font-size:12px">Download</a></td></tr>`;
          }).join('');
        }
      }

      div.querySelector('#sniff-go').addEventListener('click', () => {
        const channel = parseInt(ch.value, 10) || 1;
        const seconds = parseInt(sec.value, 10) || 30;
        window._snifferCb = (text) => { status.textContent = text; };
        ctx.sendCmd('sniff -c ' + channel + ' -t ' + seconds);
        status.textContent = 'Starting capture on channel ' + channel + ' for ' + seconds + ' s...';
      });

      div.querySelector('#sniff-stop').addEventListener('click', () => {
        ctx.sendCmd('sniff stop');
        status.textContent = 'Stopped.';
      });

      div.querySelector('#sniff-refresh').addEventListener('click', () => {
        window._filesCb = (msg) => renderFiles(msg);
        ctx.sendCmd('ls /captures');
      });

      window._filesCb = (msg) => renderFiles(msg);
      ctx.sendCmd('ls /captures');
    }
  };
})();
