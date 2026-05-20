// utils/storage.js - 用户profile本地存储工具
// A区2槽位映射到本地：slot0 → wx.getStorageSync('slot0_profile')

const SLOT_KEYS = ['slot0_profile', 'slot1_profile'];

function getCurrentUser() {
  return wx.getStorageSync('current_user') || null;
}

function setCurrentUser(user) {
  if (!user || !user.slot !== undefined) return;
  wx.setStorageSync('current_user', user);
}

function getSlotProfile(slot) {
  if (slot !== 0 && slot !== 1) return null;
  return wx.getStorageSync(SLOT_KEYS[slot]) || null;
}

function saveSlotProfile(slot, profile) {
  if (slot !== 0 && slot !== 1) return;
  wx.setStorageSync(SLOT_KEYS[slot], profile);
}

function clearSlotProfile(slot) {
  if (slot !== 0 && slot !== 1) return;
  wx.removeStorageSync(SLOT_KEYS[slot]);
  const cur = getCurrentUser();
  if (cur && cur.slot === slot) {
    wx.removeStorageSync('current_user');
  }
}

function getActiveSlot() {
  const cur = getCurrentUser();
  return cur ? cur.slot : null;
}

function getAgeGroup(age) {
  if (age < 18) return 0;  // <18
  if (age <= 35) return 1; // 18-35
  if (age <= 55) return 2; // 36-55
  return 3;                 // 56+
}

module.exports = {
  getCurrentUser,
  setCurrentUser,
  getSlotProfile,
  saveSlotProfile,
  clearSlotProfile,
  getActiveSlot,
  getAgeGroup,
  SLOT_KEYS,
};
