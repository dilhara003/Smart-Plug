// Read-only live checks. No privileged key, test users, or test devices are needed.
import { createClient } from '@supabase/supabase-js';
import { randomBytes,randomUUID } from 'node:crypto';
import { readFile,writeFile } from 'node:fs/promises';
import assert from 'node:assert/strict';
const url=process.env.SUPABASE_URL||'https://udmbcrnrdmxyhvgxcyfe.supabase.co';
const key=process.env.SUPABASE_PUBLISHABLE_KEY||'sb_publishable_SdL4E2M2PGyJLZ-DGfx_qQ_-yLw0cNh';
const owner=createClient(url,key,{auth:{persistSession:false,autoRefreshToken:false}});
const credentials=JSON.parse(await readFile('artifacts/owner-login.json','utf8'));
const results=[];const pass=s=>{results.push(s);console.log('PASS:',s);};let ownChannel,foreignChannel,ws;
try{
 const {data,error}=await owner.auth.signInWithPassword({email:credentials.email,password:credentials.password});if(error)throw error;
 pass('Owner sign-in using the public browser key');
 await owner.realtime.setAuth(data.session.access_token);
 const read=await owner.from('sp_devices').select('id,name,owner_id,boot_id,state,last_seen,created_at');if(read.error)throw read.error;
 assert.ok(read.data.every(d=>d.owner_id===data.user.id));pass('Owner-scoped device read');
 const secrets=await owner.from('sp_devices').select('secret_hash');assert.ok(secrets.error);pass('Device secret hashes are not readable by browser users');
 const invalid=await fetch(url+'/rest/v1/rpc/sp_register_device',{method:'POST',headers:{apikey:key,'Content-Type':'application/json'},body:JSON.stringify({p_device_id:'invalid',p_secret:'invalid',p_claim_code:'invalid',p_boot_id:'invalid'})});assert.equal(invalid.status,400);pass('Malformed device registration rejected without creating data');
 ownChannel=owner.channel('sp:user:'+data.user.id,{config:{private:true}});
 await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('Owner channel timeout')),15000);ownChannel.subscribe((s,err)=>{if(s==='SUBSCRIBED'){clearTimeout(t);resolve();}else if(s==='CHANNEL_ERROR'){clearTimeout(t);reject(Error('Owner channel rejected: '+(err?.message||'unknown')));}});});
 pass('Authenticated owner private Realtime channel joins');
 foreignChannel=owner.channel('sp:user:'+randomUUID(),{config:{private:true}});
 await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('Foreign channel rejection timeout')),15000);foreignChannel.subscribe(s=>{if(s==='CHANNEL_ERROR'){clearTimeout(t);resolve();}else if(s==='SUBSCRIBED'){clearTimeout(t);reject(Error('Foreign private channel unexpectedly authorized'));}});});
 pass('Another owner’s private Realtime channel is denied');
 ws=new WebSocket(url.replace('https:','wss:')+'/realtime/v1/websocket?apikey='+key+'&vsn=1.0.0');
 await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('Device wire-protocol timeout')),15000);ws.addEventListener('open',()=>ws.send(JSON.stringify({topic:'realtime:sp:wake:'+randomBytes(32).toString('hex'),event:'phx_join',payload:{config:{broadcast:{self:false},private:false}},ref:'1'})));ws.addEventListener('message',e=>{const m=JSON.parse(e.data);if(m.event==='phx_reply'&&m.ref==='1'){clearTimeout(t);m.payload.status==='ok'?resolve():reject(Error('Device channel rejected'));}});ws.addEventListener('error',()=>{clearTimeout(t);reject(Error('Device WebSocket failed'));});});
 pass('Exact ESP32 Phoenix/WebSocket join format works with the publishable key');
 await writeFile('artifacts/live-verification.json',JSON.stringify({date:new Date().toISOString(),project:url,checks:results,limits:'Read-only live verification; command execution, expiry and cross-owner data tests run in isolated PostgreSQL and controller harness.'},null,2));
}finally{
 ws?.close();if(ownChannel)await owner.removeChannel(ownChannel);if(foreignChannel)await owner.removeChannel(foreignChannel);await owner.realtime.disconnect();await owner.auth.signOut({scope:'local'});
}
