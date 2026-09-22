import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

const encoder = new TextEncoder();

function base64Url(bytes: Uint8Array): string {
  let binary = "";
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
}

async function sha256Base64(value: string): Promise<string> {
  return base64Url(new Uint8Array(await crypto.subtle.digest("SHA-256", encoder.encode(value))));
}

Deno.serve(async (request) => {
  if (request.method !== "GET") return Response.json({ error: "Método no permitido" }, { status: 405 });

  const deviceId = request.headers.get("x-keypro-device-id");
  const credential = request.headers.get("Authorization")?.replace(/^Bearer\s+/i, "");
  if (!deviceId || !credential) return Response.json({ error: "No autenticado" }, { status: 401 });

  const admin = createClient(Deno.env.get("SUPABASE_URL")!, Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!);
  const { data: module, error: moduleError } = await admin
    .from("modules")
    .select("id, active, module_provisioning(device_token_hash, revoked_at)")
    .eq("device_id", deviceId)
    .maybeSingle();
  const provision = Array.isArray(module?.module_provisioning)
    ? module.module_provisioning[0]
    : module?.module_provisioning;
  if (moduleError || !module?.active || !provision) {
    return Response.json({ error: "Dispositivo no registrado" }, { status: 401 });
  }

  if (provision.revoked_at || (await sha256Base64(credential)) !== provision.device_token_hash) {
    return Response.json({ error: "Credencial inválida" }, { status: 401 });
  }

  const now = new Date().toISOString();
  await admin.from("commands").update({ status: "expired" })
    .eq("module_id", module.id).in("status", ["pending_signature", "ready", "delivered"])
    .lt("expires_at", now);

  const { data: commands, error: commandsError } = await admin
    .from("commands")
    .select("id, module_id, action, nonce, expires_at, payload_b64, signature_b64, command_version")
    .eq("module_id", module.id).eq("status", "ready").gt("expires_at", now)
    .order("created_at", { ascending: true }).limit(1);
  if (commandsError) return Response.json({ error: "No se pudo leer órdenes" }, { status: 500 });

  const command = commands?.[0];
  if (!command) {
    await admin.from("module_provisioning").update({ last_seen_at: now }).eq("module_id", module.id);
    return new Response(null, { status: 204 });
  }

  // Keep the command in `ready` until the ESP32 reports a terminal result.
  // A response can be lost on Wi-Fi; returning the same signed command is safe
  // because firmware persists its nonce and must report its cached result.
  await admin.from("commands").update({ delivered_at: now })
    .eq("id", command.id).eq("status", "ready");
  await admin.from("module_provisioning").update({ last_seen_at: now }).eq("module_id", module.id);

  return Response.json({ command });
});
