// ============================================================
// CopyWithSource v2 - Content Script
// ① 选中文字松开鼠标 → 弹出复制菜单（正上方）
// ② Ctrl+C 自动附加来源（可独立开关）
// ③ 所有功能受总开关控制
// ============================================================

let menu  = null;
let toast = null;

// 状态缓存，用于同步判断，避免异步导致拦截失效
let appState = {
  master: true,
  menuOn: true,
  autoAppend: true
};

function updateState() {
  chrome.storage.local.get(['masterEnabled', 'menuEnabled', 'autoAppend'], (data) => {
    appState.master = data.masterEnabled !== false;
    appState.menuOn = data.menuEnabled !== false;
    appState.autoAppend = data.autoAppend !== false;
  });
}

// 初始化状态并监听变化
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

async function writeToClipboard(plain, html) {
  try {
    const item = new ClipboardItem({
      'text/plain': new Blob([plain], { type: 'text/plain' }),
      'text/html':  new Blob([html],  { type: 'text/html'  }),
    });
    await navigator.clipboard.write([item]);
  } catch (err) {
    console.error('Clipboard write failed:', err);
    // 回退方案
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
  document.body.appendChild(toast);
  setTimeout(() => { toast && toast.remove(); toast = null; }, 1500);
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

// ── 自动附加来源 ──────────────────────────────────────────

document.addEventListener('copy', (event) => {
  if (!appState.master || !appState.autoAppend) return;

  const selection = window.getSelection();
  if (!selection || selection.isCollapsed || !selection.toString().trim()) return;

  const selectedText = selection.toString();
  const selectedHtml = getSelectionHtml(selection);
  const pageUrl      = window.location.href;
  const sourceText   = `\n\n内容来源：- ${pageUrl}`;
  const sourceHtml   = `<br><br>内容来源：- <a href="${escapeHtml(pageUrl)}">${escapeHtml(pageUrl)}</a>`;

  // 同步设置数据，确保拦截生效
  event.clipboardData.setData('text/plain', selectedText + sourceText);
  event.clipboardData.setData('text/html',  selectedHtml + sourceHtml);
  event.preventDefault();
  event.stopImmediatePropagation();
}, true);

document.addEventListener('keydown', (event) => {
  const key = event.key.toLowerCase();
  const isCopy  = (event.ctrlKey || event.metaKey) && key === 'c' && !event.shiftKey && !event.altKey;
  const isSave  = (event.ctrlKey || event.metaKey) && key === 's' && !event.shiftKey && !event.altKey;

  if (isCopy) {
    if (!appState.master || !appState.autoAppend) return;

    // 注意：keydown 里的 Ctrl+C 通常不需要手动 writeToClipboard，
    // 因为上面的 'copy' 事件监听器已经处理了。
    // 手动调用反而可能导致某些浏览器权限警告或重复操作。
  } else if (isSave) {
    if (!appState.master) return;

    const selection = window.getSelection();
    const selectedText = selection.toString().trim();

    // 只有在有选中内容时才拦截并执行插件保存功能
    if (selectedText) {
      event.preventDefault();
      event.stopPropagation();
      event.stopImmediatePropagation();

      const range = selection.getRangeAt(0);
      const rect  = range.getBoundingClientRect();

      chrome.runtime.sendMessage({
        action: 'add_note',
        data: {
          content: selectedText,
          url: window.location.href,
          pageTitle: document.title
        }
      }, (response) => {
        if (response && response.success) {
          showToast('🚀 已直接发送到 RapidNotes', rect.left, rect.top + window.scrollY - 36);
        } else {
          showToast('❌ 发送失败 (请检查桌端服务)', rect.left, rect.top + window.scrollY - 36);
        }
      });
    }
    // 如果没有选中内容，不进行任何操作，允许浏览器执行默认的“另存为”
  }
}, true);
