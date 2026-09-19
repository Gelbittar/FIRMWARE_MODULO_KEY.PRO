// Copia los bundles UMD de los plugins nativos a web/lib para que las paginas
// estaticas (sin bundler) puedan cargarlos dentro del APK.
//
// - @capgo/capacitor-native-biometric: bundle UMD ya publicado en su dist.
// - capacitor-ntfy: no publica dist en el repo (ni en npm); se compila aqui.
import { execSync } from "node:child_process";
import { existsSync, mkdirSync, copyFileSync, readdirSync } from "node:fs";
import { join } from "node:path";

const root = process.cwd();
const webLib = join(root, "web", "lib");
mkdirSync(webLib, { recursive: true });

function log(msg) {
  console.log("[bundle-plugins] " + msg);
}

// --- @capacitor/core (runtime UMD; define global capacitorExports) ---
const coreDist = join(root, "node_modules", "@capacitor", "core", "dist");
try {
  if (existsSync(join(coreDist, "capacitor.js"))) {
    copyFileSync(join(coreDist, "capacitor.js"), join(webLib, "capacitor-core.js"));
    log("copiado capacitor-core.js a web/lib");
  } else {
    log("AVISO: no se encontro dist/capacitor.js de @capacitor/core");
  }
} catch (e) {
  log("ERROR copiando @capacitor/core: " + e.message);
}

// --- capacitor-ntfy (construir desde fuente) ---
const ntfyDir = join(root, "node_modules", "capacitor-ntfy");
try {
  if (existsSync(ntfyDir)) {
    const distFile = join(ntfyDir, "dist", "plugin.js");
    if (!existsSync(distFile)) {
      log("compilando capacitor-ntfy (dist no incluido en el repo)...");
      execSync("npm install --no-package-lock --no-save", { cwd: ntfyDir, stdio: "inherit" });
      execSync("npm run build", { cwd: ntfyDir, stdio: "inherit" });
    }
    if (existsSync(distFile)) {
      copyFileSync(distFile, join(webLib, "capacitor-ntfy.js"));
      log("copiado capacitor-ntfy.js a web/lib");
    } else {
      log("ADVERTENCIA: no se genero dist/plugin.js de capacitor-ntfy");
    }
  } else {
    log("capacitor-ntfy no esta instalado, se omite");
  }
} catch (e) {
  log("ERROR compilando capacitor-ntfy: " + e.message);
}

// --- @capgo/capacitor-native-biometric (bundle publicado) ---
const bioDist = join(root, "node_modules", "@capgo", "capacitor-native-biometric", "dist");
try {
  if (existsSync(bioDist)) {
    for (const f of readdirSync(bioDist)) {
      if (f === "plugin.js") {
        copyFileSync(join(bioDist, f), join(webLib, "capacitor-native-biometric.js"));
        log("copiado capacitor-native-biometric.js a web/lib");
      }
    }
  } else {
    log("capacitor-native-biometric no esta instalado, se omite");
  }
} catch (e) {
  log("ERROR copiando biometrico: " + e.message);
}
