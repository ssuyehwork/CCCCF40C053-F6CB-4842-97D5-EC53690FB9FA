// 初始化默认状态
chrome.runtime.onInstalled.addListener(() => {
  chrome.storage.local.set({ masterEnabled: true, autoAppend: true });
  setIcon(true);
});

// 启动时同步图标
chrome.storage.local.get('masterEnabled', ({ masterEnabled }) => {
  setIcon(masterEnabled !== false);
});

// 监听 storage 变化，同步图标颜色
chrome.storage.onChanged.addListener((changes) => {
  if (changes.masterEnabled) {
    setIcon(changes.masterEnabled.newValue);
  }
});

// 监听来自 content.js 的消息，代理 fetch 请求以绕过混合内容限制
chrome.runtime.onMessage.addListener((request, sender, sendResponse) => {
  if (request.action === 'add_note') {
    fetch('http://127.0.0.1:23333/add_note', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(request.data)
    })
    .then(res => {
      if (res.ok) sendResponse({ success: true });
      else sendResponse({ success: false, error: 'Server error' });
    })
    .catch(err => {
      sendResponse({ success: false, error: err.message });
    });
    return true; // 保持通道开启以进行异步响应
  }
});

function setIcon(enabled) {
  const iconSet = enabled
    ? { 16: 'icons/icon-on-16.png', 48: 'icons/icon-on-48.png', 128: 'icons/icon-on-128.png' }
    : { 16: 'icons/icon-off-16.png', 48: 'icons/icon-off-48.png', 128: 'icons/icon-off-128.png' };
  chrome.action.setIcon({ path: iconSet });
}
