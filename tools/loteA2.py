#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Lote A2 - extrae 5 funciones de seguridad de src/main.cpp a src/mqtt/security.{h,cpp}

Firma por ANCLA de subcadena robusta (no regex estricto): busca la linea
enmascarada que contenga el ancla y un '{'. Body por brace-matching.

Anclas  : tokens por modulo
  security : calcularHMAC(, checkRateLimit(, resetRateLimit(), roleRank(, hasRolePermission(

Uso:
  python3 tools/loteA2.py --dry     # solo reporta (default)
  python3 tools/loteA2.py --apply   # backup + extrae + reescribe main.cpp
"""
import re, sys
from pathlib import Path

BASE_STR = "/home/gutavo-elbittar/Documentos/PlatformIO/Projects/proyecto modulo tipo sovica/geylca-one"
try:
    BASE = Path(BASE_STR)
    MAIN = BASE / "src" / "main.cpp"
    assert MAIN.exists()
except Exception as e:
    print(f"Base path no accesible: {e}")
    sys.exit(2)

APPLY = "--apply" in sys.argv
OUTDIR = BASE / "src" / "mqtt"

ANCHORS = [      # (ancla, nombre)
    ("calcularHMAC(String",   "calcularHMAC"),
    ("checkRateLimit(String", "checkRateLimit"),
    ("resetRateLimit()",      "resetRateLimit"),
    ("roleRank(String",       "roleRank"),
    ("hasRolePermission(String", "hasRolePermission"),
]

HDR = """#ifndef GEYLCA_MQTT_SECURITY_H
#define GEYLCA_MQTT_SECURITY_H

#include <Arduino.h>

String calcularHMAC(String payload, String secret);
bool checkRateLimit(String clientId);
void resetRateLimit();
int roleRank(String role);
bool hasRolePermission(String requiredRole, String userRole);

#endif
"""

FDECL = [
    r"\bcalcularHMAC\(String payload, String secret\)",
    r"\bcheckRateLimit\(String clientId\)",
    r"\bresetRateLimit\(\)",
    r"\broleRank\(String role\)",
    r"\bhasRolePermission\(String requiredRole, String userRole\)",
]


def mask_line(line, in_block):
    out = list(line)
    i, n = 0, len(line)
    while i < n:
        c = line[i]
        if in_block:
            if c == "*" and i + 1 < n and line[i + 1] == "/":
                out[i] = out[i + 1] = " "; i += 2; in_block = False
            else:
                out[i] = " "; i += 1
            continue
        if c == "/" and i + 1 < n and line[i + 1] == "/":
            for j in range(i, n): out[j] = " "
            break
        if c == "/" and i + 1 < n and line[i + 1] == "*":
            out[i] = out[i + 1] = " "; i += 2; in_block = True
            continue
        if c == '"':
            out[i] = " "; i += 1
            while i < n:
                if line[i] == "\\":
                    out[i] = " "; i += 1
                    if i < n: out[i] = " "; i += 1
                    continue
                if line[i] == '"':
                    out[i] = " "; i += 1; break
                out[i] = " "; i += 1
            continue
        if c == "'":
            out[i] = " "; i += 1
            while i < n:
                if line[i] == "\\":
                    out[i] = " "; i += 1
                    if i < n: out[i] = " "; i += 1
                    continue
                if line[i] == "'":
                    out[i] = " "; i += 1; break
                out[i] = " "; i += 1
            continue
        i += 1
    return "".join(out), in_block


def main():
    lines = MAIN.read_text(encoding="utf-8").split("\n")
    masked_all = []
    in_block = False
    for ln in lines:
        m, in_block = mask_line(ln, in_block)
        masked_all.append(m)

    # ---- detectar cuerpos ----
    found = []  # (ini, fin, ancla)
    for anchor, name in ANCHORS:
        for i in range(len(lines)):
            if anchor not in masked_all[i]:
                continue
            if "{" not in masked_all[i]:
                continue  # forward decl
            depth = 0; opened = False
            for j in range(i, len(lines)):
                for ch in masked_all[j]:
                    if ch == "{": depth += 1; opened = True
                    elif ch == "}": depth -= 1
                if opened and depth == 0:
                    found.append((i, j, anchor))
                    break
            break

    print(f"== DETECCION: {len(found)}/{len(ANCHORS)} ==")
    for i, j, a in found:
        print(f"  [{a:28s}] lineas {i+1:4d}..{j+1:4d}  | {lines[i].strip()[:60]}")

    if len(found) != len(ANCHORS):
        miss = [a for a, _ in ANCHORS if not any(a == x[2] for x in found)]
        print(f"[!] faltan: {miss}")
        print("[dry] NO se aplica. Corrige anclas y reintenta.")
        return

    if not APPLY:
        print("[dry-run] range OK. Usa --apply para extraer.")
        return

    # ---- backup ----
    bak = BASE / "tools" / ".bak"
    bak.mkdir(parents=True, exist_ok=True)
    (bak / "main.pre-loteA2.cpp").write_text("\n".join(lines), encoding="utf-8")

    # ---- escribir modulo ----
    OUTDIR.mkdir(parents=True, exist_ok=True)
    (OUTDIR / "security.h").write_text(HDR, encoding="utf-8")

    parts = ['#include "security.h"', '#include <mbedtls/md.h>', '', '#ifndef RATE_LIMIT_MAX', '#define RATE_LIMIT_MAX 10', '#endif', '#ifndef RATE_LIMIT_WINDOW', '#define RATE_LIMIT_WINDOW 60000', '#endif', '#ifndef RATE_LIMIT_BLOCK', '#define RATE_LIMIT_BLOCK 300000', '#endif', '#ifndef RATE_LIMIT_ATTEMPTS', '#define RATE_LIMIT_ATTEMPTS 10', '#endif', '', 'struct RateLimitEntry {', '    unsigned long windowStart;', '    int failCount;', '    unsigned long blockedUntil;', '};', '', 'extern RateLimitEntry rateLimitEntries[RATE_LIMIT_MAX];', 'extern int rateLimitIndex;', '']
    for i, j, a in found:
        parts.append("\n".join(lines[i:j + 1]))
        parts.append("")
    (OUTDIR / "security.cpp").write_text("\n".join(parts), encoding="utf-8")
    print(f"-> src/mqtt/security.{{h,cpp}}")

    # ---- reescribir main.cpp: quitar cuerpos + forward decls ----
    remove = set()
    for i, j, a in found:
        for k in range(i, j + 1):
            remove.add(k)

    for i, m in enumerate(masked_all):
        if i in remove:
            continue
        head = m.partition("(")[0].rstrip()
        for fd in FDECL:
            if re.search(fd, m) and "{" not in m and m.rstrip().endswith(";"):
                remove.add(i)
                break

    new_lines = [ln for k, ln in enumerate(lines) if k not in remove]
    # include adicional
    anchor_idx = -1
    for k, ln in enumerate(new_lines):
        if ln.startswith('#'):
            anchor_idx = k
    ins = ['#include "mosquitto/security.h"'.replace("mosquitto", "mqtt")]
    # corregir include: la ruta es mqtt/security.h
    ins = ['#include "mqtt/security.h"']
    new_lines = new_lines[:anchor_idx + 1] + ins + new_lines[anchor_idx + 1:] if anchor_idx >= 0 else ins + new_lines

    # quitar include "drivers/eeprom.h" si existe duplicado? no tocar (ya integrado)
    MAIN.write_text("\n".join(new_lines), encoding="utf-8")
    print(f"-> src/main.cpp reescrito ({len(new_lines)} lineas)")


if __name__ == "__main__":
    main()