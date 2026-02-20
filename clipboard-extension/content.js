// ============================================================
// CopyWithSource - Content Script
// 双重拦截：copy 事件 + 键盘快捷键，兼容更多网站
// 每次操作直接读取 storage 状态，确保开关即时生效
// ============================================================

// ── 方法一：拦截 copy 事件（捕获阶段，优先于网站自身的处理）──
document.addEventListener('copy', handleCopy, true);

function handleCopy(event) {
  chrome.storage.local.get('enabled', ({ enabled }) => {
    if (!enabled) return;

    const selection = window.getSelection();
    if (!selection || selection.isCollapsed) return;

    const selectedText = selection.toString();
    const selectedHtml = getSelectionHtml(selection);
    if (!selectedText.trim()) return;

    const pageUrl = window.location.href;
    const sourceText = `\n\n内容来源：- ${pageUrl}`;
    const sourceHtml = `<br><br>内容来源：- <a href="${escapeHtml(pageUrl)}">${escapeHtml(pageUrl)}</a>`;

    event.preventDefault();
    event.stopImmediatePropagation();

    event.clipboardData.setData('text/plain', selectedText + sourceText);
    event.clipboardData.setData('text/html', selectedHtml + sourceHtml);
  });
}

// ── 方法二：监听键盘 Ctrl+C / Cmd+C（兼容屏蔽了 copy 事件的网站）──
document.addEventListener('keydown', handleKeydown, true);

function handleKeydown(event) {
  const isCopy = (event.ctrlKey || event.metaKey) && event.key === 'c' && !event.shiftKey && !event.altKey;
  if (!isCopy) return;

  chrome.storage.local.get('enabled', ({ enabled }) => {
    if (!enabled) return;

    const selection = window.getSelection();
    if (!selection || selection.isCollapsed) return;

    const selectedText = selection.toString();
    const selectedHtml = getSelectionHtml(selection);
    if (!selectedText.trim()) return;

    const pageUrl = window.location.href;
    const sourceText = `\n\n内容来源：- ${pageUrl}`;
    const sourceHtml = `<br><br>内容来源：- <a href="${escapeHtml(pageUrl)}">${escapeHtml(pageUrl)}</a>`;

    try {
      const clipboardItem = new ClipboardItem({
        'text/plain': new Blob([selectedText + sourceText], { type: 'text/plain' }),
        'text/html': new Blob([selectedHtml + sourceHtml], { type: 'text/html' }),
      });
      navigator.clipboard.write([clipboardItem]).catch(() => {
        navigator.clipboard.writeText(selectedText + sourceText).catch(() => {});
      });
    } catch (e) {
      navigator.clipboard.writeText(selectedText + sourceText).catch(() => {});
    }
  });
}

/**
 * 将当前选区的内容序列化为 HTML 字符串
 */
function getSelectionHtml(selection) {
  if (!selection || selection.rangeCount === 0) return '';

  const container = document.createElement('div');
  for (let i = 0; i < selection.rangeCount; i++) {
    const range = selection.getRangeAt(i);
    container.appendChild(range.cloneContents());
  }
  return container.innerHTML || escapeHtml(selection.toString());
}

/**
 * HTML 转义，防止 URL 中的特殊字符破坏 HTML 结构
 */
function escapeHtml(str) {
  return str
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}
