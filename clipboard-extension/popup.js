const masterToggle     = document.getElementById('masterToggle');
const menuToggle       = document.getElementById('menuToggle');
const autoAppendToggle = document.getElementById('autoAppendToggle');
const featureSection   = document.getElementById('featureSection');
const statusBadge      = document.getElementById('statusBadge');

// 读取当前状态
chrome.storage.local.get(['masterEnabled', 'menuEnabled', 'autoAppend'], (data) => {
  const master     = data.masterEnabled !== false;
  const menu       = data.menuEnabled   !== false;
  const autoAppend = data.autoAppend    !== false;

  masterToggle.checked     = master;
  menuToggle.checked       = menu;
  autoAppendToggle.checked = autoAppend;

  updateUI(master);
});

// 总开关
masterToggle.addEventListener('change', () => {
  const val = masterToggle.checked;
  chrome.storage.local.set({ masterEnabled: val });
  updateUI(val);
});

// 弹出菜单开关
menuToggle.addEventListener('change', () => {
  chrome.storage.local.set({ menuEnabled: menuToggle.checked });
});

// 自动附加来源开关
autoAppendToggle.addEventListener('change', () => {
  chrome.storage.local.set({ autoAppend: autoAppendToggle.checked });
});

function updateUI(masterOn) {
  if (masterOn) {
    featureSection.classList.remove('disabled-overlay');
    statusBadge.textContent = '已启用';
    statusBadge.className = 'status-badge badge-on';
  } else {
    featureSection.classList.add('disabled-overlay');
    statusBadge.textContent = '已关闭';
    statusBadge.className = 'status-badge badge-off';
  }
}
