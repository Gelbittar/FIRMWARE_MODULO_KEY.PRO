import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

const encoder = new TextEncoder();
const b64 = (b: Uint8Array) => btoa(String.fromCharCode(...b)).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
const hash = async (value: string) => b64(new Uint8Array(await crypto.subtle.digest("SHA-256", encoder.encode(value))));

Deno.serve(async (request) => {
  if (request.method !== "POST") return Response.json({ error: "Método no permitido" }, { status: 405 });
  const deviceId = request.headers.get("x-keypro-device-id");
  const credential = request.headers.get("Authorization")?.replace(/^Bearer\s+/i, "");
  const body = await request.json();
  if (!deviceId || !credential || !body?.command_id || !["executed", "rejected", "failed"].includes(body.outcome)) {
    return Response.json({ error: "Solicitud inválida" }, { status: 400 });
  }

  const admin = createClient(Deno.env.get("SUPABASE_URL")!, Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!);
  const { data: module } = await admin.from("modules")
    .select("id, active, module_provisioning(device_token_hash, revoked_at)").eq("device_id", deviceId).maybeSingle();
  const provision = Array.isArray(module?.module_provisioning)
    ? module.module_provisioning[0]
    : module?.module_provisioning;
  if (!module?.active || !provision || provision.revoked_at || (await hash(credential)) !== provision.device_token_hash) {
    return Response.json({ error: "No autenticado" }, { status: 401 });
  }

  const { data: command } = await admin.from("commands").select("id")
    .eq("id", body.command_id).eq("module_id", module.id).maybeSingle();
  if (!command) return Response.json({ error: "Orden desconocida" }, { status: 404 });

  const now = new Date().toISOString();
  await admin.from("command_results").upsert({
    command_id: command.id, module_id: module.id, outcome: body.outcome, details: body.details ?? {}, received_at: now,
  });
  await admin.from("commands").update({ status: body.outcome === "executed" ? "executed" : "rejected", executed_at: now })
    .eq("id", command.id);
  await admin.from("audit_log").insert({ module_id: module.id, event_type: "command_result", details: { command_id: command.id, outcome: body.outcome } });
  return Response.json({ ok: true });
});
