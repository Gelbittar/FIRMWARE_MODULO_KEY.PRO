# Manual de la App / Consola Web GEYLCA (Operador)

Interfaz para **instaladores y administradores** de módulos GEYLCA: operar un módulo a la vez (apertura, casillas, logs). Para administrar **todos los módulos** (consola maestra, OTA, alta de admins) se usa la **app aparte "GEYLCA Master"** (ver MANUAL_MASTER.md).

## Dónde conseguirla

- **Web**: `web/index.html` (se abre en cualquier navegador moderno, desktop o móvil).
- **App Android (APK)**: se publica como adjunto de cada release:
  `https://github.com/Gelbittar/FIRMWARE_MODULO_KEY.PRO/releases/latest` → archivo **`app.apk`**
  - En el celular: "Descargar APK" → instalar (activar "instalar desde fuentes desconocidas" en Android).
  - La app es de **debug/sin firmar**: es para uso interno/desarrollo.

## Roles (operador)

Esta app permite entrar con los roles **Admin** e **Installer** (la clave maestra se usa solo en la app Master).

- **Admin** — administra llaves y casillas (abrir, agregar, suspender, aprender llaves, configurar relé) y las claves de roles.
- **Installer** — solo puede programar/grabar llaves; también puede rotar su propia clave de instalador.

El login usa **una sola clave de rol** (admin o instalador). El módulo la valida con HMAC sobre `acción + timestamp` y responde cuál rol le corresponde; si la clave es del rol **master**, la app lo rechaza y avisa que se use la app Master.

## Primer uso (wizard)

1. Abrir la app por primera vez → pantalla de **Data Cruda**.
2. Pegar la data cruda copiada de la pestaña **Pairing** del módulo
   `{"device_id":"...","secret":"..."}` y pulsar **Guardar y continuar**.
3. Crear la **clave de instalador** (mínimo 16 caracteres alfanuméricos) o pulsar **Generar** para autocompletar, y pulsar **Guardar clave instalador** (se envía al módulo firmada como master).
4. Pasa a la pantalla de **login**: ingresar la clave de instalador (o la de un administrador si ya existe) para identificar su rol.

> El dispositivo queda configurado para ese módulo. Con **Configurar otro módulo** se pasa otro módulo o se repite el wizard.

## Login y detección de rol

- Escriba su clave de rol (la que le dio el instalador/admin de la obra) y pulse **Ingresar**.
- El módulo responde "Role verified" con el rol: **Administrador** o **Instalador**, y la app muestra solo las funciones de ese rol.
- Si la clave pertenece al rol **master**, se muestra un aviso: use esa clave en la app **GEYLCA Master**.
- Si la clave no corresponde al módulo configurado, se vuelve al login con un error.

## Pestañas

| Pestaña | Uso |
|---|---|
| **Dashboard** | Estado del broker, total/libres de casillas, modo de seguridad, abrir puerta, eventos recientes |
| **Slots** | Agregar/editar casillas, aprender llave, buscar casilla libre, consultar todas las casillas |
| **Roles** | Ver estado de las claves (`get_role_keys`) y cambiar la clave de instalador; el administrador también puede cambiar la clave de admin. El instalador solo ve la tarjeta de instalador |
| **Logs** | Solicitar registros del módulo |
| **Consola** | Tráfico MQTT TX/RX en vivo, útil para depurar |

(Config, Pairing y la pestaña Master son de la app **GEYLCA Master**.)

## Claves de roles

En **Roles** quien tenga permiso puede:

- Pulsar **Ver estado de claves**: el módulo responde si cada rol tiene clave configurada (por rol, según el que inicie sesión).
- Generar/cambiar la clave de **administrador** (solo admin; mínimo 16 caracteres alfanuméricos, `-` y `_` permitidos).
- Generar/cambiar la clave de **instalador** (admin o instalador).

Cada rol usa su clave como secreto HMAC; si se cambia la clave de un rol, ese rol debe iniciar sesión con la clave nueva.

## fallas reales y soluciones

| Síntoma | Causa | Solución |
|---|---|---|
| "La librería MQTT.js no se pudo cargar" | CDN (`unpkg`/`jsdelivr`) bloqueada o sin internet | Recargar; cambiar de red; alojar `mqtt.min.js` localmente |
| Se queda "CONECTANDO" | Puerto WSS 8884 bloqueado (algunas redes corporativas) | Usar otra red; o un broker propio con WSS abierto |
| No pudo guardar la clave instalador en el wizard | Módulo apagado, fuera de línea o data cruda incorrecta | Verificar la data cruda (Pairing) y que el módulo esté en línea |
| No se pudo identificar el rol | Clave incorrecta o reloj del módulo desfasado | Reingresar la clave; esperar a que el módulo sincronice NTP (~120 s) |
| El login avisa "pertenece al rol MAESTRO" | Se ingresó el secret maestro (data cruda) | Usar ese secret solo en la app GEYLCA Master |
| Módulo sin respuesta aunque el broker diga ONLINE | El módulo no está conectado a la red o reinició | Verificar energía/red del módulo; esperar que vuelva a publicar `ONLINE` |
| Broker público corta la conexión | Límite de mensajes del broker (p. ej. 10 por 60 s) | Reducir consultas; esperar ~5 minutos |

## Consejos

- Las claves de rol y la configuración se guardan solo en el dispositivo (localStorage); no compartir capturas del login.
- Para producción se recomienda un broker propio (VPS) en lugar del broker público de prueba.