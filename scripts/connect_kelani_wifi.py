#!/usr/bin/env python3
"""Provision Kelani PEAP credentials over USB; keep the Mac on its current Wi-Fi.

Uses only Python's standard library on macOS/Linux. Passwords are prompted with
getpass and are never printed, added to shell arguments, or written to a file.
"""
import argparse
import getpass
import json
import os
import select
import sys
import time

PREFIX=b'ERU_WIFI_RESULT '

def connection_failure(state):
    code=state.get('network_disconnect_reason',state.get('disconnect_reason',0))
    reasons={
        2:'Access point authentication expired',
        15:'Wi-Fi key handshake timed out',
        23:'University 802.1X authentication failed (credentials or server-certificate validation)',
        200:'Access point signal was lost',
        201:'Campus access point not found',
        202:'Access point authentication failed',
        203:'Access point association failed',
        204:'Wi-Fi handshake timed out',
        205:'Wi-Fi connection failed',
        210:'No access point with compatible security found',
    }
    return reasons.get(code,'Connection not confirmed')+f' (network reason {code}).'

def make_profile(ssid,username,password,epoch):
    if not 1<=len(ssid.encode())<=32: raise ValueError('SSID must be 1–32 bytes')
    if not 1<=len(username.encode())<=64: raise ValueError('Use a university username of 1–64 bytes')
    if not 1<=len(password.encode())<=64: raise ValueError('Password must be 1–64 bytes')
    if not 1735689600<=epoch<4102444800: raise ValueError('Check the Mac clock before provisioning')
    return {'ssid':ssid,'auth':'PEAP','username':username,'password':password,'epoch':epoch}

class USBPort:
    def __init__(self,path):
        import termios
        import fcntl
        self.termios=termios
        self.fd=os.open(path,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
        self.original=termios.tcgetattr(self.fd)
        try:
            fcntl.ioctl(self.fd,termios.TIOCEXCL)
            options=termios.tcgetattr(self.fd)
            options[0]=0; options[1]=0; options[2]=termios.CS8|termios.CREAD|termios.CLOCAL
            options[3]=0; options[4]=termios.B115200; options[5]=termios.B115200
            options[6][termios.VMIN]=0; options[6][termios.VTIME]=0
            termios.tcsetattr(self.fd,termios.TCSANOW,options)
            termios.tcflush(self.fd,termios.TCIFLUSH)
        except BaseException:
            os.close(self.fd); raise
        self.buffer=bytearray()
    def send(self,line):
        data=memoryview((line+'\n').encode())
        while data:
            try: count=os.write(self.fd,data); data=data[count:]
            except BlockingIOError: select.select([],[self.fd],[],1)
        # The device's reply confirms delivery. Avoid an unbounded tcdrain()
        # wait if a USB adapter disappears or its transmitter stops draining.
    def read_reply(self,timeout=3,kind=None):
        until=time.monotonic()+timeout
        while time.monotonic()<until:
            while b'\n' in self.buffer:
                line,_,self.buffer=self.buffer.partition(b'\n')
                if line.startswith(PREFIX):
                    try:
                        result=json.loads(line[len(PREFIX):])
                        if kind is None or result.get('kind') in (kind,'error'): return result
                    except (ValueError,UnicodeDecodeError): pass
            if select.select([self.fd],[],[],max(0,min(.3,until-time.monotonic())))[0]:
                try: self.buffer.extend(os.read(self.fd,4096))
                except BlockingIOError: continue
                if len(self.buffer)>16384: self.buffer=self.buffer[-4096:]
        return None
    def close(self):
        # Leave HUPCL disabled so closing the helper doesn't trigger an extra reset.
        try:
            options=self.termios.tcgetattr(self.fd)
            options[2]&=~self.termios.HUPCL
            self.termios.tcsetattr(self.fd,self.termios.TCSANOW,options)
        finally: os.close(self.fd)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='/dev/cu.usbserial-0001')
    parser.add_argument('--ssid',default='Kelani_WIFI')
    parser.add_argument('--status',action='store_true',help='Read connection diagnostics without prompting or changing settings')
    args=parser.parse_args()
    if not args.status and not sys.stdin.isatty(): parser.error('Run this in your own Terminal so the password stays private.')
    print('Keep the Mac connected to Kelani_WIFI. Close Arduino Serial Monitor first.')
    port=None
    try:
        port=USBPort(args.port)
        info=None
        print('Waiting for the firmware to finish starting…')
        ready_until=time.monotonic()+75
        while time.monotonic()<ready_until:
            port.send('ERU_WIFI_STATUS'); info=port.read_reply(3,'status')
            if info: break
        if not info or not info.get('enterprise_supported'):
            raise RuntimeError('No enterprise-capable firmware response. Upload the new firmware first; close Serial Monitor and retry.')
        if args.status:
            print(json.dumps(info,indent=2))
            if not info.get('connected'): print(connection_failure(info))
            return 0
        ssid=args.ssid
        until=time.monotonic()+12
        while time.monotonic()<until:
            port.send('ERU_WIFI_SCAN'); scan=port.read_reply(3,'scan')
            if scan and scan.get('kind')=='scan' and not scan.get('pending'):
                matches=sorted(scan.get('networks',[]),key=lambda x:x.get('rssi',-999),reverse=True)
                if matches: ssid=matches[0]['ssid']; print('ESP32 found network:',ssid)
                else: print('The campus SSID was not visible in the 2.4 GHz scan; using the supplied name:',ssid)
                break
            time.sleep(.6)
        username=input('University Wi-Fi username (Kelani Net ID): ').strip()
        password=getpass.getpass('University Wi-Fi password (hidden): ')
        profile=make_profile(ssid,username,password,int(time.time()))
        print('Saving directly to the ESP32 over USB. Both outputs will be OFF.')
        port.send('ERU_WIFI_CONFIG '+json.dumps(profile,ensure_ascii=True,separators=(',',':')))
        del password,profile
        saved=port.read_reply(5,'saved')
        if not saved: raise RuntimeError('No save confirmation received. Check that the USB port is not open elsewhere.')
        if saved.get('kind')=='error': raise RuntimeError(saved.get('message','Setup rejected'))
        if saved.get('kind')!='saved': raise RuntimeError('Unexpected device response; retry after the device finishes starting.')
        print('Settings saved. Waiting for enterprise authentication and restart…')
        until=time.monotonic()+100
        last_state={}
        while time.monotonic()<until:
            port.send('ERU_WIFI_STATUS')
            state=port.read_reply(4,'status')
            if state and state.get('kind')=='status': last_state=state
            if state and state.get('kind')=='status' and state.get('connected'):
                print('CONNECTED to',state['ssid'])
                print('Device IP:',state['ip'])
                print('Local dashboard: http://'+state['ip'])
                print('Open the cloud dashboard and add the LCD claim code if you have not claimed it yet.')
                return 0
            time.sleep(1)
        raise RuntimeError(connection_failure(last_state)+' Check the exact Net ID/password, 2.4 GHz coverage and campus device rules. No certificate checks were disabled.')
    except (OSError,ValueError,RuntimeError) as exc:
        print('Setup did not complete:',exc,file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print('\nCancelled.'); return 130
    finally:
        if port: port.close()

if __name__=='__main__': raise SystemExit(main())
