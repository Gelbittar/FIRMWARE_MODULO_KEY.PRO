#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FASE 2 / Lote A1 — GEYLCA ONE
Extrae funciones hoja de src/main.cpp hacia modulos usando brace-matching
sobre el CONTENIDO DEL DISCO (nada se transcribe). Tambien elimina las
forward-declarations correspondientes y anade el #include.

Modos:
  python3 tools/loteA1.py --dry         (reporta rangos detectados, no toca)
  python3 tools/loteA1.py               (aplica: backup + rewrite + modulos)

Las funciones en este lote (7 hoja):
  drivers/eeprom.{h,cpp}      writeEEPROM, readEEPROM
  mqtt/security.{h,cpp}       calcularHMAC, checkRateLimit, resetRateLimit,
                              roleRank, hasRolePermission

Pre-condicion verificada en disco:
  calcularHMAC@145, checkRateLimit@166, resetRateLimit@190, roleRank@197,
  hasRolePermission@203, writeEEPROM@207, readEEPROM@218
"""
import re
import sys
from pathlib import Path

BASE = Path("/home/gutavo-elbittar/Documentos/PlatformIO/Projects/proyecto modulo tipo sovica/geylca-one")
MAIN = BASE / "src" / "main.cpp"

DRY = "--dry" in sys.argv
APPLY = not DRY

# ---------------------------------------------------------------------------
# 1) Parseador de bloque minimo: enmascara strings/comentarios para poder
#    contar llaves con seguridad.
# ---------------------------------------------------------------------------
def mask_line(line, in_block):
    out = list(line)
    i, n = 0, len(line)
    while i < n:
        c = line[i]
        if in_block:
            if c == "*" and i + 1 < n and line[i + 1] == "/":
                out[i] = out[i + 1] = " "
                i += 2
                in_block = False
            else:
                out[i] = " "
            i += 1
            continue
        if c == "/" and i + 1 < n and line[i + 1] == "/":
            for j in range(i, n):
                out[j] = " "
            break
        if c == "/" and i + 1 < n and line[i + 1] == "*":
            out[i] = out[i + 1] = " "
            i += 2
            in_block = True
            continue
        if c == '"':
            out[i] = " "
            i += 1
            while i < n:
                if line[i] == "\\":
                    out[i] = " "
                    i += 1
                    if i < n:
                        out[i] = " "
                        i += 1
                    continue
                if line[i] == '"':
                    out[i] = " "
                    i += 1
                    break
                out[i] = " "
                i += 1
            continue
        if c == "'":
            out[i] = " "
            i += 1
            while i < n:
                if line[i] == "\\":
                    out[i] = " "
                    i += 1
                    if i < n:
                        out[i] = " "
                        i += 1
                    continue
                if line[i] == "'":
                    out[i] = " "
                    i += 1
                    break
                out[i] = " "
                i += 1
            continue
        i += 1
    return "".join(out), in_block


def find_range(lines, masked, sig_pattern, hint=0):
    """Busca la linea de firma y devuelve (ini, fin) del cuerpo por
    brace-matching sobre las lineas enmascaradas. Las forward-decls
    terminan en ';' y no se consideran."""
    for i in range(hint, len(lines)):
        if not re.match(sig_pattern, masked[i]):
            continue
        # firma de forward-decl (termina en ;) -> saltar
        if "{" not in masked[i]:
            # puede ser multilinea; comprobar siguientes
            j = i
            while j < len(lines) and "{" not in masked[j] and ";" not in masked[j]:
                j += 1
            if j >= len(lines) or (";" in masked[j] and "{" not in masked[j]):
                continue
        depth = 0
        opened = False
        in_blk = False
        for j in range(i, len(lines)):
            m, in_blk = mask_line(lines[j], in_blk)
            for ch in m:
                if ch == "{":
                    depth += 1
                    opened = True
                elif ch == "}":
                    depth -= 1
            if opened and depth == 0:
                return (i, j)
    return None


# ---------------------------------------------------------------------------
# 2) Plan del lote: (regex firma, carpeta, nombre_modulo)
# ---------------------------------------------------------------------------
PLAN = [
    (r"^void writeEEPROM\(unsigned int eeaddress, byte \*data, int length\)\s*\{?",
     "src/drivers", "eeprom"),
    (r"^void readEEPROM\(unsigned int eeaddress, byte \*buffer, int length\)\s*\{?",
     "src/drivers", "eeprom"),
    (r"^String calcularHMAC\(String payload, String secret\)\s*\{?",
     "src/mqtt", "security"),
    (r"^bool checkRateLimit\(String clientId\)\s*\{?",
     "src/mqtt", "security"),
    (r"^void resetRateLimit\(\)\s*\{?",
     "src/mqtt", "security"),
    (r"^int roleRank\(String role\)\s*\{?",
     "src/mqtt", "security"),
    (r"^bool hasRolePermission\(String requiredRole, String userRole\)\s*\{?",
     "src/mqtt", "security"),
]

# forward-declarations a retirar de main.cpp
FDECL = [
    r"^void writeEEPROM\(unsigned int eeaddress, byte \*data, int length\)\s*;\s*$",
    r"^void readEEPROM\(unsigned int eeaddress, byte \*buffer, int length\)\s*;\s*$",
    r"^String calcularHMAC\(String payload, String secret\)\s*;\s*$",
    r"^bool checkRateLimit\(String clientId\)\s*;\s*$",
    r"^void resetRateLimit\(\)\s*;\s*$",
    r"^int roleRank\(String role\)\s*;\s*$",
    r"^bool hasRolePermission\(String requiredRole, String userRole\)\s*;\s*$",
]

HEADERS = {
    "eeprom": """#ifndef GEYLCA_DRIVERS_EEPROM_H
#define GEYLCA_DRIVERS_EEPROM_H

#include <Arduino.h>

void writeEEPROM(unsigned int eeaddress, byte *data, int length);
void readEEPROM(unsigned int eeaddress, byte *buffer, int length);

#endif
""",
    "security": """#ifndef GEYLCA_MQTT_SECURITY_H
#define GEYLCA_MQTT_SECURITY_H

#include <Arduino.h>
#include <state.h>

String calcularHMAC(String payload, String secret);
bool checkRateLimit(String clientId);
void resetRateLimit();
int roleRank(String role);
bool hasRolePermission(String requiredRole, String userRole);

#endif
""",
}


def main():
    if not MAIN.exists():
        print("ERROR: no existe src/main.cpp"); sys.exit(1)
    lines = MAIN.read_text(encoding="utf-8").split("\n")

    # enmascarar todo primero para el match (comentarios en bloque entre lineas)
    masked = []
    in_block = False
    for ln in lines:
        m, in_block = mask_line(ln, in_block)
        masked.append(m)

    # localizar rangos (sin forward-decls)
    found = []
    hint = 0
    for sig, folder, mod in PLAN:
        rng = find_range(lines, masked, sig, hint)
        if rng is None:
            print(f"[!] NO encontrada: {sig[:60]}")
            continue
        found.append((sig, folder, mod, rng))
        hint = rng[0] + 1

    # forward-declarations a borrar (lineas que matchean FDECL)
    fdecl_idxs = set()
    for i, ln in enumerate(lines):
        for fd in FDECL:
            if re.match(fd, ln):
                fdecl_idxs.add(i)
                break

    # cuerpos a borrar (rango de lineas)
    body_idxs = set()
    for *_ , (ini, fin) in found:
        body_idxs.update(range(ini, fin + 1))

    print(f"== DETECCION: {len(found)}/{len(PLAN)} funciones encontradas ==")
    for sig, folder, mod, (ini, fin) in found:
        print(f"  [{folder.split('/')[1]:8s}] lineas {ini+1:5d}-{fin+1:5d}  {sig[:60]}")
    print(f"  forward-decls a retirar: {len(fdecl_idxs)}")
    print(f"  lineas totales: {len(lines)} -> quedan {len(lines)-len(body_idxs)-len(fdecl_idxs)}")

    if DRY:
        print("\n[DRY] sin cambios. Usa sin --dry para aplicar.")
        return

    if not found:
        print("Nada que mover; abortando."); return

    # ---- backup ----
    bak_dir = BASE / "tools" / ".bak"
    bak_dir.mkdir(parents=True, exist_ok=True)
    bak = bak_dir / "main.pre-loteA1.cpp"
    bak.write_text("\n".join(lines), encoding="utf-8")
    print(f"-> backup: {bak}")

    # ---- agrupar cuerpos por modulo (en orden de aparicion) ----
    mods = {}
    for sig, folder, mod, (ini, fin) in found:
        key = (folder, mod)
        mods.setdefault(key, []).append((ini, fin))

    for (folder, mod), ranges in mods.items():
        folder_path = BASE / folder
        folder_path.mkdir(parents=True, exist_ok=True)
        (folder_path / f"{mod}.h").write_text(HEADERS[mod], encoding="utf-8")

        # .cpp: cabecera del modulo
        parts = []
        if mod == "eeprom":
            parts.append("#include \"eeprom.h\"")
            parts.append("#include <Wire.h>")
            parts.append("")
            # macro EEPROM_ADDR desde config si existe
            parts.append("#ifndef EEPROM_ADDR")
            parts.append("#define EEPROM_ADDR 0x50")
            parts.append("#endif")
            parts.append("")
        else:  # security
            parts.append("#include \"security.h\"")
            parts.append("#include \"state.h\"")
            parts.append("")
            parts.append("#ifndef RATE_LIMIT_MAX")
            parts.append("#define RATE_LIMIT_MAX 10")
            parts.append("#endif")
            parts.append("")

        for ini, fin in ranges:
            parts.append("")
            for ln in lines[ini:fin + 1]:
                parts.append(ln)
        parts.append("")
        (folder_path / f"{mod}.cpp").write_text("\n".join(parts), encoding="utf-8")
        print(f"-> src/{folder.split('/')[1]}/{mod}.{{h,cpp}}")

    # ---- reescribir main.cpp sin cuerpos ni forward-decls ----
    remove = body_idxs | fdecl_idxs
    new_lines = [ln for i, ln in enumerate(lines) if i not in remove]

    # anadir includes tras el ultimo #include existente
    last_inc = -1
    for i, ln in enumerate(new_lines):
        if ln.startswith("#include"):
            last_inc = i
    incblock = ['#include "drivers/eeprom.h"', '#include "mqtt/security.h"']
    if last_inc >= 0:
        new_lines = new_lines[:last_inc + 1] + incblock + new_lines[last_inc + 1:]
    else:
        new_lines = incblock + new_lines

    MAIN.write_text("\n".join(new_lines), encoding="utf-8")
    print(f"-> src/main.cpp reescrito ({len(new_lines)} lineas, includes anadidos)")


if __name__ == "__main__":
    main()
