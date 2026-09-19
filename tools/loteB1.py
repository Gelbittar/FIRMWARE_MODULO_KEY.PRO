import re, sys, os
# Lote B1 (drivers 1-wire/ibutton/timer) - SOLO dry-run salvo --apply
F="src/main.cpp"
dry = "--apply" not in sys.argv
src = open(F, encoding='utf-8', errors='surrogateescape').read()
mod = ("// fw: drivers de bajo nivel (lectura 1-Wire SOVICA, temporizador, LED)\n"
       "// No cambiar el protocolo 1-Wire / iButton: hardware SOVICA descifra con estos bytes.\n")
# anclas por subcadena tolerante (firmas reales en disco, con el nombre SOLO en firma col-0)
goals = [
 ("ibuttonReset",      "bool ibuttonReset() {",            None),
 ("ibuttonWrite",      "void ibuttonWrite(uint8_t",        None),
 ("ibuttonRead",       "uint8_t ibuttonRead() {",          None),
 ("readSlotROM",       "bool readSlotROM(int slot",        None),
 ("fetchSensorTemp",   "void fetchSensorTemp(",            None),
]
def find_func(name, anchor, src):
    # subcadena tolerante a espacios: 'bool ibuttonReset() {' con \s* entre tokens
    toks = anchor.split()
    pat = r"\s*".join(re.escape(t) for t in toks)
    m = re.search(pat, src)
    if not m: return None
    i = m.start()
    # si la firma ya fue movida (no esta en main) -> coord -1
    start = i
    depth = 0; j = src.index("{", i)
    # recorrer hasta cierre por brace-matching (ignora strings/comments con mascara)
    k = j
    while k < len(src):
        c = src[k]
        if c == "{": depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0: break
        k += 1
    return start, k+1
found = {}
for name, anchor, _ in goals:
    r = find_func(name, anchor, src)
    found[name] = r
if dry:
    ok = sum(1 for v in found.values() if v)
    print(f"[dry-run] detectadas {ok}/{len(goals)}")
    for n, v in found.items():
        if v is None: print(f"  PENDIENTE {n}: ancla no hallada (firma difiere)")
        else: print(f"  lineas {v[0]+1:>4}..{v[1]:>4} | {n}")
    if ok == len(goals): print("[dry-run] 6b OK. Usa --apply para extraer.")
    sys.exit(0)
# apply
if not all(found.values()):
    print("ABORTO: no todas detectadas en dry-run. Nada tocado."); sys.exit(1)
# extraer en orden DESCENDENTE de linea
items = sorted(((found[n][0], found[n][1], n) for n in goals if found[n]), reverse=True)
os.makedirs("src/drivers", exist_ok=True)
cuerpos = []
for start, end, name in items:
    body = src[start:end]
    cuerpos.append(body)
# construir security_ext e inyectar
defsn = []
funciones = []
for _, _, name in items:  # orden variable; mejor: reconstruir por goals
    pass
ord_map = {}
for start, end, name in items:
    ord_map[name] = src[start:end]
# volver a montar en orden original goals
defs_h = "// drivers 1-Wire / iButton / temporizador (GEYLCA ONE)\n#pragma once\n\n"
# usamos nombres de prototipos simples (sin params en header para no errar)
for name, *_ in goals:
    if name in ord_map:
        body = ord_map[name]
        sig = body.split("{")[0].strip()
        # prototipo: quitamos el cuerpo, anyadimos ;
        proto = sig + ";"
        defs_h += proto + "\n"
defs_h += "\n"
op = "#include \"drivers/ibutton.h\"\n#ifndef IBTN_MODULE\n#define IBTN_MODULE\n" + defs_h + "#endif\n"
# remover cuerpos de main en orden descendente
for start, end, name in items:
    src = src[:start] + src[end:]
# inyectar include tras el include de security (o al inicio de includes)
# insertar include "drivers/ibutton.h" junto a los otros include de modulo
src = src.replace('#include "mqtt/security.h"', '#include "mqtt/security.h"\n#include "drivers/ibutton.h"', 1)
# crear el cpp con las 5 funciones
cpp = "#include \"drivers/ibutton.h\"\n#include <Arduino.h>\n#include <OneWire.h>\n\n"
cpp += mod + "\n"
# orden natural: goals order
for name, _, _ in goals:
    if name in ord_map:
        cpp += ord_map[name] + "\n\n"
# El puerto/sensor usan globals del main (OW etc) -> dejar como externs minimos
open("src/drivers/ibutton.h","w").write(defs_h)
open("src/drivers/ibutton.cpp","w").write(cpp)
open(F,"w",encoding='utf-8',errors='surrogateescape').write(src)
print("APLICADO: 5 funciones a src/drivers/ibutton.{h,cpp}; main reescrito")
