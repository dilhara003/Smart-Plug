import {test} from 'node:test';
import {execFileSync} from 'node:child_process';
import {mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
test('USB enterprise provisioning uses real serial framing without external packages',()=>{
  execFileSync('python3',['-m','unittest','discover','-s','tests','-p','test_wifi_provisioning.py'],{stdio:'pipe'});
});
test('firmware Wi-Fi profile bounds reject empty, oversized and invalid-clock inputs',()=>{
  const p=mkdtempSync(join(tmpdir(),'eru-wifi-'));
  try{
    writeFileSync(join(p,'check.cpp'),`#include <cassert>\n#include "${resolve('SmartPlug_eru/WiFiProvisioningRules.h')}"\nint main(){using namespace WiFiProvisioningRules;assert(validCredentials(true,11,15,16));assert(!validCredentials(true,11,0,16));assert(!validCredentials(true,33,15,16));assert(!validCredentials(true,11,15,0));assert(!validCredentials(true,11,15,65));assert(validCredentials(false,4,0,0));assert(validEpoch(1792990000));assert(!validEpoch(0));assert(!validEpoch(UINT64_MAX));}`);
    execFileSync(process.env.CXX||'clang++',['-std=c++17',join(p,'check.cpp'),'-o',join(p,'check')],{stdio:'pipe'});
    execFileSync(join(p,'check'));
  }finally{rmSync(p,{recursive:true,force:true});}
});
