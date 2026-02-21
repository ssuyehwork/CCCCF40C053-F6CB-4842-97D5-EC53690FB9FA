// ============================================================
// CopyWithSource v2 - Content Script
// ============================================================

let menu  = null;
let toast = null;

// 状态缓存，用于同步判断
let appState = {
  master: true,
  menuOn: true,
  autoAppend: true
};

function updateState() {
  chrome.storage.local.get(['masterEnabled', 'menuEnabled', 'autoAppend'], (data) => {
    if (chrome.runtime.lastError) return;
    appState.master = data.masterEnabled !== false;
    appState.menuOn = data.menuEnabled !== false;
    appState.autoAppend = data.autoAppend !== false;
  });
}

updateState();
chrome.storage.onChanged.addListener(updateState);

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

function getSelectedTextRobust() {
  let text = window.getSelection().toString().trim();
  if (!text) {
    const active = document.activeElement;
    if (active && (active.tagName === 'INPUT' || active.tagName === 'TEXTAREA')) {
      try {
        text = active.value.substring(active.selectionStart, active.selectionEnd).trim();
      } catch (e) {}
    }
  }
  return text;
}

function getSelectionRect() {
  const selection = window.getSelection();
  if (selection.rangeCount > 0) {
    const range = selection.getRangeAt(0);
    const rect = range.getBoundingClientRect();
    if (rect.width > 0 || rect.height > 0) {
        return {
            left: rect.left + window.scrollX,
            top: rect.top + window.scrollY,
            width: rect.width,
            height: rect.height
        };
    }
  }
  const active = document.activeElement;
  if (active && (active.tagName === 'INPUT' || active.tagName === 'TEXTAREA')) {
     const rect = active.getBoundingClientRect();
     return {
        left: rect.left + window.scrollX,
        top: rect.top + window.scrollY,
        width: rect.width,
        height: rect.height
     };
  }
  return { left: window.innerWidth / 2, top: 100, width: 0, height: 0 };
}

async function writeToClipboard(plain, html) {
  try {
    const item = new ClipboardItem({
      'text/plain': new Blob([plain], { type: 'text/plain' }),
      'text/html':  new Blob([html],  { type: 'text/html'  }),
    });
    await navigator.clipboard.write([item]);
  } catch (err) {
    const textArea = document.createElement("textarea");
    textArea.value = plain;
    document.body.appendChild(textArea);
    textArea.select();
    document.execCommand('copy');
    document.body.removeChild(textArea);
  }
}

function showToast(msg, x, y) {
  if (toast) toast.remove();
  toast = document.createElement('div');
  toast.id = 'cws-toast';
  toast.textContent = msg;
  toast.style.left = x + 'px';
  toast.style.top  = y + 'px';
  // 确保 toast 在最前面
  toast.style.zIndex = "2147483647";
  document.body.appendChild(toast);
  setTimeout(() => { if (toast) { toast.remove(); toast = null; } }, 2500);
}

function removeMenu() {
  if (menu) { menu.remove(); menu = null; }
}

// ── 弹出复制菜单 ──────────────────────────────────────────

document.addEventListener('mouseup', (e) => {
  if (menu && menu.contains(e.target)) return;
  removeMenu();

  if (!appState.master || !appState.menuOn) return;

  const selection = window.getSelection();
  if (!selection || selection.isCollapsed || !selection.toString().trim()) return;

  const range = selection.getRangeAt(0);
  const rect  = range.getBoundingClientRect();
  if (!rect.width && !rect.height) return;

  buildMenu(selection, rect);
});

document.addEventListener('mousedown', (e) => {
  if (menu && !menu.contains(e.target)) removeMenu();
});

function buildMenu(selection, rect) {
  const pageUrl      = window.location.href;
  const selectedText = selection.toString();
  const selectedHtml = getSelectionHtml(selection);
  const sourceText   = `\n\n内容来源：- ${pageUrl}`;
  const sourceHtml   = `<br><br>内容来源：- <a href="${escapeHtml(pageUrl)}">${escapeHtml(pageUrl)}</a>`;

  menu = document.createElement('div');
  menu.id = 'cws-menu';
  menu.style.zIndex = "2147483647";

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

// ── 自动附加来源 ──────────────────────────────────────────

document.addEventListener('copy', (event) => {
  if (!appState.master || !appState.autoAppend) return;

  const selection = window.getSelection();
  const selectedText = selection.toString();
  if (!selectedText.trim()) return;

  const selectedHtml = getSelectionHtml(selection);
  const pageUrl      = window.location.href;
  const sourceText   = `\n\n内容来源：- ${pageUrl}`;
  const sourceHtml   = `<br><br>内容来源：- <a href="${escapeHtml(pageUrl)}">${escapeHtml(pageUrl)}</a>`;

  event.clipboardData.setData('text/plain', selectedText + sourceText);
  event.clipboardData.setData('text/html',  selectedHtml + sourceHtml);
  event.preventDefault();
  event.stopImmediatePropagation();
}, true);

// ── Ctrl+S 直接采集 ──────────────────────────────────────

document.addEventListener('keydown', (event) => {
  const isS = event.code === 'KeyS' || event.key.toLowerCase() === 's';
  const isCtrlSave = (event.ctrlKey || event.metaKey) && isS && !event.shiftKey && !event.altKey;

  if (isCtrlSave) {
    // 如果插件总开关没开，放行给浏览器
    if (!appState.master) return;

    // 只要开启了插件，Ctrl+S 就被接管，坚决拦截浏览器的“另存为”
    event.preventDefault();
    event.stopPropagation();
    event.stopImmediatePropagation();

    const selectedText = getSelectedTextRobust();
    const rect = getSelectionRect();

    if (selectedText) {
      chrome.runtime.sendMessage({
        action: 'add_note',
        data: {
          content: selectedText,
          url: window.location.href,
          pageTitle: document.title
        }
      }, (response) => {
        if (response && response.success) {
          showToast('🚀 已成功采集到 RapidNotes', rect.left, rect.top - 40);
        } else {
          showToast('❌ 采集失败，请确保桌面端已启动', rect.left, rect.top - 40);
        }
      });
    } else {
      showToast('⚠️ 请先选中要采集的文字', window.innerWidth / 2 - 100, 100);
    }
  }
}, true);
