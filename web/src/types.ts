export type Output = 'plug' | 'iron';
export type Action = 'on' | 'off' | 'settings' | 'rearm';
export type DeviceState = {
  power_state?: 'ON' | 'OFF'; mode?: string; human_detected?: boolean;
  current_amps?: number; apparent_va?: number; power_reason?: string;
  rssi?: number; firmware_version?: string; uptime_seconds?: number;
  max_on_enabled?: boolean; max_on_seconds?: number; human_on_seconds?: number;
  detection_delay_seconds?: number; plug_tripped?: boolean; plug_remaining_seconds?: number;
  iron_supported?: boolean; bmi160_healthy?: boolean; iron_power_state?: 'ON' | 'OFF';
  iron_mode?: string; iron_motion_detected?: boolean; iron_inactivity_seconds?: number;
  iron_max_on_enabled?: boolean; iron_max_on_seconds?: number;
  iron_fault?: boolean; iron_power_reason?: string; iron_tripped?: boolean;
  iron_remaining_seconds?: number; maintenance?: boolean;
};
export type Device = { id: string; name: string; owner_id: string; boot_id: string; state: DeviceState; last_seen: string; created_at: string };
export type Command = { id: string; device_id: string; target: Output; action: Action; args: Record<string, unknown>; status: 'pending' | 'applied' | 'rejected' | 'expired'; reason: string | null; created_at: string; expires_at: string; acknowledged_at?: string };
export type DeviceEvent = { id: number; device_id: string; kind: string; message: string; created_at: string };
export type Sample = { device_id: string; minute: string; current_amps: number; apparent_va: number };
