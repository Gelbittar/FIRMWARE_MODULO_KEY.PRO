const BiometricAuth = (function() {
  'use strict';

  let isAvailable = false;
  let isEnrolled = false;
  let currentUserId = null;

  const STORAGE_KEYS = {
    BIOMETRIC_ENABLED: 'geylca_biometric_enabled',
    BIOMETRIC_USER: 'geylca_biometric_user',
    BIOMETRIC_CREDENTIALS: 'geylca_biometric_creds',
  };

  async function checkAvailability() {
    try {
      if (typeof Capacitor === 'undefined' || !Capacitor.Plugins?.NativeBiometric) {
        console.warn('[Biometric] Plugin no disponible (¿web?)');
        return false;
      }
      const { NativeBiometric } = Capacitor.Plugins;
      const result = await NativeBiometric.isAvailable();
      isAvailable = result.isAvailable;
      isEnrolled = result.isEnrolled || false;
      return isAvailable;
    } catch (e) {
      console.error('[Biometric] Error checking availability:', e);
      isAvailable = false;
      return false;
    }
  }

  async function checkEnrollment() {
    try {
      if (!isAvailable) await checkAvailability();
      const { NativeBiometric } = Capacitor.Plugins;
      const result = await NativeBiometric.isEnrolled();
      isEnrolled = result.isEnrolled || false;
      return isEnrolled;
    } catch (e) {
      console.error('[Biometric] Error checking enrollment:', e);
      isEnrolled = false;
      return false;
    }
  }

  async function enableBiometric(userId, credentials) {
    try {
      const available = await checkAvailability();
      if (!available) {
        throw new Error('Biometría no disponible en este dispositivo');
      }
      const enrolled = await checkEnrollment();
      if (!enrolled) {
        throw new Error('No hay biometría registrada en el dispositivo. Configúrala en Ajustes del sistema.');
      }

      const { NativeBiometric } = Capacitor.Plugins;
      await NativeBiometric.verifyIdentity({
        reason: 'Autorizar acceso biométrico a GEYLCA',
        title: 'Autenticación GEYLCA',
        subtitle: 'Usa tu huella/rostro para desbloquear',
        fallbackTitle: 'Usar PIN',
      });

      localStorage.setItem(STORAGE_KEYS.BIOMETRIC_ENABLED, 'true');
      localStorage.setItem(STORAGE_KEYS.BIOMETRIC_USER, userId);
      localStorage.setItem(STORAGE_KEYS.BIOMETRIC_CREDENTIALS, JSON.stringify(credentials));
      currentUserId = userId;
      return true;
    } catch (e) {
      console.error('[Biometric] Error enabling:', e);
      throw e;
    }
  }

  async function authenticate(userId) {
    try {
      if (!isAvailable) await checkAvailability();
      if (!isEnrolled) await checkEnrollment();

      const enabled = localStorage.getItem(STORAGE_KEYS.BIOMETRIC_ENABLED) === 'true';
      const storedUser = localStorage.getItem(STORAGE_KEYS.BIOMETRIC_USER);
      if (!enabled || storedUser !== userId) {
        throw new Error('Biometría no configurada para este usuario');
      }

      const { NativeBiometric } = Capacitor.Plugins;
      await NativeBiometric.verifyIdentity({
        reason: 'Desbloquear GEYLCA',
        title: 'Autenticación requerida',
        subtitle: 'Verifica tu identidad para acceder',
        fallbackTitle: 'Usar PIN',
      });

      const creds = JSON.parse(localStorage.getItem(STORAGE_KEYS.BIOMETRIC_CREDENTIALS) || '{}');
      return creds;
    } catch (e) {
      console.error('[Biometric] Error authenticating:', e);
      throw e;
    }
  }

  async function quickUnlock(userId) {
    try {
      const enabled = localStorage.getItem(STORAGE_KEYS.BIOMETRIC_ENABLED) === 'true';
      const storedUser = localStorage.getItem(STORAGE_KEYS.BIOMETRIC_USER);
      if (!enabled || storedUser !== userId) return null;

      const { NativeBiometric } = Capacitor.Plugins;
      await NativeBiometric.verifyIdentity({
        reason: 'Acceso rápido',
        title: 'GEYLCA',
        subtitle: '',
        fallbackTitle: 'PIN',
      });

      return JSON.parse(localStorage.getItem(STORAGE_KEYS.BIOMETRIC_CREDENTIALS) || '{}');
    } catch (e) {
      return null;
    }
  }

  function disableBiometric() {
    localStorage.removeItem(STORAGE_KEYS.BIOMETRIC_ENABLED);
    localStorage.removeItem(STORAGE_KEYS.BIOMETRIC_USER);
    localStorage.removeItem(STORAGE_KEYS.BIOMETRIC_CREDENTIALS);
    currentUserId = null;
  }

  function getStatus() {
    return {
      available: isAvailable,
      enrolled: isEnrolled,
      enabled: localStorage.getItem(STORAGE_KEYS.BIOMETRIC_ENABLED) === 'true',
      user: localStorage.getItem(STORAGE_KEYS.BIOMETRIC_USER),
    };
  }

  async function init() {
    await checkAvailability();
    await checkEnrollment();
    return getStatus();
  }

  return {
    init,
    checkAvailability,
    checkEnrollment,
    enableBiometric,
    authenticate,
    quickUnlock,
    disableBiometric,
    getStatus,
  };
})();