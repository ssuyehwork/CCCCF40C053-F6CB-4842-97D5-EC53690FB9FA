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
        const includeUrl = cfg.append;
        await fetch('http://localhost:23333/add_note', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                content: content,
                url: includeUrl ? location.href : "",
                pageTitle: document.title
            })
        });
    } catch (e) {
        console.log('[RapidNotes] 数据同步失败，请检查主程序是否运行');
    }
}

// ── Toast ──────────────────────────────────────────────────
let toast = null;
function toast_(msg, rect) {
  toast?.remove();
  toast = Object.assign(document.createElement('div'), { id: 'cws-toast', textContent: msg });
  document.body.appendChild(toast);
  const tw = toast.offsetWidth;
  const th = toast.offsetHeight;
  let l = rect.left + rect.width / 2 - tw / 2;
  l = Math.max(8, Math.min(l, window.innerWidth - tw - 8));
  let t = rect.top - th - 8;
  if (t < 8) t = rect.bottom + 8;
  toast.style.cssText = `left:${l}px;top:${t}px`;
  setTimeout(() => { toast?.remove(); toast = null; }, 1600);
}

// ── 累计面板 ───────────────────────────────────────────────
let accPanel = null;

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

function renderAccPanel() {
  if (!acc || !acc.items || acc.items.length === 0) {
    if (accPanel) { accPanel.remove(); accPanel = null; }
    return;
  }

  const isNew = !accPanel;
  if (isNew) {
    accPanel = document.createElement('div');
    accPanel.id = 'cws-acc-panel';
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

  updatePanelContent();
}

function updatePanelContent() {
  const activeCount = acc.index + 1;

  const titleEl = accPanel.querySelector('#cws-acc-title');
  if (titleEl) titleEl.textContent = `待输出累计 (${activeCount})`;

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

  const btnUndo = accPanel.querySelector('#cws-btn-undo');
  const btnRedo = accPanel.querySelector('#cws-btn-redo');
  const btnOut  = accPanel.querySelector('#cws-btn-out');
  if (btnUndo) btnUndo.disabled = acc.index < 0;
  if (btnRedo) btnRedo.disabled = acc.index >= acc.items.length - 1;
  if (btnOut)  btnOut.disabled  = activeCount === 0;
}

// ── 将内容加入累计（公共逻辑，菜单按钮和拖拽共用）─────────────
function appendToAcc(plain, html) {
  acc.items = acc.items.slice(0, acc.index + 1);
  acc.items.push({ plain, html });
  acc.index++;
  safeSet({ cws_accumulator: { items: [...acc.items], index: acc.index } });
  renderAccPanel();
}

// ── 拖放区（当累计面板不可见时，拖拽期间显示的临时目标）─────────
let dropZone = null;

function showDropZone(x, y) {
  if (dropZone || accPanel) return;
  dropZone = document.createElement('div');
  dropZone.id = 'cws-drop-zone';
  dropZone.textContent = '拖入累计';
  dropZone.style.cssText = `
    position:fixed; z-index:2147483647;
    width:90px; height:36px;
    display:flex; align-items:center; justify-content:center;
    background:#1a1a2e; border:2px dashed #3498db; border-radius:8px;
    color:#3498db; font-size:12px;
    font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;
    pointer-events:all; transition:background 0.15s;
    pointer-events:all;
  `;
  document.body.appendChild(dropZone);
  // 定位在鼠标右下方，避免遮挡拖拽内容
  const dw = 90, dh = 36, offset = 16;
  let lx = x + offset;
  let ly = y + offset;
  if (lx + dw > window.innerWidth  - 8) lx = x - dw - offset;
  if (ly + dh > window.innerHeight - 8) ly = y - dh - offset;
  dropZone.style.left = lx + 'px';
  dropZone.style.top  = ly + 'px';

  dropZone.addEventListener('dragover', e => {
    e.preventDefault();
    e.dataTransfer.dropEffect = 'copy';
    dropZone.style.background = '#0f3460';
  });
  dropZone.addEventListener('dragleave', () => {
    dropZone.style.background = '#1a1a2e';
  });
  dropZone.addEventListener('drop', e => {
    e.preventDefault();
    e.stopPropagation();
    handleDrop(e.dataTransfer);
    hideDropZone();
  });
}

function hideDropZone() {
  dropZone?.remove();
  dropZone = null;
}

function handleDrop(dataTransfer) {
  const plain = (dataTransfer.getData('text/plain') || '').trim();
  const html  = dataTransfer.getData('text/html') || esc(plain);
  if (!plain) return;
  appendToAcc(plain, html);
  // Toast 显示在面板上（面板此时一定存在，因为 appendToAcc 会触发 renderAccPanel）
  if (accPanel) toast_('已拖入累计', accPanel.getBoundingClientRect());
}

function bindPanelEvents() {
  // ── 面板拖动（标题栏）──
  const header = accPanel.querySelector('#cws-acc-header');
  header.onmousedown = (e) => {
    if (e.target.closest('#cws-acc-clear')) return;
    e.preventDefault();

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

  // ── [新增] 面板作为拖放目标 ──
  accPanel.addEventListener('dragover', e => {
    e.preventDefault();
    e.dataTransfer.dropEffect = 'copy';
    accPanel.style.outline = '2px solid #3498db';
  });
  accPanel.addEventListener('dragleave', () => {
    accPanel.style.outline = '';
  });
  accPanel.addEventListener('drop', e => {
    e.preventDefault();
    e.stopPropagation();
    accPanel.style.outline = '';
    handleDrop(e.dataTransfer);
  });
}

// ── 窗口大小变化时重新校正面板位置 ──
window.addEventListener('resize', () => {
  if (!accPanel) return;
  requestAnimationFrame(() => applyPanelPosition());
});

// ── 悬浮菜单 ──────────────────────────────────────────────
let menuGlobal = null;
function closeMenu() { menuGlobal?.remove(); menuGlobal = null; }

async function openMenu(text, html, rect) {
  const menuLocal = document.createElement('div');
  menuLocal.id = 'cws-menu';
  const url = location.href;

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
    ['复制+来源',  SVG.link,  text + `\n\n内容来源：- ${url}`, html + `<br><br>内容来源：- <a href="${esc(url)}">${esc(url)}</a>`,  '已复制（含来源）'],
    ['仅复制链接', SVG.url,   url,                    `<a href="${esc(url)}">${esc(url)}</a>`,                        '已复制链接'],
  ].forEach(([label, svg, plain, rich, msg], i) => {
    if (i) menuLocal.appendChild(Object.assign(document.createElement('div'), { className: 'cws-divider' }));
    const btn = Object.assign(document.createElement('button'), { className: 'cws-btn', innerHTML: svg + label });
    btn.addEventListener('click', async e => {
      e.preventDefault(); e.stopPropagation();
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
    appendToAcc(text, html);
    toast_('已加入累计', rect);
    closeMenu();
  }, true);
  menuLocal.appendChild(btnAppend);

  closeMenu();
  menuGlobal = menuLocal;
  document.body.appendChild(menuGlobal);

  const mw = menuGlobal.offsetWidth, mh = menuGlobal.offsetHeight;
  let l = rect.left + rect.width / 2 - mw / 2;
  l = Math.max(8, Math.min(l, window.innerWidth - mw - 8));
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

// ── [修复1&2] 鼠标事件：区分「空单击」与「有效选中」──────────
//
// 核心思路：
//   空单击    → 鼠标未移动 AND 选区内容没有变化 → 不弹菜单
//   拖拽选中  → 鼠标移动超过阈值              → 弹菜单
//   双击选词  → 鼠标未移动，但选区文本变化了  → 弹菜单
//
// 上一版仅用「鼠标位移」判断，导致双击（无位移）被错误拦截。
let mouseDownPos = null;
let selAtMouseDown = '';        // 记录 mousedown 时的选区内容
const DRAG_THRESHOLD = 5;       // 像素，区分单击与拖拽的最小位移

document.addEventListener('mousedown', e => {
  if (!menuGlobal?.contains(e.target) && !accPanel?.contains(e.target)) closeMenu();
  mouseDownPos = { x: e.clientX, y: e.clientY };
  selAtMouseDown = getSelection()?.toString() ?? '';
}, true);

document.addEventListener('mouseup', e => {
  if (menuGlobal?.contains(e.target) || accPanel?.contains(e.target)) return;
  if (!cfg.master || !cfg.menu) return;

  const dx = Math.abs(e.clientX - (mouseDownPos?.x ?? e.clientX));
  const dy = Math.abs(e.clientY - (mouseDownPos?.y ?? e.clientY));
  const mouseMoved  = dx >= DRAG_THRESHOLD || dy >= DRAG_THRESHOLD;

  // 当前选区（mouseup 后浏览器已更新选区）
  const currentSel  = getSelection()?.toString() ?? '';
  const selChanged  = currentSel.trim() !== '' && currentSel !== selAtMouseDown;

  // 空单击：无移动 + 选区无变化 → 忽略
  if (!mouseMoved && !selChanged) return;

  const info = getInfo(e.target);
  if (info) {
    // [修复1] 阻断同文档内其他 mouseup 监听器（含 RapidNotes content script），防止双重菜单
    e.stopImmediatePropagation();
    openMenu(...info);
  }
}, true);

// ── [新增] 拖拽支持：捕获选中文本拖入累计面板 ──────────────────
// 浏览器对已选中文本的拖拽是原生行为，dataTransfer 里已包含文字和 HTML
document.addEventListener('dragstart', e => {
  // 拖拽开始时，若面板不可见则显示临时拖放区
  if (!accPanel) showDropZone(e.clientX, e.clientY);
}, true);

document.addEventListener('dragend', () => {
  // 拖拽结束后，清理临时拖放区（若内容已 drop 则在 drop 回调中已清理）
  hideDropZone();
}, true);

// ── Ctrl+C 自动附加来源 ──────────────────────────────────
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
  e.clipboardData.setData('text/plain', text + `\n\n内容来源：- ${url}`);
  e.clipboardData.setData('text/html',  (div.innerHTML || esc(text)) + `<br><br>内容来源：- <a href="${esc(url)}">${esc(url)}</a>`);
}, true);