import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

type RequestBody = {
  module_code: string;
  action: string;
  payload?: Record<string, unknown>;
  ttl_seconds?: number;
};

const encoder = new TextEncoder();

function base64Url(bytes: Uint8Array): string {
  let binary = "";
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
}

function payloadB64(payload: Record<string, unknown>): string {
  return base64Url(encoder.encode(JSON.stringify(payload)));
}

// This exact UTF-8 format is the firmware contract. Do not switch it to a
// JSON signature: object-key serialization can differ across platforms.
function signingMessage(command: {
  command_id: string;
  module_id: string;
  action: string;
  nonce: string;
  expires_at: string;
}, encodedPayload: string): Uint8Array {
  const expiry = Math.floor(new Date(command.expires_at).getTime() / 1000);
  return encoder.encode([
    "keypro-command-v1", command.command_id, command.module_id, command.action,
    command.nonce, String(expiry), encodedPayload,
  ].join("\n"));
}

Deno.serve(async (request) => {
  if (request.method !== "POST") {
    return Response.json({ error: "Método no permitido" }, { status: 405 });
  }

  const authorization = request.headers.get("Authorization");
  if (!authorization) return Response.json({ error: "No autenticado" }, { status: 401 });

  const supabaseUrl = Deno.env.get("SUPABASE_URL")!;
  const anonKey = Deno.env.get("SUPABASE_ANON_KEY")!;
  const serviceRoleKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!;
  const privateJwk = Deno.env.get("COMMAND_SIGNING_PRIVATE_JWK");
  if (!privateJwk) return Response.json({ error: "Firma del servidor no configurada" }, { status: 503 });

  const asUser = createClient(supabaseUrl, anonKey, {
    global: { headers: { Authorization: authorization } },
  });
  const { data: auth, error: authError } = await asUser.auth.getUser();
  if (authError || !auth.user) return Response.json({ error: "Sesión inválida" }, { status: 401 });

  const body = await request.json() as RequestBody;
  const { data: rows, error: commandError } = await asUser.rpc("create_command", {
    p_module_code: body.module_code,
    p_action: body.action,
    p_payload: body.payload ?? {},
    p_ttl_seconds: body.ttl_seconds ?? 90,
  });
  if (commandError) return Response.json({ error: commandError.message }, { status: 403 });

  const command = rows?.[0];
  if (!command) return Response.json({ error: "No se pudo crear la orden" }, { status: 500 });

  const encodedPayload = payloadB64(command.payload);
  const key = await crypto.subtle.importKey(
    "jwk", JSON.parse(privateJwk), { name: "Ed25519" }, false, ["sign"],
  );
  const signature = new Uint8Array(await crypto.subtle.sign(
    "Ed25519", key, signingMessage(command, encodedPayload),
  ));

  const admin = createClient(supabaseUrl, serviceRoleKey);
  const { error: signatureError } = await admin
    .from("commands")
    .update({ payload_b64: encodedPayload, signature_b64: base64Url(signature), signed_at: new Date().toISOString(), status: "ready" })
    .eq("id", command.command_id)
    .eq("status", "pending_signature");
  if (signatureError) return Response.json({ error: "No se pudo firmar la orden" }, { status: 500 });

  return Response.json({
    id: command.command_id, nonce: command.nonce, expires_at: command.expires_at,
    status: "ready",
  }, { status: 201 });
});
