-- KEY.PRO secure command pipeline (v1)
-- Apply after the initial modules/module_members/commands/audit_log migration.
-- This migration deliberately grants no direct write access to application clients.

create extension if not exists pgcrypto;

alter table public.modules
  add column if not exists device_id text,
  add column if not exists firmware_channel text not null default 'stable';

create unique index if not exists modules_device_id_unique
  on public.modules (device_id)
  where device_id is not null;

alter table public.commands
  add column if not exists command_version smallint not null default 1,
  add column if not exists payload_b64 text,
  add column if not exists signature_b64 text,
  add column if not exists signed_at timestamptz,
  add column if not exists delivered_at timestamptz;

-- The first migration used `pending`; normalize it before installing the
-- stricter status constraint used by this protocol.
update public.commands
set status = 'expired'
where status = 'pending';

alter table public.commands
  drop constraint if exists commands_status_check;

alter table public.commands
  add constraint commands_status_check
  check (status in (
    'pending_signature', 'ready', 'delivered', 'executed', 'rejected', 'expired'
  ));

alter table public.commands
  alter column status set default 'pending_signature';

create table if not exists public.module_provisioning (
  module_id uuid primary key references public.modules(id) on delete cascade,
  device_token_hash text not null,
  provisioned_at timestamptz not null default now(),
  revoked_at timestamptz,
  last_seen_at timestamptz,
  last_firmware_version text
);

create table if not exists public.command_results (
  command_id uuid primary key references public.commands(id) on delete cascade,
  module_id uuid not null references public.modules(id) on delete cascade,
  outcome text not null check (outcome in ('executed', 'rejected', 'failed')),
  details jsonb not null default '{}'::jsonb,
  received_at timestamptz not null default now()
);

alter table public.module_provisioning enable row level security;
alter table public.command_results enable row level security;

-- This policy is intentionally read-only: app clients can see results only for
-- modules where they are already members. Device credentials never leave this table.
drop policy if exists "members_can_view_command_results" on public.command_results;
create policy "members_can_view_command_results"
on public.command_results for select to authenticated
using (public.is_module_member(module_id));

create or replace function public.module_role_rank(p_role public.module_role)
returns integer
language sql
immutable
as $$
  select case p_role
    when 'admin'::public.module_role then 1
    when 'installer'::public.module_role then 2
    when 'master'::public.module_role then 3
  end;
$$;

create or replace function public.required_role_for_action(p_action text)
returns public.module_role
language plpgsql
immutable
as $function$
begin
  case p_action
    -- Daily access management: Admin, Installer and Master.
    when 'open', 'get_device_info', 'get_free_slots', 'get_first_free_slot',
         'get_slots', 'get_slot_info', 'set_key', 'clear_slot', 'learn_key',
         'confirm_key', 'cancel_learn', 'suspend_slot', 'block_slot',
         'unblock_slot'
      then return 'admin'::public.module_role;

    -- Technical configuration: Installer and Master.
    when 'set_apto', 'set_relay', 'reset_wifi', 'set_door_config', 'get_logs',
         'backup_config', 'set_ntfy'
      then return 'installer'::public.module_role;

    -- Destructive/global operations: Master only.
    when 'set_security_mode', 'factory_reset', 'wipe_slots', 'ota_update',
         'set_family_filter', 'set_rewrite_probe', 'restore_config'
      then return 'master'::public.module_role;
    else
      raise exception 'Acción no permitida: %', p_action using errcode = '22023';
  end case;
end;
$function$;

create or replace function public.create_command(
  p_module_code text,
  p_action text,
  p_payload jsonb default '{}'::jsonb,
  p_ttl_seconds integer default 90
)
returns table (
  command_id uuid,
  module_id uuid,
  action text,
  payload jsonb,
  nonce uuid,
  expires_at timestamptz
)
language plpgsql
security definer
set search_path = public
as $function$
declare
  v_module_id uuid;
  v_actor_role public.module_role;
  v_required_role public.module_role;
  v_command_id uuid;
  v_nonce uuid;
  v_expires_at timestamptz;
begin
  if p_payload is null or jsonb_typeof(p_payload) <> 'object' then
    raise exception 'payload debe ser un objeto JSON' using errcode = '22023';
  end if;

  if p_ttl_seconds < 15 or p_ttl_seconds > 300 then
    raise exception 'La caducidad debe estar entre 15 y 300 segundos' using errcode = '22023';
  end if;

  select m.id into v_module_id
  from public.modules m
  where m.module_code = p_module_code and m.active = true;

  if v_module_id is null then
    raise exception 'Módulo no encontrado o inactivo';
  end if;

  select mm.role into v_actor_role
  from public.module_members mm
  where mm.module_id = v_module_id and mm.user_id = auth.uid();

  if v_actor_role is null then
    raise exception 'Sin acceso al módulo';
  end if;

  v_required_role := public.required_role_for_action(p_action);
  if public.module_role_rank(v_actor_role) < public.module_role_rank(v_required_role) then
    raise exception 'Tu rol no autoriza esta acción';
  end if;

  v_command_id := gen_random_uuid();
  v_nonce := gen_random_uuid();
  v_expires_at := now() + make_interval(secs => p_ttl_seconds);

  insert into public.commands (
    id, module_id, requested_by, action, payload, nonce, expires_at, status
  ) values (
    v_command_id, v_module_id, auth.uid(), p_action, p_payload, v_nonce,
    v_expires_at, 'pending_signature'
  );

  insert into public.audit_log (module_id, actor_id, event_type, details)
  values (
    v_module_id, auth.uid(), 'command_requested',
    jsonb_build_object('command_id', v_command_id, 'action', p_action, 'nonce', v_nonce)
  );

  return query select v_command_id, v_module_id, p_action, p_payload, v_nonce, v_expires_at;
end;
$function$;

-- This is an operator-only provisioning primitive. It creates a random device
-- credential once; retain it only long enough to flash the matching ESP32.
create or replace function public.provision_module_device(
  p_module_code text,
  p_device_id text
)
returns table (module_id uuid, device_id text, device_token text)
language plpgsql
security definer
set search_path = public
as $function$
declare
  v_module_id uuid;
  v_actor_role public.module_role;
  v_device_token text;
begin
  if p_device_id !~ '^[A-Za-z0-9_-]{8,64}$' then
    raise exception 'device_id inválido' using errcode = '22023';
  end if;

  select m.id into v_module_id
  from public.modules m
  where m.module_code = p_module_code and m.active = true;
  if v_module_id is null then
    raise exception 'Módulo no encontrado o inactivo';
  end if;

  select mm.role into v_actor_role
  from public.module_members mm
  where mm.module_id = v_module_id and mm.user_id = auth.uid();
  if v_actor_role <> 'master'::public.module_role then
    raise exception 'Solo Master puede aprovisionar un dispositivo';
  end if;

  if exists (
    select 1 from public.module_provisioning where module_id = v_module_id
  ) then
    raise exception 'El módulo ya está aprovisionado; revoca y rota la credencial mediante un flujo administrativo';
  end if;

  v_device_token := encode(gen_random_bytes(32), 'hex');

  update public.modules set device_id = p_device_id where id = v_module_id;
  insert into public.module_provisioning (module_id, device_token_hash)
  values (
    v_module_id,
    translate(trim(trailing '=' from encode(digest(v_device_token, 'sha256'), 'base64')), '+/', '-_')
  );

  insert into public.audit_log (module_id, actor_id, event_type, details)
  values (v_module_id, auth.uid(), 'device_provisioned', jsonb_build_object('device_id', p_device_id));

  return query select v_module_id, p_device_id, v_device_token;
end;
$function$;

revoke all on function public.module_role_rank(public.module_role) from public;
revoke all on function public.required_role_for_action(text) from public;
revoke all on function public.create_command(text, text, jsonb, integer) from public;
revoke all on function public.provision_module_device(text, text) from public;
grant execute on function public.create_command(text, text, jsonb, integer) to authenticated;
grant execute on function public.provision_module_device(text, text) to authenticated;

-- The old role-key commands are intentionally absent from required_role_for_action.
-- Direct inserts/updates remain blocked by RLS; only Edge Functions with the
-- service role may attach a server signature or record a device result.
