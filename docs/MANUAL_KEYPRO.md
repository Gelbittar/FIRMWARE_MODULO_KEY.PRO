# KeyPro — Módulo de alarma (ESP-01) + App Android

Sistema de alarma basado en módulos ESP-01 con control remoto por app Android, cifrado **AES-256-GCM end-to-end** y emparejamiento por QR único por módulo (para producción en serie).

- Código del firmware: `alarm-keypro/` (este repo privado).
- Código de la app: repo privado `Gelbittar/key_pro`.
- APK pública: `https://github.com/Gelbittar/GEYLCA-Assets/releases/download/kp-v1.4.1/keypro.apk` (descarga sin autenticación).

## Características

- **WiFi**: el módulo arranca con un AP `KeyPro-XXXX` (password `12345678`) para configurar la red hogareña (portal en `192.168.4.1`, time-out 180 s). Configurado, apaga el AP y queda conectado a tu LAN. Botón RESET (GPIO2, 5 s) borra la config y vuelve al portal.
- **Identidad**: `deviceId` `KPxxxx` + `secret` 64 hex generados una vez (aleatorios). La línea `KP_PAIR {...}` se imprime en el Serial (115200) en cada boot para producirlo una sola vez en fábrica; de ahí se genera el QR del módulo.
- **Transporte**: MQTT público `broker.hivemq.com` puerto **8883 TLS** (app usa WSS 8884). Topics `keypro/{dev}/cmd`, `keypro/{dev}/state` (retained), `keypro/{dev}/events`.
- **Cifrado**: AES-256-GCM por mensaje (BearSSL en el módulo, WebCrypto en la app). Envelope `{"v":1,"t":epoch,"n":"1","c":base64(iv(12)+ct+tag(16))}`; AD = `topic|t` (topic completo del mensaje). Anti-replay ±90 s cuando el reloj NTP está sincronizado.

## Mapa de pines (ESP-01, sin expansor; TX/RX funcionan como E/S)

| Pin | Función | Detalle |
|-----|---------|---------|
| GPIO1 (TX) | Relé A ARMA/DESARMA | Salida activo-LOW (PNP high-side). `KP_PAIR` se imprime por Serial antes de habilitarlo. |
| GPIO0 | Relé B PÁNICO | Salida activo-LOW. |
| GPIO3 (RX) | Sensado de sirena | Entrada; HIGH reposo, LOW = pulso activo. |
| GPIO2 | Botón RESET config | INPUT_PULLUP, 5 s → restablecer config. |

Relés OFF en boot (HIGH).

## Sensado de sirena (entrada GPIO3)

El módulo usa **una única ventana de detección** (`senseWindowMs`, por defecto **2000 ms**) ajustable desde la app (Programación → Sensado). Dentro de ese lapso detecta automáticamente y reporta el estado a la app:

- **1 pulso** → **ARMADO** (relé A activa, estado publicado).
- **2 pulsos** → **DESARMADO**.
- **Señal mantenida** (LOW toda la ventana) → **ALARMA ACTIVADA** (evento `ALARM`).

Note: la alarma por sirena sostenida se detecta al completarse la ventana (~2 s), no a los 30 s de versiones anteriores.

## Comandos (menú Control / Programación de la app)

- `arm`, `disarm`, `panic` (relé A, relé A, relé B).
- `get_state` (demanda estado público al módulo; la respuesta incluye la configuración: `relayMode`, `pulseArmMs`, `panicMs`, `senseWindowMs`).
- `set_cfg {relayMode, pulseArmMs, panicMs, senseWindowMs}` (guardado en `/config.json` de LittleFS; migra un `pulseWindowMs` previo como nueva ventana).

## Flasheo y QR de fábrica

1. Compilar: `~/.platformio/penv/bin/pio run` (requiere módulo con flash ≥ 1 MB).
2. Flashear y capturar el pairing:
   `~/.platformio/penv/bin/pio run -t erase` luego `-t upload`; abrir monitor 115200 y copiar la línea `KP_PAIR {"dev":"KPxxxx","secret":"<64hex>"}`.
3. Generar el QR para imprimir y pegar en el módulo:
   `python tools/keypro_qr.py --dev KPxxxx --secret <64hex> --out qr_KPxxxx.png`
   o directo desde el puerto: `python tools/keypro_qr.py --serial /dev/ttyUSB0`.

## App Android (wizard)

1. Instalar `keypro.apk`.
2. Primer uso: **Escaneá el QR** → creá un **PIN de app** (6 dígitos). El secret queda cifrado localmente (PBKDF2 + AES-GCM); nunca viaja a servidores.
3. Menú **Control**: ver estado (ARMADO/DESARMADO/ALARMA), botones ARMAR/DESARMAR/PÁNICO, silenciar sirena.
4. Menú **Programación**: configura salidas y sensado. Menú **Red**: detecta y abre el portal `192.168.4.1` sin teclear IP (Android además muestra la notificación "iniciar sesión en la red" al conectarte al AP KeyPro).

## Seguridad

- El secret del QR es el único secreto maestro del módulo; no se loguea ni se publica (salvo la línea KP_PAIR en el primera boot de producción).
- Cambiar el PIN de la app "restablece" la app (nueva re-provisioning por QR).
- Producción: imprimir 1 QR por módulo y no conservar la lista en texto plano.