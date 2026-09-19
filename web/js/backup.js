const BackupManager = (function() {
  'use strict';

  const STORAGE_KEY = 'geylca_backups';
  const MAX_BACKUPS = 10;

  function getBackups() {
    try {
      return JSON.parse(localStorage.getItem(STORAGE_KEY) || '[]');
    } catch { return []; }
  }

  function saveBackups(backups) {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(backups.slice(-MAX_BACKUPS)));
  }

  async function createBackup(moduleId, moduleName, includeKeys = true) {
    GEYLCA.showToast('Solicitando backup al módulo...', 'info');
    try {
      const backup = await GEYLCA.sendCommand('backup_config', {});
      const backupData = {
        id: 'backup_' + Date.now(),
        moduleId,
        moduleName,
        timestamp: Date.now(),
        firmwareVersion: backup.version,
        securityMode: backup.security_mode,
        relayTime: backup.relay_time,
        maxSlots: backup.max_slots,
        familyFilter: backup.family_filter,
        rewriteProbe: backup.rewrite_probe,
        doorEnabled: backup.door_enabled,
        doorTimeout: backup.door_timeout,
        doorNC: backup.door_nc,
        buzzerEnabled: backup.buzzer_enabled,
        buzzerPre: backup.buzzer_pre,
        ntfyUrl: backup.ntfy_url,
        ntfyTopic: backup.ntfy_topic,
        adminKey: backup.admin_key,
        installerKey: backup.installer_key,
        slots: includeKeys ? (backup.slots || []) : [],
        slotCount: (backup.slots || []).length,
      };

      const backups = getBackups();
      backups.push(backupData);
      saveBackups(backups);

      GEYLCA.showToast(`Backup creado: ${backupData.slotCount} slots guardados`, 'success');
      return backupData;
    } catch (e) {
      GEYLCA.showToast('Error creando backup: ' + e.message, 'error');
      throw e;
    }
  }

  async function restoreBackup(moduleId, backupData, options = {}) {
    const { restoreConfig = true, restoreKeys = true } = options;
    GEYLCA.showToast('Restaurando backup...', 'info');

    try {
      const payload = {
        config: restoreConfig ? {
          security_mode: backupData.securityMode,
          relay_time: backupData.relayTime,
          family_filter: backupData.familyFilter,
          rewrite_probe: backupData.rewriteProbe,
          door_enabled: backupData.doorEnabled,
          door_timeout: backupData.doorTimeout,
          door_nc: backupData.doorNC,
          buzzer_enabled: backupData.buzzerEnabled,
          buzzer_pre: backupData.buzzerPre,
          ntfy_url: backupData.ntfyUrl,
          ntfy_topic: backupData.ntfyTopic,
          admin_key: backupData.adminKey,
          installer_key: backupData.installerKey,
        } : undefined,
        slots: restoreKeys ? (backupData.slots || []).map(s => ({
          slot: s.slot,
          key: s.key,
          apto: s.apto,
          suspended: s.suspended,
          blocked_reason: s.blocked_reason,
          blocked_ts: s.blocked_ts,
        })) : [],
      };

      const result = await GEYLCA.sendCommand('restore_config', payload, 'master');
      GEYLCA.showToast(`Restaurado: ${result.restored_slots} slots`, 'success');
      return result;
    } catch (e) {
      GEYLCA.showToast('Error restaurando: ' + e.message, 'error');
      throw e;
    }
  }

  function deleteBackup(backupId) {
    const backups = getBackups().filter(b => b.id !== backupId);
    saveBackups(backups);
  }

  function exportBackup(backupId) {
    const backup = getBackups().find(b => b.id === backupId);
    if (!backup) return null;
    const blob = new Blob([JSON.stringify(backup, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `geylca-backup-${backup.moduleName}-${new Date(backup.timestamp).toISOString().slice(0,10)}.json`;
    a.click();
    URL.revokeObjectURL(url);
  }

  async function importBackup(file) {
    return new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onload = e => {
        try {
          const backup = JSON.parse(e.target.result);
          if (!backup.moduleId || !backup.timestamp) throw new Error('Formato inválido');
          const backups = getBackups();
          backup.id = 'backup_' + Date.now();
          backups.push(backup);
          saveBackups(backups);
          GEYLCA.showToast('Backup importado correctamente', 'success');
          resolve(backup);
        } catch (err) {
          GEYLCA.showToast('Error importando: ' + err.message, 'error');
          reject(err);
        }
      };
      reader.onerror = () => reject(new Error('Error leyendo archivo'));
      reader.readAsText(file);
    });
  }

  function getBackupsForModule(moduleId) {
    return getBackups()
      .filter(b => b.moduleId === moduleId)
      .sort((a, b) => b.timestamp - a.timestamp);
  }

  function getAllBackups() {
    return getBackups().sort((a, b) => b.timestamp - a.timestamp);
  }

  function formatBackupForDisplay(backup) {
    return {
      ...backup,
      date: new Date(backup.timestamp).toLocaleString(),
      size: `${(JSON.stringify(backup).length / 1024).toFixed(1)} KB`,
    };
  }

  return {
    createBackup,
    restoreBackup,
    deleteBackup,
    exportBackup,
    importBackup,
    getBackupsForModule,
    getAllBackups,
    formatBackupForDisplay,
  };
})();