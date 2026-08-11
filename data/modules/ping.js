(function() {
  window.icyModules = window.icyModules || {};
  window.icyModules.ping = {
    name: 'Ping',
    html: `
      <div style="display:flex;gap:8px;margin-bottom:10px;align-items:center;flex-wrap:wrap">
        <input id="ping-host" value="8.8.8.8" style="flex:1;background:#0f1115;border:1px solid #2b3039;color:#e0e6ed;padding:6px;border-radius:5px">
        <button class="std" id="ping-go">Ping</button>
      </div>
      <div id="ping-out" style="white-space:pre-wrap;font-family:monospace;font-size:13px;color:#e0e6ed;min-height:60px;background:#0f1115;border:1px solid #2b3039;padding:10px;border-radius:6px">Enter a host and press Ping. Requires STA internet.</div>
    `,
    init: function(div, ctx) {
      const out = div.querySelector('#ping-out');
      const go = div.querySelector('#ping-go');
      const host = div.querySelector('#ping-host');
      go.addEventListener('click', () => {
        out.textContent = 'Pinging ' + host.value.trim() + '...';
        if (ctx && ctx.sendCmd) ctx.sendCmd('ping ' + host.value.trim());
      });
      window._pingCb = (text) => { out.textContent = text; };
    }
  };
})();
