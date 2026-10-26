-- ERU SmartPlug v1. Run once in the Supabase SQL editor. Safe to re-run.
-- Existing, unrelated application tables and auth accounts are preserved.
begin;
create extension if not exists pgcrypto with schema extensions;

create table if not exists public.sp_devices (
  id text primary key check (id ~ '^SP-[A-F0-9]{12}$'),
  owner_id uuid references auth.users(id) on delete set null,
  name text not null default 'My SmartPlug' check (length(name) between 1 and 60),
  secret_hash text not null,
  claim_hash text not null unique,
  boot_id text not null,
  state jsonb not null default '{}'::jsonb,
  last_seen timestamptz not null default now(),
  created_at timestamptz not null default now()
);
create index if not exists sp_devices_owner_idx on public.sp_devices(owner_id);
create table if not exists public.sp_commands (
  id uuid primary key,
  device_id text not null references public.sp_devices(id) on delete cascade,
  actor_id uuid not null references auth.users(id) on delete cascade,
  boot_id text not null,
  target text not null check (target in ('plug','iron')),
  action text not null check (action in ('on','off','settings','rearm')),
  args jsonb not null default '{}'::jsonb,
  status text not null default 'pending' check (status in ('pending','applied','rejected','expired')),
  reason text,
  created_at timestamptz not null default now(),
  expires_at timestamptz not null default (now() + interval '20 seconds'),
  acknowledged_at timestamptz
);
create index if not exists sp_commands_queue_idx on public.sp_commands(device_id,created_at) where status='pending';
create index if not exists sp_commands_history_idx on public.sp_commands(device_id,created_at desc);
create table if not exists public.sp_events (
  id bigint generated always as identity primary key,
  device_id text not null references public.sp_devices(id) on delete cascade,
  kind text not null,
  message text not null,
  created_at timestamptz not null default now()
);
create index if not exists sp_events_device_idx on public.sp_events(device_id,created_at desc);
create table if not exists public.sp_samples (
  device_id text not null references public.sp_devices(id) on delete cascade,
  minute timestamptz not null,
  current_amps double precision not null,
  apparent_va double precision not null,
  primary key(device_id,minute)
);
create table if not exists public.sp_claim_attempts (
  actor_id uuid primary key references auth.users(id) on delete cascade,
  window_start timestamptz not null default now(),
  attempts int not null default 0
);
alter table public.sp_devices enable row level security;
alter table public.sp_commands enable row level security;
alter table public.sp_events enable row level security;
alter table public.sp_samples enable row level security;
alter table public.sp_claim_attempts enable row level security;
revoke all on public.sp_devices,public.sp_commands,public.sp_events,public.sp_samples,public.sp_claim_attempts from anon,authenticated;
grant select(id,owner_id,name,boot_id,state,last_seen,created_at) on public.sp_devices to authenticated;
grant select on public.sp_commands,public.sp_events,public.sp_samples to authenticated;

do $$ begin
 if not exists(select 1 from pg_policies where schemaname='public' and tablename='sp_devices' and policyname='sp_owner_read') then
  create policy sp_owner_read on public.sp_devices for select to authenticated using(owner_id=(select auth.uid()));
  create policy sp_owner_read on public.sp_commands for select to authenticated using(exists(select 1 from public.sp_devices d where d.id=device_id and d.owner_id=(select auth.uid())));
  create policy sp_owner_read on public.sp_events for select to authenticated using(exists(select 1 from public.sp_devices d where d.id=device_id and d.owner_id=(select auth.uid())));
  create policy sp_owner_read on public.sp_samples for select to authenticated using(exists(select 1 from public.sp_devices d where d.id=device_id and d.owner_id=(select auth.uid())));
 end if;
 if not exists(select 1 from pg_policies where schemaname='realtime' and tablename='messages' and policyname='sp_private_updates') then
  create policy sp_private_updates on realtime.messages for select to authenticated
   using (realtime.topic() = 'sp:user:' || (select auth.uid())::text);
 end if;
end $$;

create or replace function public.sp_hash(p_text text) returns text
language sql immutable strict set search_path=public,extensions,pg_temp
as $$select encode(extensions.digest(p_text,'sha256'),'hex')$$;
revoke all on function public.sp_hash(text) from public;

create or replace function public.sp_notify() returns trigger
language plpgsql security definer set search_path=public,pg_temp as $$
declare v_owner uuid; v_device text; v_hash text;
begin
 if TG_TABLE_NAME='sp_devices' then v_owner:=NEW.owner_id; v_device:=NEW.id;
 else v_device:=NEW.device_id; select owner_id into v_owner from public.sp_devices where id=v_device;
 end if;
 if v_owner is not null then
  perform realtime.send(jsonb_build_object('device_id',v_device),'device_changed','sp:user:'||v_owner::text,true);
 end if;
 -- Notification only: no commands, credentials, or telemetry travel on this
 -- public channel. The device still fetches commands through authenticated TLS.
 if TG_TABLE_NAME='sp_commands' and TG_OP='INSERT' then
  select secret_hash into v_hash from public.sp_devices where id=v_device;
  perform realtime.send('{}'::jsonb,'wake','sp:wake:'||v_hash,false);
 end if;
 return NEW;
end $$;
revoke all on function public.sp_notify() from public;
do $$ begin
 if not exists(select 1 from pg_trigger where tgname='sp_device_notify') then
  create trigger sp_device_notify after insert or update on public.sp_devices for each row execute function public.sp_notify();
  create trigger sp_command_notify after insert or update on public.sp_commands for each row execute function public.sp_notify();
 end if;
end $$;

create or replace function public.sp_register_device(p_device_id text,p_secret text,p_claim_code text,p_boot_id text)
returns jsonb language plpgsql security definer set search_path=public,pg_temp as $$
declare d public.sp_devices;
begin
 if p_device_id is null or p_device_id !~ '^SP-[A-F0-9]{12}$' or p_secret is null or p_secret !~ '^[A-Za-z0-9]{24}$'
  or p_claim_code is null or p_claim_code !~ '^[A-HJ-NP-Z2-9]{8}$' or p_boot_id is null or p_boot_id !~ '^[A-Fa-f0-9]{16}$'
 then raise exception 'Invalid device registration'; end if;
 insert into public.sp_devices(id,secret_hash,claim_hash,boot_id)
 values(p_device_id,public.sp_hash(p_secret),public.sp_hash(p_claim_code),p_boot_id) on conflict(id) do nothing;
 select * into d from public.sp_devices where id=p_device_id for update;
 if d.secret_hash<>public.sp_hash(p_secret) then raise exception 'Device authentication failed'; end if;
 if d.boot_id<>p_boot_id then
  update public.sp_commands set status='expired',reason='Device restarted before application' where device_id=p_device_id and status='pending';
  insert into public.sp_events(device_id,kind,message) values(p_device_id,'restart','Device restarted; outputs initialized OFF');
 end if;
 update public.sp_devices set boot_id=p_boot_id,last_seen=now() where id=p_device_id;
 return jsonb_build_object('status','ok','owned',d.owner_id is not null);
end $$;

create or replace function public.sp_claim_device(p_claim_code text,p_name text default 'My SmartPlug') returns jsonb
language plpgsql security definer set search_path=public,pg_temp as $$
declare d public.sp_devices; a public.sp_claim_attempts; v_code text;
begin
 if auth.uid() is null then raise exception 'Sign in first'; end if;
 insert into public.sp_claim_attempts(actor_id) values(auth.uid()) on conflict do nothing;
 select * into a from public.sp_claim_attempts where actor_id=auth.uid() for update;
 if a.window_start < now()-interval '10 minutes' then
  update public.sp_claim_attempts set attempts=0,window_start=now() where actor_id=auth.uid(); a.attempts:=0;
 end if;
 if a.attempts>=10 then return jsonb_build_object('error','Too many attempts. Try again in ten minutes.'); end if;
 update public.sp_claim_attempts set attempts=attempts+1 where actor_id=auth.uid();
 v_code:=upper(regexp_replace(coalesce(p_claim_code,''),'[ -]','','g'));
 select * into d from public.sp_devices where claim_hash=public.sp_hash(v_code) for update;
 if not found or (d.owner_id is not null and d.owner_id<>auth.uid()) then return jsonb_build_object('error','Code unavailable. Check the code on your device.'); end if;
 if d.last_seen<now()-interval '2 minutes' then return jsonb_build_object('error','Connect the device to Wi-Fi before claiming.'); end if;
 if length(trim(p_name)) not between 1 and 60 then return jsonb_build_object('error','Use a device name between 1 and 60 characters.'); end if;
 update public.sp_devices set owner_id=auth.uid(),name=trim(p_name) where id=d.id;
 insert into public.sp_events(device_id,kind,message) values(d.id,'ownership','Device added to your workspace');
 return jsonb_build_object('id',d.id);
end $$;

create or replace function public.sp_send_command(p_device_id text,p_target text,p_action text,p_args jsonb,p_request_id uuid)
returns jsonb language plpgsql security definer set search_path=public,pg_temp as $$
declare d public.sp_devices; c public.sp_commands; k text; v numeric;
begin
 select * into d from public.sp_devices where id=p_device_id and owner_id=auth.uid() for update;
 if not found then raise exception 'Device not available to this account'; end if;
 select * into c from public.sp_commands where id=p_request_id;
 if found then
  if c.device_id<>p_device_id or c.actor_id<>auth.uid() then raise exception 'Request ID already used'; end if;
  return to_jsonb(c);
 end if;
 if d.last_seen<now()-interval '45 seconds' then raise exception 'Device is offline. Reconnect it before sending commands.'; end if;
 if p_target is null or p_target not in ('plug','iron') or p_action is null or p_action not in ('on','off','settings','rearm')
  or p_args is null or jsonb_typeof(p_args)<>'object' or octet_length(p_args::text)>2048 then raise exception 'Invalid command'; end if;
 if exists(select 1 from public.sp_commands where device_id=p_device_id and status='pending' and expires_at>now()) then
  -- OFF is allowed to supersede other pending work; otherwise one command at a time.
  if p_action<>'off' then raise exception 'Wait for the current command to finish'; end if;
  update public.sp_commands set status='expired',reason='Superseded by OFF command' where device_id=p_device_id and status='pending';
 end if;
 if p_action='settings' then
  for k in select jsonb_object_keys(p_args) loop
   if k not in ('mode','human_on_seconds','detection_delay_seconds','max_on_enabled','max_on_seconds','iron_mode','iron_inactivity_seconds','iron_max_on_enabled','iron_max_on_seconds') then raise exception 'Unknown setting: %',k; end if;
   if (p_target='plug' and k like 'iron_%') or (p_target='iron' and k not like 'iron_%') then raise exception 'Setting belongs to another output'; end if;
   if k in ('mode','iron_mode') then
    if jsonb_typeof(p_args->k)<>'string' or (k='mode' and p_args->>k not in ('MANUAL','AUTO','HYBRID')) or (k='iron_mode' and p_args->>k not in ('OFF','MANUAL','AUTO')) then raise exception 'Invalid mode'; end if;
   elsif k in ('max_on_enabled','iron_max_on_enabled') then
    if jsonb_typeof(p_args->k)<>'boolean' then raise exception 'Expected boolean'; end if;
   else
    if jsonb_typeof(p_args->k)<>'number' then raise exception 'Expected seconds'; end if;
    v:=(p_args->>k)::numeric;
    if v<>trunc(v) or v<(case when k='detection_delay_seconds' then 0 else 1 end) or v>(case when k='detection_delay_seconds' then 3600 else 86400 end) then raise exception 'Seconds out of range'; end if;
   end if;
  end loop;
 end if;
 insert into public.sp_commands(id,device_id,actor_id,boot_id,target,action,args)
 values(p_request_id,p_device_id,auth.uid(),d.boot_id,p_target,p_action,p_args) returning * into c;
 return to_jsonb(c);
end $$;

create or replace function public.sp_device_exchange(p_device_id text,p_secret text,p_boot_id text,p_state jsonb,p_ack jsonb default null)
returns jsonb language plpgsql security definer set search_path=public,pg_temp as $$
declare d public.sp_devices; c public.sp_commands; v_state jsonb; v_kind text; v_message text;
begin
 select * into d from public.sp_devices where id=p_device_id for update;
 if not found or p_secret is null or p_boot_id is null or d.secret_hash<>public.sp_hash(p_secret) or d.boot_id<>p_boot_id then raise exception 'Device authentication failed'; end if;
 if p_state is null or jsonb_typeof(p_state)<>'object' or octet_length(p_state::text)>8192 then raise exception 'Invalid telemetry'; end if;
 -- Whitelist report fields. Neither credentials nor desired commands are stored in telemetry.
 select coalesce(jsonb_object_agg(key,value),'{}'::jsonb) into v_state from jsonb_each(p_state)
 where key in ('power_state','mode','human_detected','current_amps','apparent_va','power_reason','rssi','firmware_version','uptime_seconds','free_heap','max_on_enabled','max_on_seconds','human_on_seconds','detection_delay_seconds','plug_tripped','plug_remaining_seconds','iron_supported','bmi160_healthy','iron_power_state','iron_mode','iron_motion_detected','iron_inactivity_seconds','iron_max_on_enabled','iron_max_on_seconds','iron_fault','iron_power_reason','iron_tripped','iron_remaining_seconds','maintenance');
 if p_ack is not null then
  if p_ack->>'status' not in ('applied','rejected') then raise exception 'Invalid acknowledgement'; end if;
  update public.sp_commands set status=p_ack->>'status',reason=left(coalesce(p_ack->>'reason',''),200),acknowledged_at=now()
   where id=(p_ack->>'id')::uuid and device_id=p_device_id and boot_id=p_boot_id and status='pending';
 end if;
 update public.sp_commands set status='expired',reason='No device confirmation before expiry' where device_id=p_device_id and status='pending' and expires_at<=now();
 if d.state->>'power_state' is distinct from v_state->>'power_state' and v_state ? 'power_state' then
  insert into public.sp_events(device_id,kind,message) values(p_device_id,'plug','Plug '||(v_state->>'power_state')||' · '||coalesce(v_state->>'power_reason','State changed'));
 end if;
 if d.state->>'iron_power_state' is distinct from v_state->>'iron_power_state' and v_state ? 'iron_power_state' then
  insert into public.sp_events(device_id,kind,message) values(p_device_id,'iron','Iron '||(v_state->>'iron_power_state')||' · '||coalesce(v_state->>'iron_power_reason','State changed'));
 end if;
 if d.state->>'iron_fault' is distinct from v_state->>'iron_fault' and v_state->>'iron_fault'='true' then
  insert into public.sp_events(device_id,kind,message) values(p_device_id,'fault','Iron sensor unavailable; output locked OFF');
 end if;
 update public.sp_devices set state=v_state,last_seen=now() where id=p_device_id;
 if jsonb_typeof(v_state->'current_amps')='number' and jsonb_typeof(v_state->'apparent_va')='number' then
  insert into public.sp_samples(device_id,minute,current_amps,apparent_va)
  values(p_device_id,date_trunc('minute',now()),(v_state->>'current_amps')::double precision,(v_state->>'apparent_va')::double precision)
  on conflict(device_id,minute) do update set current_amps=excluded.current_amps,apparent_va=excluded.apparent_va;
 end if;
 -- Bound history without requiring a scheduled extension.
 delete from public.sp_samples where device_id=p_device_id and minute<now()-interval '7 days';
 delete from public.sp_events where device_id=p_device_id and created_at<now()-interval '30 days';
 delete from public.sp_commands where device_id=p_device_id and status<>'pending' and created_at<now()-interval '30 days';
 select * into c from public.sp_commands where device_id=p_device_id and boot_id=p_boot_id and status='pending' and expires_at>now() order by created_at limit 1;
 return jsonb_build_object('status','ok','owned',d.owner_id is not null,'server_time',now(),'command',case when c.id is null then null else to_jsonb(c)||jsonb_build_object('ttl_ms',greatest(0,floor(extract(epoch from (c.expires_at-now()))*1000))) end);
end $$;

create or replace function public.sp_rename_device(p_device_id text,p_name text) returns void
language plpgsql security definer set search_path=public,pg_temp as $$
begin
 if length(trim(p_name)) not between 1 and 60 then raise exception 'Use 1–60 characters'; end if;
 update public.sp_devices set name=trim(p_name) where id=p_device_id and owner_id=auth.uid();
 if not found then raise exception 'Device unavailable'; end if;
end $$;

revoke all on function public.sp_register_device(text,text,text,text),public.sp_device_exchange(text,text,text,jsonb,jsonb),public.sp_claim_device(text,text),public.sp_send_command(text,text,text,jsonb,uuid),public.sp_rename_device(text,text) from public;
grant execute on function public.sp_register_device(text,text,text,text),public.sp_device_exchange(text,text,text,jsonb,jsonb) to anon;
grant execute on function public.sp_claim_device(text,text),public.sp_send_command(text,text,text,jsonb,uuid),public.sp_rename_device(text,text) to authenticated;
notify pgrst, 'reload schema';
commit;
