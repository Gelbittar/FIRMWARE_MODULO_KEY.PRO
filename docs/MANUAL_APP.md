# Manual de la App / Consola Web GEYLCA

Interfaz para operar los módulos GEYLCA desde un navegador (web) o desde la app Android (Capacitor). Misma interfaz, dos formas de usarla.

## Dónde conseguirla

- **Web**: el proyecto la contiene en `web/index.html` (se abre en cualquier navegador moderno, desktop o móvil).
- **App Android (APK)**: se publica como adjunto de cada release:
  `https://github.com/Gelbittar/FIRMWARE_MODULO_KEY.PRO/releases/latest` → archivo `app.apk`
  - En el celular: "Descargar APK" → instalar (activar "instalar desde fuentes desconocidas" en Android).
  - La app es de **debug/sin firmar**: es para uso interno/desarrollo.

## Conectarse a un módulo

Cada módulo muestra en su pestaña **Pairing** la "data cruda":

```json
{"device_id":"mod_XXXXXXXX","secret":"CLAVE_MAESTRA"}
```

En la pantalla de login (web o app) pegar esa data en "Pegar Data Cruda del Módulo" y pulsar "Cargar Data Cruda". Seleccionar el rol:
- **Master** — control total (configuración, OTA, compartir claves admin/instalador).
- **Admin** — administra llaves y casillas.
- **Installer** — solo puede programar/grabar llaves.

El rol y su clave se validan con un código HMAC sobre `acción + timestamp`; si el reloj del dispositivo está desfasado, el módulo rechaza (`DENIED`).

## Pestañas

| Pestaña | Uso |
|---|---|
| **Dashboard** | Estado del broker, total/libres de casillas, modo de seguridad, abrir puerta, eventos recientes |
| **Slots** | Agregar/editar casillas, aprender llave, buscar casilla libre, consultar todas las casillas |
| **Config** (master) | Relé, modo de seguridad (4/8 bytes), claves instalador/admin, mantenimiento (Reset WiFi, borrar casillas, factory reset) |
| **Pairing** (master) | Ver/QR del deviceId + secret para emparejar o dar de alta |
| **Logs** | Solicitar registros del módulo |
| **Consola** | Tráfico MQTT TX/RX en vivo, útil para depurar |
| **Master** (rol master) | Consola maestra multi-módulo (ver manual del master) |

## fallas reales y soluciones

| Síntoma | Causa | Solución |
|---|---|---|
| "La librería MQTT.js no se pudo cargar" | CDN (`unpkg`/`jsdelivr`) bloqueada o sin internet | Recargar; cambiar de red; alojar `mqtt.min.js` localmente |
| Se queda "CONECTANDO" | Puerto WSS 8884 bloqueado (algunas redes corporativas) | Usar otra red; o un broker propio con WSS abierto |
| Módulo sin respuesta aunque el broker diga ONLINE | El módulo no está conectado a la red o reinició | Verificar energía/red del módulo; esperar que vuelva a publicar `ONLINE` |
| "MD5 mismatch o firmware invalido" al hacer OTA | el archivo `.bin` no coincide con el MD5 o la URL es inválida | Verificar URL/MD5 en el release |
| Broker público corta la conexión | Límite de mensajes del broker (p. ej. 10 por 60 s) | Reducir consultas; esperar ~5 minutos |
| Reloj del dispositivo sin sincronizar | NTP tarda hasta ~120 s en ajustarse | Esperar; reabrir la app |

## Consejos

- Los secrets de roles se guardan solo en el dispositivo (localStorage); no compartir capturas del login.
- Para producción se recomienda un broker propio (VPS) en lugar del broker público de prueba.