(function() {
  window.icyModules = window.icyModules || {};
  window.icyModules.curlview = {
    name: 'CurlView',
    icon: '🌐',
    html: `
      <div style="display:flex;gap:8px;margin-bottom:10px;align-items:center;flex-wrap:wrap">
        <input id="curl-url" value="http://example.com" style="flex:1;background:#0f1115;border:1px solid #2b3039;color:#e0e6ed;padding:6px;border-radius:5px">
        <button class="std" id="curl-go">Fetch</button>
      </div>
      <div id="curl-out" style="white-space:pre-wrap;font-family:monospace;font-size:13px;color:#e0e6ed;min-height:60px;background:#0f1115;border:1px solid #2b3039;padding:10px;border-radius:6px;overflow:auto;-webkit-overflow-scrolling:touch;max-height:260px">Enter a URL and press Fetch. Requires STA internet.</div>
    `,
    init: function(div, ctx) {
      const out = div.querySelector('#curl-out');
      const go = div.querySelector('#curl-go');
      const url = div.querySelector('#curl-url');
      go.addEventListener('click', () => {
        out.textContent = 'Fetching ' + url.value.trim() + '...';
        window._curlCallback = (text) => { out.textContent = text; };
        if (ctx && ctx.sendCmd) ctx.sendCmd('curl ' + url.value.trim());
      });
    }
  };
})();
