# Cableado y Datos Validados — Módulo SÓVICA (24LC128) + ESP8266 D1 Mini

Fecha validación: 2026-09-15 (lectura y clonado CONFIRMADOS).

## Conexión MEMORIA → D1 Mini (validada y funcional)

| Memoria 24LC128 | → | D1 Mini |
|---|---|---|
| Pin 8 (VCC) | → | 5V (VIN) |
| Pin 4 | → | GND |
| Pin 5 (SDA) | → | D2 (GPIO4) |
| Pin 6 (SCL) | → | D1 (GPIO5) |
| Pin 7 (WP) | → | GND |
| Pull-ups 10kΩ | → | SDA→5V y SCL→5V (bus a 5V) |

## Por qué importan los 5V

- Chip a VCC=5V → umbral de "alto" = 3,5V. Con pull-ups a 3,3V el bus NO llegaba a 3,5V → sin ACK.
- Con pull-ups a 5V (y VCC 5V) responde. Los pines GPIO4/5 del ESP8266 leen bien ese bus alto.

## Pines I2C recomendados ESP8266 (standard comunidad)

| Señal | GPIO | Pin de placa |
|---|---|---|
| SDA | GPIO4 | D2 |
| SCL | GPIO5 | D1 |

- Evitar GPIO2 (boot + LED), GPIO3 (RX), GPIO1 (TX) → fallan o interfieren.
- ESP8266 no tiene I2C por hardware: usar software I2C bit-bang (como nuestros firmwares).

## Memoria leída → Respaldo

- Archivo: `respaldo eeprom/sovica_memoria_GPIO4-5_16k.bin` (16384 bytes)
- md5: `95d6fe43f57f86250b3d799b14026d2b`
- Contenido clave:
  - Fragmentos = respaldo memoria 1/2/3: `9B4AE800 984AE800 | A2481300 00000000 | 5A000000 00000000`
  - Master @0x3FFC: `04 03 02 01` ("1234")
  - Slots ocupados: 558

## Clonado (segunda memoria)

- Resultado: CLONADO Y VERIFICADO 16384/16384 bytes idénticos.
- Clave maestra destino: `04 03 02 01` ("1234").

## Formato de las llaves y del display (CONFIRMADO 2026-09-15)

Experimentación controlada: se borraron las cédulas 0–16 y se programaron 2 llaves con el programador SÓVICA.

- **Llave física = Dallas iButton**, ROM de 8 bytes `[familia 0x81][serie 6B][CRC-8 Dallas]`.
  - Llave 1: `81 A2 48 13 10 50 03 20` (CRC `20` ✓)
  - Llave 2: `81 72 66 21 10 50 03 F1` (CRC `F1` ✓)
- **Los 3 últimos bytes de la serie son constantes** en este módulo: `10 50 03` (bytes 3–5). El módulo solo distingue por los 3 primeros bytes de la serie.
- **Slot EEPROM (4 bytes)** = `[serie[0] serie[1] serie[2] pad]`, con `pad = 0x00` (esta memoria) o `0x08` (variante de respaldos).
- **Caso perf ecto**:

| Serie (3B únicos) | Slot @ EEPROM | Display programador |
|---|---|---|
| `A2 48 13` | `A2 48 13 00` @ 0x0000 | `01348A2` |
| `72 66 21` | `72 66 21 00` @ 0x0004 | `0216672` |

- **Display = hex de 7 dígitos de `LE24(slot[0..2])`** (los 3 bytes del slot leídos little-endian):
  - `A2 48 13 00` → `0x1348A2` → `01348A2`
  - `72 66 21 00` → `0x216672` → `0216672`
- Los valores viejos `00054FC` / `018C77F…` NO encajan con esta fórmula (probablemente leídos en otro modo/pantalla); descartarlos.

## Firmwares

| Proyecto | Uso | Pines |
|---|---|---|
| `sovica-i2cprobe` | Probe/dump 16K | GPIO4/5 |
| `sovica-clone` | Clonado (dump en `src/mem_dump.h`) | GPIO4/5 |
| `sovica-dump` | Dump original (MODO VIEJO) | GPIO3/2 — NO usar |
| `sovica-1wire` | Dump 16K + lector multi-llave (dump auto prueba ambos órdenes D1/D2) | EEPROM D1/D2, 1-Wire D5 (GPIO14) |
| `sovica-1wire-lector` | Lector 1-Wire sin timeout (lectura series de llaves) | D5 (GPIO14) |

## Notas de hardware

- La primera vez que este ESP32 ("esp32u") se probó quedó en modo descarga / bootloader en bucle: NO es apto para este trabajo (board clon/flaky). Usar el D1 Mini.
- El ESP32 quedó flasheado con firmwares de prueba; no usar para clonado.