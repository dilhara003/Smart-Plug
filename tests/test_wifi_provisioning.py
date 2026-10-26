import importlib.util
import json
import os
from pathlib import Path
import pty
import select
import threading
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('connect_kelani_wifi',ROOT/'scripts/connect_kelani_wifi.py')
helper=importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)

class ProvisioningTests(unittest.TestCase):
    def test_profile_keeps_exact_network_and_credentials(self):
        password='spaces " and \\ characters'
        p=helper.make_profile('Kelani_WiFi','student-id',password,1792990000)
        self.assertEqual(p['ssid'],'Kelani_WiFi')
        self.assertEqual(json.loads(json.dumps(p))['password'],password)
        self.assertEqual(p['auth'],'PEAP')

    def test_invalid_profiles_are_rejected_before_serial_write(self):
        for values in [('', 'student','pw',1792990000),('x'*33,'student','pw',1792990000),('Kelani_WIFI','','pw',1792990000),('Kelani_WIFI','student','',1792990000),('Kelani_WIFI','student','x'*65,1792990000),('Kelani_WIFI','student','pw',0)]:
            with self.subTest(values=(len(values[0]),len(values[1]),len(values[2]),values[3])):
                with self.assertRaises(ValueError): helper.make_profile(*values)

    def test_real_serial_transport_filters_logs_and_preserves_json(self):
        master,slave=pty.openpty()
        port=None
        try:
            port=helper.USBPort(os.ttyname(slave))
            received=[]
            def peer():
                if not select.select([master],[],[],5)[0]: return
                received.append(os.read(master,1024))
                os.write(master,b'[BMI160] Not detected; iron locked OFF\n')
                os.write(master,b'ERU_WIFI_RESULT '+json.dumps({'kind':'status','connected':True,'ssid':'Kelani_WiFi','ip':'192.0.2.10'}).encode()+b'\n')
            thread=threading.Thread(target=peer,daemon=True)
            thread.start()
            port.send('ERU_WIFI_STATUS')
            reply=port.read_reply(1)
            thread.join(2)
            self.assertIn(b'ERU_WIFI_STATUS\n',b''.join(received))
            self.assertTrue(reply['connected'])
            self.assertEqual(reply['ssid'],'Kelani_WiFi')
        finally:
            if port: port.close()
            os.close(master);os.close(slave)

if __name__=='__main__': unittest.main()
