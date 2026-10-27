import { useCallback, useEffect, useRef, useState } from 'react';
import { deviceColumns, supabase } from './lib';
import { demoData } from './demo';
import type { Action, Command, Device, DeviceEvent, Output, Sample } from './types';

function newCommandId() {
  // randomUUID is unavailable to a phone visiting the Mac over plain LAN HTTP.
  const bytes = crypto.getRandomValues(new Uint8Array(16));
  bytes[6] = (bytes[6] & 0x0f) | 0x40;
  bytes[8] = (bytes[8] & 0x3f) | 0x80;
  const hex = Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('');
  return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${hex.slice(16, 20)}-${hex.slice(20)}`;
}

export function useWorkspace(userId: string | undefined, demo: boolean) {
  const [devices, setDevices] = useState<Device[]>([]);
  const [selected, setSelected] = useState('');
  const [commands, setCommands] = useState<Command[]>([]);
  const [events, setEvents] = useState<DeviceEvent[]>([]);
  const [samples, setSamples] = useState<Sample[]>([]);
  const [loading, setLoading] = useState(true);
  const [connection, setConnection] = useState('Connecting');
  const [error, setError] = useState('');
  const [sending, setSending] = useState(false);
  const selectedRef = useRef(selected); selectedRef.current = selected;
  const generation = useRef(0);
  useEffect(() => {
    if (!demo) { setDevices([]); setSelected(''); setCommands([]); setEvents([]); setSamples([]); setLoading(true); }
  }, [userId, demo]);
  const refresh = useCallback(async () => {
    if (!userId || demo) return;
    const gen = ++generation.current;
    try {
      const { data, error: err } = await supabase.from('sp_devices').select(deviceColumns).order('created_at');
      if (err) throw err;
      if (gen !== generation.current) return;
      const list = (data || []) as Device[];
      setDevices(list);
      const id = list.some(d => d.id === selectedRef.current) ? selectedRef.current : list[0]?.id;
      if (id) {
        setSelected(id);
        const [c, e, s] = await Promise.all([
          supabase.from('sp_commands').select('*').eq('device_id', id).order('created_at', { ascending: false }).limit(30),
          supabase.from('sp_events').select('*').eq('device_id', id).order('created_at', { ascending: false }).limit(50),
          supabase.from('sp_samples').select('*').eq('device_id', id).gte('minute', new Date(Date.now() - 86400000).toISOString()).order('minute').limit(1440),
        ]);
        if (gen !== generation.current) return;
        if (c.error || e.error || s.error) throw c.error || e.error || s.error;
        setCommands(c.data || []); setEvents(e.data || []); setSamples(s.data || []);
      } else { setCommands([]); setEvents([]); setSamples([]); }
      setError('');
    } catch (err) {
      if (gen === generation.current) setError(err instanceof Error ? err.message : String((err as { message?: string }).message || err));
    } finally { if (gen === generation.current) setLoading(false); }
  }, [userId, demo]);

  useEffect(() => {
    if (!demo) return;
    const d = demoData(); setDevices([d.device]); setSelected(d.device.id); setEvents(d.events); setSamples(d.samples); setLoading(false); setConnection('Demo');
    const timer = setInterval(() => setDevices(ds => ds.map(d => ({ ...d, last_seen: new Date().toISOString() }))), 5000);
    return () => clearInterval(timer);
  }, [demo]);
  useEffect(() => {
    if (!userId || demo) return;
    let disposed = false; let scheduled: ReturnType<typeof setTimeout> | undefined;
    const schedule = () => { if (!scheduled) scheduled = setTimeout(() => { scheduled = undefined; if (!disposed) void refresh(); }, 180); };
    void refresh();
    let channel: ReturnType<typeof supabase.channel> | undefined;
    void (async () => {
      const { data } = await supabase.auth.getSession();
      if (disposed || !data.session) return;
      await supabase.realtime.setAuth(data.session.access_token);
      if (disposed) return;
      channel = supabase.channel(`sp:user:${userId}`, { config: { private: true } })
        .on('broadcast', { event: 'device_changed' }, schedule)
        .subscribe(status => { if (!disposed) { setConnection(status === 'SUBSCRIBED' ? 'Live' : status === 'CHANNEL_ERROR' || status === 'TIMED_OUT' || status === 'CLOSED' ? 'Reconnecting' : 'Connecting'); if (status === 'SUBSCRIBED') schedule(); } });
    })().catch(() => { if (!disposed) setConnection('Reconnecting'); });
    const timer = setInterval(schedule, 15_000); // Recovery refresh if a WebSocket notification is missed.
    const onVisible = () => { if (document.visibilityState === 'visible') schedule(); };
    document.addEventListener('visibilitychange', onVisible);
    return () => { disposed = true; clearInterval(timer); clearTimeout(scheduled); document.removeEventListener('visibilitychange', onVisible); if (channel) void supabase.removeChannel(channel); generation.current++; };
  }, [userId, demo, refresh]);
  useEffect(() => { if (selected && !demo) void refresh(); }, [selected, demo, refresh]);

  const send = async (target: Output, action: Action, args: Record<string, unknown> = {}) => {
    if (!selected) throw new Error('Select a device first');
const id = newCommandId(); setSending(true);
    try {
      if (demo) {
        const c: Command = { id, device_id: selected, target, action, args, status: 'applied', reason: 'Demo simulation', created_at: new Date().toISOString(), expires_at: new Date(Date.now() + 20000).toISOString() };
        await new Promise(r => setTimeout(r, 450));
        setCommands(cs => [c, ...cs]);
        setDevices(ds => ds.map(d => ({ ...d, state: { ...d.state, ...(action === 'settings' ? args : action === 'rearm' ? { [target === 'plug' ? 'plug_tripped' : 'iron_tripped']: false } : target === 'plug' ? { power_state: action === 'on' ? 'ON' : 'OFF', mode: 'MANUAL', power_reason: 'Demo manual command' } : { iron_power_state: action === 'on' ? 'ON' : 'OFF', iron_mode: action === 'on' ? 'MANUAL' : 'OFF', iron_power_reason: 'Demo manual command' }) } })));
        setEvents(es => [{ id: Date.now(), device_id: selected, kind: target, message: `${target === 'plug' ? 'Plug' : 'Iron'} · ${action === 'settings' ? 'Settings applied' : action.toUpperCase()} (demo)`, created_at: new Date().toISOString() }, ...es]);
        return c;
      }
      const controller = new AbortController(); const timer = setTimeout(() => controller.abort(), 8000);
      try {
        const { data, error: err } = await supabase.rpc('sp_send_command', { p_device_id: selected, p_target: target, p_action: action, p_args: args, p_request_id: id }).abortSignal(controller.signal);
        if (err) throw err;
        const c = data as Command; setCommands(cs => [c, ...cs.filter(x => x.id !== c.id)]); void refresh(); return c;
      } finally { clearTimeout(timer); }
    } catch (err) {
      const message = (err as { message?: string }).message || 'Request outcome unknown. Refresh activity before retrying.';
      throw new Error(message.includes('abort') || message.includes('fetch') ? 'Connection interrupted. The result is uncertain; check activity before retrying.' : message);
    } finally { setSending(false); }
  };
  const claim = async (code: string, name: string) => {
    if (demo) throw new Error('Exit demo and sign in to add a real device.');
    const { data, error } = await supabase.rpc('sp_claim_device', { p_claim_code: code, p_name: name });
    if (error) throw new Error(error.message);
    if (data.error) throw new Error(data.error);
    setSelected(data.id); await refresh();
  };
  const rename = async (name: string) => {
    if (demo) { setDevices(ds => ds.map(d => d.id === selected ? { ...d, name } : d)); return; }
    const { error } = await supabase.rpc('sp_rename_device', { p_device_id: selected, p_name: name });
    if (error) throw new Error(error.message); await refresh();
  };
  return { devices, device: devices.find(d => d.id === selected), selected, setSelected, commands, events, samples, loading, connection, error, sending, send, claim, rename, refresh };
}
