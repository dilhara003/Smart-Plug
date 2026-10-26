-- Supabase-compatible schema stand-ins are created only in the isolated test DB.
create schema if not exists auth;
create schema if not exists realtime;
create schema if not exists extensions;
do $$begin
 if not exists(select 1 from pg_roles where rolname='anon') then create role anon; end if;
 if not exists(select 1 from pg_roles where rolname='authenticated') then create role authenticated; end if;
end$$;
create table if not exists auth.users(id uuid primary key);
create table if not exists realtime.messages(id int);
alter table realtime.messages enable row level security;
create table if not exists realtime.test_notifications(payload jsonb,event text,topic text,private boolean);
create or replace function auth.uid() returns uuid language sql stable as $$select nullif(current_setting('request.jwt.claim.sub',true),'')::uuid$$;
create or replace function realtime.topic() returns text language sql stable as $$select current_setting('realtime.topic',true)$$;
create or replace function realtime.send(payload jsonb,event text,topic text,private boolean) returns void language sql as $$insert into realtime.test_notifications values(payload,event,topic,private)$$;
grant usage on schema auth,realtime,public to anon,authenticated;
grant select on realtime.messages to authenticated;
