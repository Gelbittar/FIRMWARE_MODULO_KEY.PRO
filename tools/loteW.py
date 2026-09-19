#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
GEYLCA ONE - Lote W (WIPE-EEPROM + SLOTS derivados). Modo SOVICA INTACTO.
=======================================================================
Que hace:
 1. Localiza el proyecto por lookup real (find sobre src/main.cpp + marcador unico tools/).
 2. VERIFICA hash SHA-256 previo contra la referencia verde conocida (f373ee52...).
 3. Backup determinista en tools/.bak/ (con marca temporal).
 4. DRY-RUN por defecto; --apply solo si las 4 anclas se detectan EXACTAS.
 5. Sustituciones dirigidas (NO reemplazo global):
      a) Wipes:  MAX_SLOTS * 8  ->  maxSlots() * slotBytes()   (los 3 sitios totalBytes)
      b) Bucles/límites runtime MAX_SLOTS  ->  maxSlots()
      c) JSON "max_slots": valor derivado maxSlots()
      d) NO se toca: defines MAX_SLOTS/SLOTS_BITMAP_LEN (dimensionado), layout SOVICA,
         sovicaEnabled()/slotAddr()/slotBytes()/keyMatches()/writeFullRomToSlot().
 6. pio run al final y reporta SUCCESS/FAILED. Cualquier anomalia => aborta, nada tocado.
Uso:  python3 tools/loteW.py [--apply]
"""
import os, re, sys, hashlib, shutil, subprocess, datetime, glob

DRY = "--apply" not in sys.argv
REF_HASH = "f373ee5200b7d3cbfa410ba9aad1cdc412e868f45ea910aa5f4dcea0d207401"

def find_project():
    cand = []
    for pattern in ("/home/*/Documentos/PlatformIO/Projects/proyecto modulo tipo sovica/geylca-one/src/main.cpp",
                    "/home/*/Documentos*/PlatformIO*/Projects*/*sovica*/geylca-one/src/main.cpp",
                    "/home/*/Documentos*/PlatformIO*/Projects*/*sovica*/*geylca*/*one*/src/main.cpp"):
        for f in glob.glob(pattern):
            if os.path.isfile(f):
                cand.append(f)
    # fallback: busqueda amplia por nombre de archivo + marcador
    if not cand:
        for root, dirs, files in os.walk("/home"):
            dirs[:] = [d for d in dirs if d not in (".git", ".pio", "node_modules", ".platformio")]
            if "main.cpp" in files and "geylca-one" in root and root.count(os.sep) < 16:
                p = os.path.join(root, "main.cpp")
                if os.path.isfile(p):
                    cand.append(p)
                    break
    if not cand:
        return None
    cand.sort(key=lambda p: -len(p))
    return cand[0]

def h(f):
    h = hashlib.sha256()
    with open(f, "rb") as fh:
        for blk in iter(lambda: fh.read(1 << 16), b""):
            h.update(blk)
    return h.hexdigest()

def brace_slice(src, base):
    """base = indice de '{'; devuelve indice del '}' que cierra (con ';' terminal si existe)."""
    depth = 0
    i = base
    while i < len(src):
        c = src[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                # incluir un ';' siguiente si hay (funcion hoja simple)
                j = i + 1
                while j < len(src) and src[j] in " \t\r\n":
                    j += 1
                if j < len(src) and src[j] == ";":
                    return j + 1
                return i + 1
        i += 1
    return -1

def strip_comments(src):
    """Quita comentarios (no de strings) devolviendo una copia con espacios, misma longitud."""
    out = list(src)
    i, n = 0, len(src)
    in_str = None
    while i < n:
        if in_str:
            if src[i] == "\\":
                out[i] = " "; i += 2; continue
            if src[i] == in_str:
                in_str = None
            out[i] = " "
            i += 1
            continue
        if src[i] == '"' or src[i] == "'":
            in_str = src[i]
            out[i] = " "
            i += 1
            continue
        if src[i] == "/" and i + 1 < n and src[i + 1] == "/":
            while i < n and src[i] != "\n":
                out[i] = " "
                i += 1
            continue
        if src[i] == "/" and i + 1 < n and src[i + 1] == "*":
            out[i] = out[i + 1] = " "
            i += 2
            while i + 1 < n and not (src[i] == "*" and src[i + 1] == "/"):
                out[i] = " "
                i += 1
            if i + 1 < n:
                out[i] = out[i + 1] = " "
                i += 2
            continue
        i += 1
    return "".join(out)

def find_fdef(src_clean, name):
    """Ancla tolerante: 'name(' como inicio de definicion (col-0 o precedido de \n)."""
    pat = re.compile(r"^(?=\s*" + re.escape(name) + r"\s*\()", re.M)
    for m in pat.finditer(src_clean):
        i = m.start()
        # verificar que no sea una llamada: buscar '{' despues; si hay ';' antes de '{' => prototipo, saltar? no: buscamos el cuerpo
        j = src_clean.index("{", i)
        end = brace_slice(src_clean, j)
        if end < 0:
            continue
        return (i, end)
    return None

def find_wipe_totalbytes(src):
    """Ancla exacta de los 3 totalBytes = MAX_SLOTS * 8  (tolerante a espacios)."""
    pat = re.compile(r"unsigned\s+int\s+totalBytes\s*=\s*\(\s*unsigned\s+int\s*\)\s*MAX_SLOTS\s*\*\s*8\s*;")
    return [(m.start(), m.end()) for m in pat.finditer(src)]

PROJ = find_project()
if not PROJ:
    print("ABORTO: no se hallo el proyecto (main.cpp de geylca-one). Nada tocado.")
    sys.exit(2)
MAIN = PROJ
SRC = open(MAIN, encoding="utf-8", errors="surrogateescape").read()
hpre = h(MAIN)

print("=== Lote W (wipe+slots) |", "DRY-RUN" if DRY else "APPLY", "===")
print("proyecto:", MAIN)
print("hash_pre:", hpre[:16], "...", "OK" if hpre == REF_HASH else "DIFERENTE!")
if hpre != REF_HASH:
    print("ABORTO: el hash no coincide con la referencia verde. No se toca NADA.")
    sys.exit(3)

# ---- 1. Wipes: las 3 lineas totalBytes = MAX_SLOTS * 8
wipes = find_wipe_totalbytes(SRC)
print("wipe_totalBytes=MAX_SLOTS*8 detectados:", len(wipes), "(esperado 3)")
if len(wipes) != 3:
    print("ABORTO (dry-run): wipes != 3. Nada tocado.")
    sys.exit(4)

# ---- 2. Verificar que la funcion maxSlots() NO exista ya (evitar duplicado)
clean = strip_comments(SRC)
if re.search(r"(?m)^\s*int\s+maxSlots\s*\(", clean):
    print("NOTA: int maxSlots() ya existe; la omito al insertar.")
    has_maxslots = True
else:
    has_maxslots = False

# ---- 3. Verificar ancla slotBytes() para insertar despues
m_anchor = re.search(r"(?ms)(^int slotBytes\(\)\s*\{.*?^\})", clean)
if not m_anchor:
    print("ABORTO (dry-run): no halle slotBytes() para anclar maxSlots(). Nada tocado.")
    sys.exit(5)
anchor_slotbytes = m_anchor.group(1)
print("ancla slotBytes() OK para insertar maxSlots()")

# ---- 4. Bucles/límites runtime: patron 'MAX_SLOTS' como valor (no dentro de define ni SLOTS_BITMAP_LEN)
# Usamos el SRC limpio para decidir; aplicamos sobre src real con offsets coincidentes (mismo largo).
usos_nofragment = []
for m in re.finditer(r"\bMAX_SLOTS\b", clean):
    i = m.start()
    # excluir la linea del define y de SLOTS_BITMAP_LEN y de cualquier '#define'
    linestart = clean.rfind("\n", 0, i) + 1
    linetxt = clean[linestart:clean.find("\n", i)]
    if re.match(r"\s*#\s*define", linetxt):
        continue
    if "SLOTS_BITMAP_LEN" in linetxt:
        continue
    usos_nofragment.append(i)
print("usos runtime de MAX_SLOTS a derivar:", len(usos_nofragment))

if DRY:
    print("\n[DRY-RUN] todo detectado. Con --apply: 3 wipes reescritos, %d usos -> maxSlots(), "
          "funcion maxSlots()%s insertada, build pio run." % (
              len(usos_nofragment), "" if has_maxslots else " y slotBytes() intacta"))
    print("[DRY-RUN] modo SOVICA: sovicaEnabled()/slotAddr()/slotBytes()/keyMatches() NO se tocan.")
    sys.exit(0)

# ================= APPLY =================
# backup
bak_dir = os.path.join(os.path.dirname(PROJ), "tools", ".bak")
os.makedirs(bak_dir, exist_ok=True)
bak = os.path.join(bak_dir, "main.pre-loteW." + datetime.datetime.now().strftime("%H%M%S") + ".cpp")
shutil.copy2(MAIN, bak)
print("backup:", bak)

# reemplazos dirigidos sobre el src REAL usando offsets del clean (mismos bytes)
# 1) wipes
for a, b in sorted(wipes, reverse=True):
    SRC = SRC[:a] + "unsigned int totalBytes = (unsigned int)maxSlots() * slotBytes();" + SRC[b:]

# 2) insertar maxSlots() tras slotBytes() si no existe
if not has_maxslots:
    # re-descubrir slotBytes() en SRC real (mismo ancla, offsets pueden cambiar por wipes arriba - arriba no toco)
    ma = re.search(r"(?ms)(^int slotBytes\(\)\s*\{.*?^\})", SRC)
    ar = ma.group(1)
    fn = ("\n\n// Numero de casillas usables segun el modo. Referencia 4B/SOVICA = 4000.\n"
          "// 3B -> 5333 (4000*4/3), 4B/SOVICA -> 4000, 8B -> 2000 (4000*4/8).\n"
          "// El modo SOVICA conserva EXACTAMENTE el layout fisico (slot-1)*4.\n"
          "int maxSlots() {\n"
          "    return (4000L * 4) / slotBytes();\n"
          "}\n")
    SRC = SRC.replace(ar, ar + fn, 1)

# 3) usos runtime -> maxSlots()
# re-calcular offsets en el SRC actualizado
clean2 = strip_comments(SRC)
for m in sorted([x for x in re.finditer(r"\bMAX_SLOTS\b", clean2)], reverse=True):
    i = m.start()
    linestart = clean2.rfind("\n", 0, i) + 1
    linetxt = clean2[linestart:clean2.find("\n", i)]
    if re.match(r"\s*#\s*define", linetxt):
        continue
    if "SLOTS_BITMAP_LEN" in linetxt:
        continue
    SRC = SRC[:i] + "maxSlots()" + SRC[m.end():]

open(MAIN, "w", encoding="utf-8", errors="surrogateescape").write(SRC)
print("main.cpp reescrito. hash_post:", h(MAIN)[:16])

# ---- build ----
r = subprocess.run(
    ["/home/gutavo-elbittar/.platformio/penv/bin/pio", "run"],
    cwd=os.path.dirname(os.path.dirname(MAIN)),
    capture_output=True, text=True, timeout=300,
)
tail = "\n".join(r.stdout.splitlines()[-8:]) if r.stdout else ""
ok = "SUCCESS" in r.stdout
print("=== BUILD ===")
print(tail)
print("RESULTADO:", "SUCCESS (verde)" if ok else "FAILED")
sys.exit(0 if ok else 1)
