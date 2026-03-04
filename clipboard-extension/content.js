// CopyWithSource — Content Script (优化版)

const cfg = { master: true, menu: true, append: true };
let acc = { items: [], index: -1 };
let panelPos = null;

function safeSet(obj) { try { chrome.storage.local.set(obj); } catch (e) {} }

try {
  chrome.storage.local.get(['masterEnabled', 'menuEnabled', 'autoAppend', 'cws_accumulator', 'cws_panel_pos'], d => {
    cfg.master = d.masterEnabled !== false;
    cfg.menu   = d.menuEnabled   !== false;
    cfg.append = d.autoAppend    !== false;
    if (d.cws_panel_pos) panelPos = d.cws_panel_pos;
    if (d.cws_accumulator) {
      acc.items = d.cws_accumulator.items || [];
      acc.index = d.cws_accumulator.index ?? -1;
      renderAccPanel();
    }
  });
} catch (e) {}

chrome.storage.onChanged.addListener(c => {
  if ('masterEnabled' in c) cfg.master = c.masterEnabled.newValue;
  if ('menuEnabled'   in c) cfg.menu   = c.menuEnabled.newValue;
  if ('autoAppend'    in c) cfg.append = c.autoAppend.newValue;

  if ('cws_panel_pos' in c) {
    panelPos = c.cws_panel_pos.newValue;
    applyPanelPosition();
  }

  if ('cws_accumulator' in c) {
    // 仅当本 Tab 不是触发源时才重渲（避免多 Tab 重复渲染）
    const newAcc = c.cws_accumulator.newValue;
    if (!newAcc) return;
    acc.items = newAcc.items || [];
    acc.index = newAcc.index ?? -1;
    renderAccPanel();
  }
});

const SVG = {
  copy:    `<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></svg>`,
  link:    `<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/><path d="M13 17h1.5a2.5 2.5 0 0 0 0-5H13"/><path d="M17 17h-1.5a2.5 2.5 0 0 1 0-5H17"/><line x1="13" y1="15" x2="17" y2="15"/></svg>`,
  url:     `<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71"/><path d="M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71"/></svg>`,
  append:  `<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"/><line x1="12" y1="8" x2="12" y2="16"/><line x1="8" y1="12" x2="16" y2="12"/></svg>`,
  copyAll: `<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/><polyline points="13 14 15 16 19 12"/></svg>`,
  undo:    `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="1 4 1 10 7 10"/><path d="M3.51 15a9 9 0 1 0 2.13-9.36L1 10"/></svg>`,
  redo:    `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="23 4 23 10 17 10"/><path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/></svg>`,
  trash:   `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/></svg>`
};

function esc(s) {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

async function copy(plain, html) {
  try {
    await navigator.clipboard.write([new ClipboardItem({
      'text/plain': new Blob([plain], { type: 'text/plain' }),
      'text/html':  new Blob([html],  { type: 'text/html' }),
    })]);
  } catch {
    await navigator.clipboard.writeText(plain).catch(() => {});
  }
}

async function sendToRapidNotes(content) {
    try {
        await fetch('http://localhost:23333/add_note', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                content: content,
                url: location.href,
                pageTitle: document.title
            })
        });
    } catch (e) {
        console.log('[RapidNotes] 数据同步失败，请检查主程序是否运行');
    }
}

// ── Toast ──────────────────────────────────────────────────
// 【修复】position:fixed 不需要加 scrollX/Y，去掉偏移量
let toast = null;
function toast_(msg, rect) {
  toast?.remove();
  toast = Object.assign(document.createElement('div'), { id: 'cws-toast', textContent: msg });
  document.body.appendChild(toast);
  const tw = toast.offsetWidth;
  const th = toast.offsetHeight;
  // 水平居中于选区，不超左右边界
  let l = rect.left + rect.width / 2 - tw / 2;
  l = Math.max(8, Math.min(l, window.innerWidth - tw - 8));
  // 默认显示在选区上方，空间不够时显示在下方
  let t = rect.top - th - 8;
  if (t < 8) t = rect.bottom + 8;
  toast.style.cssText = `left:${l}px;top:${t}px`;
  setTimeout(() => { toast?.remove(); toast = null; }, 1600);
}

// ── 累计面板 ───────────────────────────────────────────────
let accPanel = null;

// 【修复】边界计算使用面板真实尺寸，不再硬编码 100px
function clampPosition(x, y) {
  const pw = accPanel ? accPanel.offsetWidth  : 280;
  const ph = accPanel ? accPanel.offsetHeight : 200;
  return {
    x: Math.max(0, Math.min(x, window.innerWidth  - pw)),
    y: Math.max(0, Math.min(y, window.innerHeight - ph)),
  };
}

function applyPanelPosition() {
  if (!accPanel) return;
  let useDefault = true;
  if (panelPos && typeof panelPos.left === 'string' && typeof panelPos.top === 'string') {
    const parsedL = parseInt(panelPos.left);
    const parsedT = parseInt(panelPos.top);
    if (!isNaN(parsedL) && !isNaN(parsedT)) {
      const { x, y } = clampPosition(parsedL, parsedT);
      accPanel.style.setProperty('left',   x + 'px', 'important');
      accPanel.style.setProperty('top',    y + 'px', 'important');
      accPanel.style.setProperty('right',  'auto',   'important');
      accPanel.style.setProperty('bottom', 'auto',   'important');
      useDefault = false;
    }
  }
  if (useDefault) {
    accPanel.style.setProperty('right',  '24px', 'important');
    accPanel.style.setProperty('bottom', '24px', 'important');
    accPanel.style.setProperty('left',   'auto', 'important');
    accPanel.style.setProperty('top',    'auto', 'important');
  }
}

// 【优化】只重建列表区和按钮状态，不重建整个面板 DOM
function renderAccPanel() {
  if (!acc || !acc.items || acc.items.length === 0) {
    if (accPanel) { accPanel.remove(); accPanel = null; }
    return;
  }

  const isNew = !accPanel;
  if (isNew) {
    accPanel = document.createElement('div');
    accPanel.id = 'cws-acc-panel';
    // 面板首次创建：构建完整结构
    accPanel.innerHTML = `
      <div class="cws-acc-header" id="cws-acc-header">
        <span id="cws-acc-title">待输出累计 (0)</span>
        <span class="cws-acc-clear" id="cws-acc-clear" title="清空全部">${SVG.trash}</span>
      </div>
      <div class="cws-acc-list" id="cws-acc-list"></div>
      <div class="cws-acc-footer">
        <button class="cws-acc-action-btn icon-only" id="cws-btn-undo" title="撤销">${SVG.undo}</button>
        <button class="cws-acc-action-btn icon-only" id="cws-btn-redo" title="恢复">${SVG.redo}</button>
        <button class="cws-acc-action-btn flex-fill" id="cws-btn-out">${SVG.copyAll} 合并输出</button>
      </div>
    `;
    document.body.appendChild(accPanel);
    applyPanelPosition();
    bindPanelEvents();
  }

  // 每次只更新动态内容，避免全量重建
  updatePanelContent();
}

function updatePanelContent() {
  const activeCount = acc.index + 1;

  // 更新标题
  const titleEl = accPanel.querySelector('#cws-acc-title');
  if (titleEl) titleEl.textContent = `待输出累计 (${activeCount})`;

  // 更新列表
  const listEl = accPanel.querySelector('#cws-acc-list');
  if (listEl) {
    let html = '';
    acc.items.forEach((item, i) => {
      const isUndone = i > acc.index;
      const firstLine = (item.plain || '').split('\n').find(l => l.trim()) || '空白片段';
      const snippet = firstLine.length > 18 ? firstLine.slice(0, 18) + '...' : firstLine;
      html += `<div class="cws-acc-item ${isUndone ? 'undone' : ''}">
                 <span class="cws-acc-item-num">${i + 1}.</span> ${esc(snippet)}
               </div>`;
    });
    listEl.innerHTML = html;
    listEl.scrollTop = listEl.scrollHeight;
  }

  // 更新按钮状态
  const btnUndo = accPanel.querySelector('#cws-btn-undo');
  const btnRedo = accPanel.querySelector('#cws-btn-redo');
  const btnOut  = accPanel.querySelector('#cws-btn-out');
  if (btnUndo) btnUndo.disabled = acc.index < 0;
  if (btnRedo) btnRedo.disabled = acc.index >= acc.items.length - 1;
  if (btnOut)  btnOut.disabled  = activeCount === 0;
}

function bindPanelEvents() {
  // ── 拖拽（限制在屏幕范围内）──
  const header = accPanel.querySelector('#cws-acc-header');
  header.onmousedown = (e) => {
    if (e.target.closest('#cws-acc-clear')) return;
    e.preventDefault();

    // 若当前是 right/bottom 定位，先转换为 left/top
    const rect = accPanel.getBoundingClientRect();
    accPanel.style.setProperty('left',   rect.left + 'px', 'important');
    accPanel.style.setProperty('top',    rect.top  + 'px', 'important');
    accPanel.style.setProperty('right',  'auto', 'important');
    accPanel.style.setProperty('bottom', 'auto', 'important');

    const offsetX = e.clientX - rect.left;
    const offsetY = e.clientY - rect.top;

    const onMouseMove = (ev) => {
      const { x, y } = clampPosition(ev.clientX - offsetX, ev.clientY - offsetY);
      accPanel.style.setProperty('left', x + 'px', 'important');
      accPanel.style.setProperty('top',  y + 'px', 'important');
    };

    const onMouseUp = () => {
      document.removeEventListener('mousemove', onMouseMove);
      document.removeEventListener('mouseup',   onMouseUp);
      // 持久化位置
      panelPos = { left: accPanel.style.left, top: accPanel.style.top };
      safeSet({ cws_panel_pos: panelPos });
    };

    document.addEventListener('mousemove', onMouseMove);
    document.addEventListener('mouseup',   onMouseUp);
  };

  // ── 清空 ──
  accPanel.querySelector('#cws-acc-clear').onclick = () => {
    acc.items = []; acc.index = -1;
    safeSet({ cws_accumulator: { items: [], index: -1 } });
    renderAccPanel();
  };

  // ── 撤销 ──
  accPanel.querySelector('#cws-btn-undo').onclick = () => {
    if (acc.index < 0) return;
    acc.index--;
    safeSet({ cws_accumulator: { items: [...acc.items], index: acc.index } });
    updatePanelContent();
  };

  // ── 恢复 ──
  accPanel.querySelector('#cws-btn-redo').onclick = () => {
    if (acc.index >= acc.items.length - 1) return;
    acc.index++;
    safeSet({ cws_accumulator: { items: [...acc.items], index: acc.index } });
    updatePanelContent();
  };

  // ── 合并输出 ──
  accPanel.querySelector('#cws-btn-out').onclick = async () => {
    const activeCount = acc.index + 1;
    if (activeCount === 0) return;
    const activeItems = acc.items.slice(0, activeCount);
    const outPlain = activeItems.map(it => it.plain).join('\n\n');
    const outHtml  = activeItems.map(it => it.html).join('<br><br>');
    await copy(outPlain, outHtml);
    await sendToRapidNotes(outPlain);
    toast_('合并输出成功 (已保留内容)', accPanel.getBoundingClientRect());
  };
}

// ── 窗口大小变化时重新校正面板位置 ──
window.addEventListener('resize', () => {
  if (!accPanel) return;
  // 等 layout 稳定后再校正
  requestAnimationFrame(() => applyPanelPosition());
});

// ── 悬浮菜单 ──────────────────────────────────────────────
let menuGlobal = null;
function closeMenu() { menuGlobal?.remove(); menuGlobal = null; }

// 【修复】position:fixed 不需要加 scrollX/Y
async function openMenu(text, html, rect) {
  // [CRITICAL] 采用局部变量隔离，防止异步竞争导致的菜单项重复显示
  const menuLocal = document.createElement('div');
  menuLocal.id = 'cws-menu';
  const url = location.href;

  // 尝试获取后端指定的分类名称
  try {
    const resp = await fetch('http://localhost:23333/get_extension_config');
    if (resp.ok) {
        const config = await resp.json();
        if (config.targetCategoryName) {
            const catEl = document.createElement('div');
            catEl.className = 'cws-target-cat';
            catEl.title = '点击直接采集当前选中的内容到: ' + config.targetCategoryName;
            catEl.textContent = config.targetCategoryName;
            catEl.style.cursor = 'pointer';

            // 点击分类名称直接触发采集
            catEl.onclick = async (e) => {
              e.preventDefault(); e.stopPropagation();
              if (text) {
                await sendToRapidNotes(text);
                toast_('已快速采集到: ' + config.targetCategoryName, rect);
                closeMenu();
              }
            };
            menuLocal.prepend(catEl);
        }
    }
  } catch (e) {
    console.log('[RapidNotes] 无法连接到主程序以获取配置');
  }

  [
    ['普通复制',   SVG.copy,  text,                   html,                                                          '已复制'],
    ['复制+来源',  SVG.link,  text + `\n\n来自 ${url}`, html + `<br><br>来自 <a href="${esc(url)}">${esc(url)}</a>`,  '已复制（含来源）'],
    ['仅复制链接', SVG.url,   url,                    `<a href="${esc(url)}">${esc(url)}</a>`,                        '已复制链接'],
  ].forEach(([label, svg, plain, rich, msg], i) => {
    if (i) menuLocal.appendChild(Object.assign(document.createElement('div'), { className: 'cws-divider' }));
    const btn = Object.assign(document.createElement('button'), { className: 'cws-btn', innerHTML: svg + label });
    btn.addEventListener('click', async e => {
      e.preventDefault(); e.stopPropagation();
      // 同时写入剪贴板和发送 API
      await copy(plain, rich);
      await sendToRapidNotes(plain);
      toast_(msg, rect);
      closeMenu();
    }, true);
    menuLocal.appendChild(btn);
  });

  menuLocal.appendChild(Object.assign(document.createElement('div'), { className: 'cws-divider' }));
  const btnAppend = Object.assign(document.createElement('button'), { className: 'cws-btn', innerHTML: SVG.append + '加入累计' });
  btnAppend.addEventListener('click', async e => {
    e.preventDefault(); e.stopPropagation();
    acc.items = acc.items.slice(0, acc.index + 1);
    acc.items.push({ plain: text, html: html });
    acc.index++;
    safeSet({ cws_accumulator: { items: [...acc.items], index: acc.index } });
    renderAccPanel();
    toast_('已加入累计', rect);
    closeMenu();
  }, true);
  menuLocal.appendChild(btnAppend);

  // 在最后一步才更新全局引用并上屏，彻底解决异步重叠问题
  closeMenu();
  menuGlobal = menuLocal;
  document.body.appendChild(menuGlobal);

  const mw = menuGlobal.offsetWidth, mh = menuGlobal.offsetHeight;
  // 水平居中于选区
  let l = rect.left + rect.width / 2 - mw / 2;
  l = Math.max(8, Math.min(l, window.innerWidth - mw - 8));
  // 默认显示在选区上方，空间不够时显示在下方
  let t = rect.top - mh - 8;
  if (t < 8) t = rect.bottom + 8;
  menuGlobal.style.cssText = `left:${l}px;top:${t}px`;
}

// ── 选区读取 ──────────────────────────────────────────────
function getInfo(target) {
  const tag = target?.tagName;
  if (tag === 'INPUT' || tag === 'TEXTAREA') {
    const { selectionStart: s, selectionEnd: e, value } = target;
    if (e > s) {
      const text = value.slice(s, e).trim();
      if (text) return [text, esc(text), target.getBoundingClientRect()];
    }
  }
  const sel = getSelection();
  if (sel && !sel.isCollapsed) {
    const text = sel.toString().trim();
    if (text) {
      const r = sel.getRangeAt(0), div = document.createElement('div');
      div.appendChild(r.cloneContents());
      return [text, div.innerHTML || esc(text), r.getBoundingClientRect()];
    }
  }
  return null;
}

document.addEventListener('mousedown', e => {
  if (!menuGlobal?.contains(e.target) && !accPanel?.contains(e.target)) closeMenu();
}, true);

document.addEventListener('mouseup', e => {
  if (menuGlobal?.contains(e.target) || accPanel?.contains(e.target)) return;
  if (!cfg.master || !cfg.menu) return;
  const info = getInfo(e.target);
  if (info) openMenu(...info);
}, true);

// 【修复】去掉 keydown 监听，仅保留 copy 事件，避免 Ctrl+C 双重写入剪贴板
document.addEventListener('copy', e => {
  if (!cfg.master || !cfg.append) return;
  const sel = getSelection();
  if (!sel?.toString().trim()) return;
  const text = sel.toString();
  const div = document.createElement('div');
  div.appendChild(sel.getRangeAt(0).cloneContents());
  const url = location.href;
  e.preventDefault();
  e.stopImmediatePropagation();
  e.clipboardData.setData('text/plain', text + `\n\n来自 ${url}`);
  e.clipboardData.setData('text/html',  (div.innerHTML || esc(text)) + `<br><br>来自 <a href="${esc(url)}">${esc(url)}</a>`);
}, true);