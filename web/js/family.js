const FamilyManager = (function() {
  'use strict';

  const STORAGE_KEYS = {
    FAMILIES: 'geylca_families',
    ACTIVE_FAMILY: 'geylca_active_family',
    MODULE_REGISTRY: 'geylca_module_registry',
  };

  function getFamilies() {
    try {
      return JSON.parse(localStorage.getItem(STORAGE_KEYS.FAMILIES) || '[]');
    } catch { return []; }
  }

  function saveFamilies(families) {
    localStorage.setItem(STORAGE_KEYS.FAMILIES, JSON.stringify(families));
  }

  function getActiveFamilyId() {
    return localStorage.getItem(STORAGE_KEYS.ACTIVE_FAMILY);
  }

  function setActiveFamily(familyId) {
    localStorage.setItem(STORAGE_KEYS.ACTIVE_FAMILY, familyId);
  }

  function getModuleRegistry() {
    try {
      return JSON.parse(localStorage.getItem(STORAGE_KEYS.MODULE_REGISTRY) || '{}');
    } catch { return {}; }
  }

  function saveModuleRegistry(registry) {
    localStorage.setItem(STORAGE_KEYS.MODULE_REGISTRY, JSON.stringify(registry));
  }

  function registerModule(module) {
    const registry = getModuleRegistry();
    registry[module.id] = {
      ...module,
      registeredAt: Date.now(),
      lastSeen: Date.now(),
    };
    saveModuleRegistry(registry);
  }

  function getRegisteredModules() {
    return Object.values(getModuleRegistry());
  }

  function createFamily(name, description = '', color = '#00d4ff') {
    const families = getFamilies();
    const family = {
      id: 'fam_' + Date.now() + '_' + Math.random().toString(36).slice(2, 8),
      name,
      description,
      color,
      modules: [],
      createdAt: Date.now(),
      updatedAt: Date.now(),
    };
    families.push(family);
    saveFamilies(families);
    return family;
  }

  function updateFamily(familyId, updates) {
    const families = getFamilies();
    const idx = families.findIndex(f => f.id === familyId);
    if (idx === -1) return null;
    families[idx] = { ...families[idx], ...updates, updatedAt: Date.now() };
    saveFamilies(families);
    return families[idx];
  }

  function deleteFamily(familyId) {
    const families = getFamilies().filter(f => f.id !== familyId);
    saveFamilies(families);
    if (getActiveFamilyId() === familyId) {
      localStorage.removeItem(STORAGE_KEYS.ACTIVE_FAMILY);
    }
  }

  function addModuleToFamily(familyId, moduleId) {
    const families = getFamilies();
    const family = families.find(f => f.id === familyId);
    if (!family) return false;
    if (!family.modules.includes(moduleId)) {
      family.modules.push(moduleId);
      family.updatedAt = Date.now();
      saveFamilies(families);
    }
    return true;
  }

  function removeModuleFromFamily(familyId, moduleId) {
    const families = getFamilies();
    const family = families.find(f => f.id === familyId);
    if (!family) return false;
    family.modules = family.modules.filter(m => m !== moduleId);
    family.updatedAt = Date.now();
    saveFamilies(families);
    return true;
  }

  function getFamilyModules(familyId) {
    const family = getFamilies().find(f => f.id === familyId);
    if (!family) return [];
    const registry = getModuleRegistry();
    return family.modules.map(id => registry[id]).filter(Boolean);
  }

  function getActiveFamily() {
    const activeId = getActiveFamilyId();
    return activeId ? getFamilies().find(f => f.id === activeId) : null;
  }

  function getActiveFamilyModules() {
    const family = getActiveFamily();
    return family ? getFamilyModules(family.id) : [];
  }

  function copyModuleConfig(sourceModuleId, targetModuleIds) {
    return GEYLCA.sendCommand('backup_config', {}, 'master', null, sourceModuleId)
      .then(backup => {
        const promises = targetModuleIds.map(targetId =>
          GEYLCA.sendCommand('restore_config', {
            config: {
              security_mode: backup.security_mode,
              relay_time: backup.relay_time,
              family_filter: backup.family_filter,
              rewrite_probe: backup.rewrite_probe,
              door_enabled: backup.door_enabled,
              door_timeout: backup.door_timeout,
              door_nc: backup.door_nc,
              buzzer_enabled: backup.buzzer_enabled,
              buzzer_pre: backup.buzzer_pre,
              ntfy_url: backup.ntfy_url,
              ntfy_topic: backup.ntfy_topic,
            },
            slots: backup.slots,
          }, 'master', null, targetId)
        );
        return Promise.all(promises);
      });
  }

  function copyConfigToFamily(familyId, sourceModuleId, options = {}) {
    const { config = true, keys = false } = options;
    const family = getFamilies().find(f => f.id === familyId);
    if (!family) return Promise.reject(new Error('Familia no encontrada'));

    const targetModules = family.modules.filter(m => m !== sourceModuleId);
    if (targetModules.length === 0) return Promise.reject(new Error('No hay otros módulos en la familia'));

    return GEYLCA.sendCommand('backup_config', {}, 'master', null, sourceModuleId)
      .then(backup => {
        const promises = targetModules.map(targetId =>
          GEYLCA.sendCommand('restore_config', {
            config: config ? {
              security_mode: backup.security_mode,
              relay_time: backup.relay_time,
              family_filter: backup.family_filter,
              rewrite_probe: backup.rewrite_probe,
              door_enabled: backup.door_enabled,
              door_timeout: backup.door_timeout,
              door_nc: backup.door_nc,
              buzzer_enabled: backup.buzzer_enabled,
              buzzer_pre: backup.buzzer_pre,
              ntfy_url: backup.ntfy_url,
              ntfy_topic: backup.ntfy_topic,
            } : undefined,
            slots: keys ? (backup.slots || []) : [],
          }, 'master', null, targetId)
        );
        return Promise.all(promises);
      });
  }

  function exportFamily(familyId) {
    const family = getFamilies().find(f => f.id === familyId);
    if (!family) return null;
    const registry = getModuleRegistry();
    const exportData = {
      family: { ...family },
      modules: family.modules.map(id => registry[id]).filter(Boolean),
      exportedAt: Date.now(),
    };
    return exportData;
  }

  function importFamily(data) {
    if (!data.family || !data.modules) return false;
    const families = getFamilies();
    const newFamily = { ...data.family, id: 'fam_' + Date.now(), updatedAt: Date.now() };
    families.push(newFamily);
    saveFamilies(families);

    const registry = getModuleRegistry();
    data.modules.forEach(m => {
      registry[m.id] = { ...m, registeredAt: Date.now() };
    });
    saveModuleRegistry(registry);
    return newFamily;
  }

  return {
    getFamilies,
    getActiveFamily,
    getActiveFamilyModules,
    setActiveFamily,
    createFamily,
    updateFamily,
    deleteFamily,
    addModuleToFamily,
    removeModuleFromFamily,
    getFamilyModules,
    registerModule,
    getRegisteredModules,
    copyModuleConfig,
    copyConfigToFamily,
    exportFamily,
    importFamily,
  };
})();