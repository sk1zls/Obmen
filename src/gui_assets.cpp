#include "gui_assets.h"

const char* kIndexHtml = R"HTML(<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>Обмен — общие папки в локальной сети</title>
<link rel="stylesheet" href="/app.css"/>
</head>
<body>
<svg class="sprite" aria-hidden="true" focusable="false"><defs>
  <symbol id="i-folder" viewBox="0 0 24 24">
    <path d="M3.2 7.4A2.4 2.4 0 0 1 5.6 5h3.3c.6 0 1.2.25 1.6.7l1 1a2.4 2.4 0 0 0 1.7.7h5.2A2.4 2.4 0 0 1 20.8 9.8v7.6a2.4 2.4 0 0 1-2.4 2.4H5.6a2.4 2.4 0 0 1-2.4-2.4z"/>
  </symbol>
  <symbol id="i-file" viewBox="0 0 24 24">
    <path d="M6.8 3.6h7.4L19.2 8.6v11.8H6.8z"/>
    <path d="M14.2 3.6v5h5"/>
  </symbol>
  <symbol id="i-file-text" viewBox="0 0 24 24">
    <path d="M6.8 3.6h7.4L19.2 8.6v11.8H6.8z"/>
    <path d="M14.2 3.6v5h5"/>
    <path d="M9.4 12.4h7.4M9.4 16.2h4.8"/>
  </symbol>
  <symbol id="i-file-code" viewBox="0 0 24 24">
    <path d="M6.8 3.6h7.4L19.2 8.6v11.8H6.8z"/>
    <path d="M14.2 3.6v5h5"/>
    <path d="M11.3 12.6 9.3 15l2 2.4M15.1 12.6 17.1 15l-2 2.4"/>
  </symbol>
  <symbol id="i-book" viewBox="0 0 24 24">
    <path d="M4.2 5.8a7.4 7.4 0 0 1 7 .6v13.2a7.4 7.4 0 0 0-7-.6z"/>
    <path d="M19.8 5.8a7.4 7.4 0 0 0-7 .6v13.2a7.4 7.4 0 0 1 7-.6z"/>
  </symbol>
  <symbol id="i-image" viewBox="0 0 24 24">
    <rect x="3.4" y="5" width="17.2" height="14" rx="2.4"/>
    <circle cx="8.9" cy="9.9" r="1.7"/>
    <path d="M4.2 17.4 8.6 13a1.8 1.8 0 0 1 2.5 0l3 3 1.9-1.9a1.8 1.8 0 0 1 2.5 0l1.9 1.9"/>
  </symbol>
  <symbol id="i-video" viewBox="0 0 24 24">
    <rect x="3.4" y="5.4" width="17.2" height="13.2" rx="2.4"/>
    <path d="M10.4 9.2 15.4 12l-5 2.8z" fill="currentColor" stroke="none"/>
  </symbol>
  <symbol id="i-audio" viewBox="0 0 24 24">
    <circle cx="7.2" cy="16.9" r="2.1"/>
    <circle cx="16.4" cy="15" r="2.1"/>
    <path d="M9.3 16.9V6.6l9.2-2v10.4"/>
  </symbol>
  <symbol id="i-archive" viewBox="0 0 24 24">
    <rect x="3.4" y="4.4" width="17.2" height="4.6" rx="1.6"/>
    <path d="M4.9 9v9.2a1.8 1.8 0 0 0 1.8 1.8h10.6a1.8 1.8 0 0 0 1.8-1.8V9"/>
    <path d="M10.6 12.6h2.8"/>
  </symbol>
  <symbol id="i-app" viewBox="0 0 24 24">
    <rect x="3.4" y="4.6" width="17.2" height="14.8" rx="2.4"/>
    <path d="M3.4 9.2h17.2"/>
    <circle cx="6.6" cy="7" r=".85" fill="currentColor" stroke="none"/>
    <circle cx="9.3" cy="7" r=".85" fill="currentColor" stroke="none"/>
  </symbol>
  <symbol id="i-disk" viewBox="0 0 24 24">
    <rect x="3.4" y="6" width="17.2" height="12" rx="2.4"/>
    <path d="M3.4 13.2h17.2"/>
    <circle cx="17" cy="15.6" r="1.05" fill="currentColor" stroke="none"/>
  </symbol>
  <symbol id="i-up" viewBox="0 0 24 24">
    <path d="M12 19V5.4M6.4 11 12 5.4 17.6 11"/>
  </symbol>
  <symbol id="i-close" viewBox="0 0 24 24">
    <path d="M7.2 7.2 16.8 16.8M16.8 7.2 7.2 16.8"/>
  </symbol>
  <symbol id="i-up-right" viewBox="0 0 24 24">
    <path d="M7 17 17 7M9.6 7H17v7.4"/>
  </symbol>
  <symbol id="i-down-left" viewBox="0 0 24 24">
    <path d="M17 7 7 17M14.4 17H7V9.6"/>
  </symbol>
  <symbol id="i-swap" viewBox="0 0 24 24">
    <path d="M3.8 9.2h13.4M14 5.6 17.6 9.2 14 12.8"/>
    <path d="M20.2 14.8H6.8M10 11.2 6.4 14.8 10 18.4"/>
  </symbol>
  <symbol id="i-updown" viewBox="0 0 24 24">
    <path d="M8.4 19.6V6M4.8 9.6 8.4 6l3.6 3.6"/>
    <path d="M15.6 4.4V18M12 14.4 15.6 18l3.6-3.6"/>
  </symbol>
</defs></svg>
<header class="topbar">
  <div class="brand">
    <div class="logo"><svg class="ic" aria-hidden="true"><use href="#i-swap"></use></svg></div>
    <div class="brandtext"><b>Обмен</b><small id="nodeName">запуск…</small></div>
  </div>
  <div class="netinfo">
    <span class="chip"><i class="dot" id="myDot"></i><span id="chipAddr">—</span></span>
    <span class="chip" id="chipPeers">0 устройств</span>
    <label class="switch"><input type="checkbox" id="syncToggle" checked/><span class="slider"></span>Автосинхронизация</label>
    <button class="btn" id="btnSync" title="Отправить все папки этого устройства другим устройствам сети">Синхронизировать</button>
  </div>
</header>

<div class="syncbar" id="syncbar">
  <span class="synclabel" id="syncLabel">Синхронизация</span>
  <div class="syncmeter"><div class="syncfill" id="syncFill"></div></div>
  <span class="syncstat" id="syncStat">—</span>
</div>

<main class="layout">
  <aside class="side">
    <section class="card">
      <h2>Сетевые папки <span class="muted" id="shareCount">0</span></h2>
      <ul class="shares" id="shareList"></ul>
      <div class="rootwarn" id="shareWarn" hidden></div>
      <button class="btn wide" id="btnAddShare">+ Добавить папку</button>
      <button class="btn ghost wide" id="btnOpenRoot">Открыть в проводнике</button>
    </section>

    <section class="card">
      <h2>Устройства <span class="muted" id="peerCount">0</span></h2>
      <ul class="peers" id="peerList"></ul>
      <form class="addr" id="addPeerForm">
        <input id="peerAddr" placeholder="192.168.0.10:8080" autocomplete="off"/>
        <button class="btn small" type="submit">Добавить</button>
      </form>
    </section>

    <section class="card">
      <h2>Сервер</h2>
      <dl class="kv">
        <dt>HTTP</dt><dd id="srvHttp">—</dd>
        <dt>FTP</dt><dd id="srvFtp">—</dd>
        <dt>UDP</dt><dd id="srvUdp">—</dd>
        <dt>IP</dt><dd id="srvIp">—</dd>
        <dt>Принято</dt><dd id="statIn">0</dd>
        <dt>Отдано</dt><dd id="statOut">0</dd>
      </dl>
    </section>
  </aside>

  <section class="content">
    <div class="card grow">
      <div class="toolbar">
        <button class="btn ghost small" id="btnUp" title="На уровень выше"><svg class="ic" aria-hidden="true"><use href="#i-up"></use></svg></button>
        <nav class="crumbs" id="crumbs"></nav>
        <input class="search" id="search" placeholder="Фильтр по имени…"/>
        <button class="btn ghost small" id="btnMkdir">+ Папка</button>
        <button class="btn ghost small" id="btnRefresh">Обновить</button>
      </div>

      <div class="dropzone" id="drop">
        <span id="dropHint">Перетащите файлы сюда — они появятся в общей папке. Нажмите, чтобы выбрать вручную.</span>
      </div>
      <div class="uploads" id="uploads"></div>

      <div class="tablewrap">
        <table class="files">
          <thead><tr><th>Имя</th><th class="num">Размер</th><th>Изменён</th><th class="act"></th></tr></thead>
          <tbody id="fileRows"></tbody>
        </table>
        <div class="empty" id="emptyFiles" hidden>Папка пуста</div>
      </div>
    </div>

    <div class="card">
      <h2>Журнал обмена <button class="btn ghost small" id="btnClearLog">Очистить</button></h2>
      <ul class="log" id="log"></ul>
    </div>
  </section>
</main>

<div class="modal" id="modalRoot" hidden>
  <div class="modal-card">
    <h3>Добавить сетевую папку</h3>
    <div class="places" id="browsePlaces"></div>
    <div class="browse-path" id="browseCurrent">—</div>
    <ul class="dirlist" id="browseList"></ul>
    <div class="matches" id="browseMatches" hidden></div>
    <div class="sharename-row">
      <label for="browseName">Имя папки в списке</label>
      <input id="browseName" autocomplete="off" spellcheck="false"/>
    </div>
    <div class="sharename-row">
      <label for="browseKey">Ключ синхронизации</label>
      <input id="browseKey" placeholder="номер или имя папки на другом ПК (пусто — по имени)"
             autocomplete="off" spellcheck="false"/>
    </div>
    <div class="pairhint" id="browseHint"></div>
    <div class="modal-actions">
      <button class="btn ghost" id="btnBrowseCancel">Отмена</button>
      <button class="btn" id="btnBrowsePick">Сделать общей</button>
    </div>
  </div>
</div>

<div class="toasts" id="toasts"></div>
<script src="/app.js"></script>
</body>
</html>
)HTML";

const char* kAppCss = R"CSS(:root{
  --bg:#080b10; --panel:#111823; --panel2:#0d131c; --line:#1d2836;
  --txt:#e8eef7; --mut:#8ba0b6; --acc:#2fd3b6; --acc2:#5b8cff;
  --warn:#ffb454; --err:#ff6b6b; --ok:#38d39f;
}
*{box-sizing:border-box}
html,body{height:100%}
body{
  margin:0; color:var(--txt); font:14px/1.45 "Segoe UI",Roboto,Arial,sans-serif;
  background:
    radial-gradient(1200px 620px at 82% -12%, #14243a 0%, rgba(8,11,16,0) 62%),
    radial-gradient(900px 500px at 6% 108%, #10242a 0%, rgba(8,11,16,0) 60%),
    var(--bg);
  min-height:100vh;
}
h2{font-size:13px;text-transform:uppercase;letter-spacing:.09em;color:var(--mut);margin:0 0 12px;display:flex;align-items:center;gap:8px;justify-content:space-between}
h3{font-size:16px;margin:0}
a{color:var(--acc2);text-decoration:none}
.muted{color:var(--mut)}
.pad{padding:10px}

/* ---------- top bar ---------- */
.topbar{
  position:sticky;top:0;z-index:30;display:flex;align-items:center;justify-content:space-between;
  gap:16px;padding:12px 22px;border-bottom:1px solid var(--line);
  background:rgba(8,11,16,.82);backdrop-filter:blur(10px);
}
.brand{display:flex;align-items:center;gap:12px}
.logo{
  width:40px;height:40px;border-radius:12px;display:grid;place-items:center;font-size:21px;
  font-weight:700;color:#04121a;background:linear-gradient(140deg,var(--acc),var(--acc2));
  box-shadow:0 6px 20px rgba(47,211,182,.22);
}
.logo .ic{width:23px;height:23px;stroke-width:2.1;color:#04121a}
.brandtext{display:flex;flex-direction:column;line-height:1.2}
.brandtext b{font-size:16px;letter-spacing:.02em}
.brandtext small{color:var(--mut);font-size:12px}
.netinfo{display:flex;align-items:center;gap:10px;flex-wrap:wrap}
.chip{
  display:inline-flex;align-items:center;gap:7px;padding:7px 11px;border-radius:999px;
  background:var(--panel2);border:1px solid var(--line);font-size:12.5px;color:#c6d4e4;
}
.dot{width:8px;height:8px;border-radius:50%;background:var(--ok);box-shadow:0 0 10px var(--ok);display:inline-block}
.dot.off{background:#42505f;box-shadow:none}
.dot.bad{background:var(--err);box-shadow:0 0 10px var(--err)}
.dot.warn{background:var(--warn);box-shadow:0 0 10px var(--warn)}

/* ---------- статус-строка синхронизации (внизу окна) ---------- */
body{padding-bottom:54px}
.syncbar{
  position:fixed;left:0;right:0;bottom:0;z-index:40;display:flex;align-items:center;gap:12px;
  padding:11px 22px;border-top:1px solid var(--line);background:rgba(9,13,19,.95);
  backdrop-filter:blur(10px);font-size:12.5px;color:#c6d4e4;
}
.syncbar .synclabel{font-weight:600;color:#e8eef7;white-space:nowrap}
.syncmeter{position:relative;flex:1;height:8px;border-radius:999px;background:#16202d;border:1px solid var(--line);overflow:hidden}
.syncfill{position:absolute;left:0;top:0;bottom:0;width:0;border-radius:999px;background:linear-gradient(90deg,var(--acc),var(--acc2));transition:width .35s ease}
.syncstat{color:var(--mut);white-space:nowrap}
.syncbar.busy .synclabel{color:var(--acc)}
.syncbar.busy .syncstat{color:#a9bccf}
.syncbar.err .syncfill{background:linear-gradient(90deg,var(--warn),var(--err))}
.syncbar.off .syncfill{background:#233040}
.syncbar.off .synclabel{color:var(--mut)}
.syncbar.checking .syncfill{
  width:100%!important;
  background:linear-gradient(90deg,rgba(47,211,182,0),var(--acc),rgba(47,211,182,0));
  background-size:240% 100%;animation:syncslide 1.15s linear infinite;
}
@keyframes syncslide{from{background-position:140% 0}to{background-position:-140% 0}}

/* ---------- buttons ---------- */
.btn{
  border:1px solid transparent;border-radius:9px;padding:8px 13px;cursor:pointer;
  background:linear-gradient(160deg,var(--acc),#1cb49b);color:#04120f;font-weight:600;font-size:13px;
  transition:.15s ease;font-family:inherit;
}
.btn:hover{filter:brightness(1.08);transform:translateY(-1px)}
.btn:active{transform:none}
.btn.ghost{background:var(--panel2);border-color:var(--line);color:#cfdcea;font-weight:500}
.btn.ghost:hover{background:#16202d;border-color:#2b3c4e}
.btn.small{padding:5px 9px;font-size:12px}
.btn .ic{width:16px;height:16px;vertical-align:-3px;stroke-width:2}
.btn.wide{width:100%;margin-top:8px}
.btn.danger{color:var(--err)}
.btn.danger:hover{border-color:#5a2830;background:#241318}

/* ---------- switch ---------- */
.switch{display:inline-flex;align-items:center;gap:9px;font-size:12.5px;color:#c6d4e4;cursor:pointer;user-select:none}
.switch input{display:none}
.slider{width:38px;height:21px;border-radius:999px;background:#243244;position:relative;transition:.18s}
.slider::after{content:"";position:absolute;width:15px;height:15px;border-radius:50%;background:#8ba0b6;top:3px;left:3px;transition:.18s}
.switch input:checked + .slider{background:rgba(47,211,182,.28)}
.switch input:checked + .slider::after{background:var(--acc);left:20px;box-shadow:0 0 8px var(--acc)}

/* ---------- layout ---------- */
.layout{display:grid;grid-template-columns:322px minmax(0,1fr);gap:18px;padding:18px 22px 28px;align-items:start}
.side{display:flex;flex-direction:column;gap:16px}
.content{display:flex;flex-direction:column;gap:16px;min-width:0}
.card{
  background:linear-gradient(180deg,rgba(22,31,43,.92),rgba(14,20,28,.92));
  border:1px solid var(--line);border-radius:15px;padding:16px;
  box-shadow:0 10px 30px rgba(0,0,0,.22);
}
.card.grow{display:flex;flex-direction:column;min-height:420px}

/* ---------- сетевые папки ---------- */
.shares{list-style:none;margin:0 0 4px;padding:0;display:flex;flex-direction:column;gap:6px;max-height:230px;overflow:auto}
.share{
  display:flex;align-items:center;gap:9px;padding:9px 10px;border-radius:10px;
  background:var(--panel2);border:1px solid var(--line);cursor:pointer;transition:.14s;
}
.share:hover{border-color:#2b3c4e;background:#16202d}
.share.active{border-color:rgba(47,211,182,.55);background:rgba(47,211,182,.09)}
.shareinfo{display:flex;flex-direction:column;min-width:0;flex:1}
.shareinfo b{font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.shareinfo small{color:var(--mut);font-size:11px;font-family:ui-monospace,Consolas,monospace;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;direction:rtl;text-align:left}
.share .ico{width:16px;text-align:center}
.pairsel{
  background:var(--panel2);color:var(--txt);border:1px solid var(--line);border-radius:8px;
  font-size:11px;padding:3px 2px;max-width:104px;
}
.rootwarn{
  margin:6px 0 2px;padding:8px 10px;border-radius:9px;font-size:12px;
  background:rgba(255,107,107,.12);border:1px solid #5a2830;color:#ffb1b1;
}

/* ---------- peers ---------- */
.peers{list-style:none;margin:0;padding:0;display:flex;flex-direction:column;gap:8px;max-height:250px;overflow:auto}
.peer{display:flex;align-items:center;gap:10px;padding:9px 10px;border-radius:10px;background:var(--panel2);border:1px solid var(--line)}
.peerinfo{display:flex;flex-direction:column;min-width:0;flex:1}
.peerinfo b{font-size:13px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.peerinfo small{color:var(--mut);font-size:11.5px;font-family:ui-monospace,Consolas,monospace}
.peerinfo small.peershares{font-family:inherit;font-size:11px;color:#7f95ab;overflow:hidden;text-overflow:ellipsis}
.peer .btn{margin-left:auto}

.addr{display:flex;gap:8px;margin-top:10px}
.addr input{flex:1}

input[type=text],input:not([type]),.search,.addr input{
  background:var(--panel2);border:1px solid var(--line);border-radius:9px;color:var(--txt);
  padding:8px 11px;font:inherit;font-size:13px;outline:none;transition:.15s;
}
input:focus{border-color:var(--acc);box-shadow:0 0 0 3px rgba(47,211,182,.12)}

.kv{display:grid;grid-template-columns:auto 1fr;gap:7px 12px;margin:0;font-size:12.5px}
.kv dt{color:var(--mut)}
.kv dd{margin:0;text-align:right;font-family:ui-monospace,Consolas,monospace;color:#c9d8e8}

/* ---------- toolbar ---------- */
.toolbar{display:flex;align-items:center;gap:9px;flex-wrap:wrap;margin-bottom:12px}
.crumbs{display:flex;align-items:center;gap:5px;flex-wrap:wrap;flex:1;min-width:120px}
.crumb{
  background:var(--panel2);border:1px solid var(--line);color:#cfdcea;border-radius:8px;
  padding:4px 9px;font-size:12.5px;cursor:pointer;font-family:inherit;
}
.crumb:hover{border-color:var(--acc);color:var(--acc)}
.crumb.sharename{border-color:rgba(91,140,255,.4);color:#a9c2ff}
.sep{color:#3f5164}
.search{min-width:130px;max-width:210px;flex:1}

/* ---------- drop zone ---------- */
.dropzone{
  border:1.5px dashed #2b3d50;border-radius:12px;padding:16px;text-align:center;
  color:var(--mut);font-size:13px;cursor:pointer;transition:.15s;margin-bottom:12px;
  background:rgba(13,19,28,.6);
}
.dropzone:hover{border-color:var(--acc);color:#bfeee6}
.dropzone.over{border-color:var(--acc);background:rgba(47,211,182,.09);color:#d6fff7}

.uploads{display:flex;flex-direction:column;gap:7px;margin-bottom:10px}
.upload{display:flex;align-items:center;gap:10px;font-size:12.5px;color:#c6d4e4}
.upload .uname{flex:0 0 34%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-family:ui-monospace,Consolas,monospace}
.upload .bar{flex:1;height:6px;border-radius:999px;background:#1b2532;overflow:hidden}
.upload .fill{height:100%;width:0;background:linear-gradient(90deg,var(--acc2),var(--acc));transition:width .15s}
.upload .pct{width:38px;text-align:right;color:var(--mut);font-variant-numeric:tabular-nums}

/* ---------- table ---------- */
.tablewrap{overflow:auto;flex:1;border-radius:10px}
table.files{width:100%;border-collapse:collapse;font-size:13px}
table.files thead th{
  position:sticky;top:0;background:#101823;text-align:left;color:var(--mut);font-weight:600;
  font-size:11.5px;text-transform:uppercase;letter-spacing:.07em;padding:9px 10px;border-bottom:1px solid var(--line);
}
table.files td{padding:9px 10px;border-bottom:1px solid rgba(29,40,54,.62);vertical-align:middle}
table.files tbody tr:hover{background:rgba(91,140,255,.07)}
tr.rowdir{cursor:pointer}
tr.rowdir td:first-child{color:#a9e7dc}
td.num{text-align:right;font-variant-numeric:tabular-nums;color:#c9d8e8;white-space:nowrap}
td.act{text-align:right;white-space:nowrap}
td.act .btn{margin-left:6px}
.ico{display:inline-block;margin-right:8px;width:18px;text-align:center;vertical-align:-3px}

/* ---------- иконки ---------- */
.sprite{position:absolute;width:0;height:0;overflow:hidden}
.ic{
  width:18px;height:18px;display:inline-block;vertical-align:-3.5px;
  fill:none;stroke:currentColor;stroke-width:1.7;stroke-linecap:round;stroke-linejoin:round;
}
.ic.sm{width:14px;height:14px;vertical-align:-2px;stroke-width:1.9}
.ic.folder{color:#5fd7c4}
.ic.text,.ic.file,.ic.app,.ic.code{color:#a9bccf}
.ic.book{color:#ff9d9d}
.ic.image{color:#8fc7ff}
.ic.video{color:#c79bff}
.ic.audio{color:#ffc07a}
.ic.archive{color:#ffb454}
.ic.disk{color:#9fb4cc}
.ic.up{color:#8ba0b6}
.empty{padding:26px;text-align:center;color:var(--mut);font-style:italic}

/* ---------- log ---------- */
.log{list-style:none;margin:0;padding:0;display:flex;flex-direction:column;gap:6px;max-height:210px;overflow:auto}
.tx{display:flex;align-items:center;gap:9px;font-size:12.5px;padding:7px 9px;border-radius:9px;background:var(--panel2);border:1px solid rgba(29,40,54,.8)}
.txic{width:20px;display:inline-flex;align-items:center;justify-content:center}
.txic .ic{width:15px;height:15px;stroke-width:2}
.txic.up{color:var(--acc2)}
.txic.down{color:var(--acc)}
.txic.sync{color:var(--warn)}
.txic.ftp{color:#c58cff}
.txfile{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-family:ui-monospace,Consolas,monospace}
.txmeta{color:var(--mut);font-size:11.5px;white-space:nowrap}
.txstate{font-size:11px;padding:2px 7px;border-radius:999px;white-space:nowrap}
.txstate.done{background:rgba(56,211,159,.15);color:var(--ok)}
.txstate.active{background:rgba(91,140,255,.16);color:#9dbcff}
.txstate.error{background:rgba(255,107,107,.15);color:var(--err)}

/* ---------- modal ---------- */
.modal{position:fixed;inset:0;z-index:60;background:rgba(3,6,10,.72);display:grid;place-items:center;padding:20px;backdrop-filter:blur(3px)}
.modal[hidden]{display:none}
.modal-card{
  width:min(680px,100%);background:linear-gradient(180deg,#141d28,#0e141c);
  border:1px solid var(--line);border-radius:16px;padding:20px;box-shadow:0 24px 70px rgba(0,0,0,.55);
  display:flex;flex-direction:column;gap:14px;max-height:86vh;
}
.browse-path{
  font-family:ui-monospace,Consolas,monospace;font-size:12.5px;color:#9fe9dc;
  background:var(--panel2);border:1px solid var(--line);border-radius:9px;padding:9px 11px;word-break:break-all;
}
.dirlist{list-style:none;margin:0;padding:0;overflow:auto;max-height:46vh;display:flex;flex-direction:column;gap:4px}
.dirlist .dir{padding:9px 11px;border-radius:9px;cursor:pointer;border:1px solid transparent;font-size:13px}
.dirlist .dir:hover{background:rgba(47,211,182,.08);border-color:rgba(47,211,182,.25)}
.places{display:flex;flex-wrap:wrap;gap:6px}
.place{
  display:inline-flex;align-items:center;gap:6px;padding:6px 11px;border-radius:999px;
  background:var(--panel2);border:1px solid var(--line);color:#cfdcea;font:inherit;font-size:12.5px;
  cursor:pointer;transition:.14s;
}
.place:hover{border-color:var(--acc);color:var(--acc)}
.place.active{border-color:rgba(47,211,182,.55);background:rgba(47,211,182,.12);color:#bdf3ea}
.place.busy{opacity:.55;cursor:not-allowed}
.peertag{font-size:10.5px;color:var(--mut);background:rgba(255,255,255,.05);border-radius:6px;padding:1px 5px;margin-left:2px}
.place .ic{width:15px;height:15px;stroke-width:1.8}
.matches{display:flex;flex-wrap:wrap;align-items:center;gap:6px;font-size:12px;color:var(--mut)}
.matches .place{font-size:12px;padding:4px 9px}
.sharename-row{display:flex;align-items:center;gap:10px}
.sharename-row label{font-size:12px;color:var(--mut);white-space:nowrap}
.sharename-row input{flex:1}
.pairhint{font-size:12px;color:var(--mut);line-height:1.4}
.pairhint.warn{color:var(--warn)}
.pairhint.ok{color:var(--ok)}
.shareinfo .nopair{color:var(--warn);font-size:11px;font-family:inherit;direction:ltr;text-align:left}
.shareinfo .pairkey{color:var(--ok);font-size:11px;font-family:inherit;direction:ltr;text-align:left}
.modal-actions{display:flex;justify-content:flex-end;gap:9px;padding-top:4px}

/* ---------- toasts ---------- */
.toasts{position:fixed;right:18px;bottom:66px;z-index:80;display:flex;flex-direction:column;gap:8px;max-width:340px}
.toast{
  padding:11px 14px;border-radius:10px;background:#16202d;border:1px solid var(--line);
  font-size:13px;box-shadow:0 12px 30px rgba(0,0,0,.45);animation:in .18s ease;
  border-left:3px solid var(--acc2);
}
.toast.ok{border-left-color:var(--ok)}
.toast.err{border-left-color:var(--err)}
.toast.out{opacity:0;transform:translateY(6px);transition:.28s}
@keyframes in{from{opacity:0;transform:translateY(8px)}to{opacity:1;transform:none}}

@media (max-width:960px){
  .layout{grid-template-columns:1fr}
  .topbar{flex-wrap:wrap}
  .netinfo{width:100%;justify-content:flex-start}
}
)CSS";

const char* kAppJs = R"JS((function () {
  'use strict';

  var el = function (id) { return document.getElementById(id); };
  var S = { status: null, share: '', shares: [], sharesSig: '', pairSig: '', path: '', files: [], browse: '' };
  // Идентификаторы символов SVG-спрайта из начала страницы.
  var SYM = {
    folder: 'folder', text: 'file-text', book: 'book', image: 'image', video: 'video',
    audio: 'audio', archive: 'archive', app: 'app', code: 'file-code', file: 'file',
    disk: 'disk', up: 'up', close: 'close',
    'up-right': 'up-right', 'down-left': 'down-left', swap: 'swap', updown: 'updown'
  };

  function svgIcon(type, extra) {
    var id = SYM[type] || 'file';
    return '<svg class="ic ' + type + (extra ? ' ' + extra : '') +
      '" aria-hidden="true"><use href="#i-' + id + '"></use></svg>';
  }

  function iconType(name, isDir) {
    if (isDir) return 'folder';
    var i = name.lastIndexOf('.');
    if (i < 0) return 'file';
    var e = name.slice(i + 1).toLowerCase();
    if (e === 'txt' || e === 'md' || e === 'log' || e === 'ini' || e === 'csv') return 'text';
    if (e === 'pdf' || e === 'doc' || e === 'docx' || e === 'rtf' || e === 'odt') return 'book';
    if (e === 'png' || e === 'jpg' || e === 'jpeg' || e === 'gif' || e === 'svg' || e === 'webp' || e === 'bmp' || e === 'ico') return 'image';
    if (e === 'mp4' || e === 'mkv' || e === 'avi' || e === 'mov' || e === 'webm' || e === 'wmv') return 'video';
    if (e === 'mp3' || e === 'wav' || e === 'flac' || e === 'ogg' || e === 'm4a' || e === 'aac') return 'audio';
    if (e === 'zip' || e === 'rar' || e === '7z' || e === 'gz' || e === 'tar' || e === 'bz2' || e === 'xz') return 'archive';
    if (e === 'exe' || e === 'msi' || e === 'bat' || e === 'cmd' || e === 'dll' || e === 'lnk') return 'app';
    if (e === 'c' || e === 'cpp' || e === 'h' || e === 'hpp' || e === 'js' || e === 'ts' || e === 'css' || e === 'html' || e === 'json' || e === 'py' || e === 'ps1' || e === 'xml' || e === 'yml' || e === 'yaml' || e === 'sh') return 'code';
    return 'file';
  }

  function fmtSize(n) {
    n = Number(n) || 0;
    if (n <= 0) return '0 Б';
    var u = ['Б', 'КБ', 'МБ', 'ГБ', 'ТБ'];
    var i = 0, v = n;
    while (v >= 1024 && i < u.length - 1) { v = v / 1024; i++; }
    return (i === 0 ? String(Math.round(v)) : v.toFixed(v < 10 ? 1 : 0)) + ' ' + u[i];
  }

  function fmtTime(sec) {
    if (!sec) return '—';
    var d = new Date(sec * 1000);
    if (isNaN(d.getTime())) return '—';
    var p = function (x) { return (x < 10 ? '0' : '') + x; };
    return p(d.getDate()) + '.' + p(d.getMonth() + 1) + '.' + d.getFullYear() + ' ' + p(d.getHours()) + ':' + p(d.getMinutes());
  }

  // Только время: «17:04:35» — для отметки последней синхронизации.
  function fmtClock(sec) {
    if (!sec) return '—';
    var d = new Date(sec * 1000);
    if (isNaN(d.getTime())) return '—';
    var p = function (x) { return (x < 10 ? '0' : '') + x; };
    return p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds());
  }

  function plural(n, f) {
    var a = Math.abs(n) % 100, b = a % 10;
    if (a > 10 && a < 20) return f[2];
    if (b > 1 && b < 5) return f[1];
    if (b === 1) return f[0];
    return f[2];
  }

  function toast(msg, kind) {
    var t = document.createElement('div');
    t.className = 'toast ' + (kind || '');
    t.textContent = msg;
    el('toasts').appendChild(t);
    setTimeout(function () {
      t.classList.add('out');
      setTimeout(function () { t.remove(); }, 300);
    }, 3600);
  }

  function api(url, opts) {
    return fetch(url, opts || {}).then(function (r) {
      var ct = r.headers.get('content-type') || '';
      var body = ct.indexOf('json') >= 0 ? r.json() : r.text();
      return body.then(function (data) {
        if (!r.ok) throw new Error((data && data.error) || ('HTTP ' + r.status));
        return data;
      });
    });
  }

  function postJson(url, obj) {
    return api(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(obj || {})
    });
  }

  // query-строка: делит значения безопасно, пропускает пустые share
  function qs(params) {
    var parts = [];
    for (var k in params) {
      if (!Object.prototype.hasOwnProperty.call(params, k)) continue;
      var v = params[k];
      if (v === undefined || v === null) continue;
      parts.push(k + '=' + encodeURIComponent(v));
    }
    return parts.length ? '?' + parts.join('&') : '';
  }

  /* ------------------------------ статус --------------------------- */
  function loadStatus() {
    return api('/api/status').then(function (d) {
      S.status = d;
      renderStatus(d);
    }).catch(function () {
      el('chipAddr').textContent = 'нет связи';
      el('myDot').className = 'dot bad';
    });
  }

  function applyShares(list) {
    list = list || [];
    var sig = JSON.stringify(list);
    if (sig === S.sharesSig) return false;
    S.sharesSig = sig;
    S.shares = list;
    var exists = list.some(function (s) { return s.name === S.share; });
    var switched = false;
    if (!exists) {
      S.share = list.length ? list[0].name : '';
      S.path = '';
      switched = true;
    }
    renderShares();
    return switched;
  }

  function renderShares() {
    var ul = el('shareList');
    ul.innerHTML = '';
    el('shareCount').textContent = S.shares.length;
    if (!S.shares.length) {
      var li = document.createElement('li');
      li.className = 'muted pad';
      li.textContent = 'Пока нет общих папок — добавьте хотя бы одну.';
      ul.appendChild(li);
      return;
    }
    // Ключи папок, которые раздают другие узлы: по ним идёт синхронизация.
    var online = ((S.status && S.status.peers) || []).filter(function (p) { return p.online; });
    var peerKeys = {};
    online.forEach(function (p) {
      (p.shares || []).forEach(function (n) { peerKeys[String(n).toLowerCase()] = true; });
    });
    S.shares.forEach(function (s) {
      var key = s.key || s.name;
      var li = document.createElement('li');
      li.className = 'share' + (s.name === S.share ? ' active' : '');
      var ico = document.createElement('span');
      ico.className = 'ico';
      ico.innerHTML = svgIcon('folder');
      var box = document.createElement('div');
      box.className = 'shareinfo';
      var nm = document.createElement('b');
      nm.textContent = s.name;
      var pth = document.createElement('small');
      pth.textContent = s.path;
      pth.title = s.path;
      box.appendChild(nm);
      box.appendChild(pth);
      if (key !== s.name) {
        var pk = document.createElement('small');
        pk.className = 'pairkey';
        pk.textContent = '⇄ ' + key;
        pk.title = 'Синхронизируется с папкой «' + key + '» на другом устройстве';
        box.appendChild(pk);
      }
      if (online.length && !peerKeys[String(key).toLowerCase()]) {
        var np = document.createElement('small');
        np.className = 'nopair';
        np.textContent = 'нет пары на других устройствах';
        np.title = 'Ни на одном узле нет папки с ключом «' + key +
          '». Файлы синхронизируются только между одинаковыми ключами.';
        box.appendChild(np);
      }
      li.onclick = function () { selectShare(s.name); };
      li.appendChild(ico);
      li.appendChild(box);
      if (online.length) {
        // Связать с папкой другого устройства: ключ может быть любым (номер, имя).
        var pair = document.createElement('select');
        pair.className = 'pairsel';
        pair.title = 'Синхронизировать с папкой другого устройства';
        var seen = {};
        var addOpt = function (v, label) {
          var o = document.createElement('option');
          o.value = v;
          o.textContent = label;
          pair.appendChild(o);
          seen[String(v).toLowerCase()] = true;
        };
        addOpt(s.name, 'по имени');
        if (!seen[String(key).toLowerCase()]) addOpt(key, key);
        peerFolders().forEach(function (f) {
          if (!f.busy && !seen[f.key.toLowerCase()]) addOpt(f.key, f.key);
        });
        addOpt('__custom', 'другой…');
        pair.value = key;
        pair.onclick = function (ev) { ev.stopPropagation(); };
        pair.onchange = function (ev) {
          ev.stopPropagation();
          var v = pair.value;
          if (v === '__custom') {
            v = prompt('Ключ синхронизации: номер или имя папки на другом ПК.\n' +
                       'Пустое поле — синхронизация по имени папки.', key);
            if (v === null) { pair.value = key; return; }
            v = v.trim();
          }
          if (v === s.name) v = '';
          setShareKey(s.name, v);
        };
      }
      var rm = document.createElement('button');
      rm.className = 'btn ghost small danger';
      rm.innerHTML = svgIcon('close', 'sm');
      rm.title = 'Убрать из списка общих (файлы останутся на диске)';
      rm.onclick = function (ev) {
        ev.stopPropagation();
        removeShare(s.name);
      };
      li.onclick = function () { selectShare(s.name); };
      li.appendChild(ico);
      li.appendChild(box);
      if (pair) li.appendChild(pair);
      li.appendChild(rm);
      ul.appendChild(li);
    });
  }

  function selectShare(name) {
    if (name === S.share) return;
    S.share = name;
    S.path = '';
    renderShares();
    loadFiles('');
  }

  function removeShare(name) {
    if (!confirm('Убрать папку «' + name + '» из списка общих?\nФайлы на диске останутся.')) return;
    postJson('/api/shares', { action: 'remove', name: name }).then(function (d) {
      applyShares(d.shares);
      toast('Папка убрана: ' + name, 'ok');
      loadFiles('');
    }).catch(function (e) { toast(e.message, 'err'); });
  }

  // Привязка к папке другого устройства: ключ может быть номером или именем.
  function setShareKey(name, key) {
    postJson('/api/shares', { action: 'setkey', name: name, key: key }).then(function (d) {
      applyShares(d.shares);
      toast(key ? ('Папка «' + name + '» синхронизируется как «' + key + '»')
                : ('Папка «' + name + '» синхронизируется по имени'), 'ok');
      loadStatus();
    }).catch(function (e) { toast(e.message, 'err'); });
  }

  // Статус-строка внизу окна: сколько файлов и байт осталось в текущем раунде.
  function renderSyncBar(d) {
    var bar = el('syncbar');
    var fill = el('syncFill');
    var label = el('syncLabel');
    var stat = el('syncStat');
    var s = d.sync || {};
    if (!bar || !fill || !label || !stat) return;  // старая страница в браузере
    var filesTotal = Number(s.filesTotal) || 0;
    var filesDone = Number(s.filesDone) || 0;
    var bytesTotal = Number(s.bytesTotal) || 0;
    var bytesDone = Number(s.bytesDone) || 0;
    var failed = Number(s.failed) || 0;
    if (!s.enabled) {
      bar.className = 'syncbar off';
      fill.style.width = '0%';
      label.textContent = 'Автосинхронизация выключена';
      stat.textContent = 'включите переключателем вверху';
      return;
    }
    if (s.running && bytesTotal > 0) {
      var pct = Math.max(0, Math.min(100, Math.round(bytesDone * 100 / bytesTotal)));
      var left = Math.max(0, bytesTotal - bytesDone);
      bar.className = 'syncbar busy' + (failed ? ' err' : '');
      fill.style.width = pct + '%';
      label.textContent = 'Синхронизация ' + pct + '%';
      stat.textContent = 'файлы: ' + filesDone + ' / ' + filesTotal +
        ' · ' + fmtSize(bytesDone) + ' из ' + fmtSize(bytesTotal) +
        ' · осталось ' + fmtSize(left) +
        (failed ? ' · ошибок: ' + failed : '');
      return;
    }
    if (s.running) {
      var pt = Number(s.peersTotal) || 0;
      var pd = Number(s.peersDone) || 0;
      bar.className = 'syncbar checking';
      label.textContent = 'Проверка изменений…';
      stat.textContent = pt ? 'устройства: ' + pd + ' / ' + pt : 'ждём другие устройства';
      return;
    }
    var lastFiles = Number(s.lastFiles) || 0;
    bar.className = 'syncbar';
    fill.style.width = '100%';
    label.textContent = 'Синхронизировано';
    if (!s.lastAt) {
      stat.textContent = 'ещё не запускалась';
    } else if (lastFiles) {
      stat.textContent = 'проверено ' + fmtClock(s.lastAt) + ' · передано ' + lastFiles + ' ' +
        plural(lastFiles, ['файл', 'файла', 'файлов']) + ' (' + fmtSize(s.lastBytes) + ')';
    } else {
      stat.textContent = 'проверено ' + fmtClock(s.lastAt) + ' · изменений нет';
    }
  }

  function renderStatus(d) {
    el('nodeName').textContent = d.node.name + ' · порт ' + d.ports.http;
    el('chipAddr').textContent = (d.ips && d.ips[0] ? d.ips[0] : '127.0.0.1') + ':' + d.ports.http;
    var shares = d.shares || [];
    el('myDot').className = 'dot' + (shares.length ? '' : ' warn');
    var peers = d.peers || [];
    var online = peers.filter(function (p) { return p.online; }).length;
    el('chipPeers').textContent = online + ' ' + plural(online, ['устройство', 'устройства', 'устройств']);
    el('peerCount').textContent = peers.length;
    el('syncToggle').checked = !!d.sync.enabled;
    el('srvHttp').textContent = ':' + d.ports.http;
    el('srvFtp').textContent = d.ports.ftp ? ':' + d.ports.ftp : 'выключен';
    el('srvUdp').textContent = ':' + d.ports.udp;
    el('srvIp').textContent = (d.ips || []).join(', ') || '—';
    el('statIn').textContent = fmtSize(d.stats.bytesIn);
    el('statOut').textContent = fmtSize(d.stats.bytesOut);
    el('btnSync').textContent = d.sync.running ? 'Синхронизация…' : 'Синхронизировать';
    var warn = el('shareWarn');
    if (d.rootError) {
      warn.textContent = d.rootError;
      warn.hidden = false;
    } else {
      warn.hidden = true;
    }
    var switched = applyShares(shares);
    // Узлы могли добавить/убрать папки — тогда меняются отметки «нет пары».
    var pairSig = peers
      .filter(function (p) { return p.online; })
      .map(function (p) { return (p.shares || []).join('|'); })
      .join(';');
    if (pairSig !== S.pairSig) {
      S.pairSig = pairSig;
      renderShares();
    }
    renderPeers(peers);
    renderLog(d.transfers || []);
    renderSyncBar(d);
    updateDropHint();
    if (switched) loadFiles('');
  }

  function updateDropHint() {
    var t = el('dropHint');
    if (!S.share) {
      t.textContent = 'Сначала добавьте сетевую папку слева.';
      return;
    }
    t.textContent = 'Перетащите файлы сюда — они появятся в «' + S.share +
      (S.path ? '\\' + S.path.replace(/\//g, '\\') : '') + '». Нажмите, чтобы выбрать вручную.';
  }

  function renderPeers(peers) {
    var ul = el('peerList');
    ul.innerHTML = '';
    if (!peers.length) {
      var li = document.createElement('li');
      li.className = 'muted pad';
      li.textContent = 'Пока никого не видно. Включите программу на других устройствах той же сети.';
      ul.appendChild(li);
      return;
    }
    peers.sort(function (a, b) {
      return (b.online ? 1 : 0) - (a.online ? 1 : 0) || String(a.name).localeCompare(String(b.name));
    });
    peers.forEach(function (p) {
      var li = document.createElement('li');
      li.className = 'peer';
      var dot = document.createElement('i');
      dot.className = 'dot' + (p.online ? '' : ' off');
      var box = document.createElement('div');
      box.className = 'peerinfo';
      var nm = document.createElement('b');
      nm.textContent = p.name || p.ip;
      var meta = document.createElement('small');
      meta.textContent = p.ip + ':' + p.http + (p.ftp ? ' · ftp ' + p.ftp : '');
      box.appendChild(nm);
      box.appendChild(meta);
      if (p.shares && p.shares.length) {
        var sh = document.createElement('small');
        sh.className = 'peershares';
        sh.textContent = 'папки: ' + p.shares.join(', ');
        sh.title = p.shares.join(', ');
        box.appendChild(sh);
      }
      var a = document.createElement('a');
      a.className = 'btn ghost small';
      a.href = 'http://' + p.ip + ':' + p.http + '/';
      a.target = '_blank';
      a.rel = 'noopener';
      a.textContent = 'Веб';
      a.title = 'Открыть веб-интерфейс этого узла в браузере (файлы синхронизируются сами)';
      li.appendChild(dot);
      li.appendChild(box);
      li.appendChild(a);
      ul.appendChild(li);
    });
  }

  function renderLog(list) {
    var ul = el('log');
    ul.innerHTML = '';
    var arr = list.slice().reverse().slice(0, 40);
    if (!arr.length) {
      var li = document.createElement('li');
      li.className = 'muted pad';
      li.textContent = 'Передач пока не было.';
      ul.appendChild(li);
      return;
    }
    arr.forEach(function (t) {
      var li = document.createElement('li');
      li.className = 'tx';
      var ic = document.createElement('span');
      ic.className = 'txic ' + t.dir;
      ic.innerHTML = svgIcon(t.dir === 'up' ? 'up-right' : t.dir === 'down' ? 'down-left' : t.dir === 'ftp' ? 'updown' : 'swap');
      var file = document.createElement('span');
      file.className = 'txfile';
      file.textContent = t.file;
      var meta = document.createElement('span');
      meta.className = 'txmeta';
      meta.textContent = fmtSize(t.bytes) + (t.total ? ' / ' + fmtSize(t.total) : '') + (t.peer ? ' · ' + t.peer : '');
      var st = document.createElement('span');
      var status = String(t.status || '');
      st.className = 'txstate ' + (status === 'done' ? 'done' : status === 'active' ? 'active' : 'error');
      st.textContent = status === 'done' ? 'готово' : status === 'active' ? 'идёт' : status.replace(/^error:?\s*/, 'ошибка');
      li.appendChild(ic);
      li.appendChild(file);
      li.appendChild(meta);
      li.appendChild(st);
      ul.appendChild(li);
    });
  }

  /* ------------------------------ файлы ---------------------------- */
  function loadFiles(path) {
    S.path = path || '';
    if (!S.share) {
      S.files = [];
      renderCrumbs('');
      renderFiles();
      return Promise.resolve();
    }
    var url = '/api/files' + qs({ share: S.share, path: S.path });
    return api(url).then(function (d) {
      if (d.share && d.share !== S.share) return;  // ответ от другой папки — устарел
      S.files = d.entries || [];
      renderCrumbs(S.path);
      renderFiles();
      updateDropHint();
    }).catch(function (e) {
      toast('Не удалось прочитать папку: ' + e.message, 'err');
    });
  }

  function renderCrumbs(path) {
    var box = el('crumbs');
    box.innerHTML = '';
    if (!S.share) {
      var em = document.createElement('span');
      em.className = 'muted';
      em.textContent = 'папка не выбрана';
      box.appendChild(em);
      return;
    }
    var root = document.createElement('button');
    root.className = 'crumb sharename';
    root.textContent = S.share;
    root.onclick = function () { loadFiles(''); };
    box.appendChild(root);

    var acc = '';
    String(path || '').split('/').filter(Boolean).forEach(function (part) {
      acc = acc ? acc + '/' + part : part;
      var sep = document.createElement('span');
      sep.className = 'sep';
      sep.textContent = '›';
      box.appendChild(sep);
      var b = document.createElement('button');
      b.className = 'crumb';
      b.textContent = part;
      b.onclick = function () { loadFiles(acc); };
      box.appendChild(b);
    });
  }

  function renderFiles() {
    var tb = el('fileRows');
    tb.innerHTML = '';
    var q = el('search').value.trim().toLowerCase();
    var list = S.files.filter(function (f) {
      return !q || f.name.toLowerCase().indexOf(q) >= 0;
    });
    el('emptyFiles').hidden = list.length > 0 || !S.share;
    list.forEach(function (f) {
      var tr = document.createElement('tr');
      var nameCell = document.createElement('td');
      var ico = document.createElement('span');
      ico.className = 'ico';
      ico.innerHTML = svgIcon(iconType(f.name, f.isDir));
      var nm = document.createElement('span');
      nm.textContent = f.name;
      nameCell.appendChild(ico);
      nameCell.appendChild(nm);

      var sizeCell = document.createElement('td');
      sizeCell.className = 'num';
      sizeCell.textContent = f.isDir ? '—' : fmtSize(f.size);

      var timeCell = document.createElement('td');
      timeCell.className = 'muted';
      timeCell.textContent = fmtTime(f.mtime);

      var actCell = document.createElement('td');
      actCell.className = 'act';
      if (!f.isDir) {
        var a = document.createElement('a');
        a.className = 'btn ghost small';
        a.textContent = 'Скачать';
        a.href = '/api/download' + qs({ share: S.share, path: f.rel });
        actCell.appendChild(a);
      }
      var del = document.createElement('button');
      del.className = 'btn ghost small danger';
      del.textContent = 'Удалить';
      del.onclick = function (ev) {
        ev.stopPropagation();
        removeEntry(f);
      };
      actCell.appendChild(del);

      if (f.isDir) {
        tr.className = 'rowdir';
        tr.onclick = function () { loadFiles(f.rel); };
      }
      tr.appendChild(nameCell);
      tr.appendChild(sizeCell);
      tr.appendChild(timeCell);
      tr.appendChild(actCell);
      tb.appendChild(tr);
    });
  }

  function removeEntry(f) {
    if (!confirm('Удалить «' + f.rel + '» из папки «' + S.share + '»? Действие необратимо.')) return;
    api('/api/delete' + qs({ share: S.share, path: f.rel }), { method: 'POST' }).then(function () {
      toast('Удалено: ' + f.name, 'ok');
      loadFiles(S.path);
    }).catch(function (e) { toast(e.message, 'err'); });
  }

  function parentPath(p) {
    var parts = String(p || '').split('/').filter(Boolean);
    parts.pop();
    return parts.join('/');
  }

  /* ----------------------------- загрузка -------------------------- */
  function uploadFiles(files, dir) {
    if (dir === undefined) dir = S.path;
    if (!S.share) {
      toast('Сначала добавьте сетевую папку', 'err');
      return;
    }
    files.forEach(function (f) {
      var url = '/api/upload' + qs({ share: S.share, path: dir, name: f.name });
      var row = document.createElement('div');
      row.className = 'upload';
      var name = document.createElement('span');
      name.className = 'uname';
      name.textContent = f.name;
      var bar = document.createElement('div');
      bar.className = 'bar';
      var fill = document.createElement('div');
      fill.className = 'fill';
      bar.appendChild(fill);
      var pct = document.createElement('span');
      pct.className = 'pct';
      pct.textContent = '0%';
      row.appendChild(name);
      row.appendChild(bar);
      row.appendChild(pct);
      el('uploads').appendChild(row);

      var xhr = new XMLHttpRequest();
      xhr.open('POST', url);
      xhr.upload.onprogress = function (e) {
        if (!e.lengthComputable) return;
        var p = Math.round(e.loaded * 100 / e.total);
        fill.style.width = p + '%';
        pct.textContent = p + '%';
      };
      xhr.onload = function () {
        row.remove();
        if (xhr.status >= 200 && xhr.status < 300) {
          toast('Отправлено: ' + f.name, 'ok');
          if (dir === S.path) loadFiles(S.path);
          loadStatus();
        } else {
          var msg = 'код ' + xhr.status;
          try { msg = JSON.parse(xhr.responseText).error || msg; } catch (e) {}
          toast('Ошибка загрузки «' + f.name + '»: ' + msg, 'err');
        }
      };
      xhr.onerror = function () {
        row.remove();
        toast('Сеть недоступна: ' + f.name, 'err');
      };
      xhr.send(f);
    });
  }

  /* --------------------------- выбор папки ------------------------- */
  // Ключи синхронизации, которые раздают другие узлы: по ним связываются папки.
  function peerFolders() {
    var out = [];
    var mine = {};
    (S.shares || []).forEach(function (s) {
      mine[String(s.key || s.name).toLowerCase()] = s.name;
    });
    ((S.status && S.status.peers) || []).forEach(function (p) {
      if (!p.online) return;
      (p.shares || []).forEach(function (n) {
        if (!n) return;
        var key = String(n);
        var low = key.toLowerCase();
        if (out.some(function (x) { return x.key.toLowerCase() === low; })) return;
        out.push({ key: key, peer: p.name || p.ip, busy: !!mine[low], mineName: mine[low] || '' });
      });
    });
    return out;
  }

  function baseName(path) {
    var s = String(path || '').replace(/[\\/]+$/, '');
    var i = Math.max(s.lastIndexOf('\\'), s.lastIndexOf('/'));
    return i >= 0 ? s.slice(i + 1) : s;
  }

  function openBrowser(start) {
    el('modalRoot').hidden = false;
    el('browseKey').value = '';
    loadStatus();
    var begin = start || (S.status && S.status.root) || '';
    browse(begin);
  }

  function browse(path, drivesOnly) {
    var url = '/api/browse?path=' + encodeURIComponent(path || '') + (drivesOnly ? '&drives=1' : '');
    return api(url).then(function (d) {
      S.browse = d.path;
      el('browseCurrent').textContent = d.path || 'Все диски и разделы';
      if (d.path) {
        // Для уже добавленной папки подставляем её сетевое имя, иначе — имя каталога.
        var known = S.shares.filter(function (s) {
          return String(s.path).toLowerCase() === String(d.path).toLowerCase();
        })[0];
        el('browseName').value = known ? known.name : baseName(d.path);
      }
      renderPlaces(d);
      var ul = el('browseList');
      ul.innerHTML = '';
      if (d.parent) ul.appendChild(dirRow('up', '..', d.parent));
      (d.drives || []).forEach(function (dr) { ul.appendChild(dirRow('disk', dr, dr)); });
      (d.dirs || []).forEach(function (dir) { ul.appendChild(dirRow('folder', dir.name, dir.path)); });
      if (!d.dirs.length && !(d.drives || []).length) {
        var li = document.createElement('li');
        li.className = 'muted pad';
        li.textContent = 'Вложенных папок нет';
        ul.appendChild(li);
      }
      renderPairing();
    }).catch(function (e) { toast(e.message, 'err'); });
  }

  function placeButton(label, icon, active, onClick) {
    var b = document.createElement('button');
    b.type = 'button';
    b.className = 'place' + (active ? ' active' : '');
    b.innerHTML = svgIcon(icon);
    var sp = document.createElement('span');
    sp.textContent = label;
    b.appendChild(sp);
    b.onclick = onClick;
    return b;
  }

  // Быстрый переход: «Документы», «Рабочий стол», «Изображения», «Все диски» и т.п.
  function renderPlaces(d) {
    var box = el('browsePlaces');
    box.innerHTML = '';
    var cur = String(d.path || '').toLowerCase();
    (d.places || []).forEach(function (p) {
      var active = String(p.path || '').toLowerCase() === cur;
      box.appendChild(placeButton(p.label, 'folder', active, function () { browse(p.path); }));
    });
    box.appendChild(placeButton('Все диски', 'disk', !d.path, function () { browse('', true); }));
  }

  // Связывание: ключ синхронизации может не совпадать с именем папки.
  function renderPairing() {
    var input = el('browseName');
    var keyInput = el('browseKey');
    var name = input.value.trim();
    var key = keyInput.value.trim();
    var folders = peerFolders();
    var box = el('browseMatches');
    var hint = el('browseHint');
    box.innerHTML = '';
    var online = ((S.status && S.status.peers) || []).some(function (p) { return p.online; });

    // Всегда показываем, какие ключи синхронизации есть на других устройствах.
    if (!online) {
      var none = document.createElement('span');
      none.className = 'muted';
      none.textContent = 'Другие устройства пока не видны — ключи появятся, когда узлы найдут друг друга.';
      box.appendChild(none);
    } else if (!folders.length) {
      var empty = document.createElement('span');
      empty.className = 'muted';
      empty.textContent = 'На других устройствах нет общих папок. Ключ можно ввести вручную — ' +
        'номером или своим именем.';
      box.appendChild(empty);
    } else {
      var lab = document.createElement('span');
      lab.textContent = 'Ключи синхронизации на других устройствах (нажмите, чтобы выбрать):';
      box.appendChild(lab);
      folders.forEach(function (f) {
        var active = String(key || name).toLowerCase() === f.key.toLowerCase();
        var b = placeButton(f.key, 'folder', active, function () {
          if (f.busy) return;
          if (!input.value.trim()) input.value = baseName(S.browse || '');
          keyInput.value = f.key;
          renderPairing();
        });
        var tag = document.createElement('span');
        tag.className = 'peertag';
        tag.textContent = f.busy ? ('занят: ' + f.mineName) : f.peer;
        b.appendChild(tag);
        if (f.busy) {
          b.disabled = true;
          b.classList.add('busy');
          b.title = 'Ключ «' + f.key + '» уже используется папкой «' + f.mineName + '»';
        } else {
          b.title = 'Связать с папкой «' + f.key + '» на «' + f.peer + '»';
        }
        box.appendChild(b);
      });
    }
    box.hidden = false;

    var effKey = key || name;
    var match = folders.some(function (f) {
      return f.key.toLowerCase() === effKey.toLowerCase() && !f.busy;
    });
    hint.className = 'pairhint';
    if (!online) {
      hint.textContent = 'Другие устройства пока не видны. Ключ синхронизации должен совпадать ' +
        'с именем папки (или её ключом) на втором ПК.';
    } else if (effKey && match) {
      hint.className = 'pairhint ok';
      hint.textContent = key
        ? ('Будет синхронизироваться с папкой «' + key + '» на другом устройстве, хотя здесь ' +
           'называется «' + (name || '…') + '».')
        : 'Одноимённая папка есть на другом устройстве: содержимое будет одинаковым на обоих ПК.';
    } else {
      hint.className = 'pairhint warn';
      hint.textContent = key
        ? ('Папки с ключом «' + key + '» на других устройствах не видно — синхронизации не будет, ' +
           'пока такая папка не появится.')
        : ('Папки «' + (name || '…') + '» нет на других устройствах. Выберите папку из списка ' +
           'выше (тогда имена могут различаться) или введите ключ вручную.');
    }
  }

  function dirRow(type, label, path) {
    var li = document.createElement('li');
    li.className = 'dir';
    var sp = document.createElement('span');
    sp.className = 'ico';
    sp.innerHTML = svgIcon(type);
    var tx = document.createElement('span');
    tx.textContent = label;
    li.appendChild(sp);
    li.appendChild(tx);
    li.onclick = function () { browse(path); };
    return li;
  }

  /* ------------------------------ кнопки --------------------------- */
  el('btnSync').onclick = function () {
    api('/api/sync/now', { method: 'POST' }).then(function () {
      toast('Все папки этого устройства отправлены в сеть', 'ok');
      loadStatus();
    }).catch(function (e) { toast(e.message, 'err'); });
  };

  el('syncToggle').onchange = function (ev) {
    postJson('/api/sync', { enabled: ev.target.checked }).then(function () {
      toast(ev.target.checked ? 'Автосинхронизация включена' : 'Автосинхронизация выключена', 'ok');
      loadStatus();
    }).catch(function (e) { toast(e.message, 'err'); });
  };

  el('btnRefresh').onclick = function () { loadFiles(S.path); };
  el('btnUp').onclick = function () { loadFiles(parentPath(S.path)); };
  el('search').oninput = renderFiles;

  el('btnMkdir').onclick = function () {
    if (!S.share) { toast('Сначала добавьте сетевую папку', 'err'); return; }
    var name = prompt('Имя новой папки:');
    if (!name) return;
    api('/api/mkdir' + qs({ share: S.share, path: S.path, name: name }), { method: 'POST' })
      .then(function () { toast('Папка создана', 'ok'); loadFiles(S.path); })
      .catch(function (e) { toast(e.message, 'err'); });
  };

  el('btnClearLog').onclick = function () {
    api('/api/transfers/clear', { method: 'POST' }).then(function () { loadStatus(); });
  };

  el('btnAddShare').onclick = function () { openBrowser(); };
  el('btnBrowseCancel').onclick = function () { el('modalRoot').hidden = true; };
  el('modalRoot').addEventListener('click', function (e) {
    if (e.target === el('modalRoot')) el('modalRoot').hidden = true;
  });
  document.addEventListener('keydown', function (e) {
    if (e.key === 'Escape') el('modalRoot').hidden = true;
  });

  el('btnBrowsePick').onclick = function () {
    if (!S.browse) { toast('Выберите папку', 'err'); return; }
    var name = el('browseName').value.trim();
    var key = el('browseKey').value.trim();
    postJson('/api/shares', { action: 'add', path: S.browse, name: name, key: key }).then(function (d) {
      el('modalRoot').hidden = true;
      applyShares(d.shares);
      toast(name ? ('Папка «' + name + '» добавлена') : 'Папка добавлена', 'ok');
      loadStatus();
      loadFiles('');
    }).catch(function (e) { toast(e.message, 'err'); });
  };
  el('browseName').oninput = renderPairing;
  el('browseKey').oninput = renderPairing;

  el('btnOpenRoot').onclick = function () {
    if (!S.share) { toast('Сначала добавьте сетевую папку', 'err'); return; }
    api('/api/openroot' + qs({ share: S.share }), { method: 'POST' })
      .catch(function (e) { toast(e.message, 'err'); });
  };

  el('addPeerForm').addEventListener('submit', function (e) {
    e.preventDefault();
    var addr = el('peerAddr').value.trim();
    if (!addr) return;
    postJson('/api/peers', { addr: addr }).then(function () {
      el('peerAddr').value = '';
      toast('Устройство добавлено', 'ok');
      loadStatus();
      api('/api/sync/now', { method: 'POST' }).catch(function () {});
    }).catch(function (err) { toast(err.message, 'err'); });
  });

  var drop = el('drop');
  ['dragenter', 'dragover'].forEach(function (ev) {
    drop.addEventListener(ev, function (e) { e.preventDefault(); drop.classList.add('over'); });
  });
  ['dragleave', 'drop'].forEach(function (ev) {
    drop.addEventListener(ev, function (e) { e.preventDefault(); drop.classList.remove('over'); });
  });
  drop.addEventListener('drop', function (e) {
    uploadFiles(Array.prototype.slice.call(e.dataTransfer.files || []));
  });
  drop.addEventListener('click', function () {
    if (!S.share) { toast('Сначала добавьте сетевую папку', 'err'); return; }
    var inp = document.createElement('input');
    inp.type = 'file';
    inp.multiple = true;
    inp.onchange = function () { uploadFiles(Array.prototype.slice.call(inp.files || [])); };
    inp.click();
  });

  /* ------------------------------ старт ---------------------------- */
  loadStatus().then(function () { loadFiles(S.path); });
  setInterval(loadStatus, 2000);
  setInterval(function () { loadFiles(S.path); }, 6000);
})();
)JS";