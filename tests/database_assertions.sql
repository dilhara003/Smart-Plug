begin;
insert into auth.users values('00000000-0000-4000-8000-000000000001'),('00000000-0000-4000-8000-000000000002') on conflict do nothing;
set role anon;
select public.sp_register_device('SP-AA0000000001','abcdefghijklmnopqrstuvwx','ABCD2345','0000000000000001');
select public.sp_device_exchange('SP-AA0000000001','abcdefghijklmnopqrstuvwx','0000000000000001','{"power_state":"OFF","current_amps":0.1,"apparent_va":23,"secret":"must-not-store"}');
do $$begin
 begin
  perform public.sp_device_exchange('SP-AA0000000001','abcdefghijklmnopqrstuvwx',null,'{}');
  raise exception 'TEST FAILED: null boot ID accepted';
 exception when raise_exception then if SQLERRM like 'TEST FAILED:%' then raise; end if; end;
 begin
  perform public.sp_device_exchange('SP-AA0000000001','wrong-secret','0000000000000001','{}');
  raise exception 'TEST FAILED: invalid secret accepted';
 exception when raise_exception then if SQLERRM like 'TEST FAILED:%' then raise; end if; end;
end$$;
reset role;
set role authenticated;
select set_config('request.jwt.claim.sub','00000000-0000-4000-8000-000000000001',true);
select public.sp_claim_device('ABCD-2345','Test workbench');
do $$declare n int; begin
 select count(id) into n from public.sp_devices; assert n=1,'Owner cannot read own device';
 begin perform secret_hash from public.sp_devices; raise exception 'TEST FAILED: secret hash exposed';
 exception when insufficient_privilege then null; end;
 begin perform public.sp_send_command('SP-AA0000000001','plug','settings','{"human_on_seconds":-1}','10000000-0000-4000-8000-000000000001'); raise exception 'TEST FAILED: negative timeout accepted';
 exception when raise_exception then if SQLERRM like 'TEST FAILED:%' then raise; end if; end;
 begin perform public.sp_send_command('SP-AA0000000001','plug','settings','{"mode":null}','10000000-0000-4000-8000-000000000001'); raise exception 'TEST FAILED: null mode accepted';
 exception when raise_exception then if SQLERRM like 'TEST FAILED:%' then raise; end if; end;
end$$;
select public.sp_send_command('SP-AA0000000001','plug','on','{}','10000000-0000-4000-8000-000000000001');
-- Same request ID must be idempotent.
select public.sp_send_command('SP-AA0000000001','plug','on','{}','10000000-0000-4000-8000-000000000001');
do $$declare n int; begin select count(*) into n from public.sp_commands; assert n=1,'Duplicate command inserted'; end$$;
select set_config('request.jwt.claim.sub','00000000-0000-4000-8000-000000000002',true);
do $$declare n int; r jsonb; begin
 select count(id) into n from public.sp_devices; assert n=0,'Cross-owner device read';
 select count(*) into n from public.sp_commands; assert n=0,'Cross-owner command read';
 r:=public.sp_claim_device('ABCD2345','Hijack'); assert r ? 'error','Owned device could be reclaimed';
 begin perform public.sp_send_command('SP-AA0000000001','plug','off','{}','10000000-0000-4000-8000-000000000002'); raise exception 'TEST FAILED: cross-owner control';
 exception when raise_exception then if SQLERRM like 'TEST FAILED:%' then raise; end if; end;
end$$;
reset role;
set role anon;
do $$declare r jsonb; begin
 r:=public.sp_device_exchange('SP-AA0000000001','abcdefghijklmnopqrstuvwx','0000000000000001','{"power_state":"OFF"}');
 assert r->'command'->>'id'='10000000-0000-4000-8000-000000000001','Queued command not delivered';
 assert (r->'command'->>'ttl_ms')::int>0,'Command TTL missing';
end$$;
select public.sp_device_exchange('SP-AA0000000001','abcdefghijklmnopqrstuvwx','0000000000000001','{"power_state":"ON"}','{"id":"10000000-0000-4000-8000-000000000001","status":"applied","reason":"Output ON"}');
reset role;
do $$begin
 assert (select status from public.sp_commands where id='10000000-0000-4000-8000-000000000001')='applied','ACK not saved';
 assert not (select state from public.sp_devices where id='SP-AA0000000001') ? 'secret','Telemetry whitelist failed';
 assert exists(select 1 from realtime.test_notifications where event='wake' and payload='{}'::jsonb and not private),'Device wake notification missing';
 assert exists(select 1 from realtime.test_notifications where topic='sp:user:00000000-0000-4000-8000-000000000001' and private),'Private owner update missing';
end$$;
set role authenticated;
select set_config('request.jwt.claim.sub','00000000-0000-4000-8000-000000000001',true);
select public.sp_send_command('SP-AA0000000001','iron','on','{}','10000000-0000-4000-8000-000000000003');
select public.sp_send_command('SP-AA0000000001','iron','off','{}','10000000-0000-4000-8000-000000000004');
reset role;
do $$begin assert (select status from public.sp_commands where id='10000000-0000-4000-8000-000000000003')='expired','OFF did not supersede pending ON'; end$$;
set role anon;
select public.sp_register_device('SP-AA0000000001','abcdefghijklmnopqrstuvwx','ABCD2345','0000000000000002');
reset role;
do $$begin assert not exists(select 1 from public.sp_commands where status='pending'),'Reboot left old commands executable'; end$$;
rollback;
select 'PASS: device authentication, owner isolation, secrets, validation, duplicate IDs, ACK, wake, OFF supersession, reboot expiry' as result;
