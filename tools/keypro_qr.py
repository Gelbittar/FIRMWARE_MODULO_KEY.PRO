"""
KeyPro — generación de QR de emparejamiento por módulo.

Modo directo (ya se conoce el pairing):
    python keypro_qr.py --dev KP123ABC --secret <64hex> --out qr_KP123ABC.png

Modo serial (captura la línea KP_PAIR del primer boot tras flashear):
    python keypro_qr.py --serial /dev/ttyUSB0 [--outdir ./qrs] [--print]

El QR codifica JSON compacto: {"v":1,"dev":"KP....","secret":"<64hex>"}
Genera un PNG con el QR y una etiqueta de texto debajo, listo para imprimir
y pegar sobre el módulo.
"""
import argparse
import json
import os
import re
import sys

def make_qr_png(content: str, out_path: str, label: str):
    import qrcode
    from qrcode.constants import ERROR_CORRECT_M
    qr = qrcode.QRCode(version=None, error_correction=ERROR_CORRECT_M,
                       box_size=10, border=4)
    qr.add_data(content)
    qr.make(fit=True)
    img = qr.make_image(fill_color="black", back_color="white").convert("RGB")

    from PIL import Image, ImageDraw, ImageFont
    font = None
    for fp in ("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
               "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"):
        if os.path.exists(fp):
            font = ImageFont.truetype(fp, 24)
            break
    if font is None:
        font = ImageFont.load_default()

    lines = [label] if label else []
    pad = 30
    text_h = len(lines) * 34 + 20 if lines else 0
    w = img.size[0] + pad * 2
    h = img.size[1] + pad * 2 + text_h
    canvas = Image.new("RGB", (w, h), "white")
    canvas.paste(img, (pad, pad))
    if lines:
        d = ImageDraw.Draw(canvas)
        y = img.size[1] + pad * 2
        for line in lines:
            d.text((pad, y), line, fill="black", font=font)
            y += 34
    canvas.save(out_path)
    print(f"QR guardado: {out_path}")

def capture_pair(port: str, timeout: float):
    import serial
    pattern = re.compile(rb"KP_PAIR\s+(\{.*\})")
    with serial.Serial(port, 115200, timeout=0.5) as ser:
        ser.reset_input_buffer()
        deadline = __import__("time").time() + timeout
        buf = b""
        while __import__("time").time() < deadline:
            buf += ser.read(256)
            m = pattern.search(buf)
            if m:
                return json.loads(m.group(1).decode())
        return None

def main():
    ap = argparse.ArgumentParser(description="Genera el QR de emparejamiento KeyPro")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--dev", help="deviceId tipo KPxxxx")
    g.add_argument("--serial", help="puerto serial (captura KP_PAIR) ej. /dev/ttyUSB0")
    ap.add_argument("--secret", help="secreto 64 hex (requerido con --dev)")
    ap.add_argument("--out", default=None, help="archivo PNG de salida")
    ap.add_argument("--outdir", default=".", help="carpeta para salidas en modo serial")
    ap.add_argument("--timeout", type=float, default=20.0)
    ap.add_argument("--print", action="store_true", help="imprimir el JSON del pairing")
    args = ap.parse_args()

    dev, secret = None, None
    if args.dev:
        if not args.secret or len(args.secret) != 64:
            ap.error("--secret requerido (64 hex) junto a --dev")
        dev, secret = args.dev.upper(), args.secret.upper()
    else:
        pair = capture_pair(args.serial, args.timeout)
        if not pair:
            sys.exit(f"Error: no se detecto KP_PAIR en {args.serial}")
        dev, secret = pair["dev"], pair["secret"]

    content = json.dumps({"v": 1, "dev": dev, "secret": secret}, separators=(",", ":"))
    out = args.out or os.path.join(args.outdir, f"qr_{dev}.png")
    if args.print:
        print("PAIR", json.dumps({"dev": dev, "secret": secret}))
    make_qr_png(content, out, f"KeyPro {dev}  |  SEC {secret[:8]}..{secret[-4:]}")
    print(content)

if __name__ == "__main__":
    main()