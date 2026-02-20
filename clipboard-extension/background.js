// 初始化默认状态：启用
chrome.runtime.onInstalled.addListener(() => {
  chrome.storage.local.set({ enabled: true });
  setIcon(true);
});

// 启动时同步图标状态（防止图标与实际状态不一致）
chrome.storage.local.get('enabled', ({ enabled }) => {
  setIcon(enabled !== false);
});

// 监听图标点击事件
chrome.action.onClicked.addListener(async () => {
  const { enabled } = await chrome.storage.local.get('enabled');
  const newState = !enabled;
  await chrome.storage.local.set({ enabled: newState });
  setIcon(newState);
});

function setIcon(enabled) {
  const iconSet = enabled
    ? { 16: 'icons/icon-on-16.png', 48: 'icons/icon-on-48.png', 128: 'icons/icon-on-128.png' }
    : { 16: 'icons/icon-off-16.png', 48: 'icons/icon-off-48.png', 128: 'icons/icon-off-128.png' };

  const title = enabled
    ? 'CopyWithSource：已启用（点击关闭）'
    : 'CopyWithSource：已关闭（点击开启）';

  chrome.action.setIcon({ path: iconSet });
  chrome.action.setTitle({ title });
}
