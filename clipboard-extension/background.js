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

function setIcon(enabled) {
  const iconSet = enabled
    ? { 16: 'icons/icon-on-16.png', 48: 'icons/icon-on-48.png', 128: 'icons/icon-on-128.png' }
    : { 16: 'icons/icon-off-16.png', 48: 'icons/icon-off-48.png', 128: 'icons/icon-off-128.png' };
  chrome.action.setIcon({ path: iconSet });
}
