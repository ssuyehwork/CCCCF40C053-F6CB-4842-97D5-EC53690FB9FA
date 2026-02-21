// ============================================================
// CopyWithSource v2 - Content Script
// ① 选中文字松开鼠标 → 弹出复制菜单（正上方）
// ② Ctrl+C 自动附加来源（可独立开关）
// ③ 所有功能受总开关控制
// ============================================================

let menu  = null;
let toast = null;

function getSelectionHtml(selection) {
  if (!selection || selection.rangeCount === 0) return '';
  const container = document.createElement('div');
  for (let i = 0; i < selection.rangeCount; i++) {
    container.appendChild(selection.getRangeAt(i).cloneContents());
  }
  return container.innerHTML || escapeHtml(selection.toString());
}

function escapeHtml(str) {
  return str
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

async function writeToClipboard(plain, html) {
  try {
    const item = new ClipboardItem({
      'text/plain': new Blob([plain], { type: 'text/plain' }),
      'text/html':  new Blob([html],  { type: 'text/html'  }),
    });
    await navigator.clipboard.write([item]);
  } catch {
    await navigator.clipboard.writeText(plain).catch(() => {});
  }
}

function showToast(msg, x, y) {
  if (toast) toast.remove();
  toast = document.createElement('div');
  toast.id = 'cws-toast';
  toast.textContent = msg;
  toast.style.left = x + 'px';
  toast.style.top  = y + 'px';
  document.body.appendChild(toast);
  setTimeout(() => { toast && toast.remove(); toast = null; }, 1500);
}

function removeMenu() {
  if (menu) { menu.remove(); menu = null; }
}

function getState(cb) {
  chrome.storage.local.get(['masterEnabled', 'menuEnabled', 'autoAppend'], (data) => {
    cb({
      master:     data.masterEnabled !== false,
      menuOn:     data.menuEnabled   !== false,
      autoAppend: data.autoAppend    !== false,
    });
  });
}

// ── 弹出复制菜单 ──────────────────────────────────────────

document.addEventListener('mouseup', (e) => {
  if (menu && menu.contains(e.target)) return;
  removeMenu();

  getState(({ master, menuOn }) => {
    if (!master || !menuOn) return;

    const selection = window.getSelection();
    if (!selection || selection.isCollapsed || !selection.toString().trim()) return;

    const range = selection.getRangeAt(0);
    const rect  = range.getBoundingClientRect();
    if (!rect.width && !rect.height) return;

    buildMenu(selection, rect);
  });
});

document.addEventListener('mousedown', (e) => {
  if (menu && !menu.contains(e.target)) removeMenu();
});

document.addEventListener('selectionchange', () => {
  const sel = window.getSelection();
  if (!sel || sel.isCollapsed) removeMenu();
});

function buildMenu(selection, rect) {
  const pageUrl      = window.location.href;
  const selectedText = selection.toString();
  const selectedHtml = getSelectionHtml(selection);
  const sourceText   = `\n\n内容来源：- ${pageUrl}`;
  const sourceHtml   = `<br><br>内容来源：- <a href="${escapeHtml(pageUrl)}">${escapeHtml(pageUrl)}</a>`;

  menu = document.createElement('div');
  menu.id = 'cws-menu';

  const buttons = [
    {
      icon: '📋', label: '普通复制',
      action: async () => {
        await writeToClipboard(selectedText, selectedHtml);
        showToast('✓ 已复制', rect.left, rect.top + window.scrollY - 36);
        removeMenu();
      }
    },
    {
      icon: '🔗', label: '复制 + 来源',
      action: async () => {
        await writeToClipboard(selectedText + sourceText, selectedHtml + sourceHtml);
        showToast('✓ 已复制（含来源）', rect.left, rect.top + window.scrollY - 36);
        removeMenu();
      }
    },
    {
      icon: '📌', label: '仅复制链接',
      action: async () => {
        await writeToClipboard(pageUrl, `<a href="${escapeHtml(pageUrl)}">${escapeHtml(pageUrl)}</a>`);
        showToast('✓ 已复制链接', rect.left, rect.top + window.scrollY - 36);
        removeMenu();
      }
    },
  ];

  buttons.forEach((btn, i) => {
    if (i > 0) {
      const divider = document.createElement('div');
      divider.className = 'cws-divider';
      menu.appendChild(divider);
    }
    const el = document.createElement('button');
    el.className = 'cws-btn';
    el.innerHTML = `<span class="cws-icon">${btn.icon}</span>${btn.label}`;
    el.addEventListener('mousedown', (e) => { e.preventDefault(); e.stopPropagation(); });
    el.addEventListener('click', btn.action);
    menu.appendChild(el);
  });

  document.body.appendChild(menu);

  const menuW = menu.offsetWidth;
  const menuH = menu.offsetHeight;
  const gap   = 8;

  let left = rect.left + window.scrollX + (rect.width / 2) - (menuW / 2);
  let top  = rect.top  + window.scrollY - menuH - gap;

  left = Math.max(8, Math.min(left, window.innerWidth - menuW - 8));
  if (top < window.scrollY + 8) top = rect.bottom + window.scrollY + gap;

  menu.style.left = left + 'px';
  menu.style.top  = top  + 'px';
}

// ── 自动附加来源（copy 事件 + Ctrl+C）────────────────────

document.addEventListener('copy', (event) => {
  getState(({ master, autoAppend }) => {
    if (!master || !autoAppend) return;

    const selection = window.getSelection();
    if (!selection || selection.isCollapsed || !selection.toString().trim()) return;

    const selectedText = selection.toString();
    const selectedHtml = getSelectionHtml(selection);
    const pageUrl      = window.location.href;
    const sourceText   = `\n\n内容来源：- ${pageUrl}`;
    const sourceHtml   = `<br><br>内容来源：- <a href="${escapeHtml(pageUrl)}">${escapeHtml(pageUrl)}</a>`;

    event.preventDefault();
    event.stopImmediatePropagation();
    event.clipboardData.setData('text/plain', selectedText + sourceText);
    event.clipboardData.setData('text/html',  selectedHtml + sourceHtml);
  });
}, true);

document.addEventListener('keydown', (event) => {
  const isCopy = (event.ctrlKey || event.metaKey) && event.key === 'c' && !event.shiftKey && !event.altKey;
  if (!isCopy) return;

  getState(({ master, autoAppend }) => {
    if (!master || !autoAppend) return;

    const selection = window.getSelection();
    if (!selection || selection.isCollapsed || !selection.toString().trim()) return;

    const selectedText = selection.toString();
    const selectedHtml = getSelectionHtml(selection);
    const pageUrl      = window.location.href;
    const sourceText   = `\n\n内容来源：- ${pageUrl}`;
    const sourceHtml   = `<br><br>内容来源：- <a href="${escapeHtml(pageUrl)}">${escapeHtml(pageUrl)}</a>`;

    writeToClipboard(selectedText + sourceText, selectedHtml + sourceHtml);
  });
}, true);
