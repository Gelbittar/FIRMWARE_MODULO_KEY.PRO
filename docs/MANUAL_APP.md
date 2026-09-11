# Manual de la App / Consola Web GEYLCA (Operador)

Interfaz para **instaladores y administradores** de módulos GEYLCA: operar un módulo a la vez (apertura, casillas, logs). Para administrar **todos los módulos** (consola maestra, OTA, alta de admins) se usa la **app aparte "GEYLCA Master"** (ver MANUAL_MASTER.md).

## Dónde conseguirla

- **Web**: `web/index.html` (se abre en cualquier navegador moderno, desktop o móvil).
- **App Android (APK)**: se publica como adjunto de cada release en el repositorio público de binarios:
  `https://github.com/Gelbittar/GEYLCA-Assets/releases/latest` → archivo **`app.apk`**
  - En el celular: "Descargar APK" → instalar (activar "instalar desde fuentes desconocidas" en Android).
  - La app es de **debug/sin firmar**: es para uso interno/desarrollo.

## Roles (operador)

Esta app permite entrar con los roles **Admin** e **Instalador** (la clave maestra se usa solo en la app Master). Jerarquía: **master > instalador > admin**.

- **Instalador** — acceso operativo completo (abrir, agregar/borrar casillas, aprender llaves, suspender, logs, relé, reset WiFi) y **gestión de PINs**: cambiar el PIN admin, cambiar su propio PIN y resetear el admin a fábrica (`123456`).
- **Admin** — puede abrir para mantenimiento puntual, consultar casillas y **suspender/activar** casillas. No gestiona PINs: la pestaña **Roles**, **Logs** y **Consola** le quedan ocultas, y los botones que su rol no permite no se muestran.

El login usa **un PIN de rol de 6 dígitos** (admin o instalador). El módulo lo valida con HMAC sobre `acción + timestamp` y responde cuál rol le corresponde; si el PIN es del rol **master**, la app lo rechaza y avisa que se use la app Master.

## Primer uso (wizard)

1. Abrir la app por primera vez (sin módulos) → pantalla de **Data Cruda**.
2. Pegar la data cruda copiada de la pestaña **Pairing** del módulo
   `{"device_id":"...","secret":"..."}` y pulsar **Guardar y continuar**.
3. Crear el **PIN de instalador** (6 dígitos) o pulsar **Generar** para autocompletar, y pulsar **Guardar clave instalador** (se envía al módulo firmada como master).
4. El módulo queda agregado a la **familia** y pasa a la pantalla de selección. Pulse **Entrar** sobre el módulo para ir al login de PIN.

> En una familia con varios módulos se recomienda usar el **mismo PIN de instalador** en todos, para que el instalador entre igual en cualquiera.

## Familia de módulos (multi-módulo)

La app operador guarda un **registro de módulos** (familia) en el dispositivo: todos comparten la misma pantalla de login y los mismos roles. Al abrir la app se muestra el **selector de módulos**:

- **Entrar** — va al login de ese módulo (mismo PIN de rol).
- **PIN** — reasigna el PIN de instalador de ese módulo (se envía firmado con el secret maestro del módulo). Útil para volver a dejar el PIN de la familia.
- **X** — quita el módulo del registro (no borra datos del módulo).

Botón **+ Agregar módulo**: repetir el wizard con la data cruda del otro módulo.

- Al pulsar **Disconnect** se cierra la sesión y vuelve al **selector de módulos** (o al wizard si la familia quedó vacía).
- Desde el login, **Configurar otro módulo** abre el mismo selector.
- El registro se guarda en localStorage (`geylca_family`). La familia de la versión previa (un solo módulo en `geylca_configured`) se **migra automáticamente** al abrir la app.

## Login y detección de rol

- Escriba su **PIN de 6 dígitos** (el que le dio el instalador/admin de la obra) y pulse **Ingresar**.
- El módulo responde "Role verified" con el rol: **Administrador** o **Instalador**, y la app muestra solo las funciones de ese rol.
- Si el PIN pertenece al rol **master**, se muestra un aviso: use esa clave en la app **GEYLCA Master**.
- Si el PIN no corresponde al módulo configurado, se vuelve al login con un error.

## Pestañas

| Pestaña | Uso |
|---|---|
| **Dashboard** | Estado del broker, total/libres de casillas, modo de seguridad, abrir puerta, eventos recientes |
| **Slots** | Agregar/editar casillas, aprender llave, buscar casilla libre, consultar todas las casillas |
| **Roles** | (solo instalador) Ver estado de las claves (`get_role_keys`), cambiar el PIN de admin, **Reset PIN admin a 123456** y cambiar el PIN de instalador. El admin no ve esta pestaña |
| **Logs** | Solicitar registros del módulo (solo instalador) |
| **Consola** | Tráfico MQTT TX/RX en vivo, útil para depurar (solo instalador) |

(Config, Pairing y la pestaña Master son de la app **GEYLCA Master**.)

## Claves de roles

En **Roles** (instalador) se puede:

- Pulsar **Ver estado de claves**: el módulo responde qué PINs están configurados (admin/instalador para el instalador).
- Cambiar o **resetear el PIN de administrador** (botón **Reset PIN admin a 123456**). Como el PIN admin se cambia solo con rol instalador o master, un instalador puede bloquear al admin; el reset devuelve `123456`.
- Cambiar el **PIN de instalador**.

Los PINs son de **exactamente 6 dígitos numéricos**. Cada rol usa su PIN como secreto HMAC; si se cambia un PIN, ese rol debe iniciar sesión con el PIN nuevo.

## Interfaz móvil

- La app está rediseñada para uso **vertical en celular**: tema oscuro con acento degradado cian→verde, tira de navegación fija abajo con iconos (botones ≥ 52 px), y las tablas (eventos, logs, casillas) se muestran como **tarjetas** con etiquetas por campo.
- Lo que el rol no permite se **oculta** (no solo se deshabilita): p. ej. el admin no ve el botón "ABRIR PUERTA" ni las pestañas Roles/Logs/Consola.

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