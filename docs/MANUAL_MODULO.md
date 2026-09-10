# Manual del Módulo GEYLCA (Firmware)

Firmware para ESP32: control de acceso por llave iButton (Dallas 1-Wire) con gestión de casillas (slots) y comunicación MQTT.

## Conexiones del hardware

| Pin ESP32 | Función |
|-----------|---------------------------|
| GPIO 14   | iButton (Dallas, 1-Wire, pull-up 4.7k a 3.3 V) |
| GPIO 25   | I2C SDA (EEPROM 24C256, dirección 0x50) |
| GPIO 26   | I2C SCL (EEPROM 24C256) |
| GPIO 27   | Relé de apertura de puerta |
| GPIO 13   | LED de estado |
| GPIO 4    | Botón de programación |

La EEPROM guarda las casillas (hasta 4000). No es la flash del ESP32: los datos persisten aunque se reinstale el firmware o se borre la partición OTA.

## Instalación del firmware (por USB)

1. Instalar [PlatformIO](https://platformio.org/).
2. Abrir el proyecto (carpeta raíz) y conectar el ESP32 por USB.
3. Compilar y subir:

   ```bash
   pio run -t upload
   ```

4. Abrir el monitor serie (`pio device monitor`) para ver el arranque (versión, MAC, deviceId).

## Configuración WiFi y secretos

El módulo, la primera vez, arranca en modo AP (punto de acceso). Conectarse a su red y configurar:
- WiFi (SSID / contraseña) a la que se conectará el módulo.
- Device ID (se auto-genera por MAC: `mod_XXXXXXXX`; se puede renombrar).
- Secret maestro (clave HMAC de máximo nivel).

Los datos quedan guardados en la NVS de la ESP32. Comandos de mantenimiento por MQTT: `reset_wifi`, `factory_reset`.

## Claves de roles (v2.2.0)

El módulo autentica por HMAC las claves de los roles. Acciones por MQTT (todas sobre `geylca/<id>/cmd` con firma según rol):

- `verify_role` — con `action + timestamp + token`; el módulo prueba la clave contra `admin`, `installer` y el secret maestro, y responde `{"status":"OK","message":"Role verified","role":"…"}` en `status_resp`.
- `set_role_key` — recibe **`target_role`** (`admin`/`installer`) y `key` (mín. 16 alfanuméricos). Permiso: master/admin pueden asignar cualquier rol, el instalador solo su propio rol. Respuesta `{"status":"OK","message":"<rol> key updated"}`.
- `get_role_keys` — perfilado por rol: master ve las 3 claves, admin ve admin+installer, instalador ve solo la suya.
- `reset_installer_key` — solo master; deja la clave de instalador en `123456` (par default de restablecimiento; luego admin/instalador puede cambiarla).

## Versiones y OTA

Cada versión de firmware se publica en **GitHub Releases**:
`https://github.com/Gelbittar/FIRMWARE_MODULO_KEY.PRO`

Archivo por versión: `firmware.bin`.
La consola maestra puede actualizar el módulo por aire (OTA) con la URL del release:
`https://github.com/Gelbittar/FIRMWARE_MODULO_KEY.PRO/releases/download/vX.Y.Z/firmware.bin`

El ESP32 tiene particiones **app0 / app1 / otadata**: si un OTA falla, el módulo vuelve solo a la versión anterior.

## fallas reales y soluciones

| Síntoma | Causa | Solución |
|---|---|---|
| El módulo no arranca tras un OTA (dice firmware viejo o `t: -1`) | La partición `otadata` quedó apuntando a la app equivocada y el sketch previo no la leía | Borrar `otadata` (rellenar 0xFF) con `python esptool.py erase_region 0xe000 0x2000` y reintentar el OTA |
| No conecta al broker | WiFi mal configurada o DNS transitorio | Reconfigurar WiFi desde AP; reintentar; verificar la red |
| La lectura de la primera llave tardaba ~1.2 s | El firmware escaneaba toda la EEPROM de forma lineal | Ya optimizado: usa un mapa de casillas ocupadas en RAM (se reconstruye una vez por sesión) |
| Errores intermitentes de I2C/EEPROM | La EEPROM 24C256 sufre al leer bloques grandes seguidos | El firmware lee por bloques pequeños con pausas (`delay(2)`) |
| `get_slots` publicaba 63+ lotes | Se enviaban lote por lote aunque estuvieran vacíos | Ya optimizado: sólo publica los lotes que tienen casillas (2 lotes típicos) |
| Mensajes `DENIED` al operar | HMAC/reloj desincronizado | El web/app y el módulo deben tener hora cercana; el web sincroniza con NTP del dispositivo |
| Límite de publicaciones del broker | Brokers públicos limitan mensajes (p. ej. 10 por 60 s) | Cuidado con `get_slots` seguidos; esperar ~5 min si el broker corta |
| El instalador no entra con su clave tras un reset | La clave de instalador volvió a `123456` | Reasignar una clave nueva desde Master (Rol/Reset) o desde Roles en la app operador |

## Datos de fábrica (para pruebas)

- deviceId: auto por MAC (`mod_XXXXXXXX`, p. ej. `mod_D8C8F54C`).
- secret master: `4CF5C8D8CBB0_1069` (de una unidad de prueba; en producción cada módulo genera el suyo por MAC+tiempo).
- Clave de instalador tras `reset_installer_key`: `123456`.
- Broker: `broker.hivemq.com` (MQTT 1883 / TLS 8883 / WSS 8884).