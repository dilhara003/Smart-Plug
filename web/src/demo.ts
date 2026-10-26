import type { Device, DeviceEvent, Sample } from './types';
export function demoData() {
  const now = new Date().toISOString();
  const device: Device = { id: 'SP-DE0000000001', name: 'Workbench', owner_id: 'demo', boot_id: 'DE00000000000001', last_seen: now, created_at: now,
    state: { power_state: 'ON', mode: 'AUTO', human_detected: true, current_amps: 0.42, apparent_va: 96.6, rssi: -48, firmware_version: '1.0.0', uptime_seconds: 7200, power_reason: 'Presence detected', max_on_enabled: true, max_on_seconds: 7200, human_on_seconds: 300, detection_delay_seconds: 0, plug_remaining_seconds: 4200, iron_supported: true, bmi160_healthy: true, iron_power_state: 'OFF', iron_mode: 'MANUAL', iron_inactivity_seconds: 300, iron_max_on_enabled: true, iron_max_on_seconds: 1800, iron_power_reason: 'Manual OFF', iron_remaining_seconds: 0, iron_fault: false } };
  const events: DeviceEvent[] = [
    { id: 3, device_id: device.id, kind: 'plug', message: 'Plug ON · Presence detected', created_at: now },
    { id: 2, device_id: device.id, kind: 'iron', message: 'Iron OFF · Manual command', created_at: new Date(Date.now() - 120000).toISOString() },
    { id: 1, device_id: device.id, kind: 'restart', message: 'Device connected to your workspace', created_at: new Date(Date.now() - 3600000).toISOString() },
  ];
  const samples: Sample[] = Array.from({ length: 32 }, (_, i) => ({ device_id: device.id, minute: new Date(Date.now() - (31 - i) * 60000).toISOString(), current_amps: Math.max(0, 0.32 + Math.sin(i * 0.7) * 0.12 + Math.cos(i * 1.4) * 0.06), apparent_va: 0 }));
  return { device, events, samples };
}
