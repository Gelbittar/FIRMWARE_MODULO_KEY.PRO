# Manual de la Consola Maestra XSMART KEYPRO (multi-módulo)

Aplicación **aparte** de la app de operador/instalador. Permite administrar **todos los módulos** del edificio desde un solo lugar, además de configurar cada módulo en profundidad (Pairing/QR, relé, modo de seguridad, claves de roles) y hacer OTA.

## Dónde conseguirla

- **Web**: `web/master.html` (se abre en cualquier navegador moderno).
- **App Android (APK)**: se publica como adjunto de cada release en el repositorio público de binarios:
  `https://github.com/Gelbittar/XSMART KEYPRO-Assets/releases/latest` → archivo **`master.apk`**
  - Instalar el APK en el celular del administrador (activar "fuentes desconocidas").

## Clave de la app (local, una vez por instalación)

Al abrir la app la primera vez pide crear la **clave de esta app**: con ella se desbloquea la app en ESE dispositivo (celular/PC). No se sube a ningún servidor; se guarda como hash local junto con una sal aleatoria en el almacenamiento del dispositivo.

- Al reabrir la app, pedirá esa clave para desbloquear (modo **Desbloquear**).
- Si se olvida, no hay forma de recuperarla desde la app: se restablece borrando los datos del sitio/app (se perderá también el registro de módulos guardado).

## Alta de módulos (selector)

Al entrar (con clave de app o recién creada) se muestra el **selector de módulos**:

1. En cada módulo, pestaña **Pairing** → copiar la "data cruda".
2. Pulsar **+ Agregar módulo con Data Cruda** → pegar la data cruda → **Agregar**.
3. El módulo aparece listado; pulsar **Operar** para conectarse a él.

- **Cambiar módulo** (en el encabezado) o **Disconnect** vuelven al **selector de módulos** para cambiar de módulo o salir.
- El registro de módulos (id + secret de cada uno) queda guardado localmente en el dispositivo (localStorage); no se sube a internet.

Alternativas: en la pestaña **Master** del módulo operado se puede **Importar** una lista JSON `[{"deviceId":"...","secret":"..."}]` o **Exportar** el registro (para pasar a otro celular). Quitar un módulo (✕) **no borra los datos del módulo**, solo lo saca del registro.

## Uso de un módulo

La consola maestra opera el módulo seleccionado con el rol **Master** (firma con el secret del módulo). La tabla de la pestaña Master muestra por módulo: alias/id, estado (ONLINE/OFFLINE), versión de firmware, casillas libres / total de llaves, último evento y acciones.

| Acción | Qué hace |
|---|---|
| **Abrir** | Activa el relé de la puerta |
| **Libres** | Consulta casillas libres |
| **Llaves** | Cuenta todas las llaves grabadas (descarga optimizada de lotes) |
| **Logs** | Solicita los registros recientes |
| **Info** | Pide versión de firmware / datos del dispositivo |
| **OTA** | Actualiza el firmware por aire (ver abajo) |
| **Rol** | Asigna o cambia el **PIN (6 dígitos)** de un rol (admin/instalador) |
| **Reset** | Restablece el **PIN de instalador** del módulo a `654321` (fábrica; se envía firmado como master); luego se puede cambiar desde la app operador |
| **Wipe** | Borra todas las casillas del módulo (pide confirmación) |

En **Config → PINs de Roles** del módulo seleccionado también se puede:

- Cambiar el PIN de **admin** o de **instalador** (exactamente 6 dígitos).
- **Reset PIN instalador a 654321** (fábrica del instalador).
- **Reset PIN admin a 123456** (fábrica del administrador) — botón nuevo.

## OTA (actualización por aire)

Al pulsar **OTA**:
1. La consola consulta el último release de GitHub y pre-carga la URL automáticamente.
2. Se puede corregir la URL o poner un MD5 opcional.
3. Aceptar → el módulo descarga `firmware.bin`, verifica el hash (si se envió MD5) y se reinicia con la nueva versión.

URL por versión (misma para todos los módulos):
`https://github.com/Gelbittar/XSMART KEYPRO-Assets/releases/download/vX.Y.Z/firmware.bin`

Si algo sale mal, el ESP32 vuelve solo a la versión anterior (app0/app1). La columna **FW** de la tabla se actualiza con `Info`.

## Estructura de releases

El repositorio de **código fuente** (`FIRMWARE_MODULO_KEY.PRO`) es **privado**; los binarios se distribuyen desde el repositorio **público `XSMART KEYPRO-Assets`**.

| Archivo | Para qué |
|---|---|
| `firmware.bin` | OTA de los módulos |
| `app.apk` | App de operador / instalador / admin |
| `master.apk` | Consola maestra (multi-módulo) |

## fallas reales y soluciones

| Síntoma | Causa | Solución |
|---|---|---|
| Pide "clave de la app" y no es la esperada | Es la clave local del dispositivo, distinta de las claves de rol | Recordar/restablecer la clave local; no es la clave del módulo |
| El módulo no se marca ONLINE | Broker público con latencia, o módulo apagado | Esperar a que publique su estado (cada reconexión envía `ONLINE`) |
| `DENIED` al enviar un comando | Secret del módulo equivocado o reloj desfasado | Verificar el secret del módulo (Pairing); sincronizar reloj |
| Un instalador ya no puede entrar con su clave | Se hizo **Reset** o el admin cambió la clave de instalador | Entregar la clave nueva o volver a asignar una desde Master/Roles |
| OTA falla con "MD5 mismatch" | Se eligió una URL antigua o se copió mal el MD5 | Volver a usar la predefinida del release |
| La tabla muestra `--` en FW | Todavía no se pidió `Info` | Pulsar Info para refrescar la versión |

## Buenas prácticas

- Hacer **una** actualización OTA a la vez en cada módulo (el broker público limita mensajes).
- Guardar el export JSON del registro como respaldo (junto con la clave local de la app).
- Cuando escale a 100+ módulos, usar un broker propio (VPS) en lugar del broker público de prueba.