# Manual de la Consola Maestra (multi-módulo) GEYLCA

La consola maestra permite **operar todos los módulos del edificio desde un solo lugar**, ya sea en la web o en la app Android, usando la pestaña **Master**.

## Requisito previo

- Soy master en el login (rol "Master") con la **clave maestra del módulo**.
- La clave maestra de cada módulo queda guardada en el registro local (localStorage); no sale del dispositivo.

## Alta de módulos

1. En cada módulo, pestaña **Pairing** → copiar la "data cruda".
2. En la consola maestra → **+ Agregar Módulo** → pegar la data cruda → Agregar.
3. El módulo aparece en la tabla con estado en vivo.

Alternativas:
- **Importar JSON**: pegar una lista de módulos `[{"deviceId":"...","secret":"..."}]`.
- **Exportar JSON**: copiar el registro completo (para pasar a otro celular).

Quitar un módulo del registro (✕) **no borra los datos del módulo**, solo lo quita de la lista.

## Uso

La tabla muestra por módulo: alias/id, estado (ONLINE/OFFLINE), versión de firmware, casillas libres / total de llaves, último evento y acciones.

| Acción | Qué hace |
|---|---|
| **Abrir** | Activa el relé de la puerta |
| **Libres** | Consulta casillas libres |
| **Llaves** | Cuenta todas las llaves grabadas (descarga optimizada de lotes) |
| **Logs** | Solicita los registros recientes |
| **Info** | Pide versión de firmware / datos del dispositivo |
| **OTA** | Actualiza el firmware por aire (ver abajo) |
| **Rol** | Asigna o cambia la clave de un rol (admin/instalador) |
| **Wipe** | Borra todas las casillas del módulo (pide confirmación) |

El módulo en uso (con el que hiciste login) también se muestra en la consola maestra si lo agregaste.

## OTA (actualización por aire)

Al pulsar **OTA**:
1. La consola consulta el último release de GitHub y pre-carga la URL automáticamente.
2. Se puede corregir la URL o poner un MD5 opcional.
3. Aceptar → el módulo descarga `firmware.bin`, verifica el hash (si se envió MD5) y se reinicia con la nueva versión.

URL por versión (misma para todos los módulos):
`https://github.com/Gelbittar/FIRMWARE_MODULO_KEY.PRO/releases/download/vX.Y.Z/firmware.bin`

Si algo sale mal, el ESP32 vuelve solo a la versión anterior (app0/app1). La columna **FW** de la tabla se actualiza con `Info`.

## Estructura de releases del repo

| Archivo | Para qué |
|---|---|
| `firmware.bin` | OTA de los módulos |
| `app.apk` | Consola maestra / app Android |

## fallas reales y soluciones

| Síntoma | Causa | Solución |
|---|---|---|
| El módulo no se marca ONLINE | Broker público con latencia, o módulo apagado | Esperar a que publique su estado (cada reconexión envía `ONLINE`) |
| `DENIED` al enviar un comando | Clave maestra equivocada o reloj desfasado | Verificar el secret del módulo; sincronizar reloj |
| OTA falla con "MD5 mismatch" | Se eligió una URL antigua o se copió mal el MD5 | Volver a usar la predefinida del release |
| La tabla muestra `--` en FW | Todavía no se pidió `Info` | Pulsar Info para refrescar la versión |
| El contador "Llaves" no marca | Libertad del broker: `get_slots` responde por lotes | La descarga completa llega en segundos (2 lotes típicos); mirar "último evento" |

## Buenas prácticas

- Hacer **una** actualización OTA a la vez en cada módulo (el broker público limita mensajes).
- Guardar el export JSON del registro como respaldo.
- Cuando escale a 100+ módulos, usar un broker propio (VPS) en lugar del broker público de prueba.