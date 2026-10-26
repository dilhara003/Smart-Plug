import { createClient } from '@supabase/supabase-js';
export const supabase = createClient(
  import.meta.env.VITE_SUPABASE_URL || 'https://udmbcrnrdmxyhvgxcyfe.supabase.co',
  import.meta.env.VITE_SUPABASE_PUBLISHABLE_KEY || 'sb_publishable_SdL4E2M2PGyJLZ-DGfx_qQ_-yLw0cNh',
  { auth: { persistSession: true, autoRefreshToken: true, detectSessionInUrl: true } },
);
export const deviceColumns = 'id,name,owner_id,boot_id,state,last_seen,created_at';
export const online = (lastSeen: string, now = Date.now()) => now - Date.parse(lastSeen) < 45_000;
export const age = (timestamp: string, now = Date.now()) => {
  const sec = Math.max(0, Math.floor((now - Date.parse(timestamp)) / 1000));
  if (sec < 5) return 'just now';
  if (sec < 60) return `${sec}s ago`;
  if (sec < 3600) return `${Math.floor(sec / 60)}m ago`;
  return `${Math.floor(sec / 3600)}h ago`;
};
export const duration = (sec = 0) => sec >= 3600 ? `${Math.floor(sec / 3600)}h ${Math.floor(sec % 3600 / 60)}m` : sec >= 60 ? `${Math.floor(sec / 60)}m ${Math.floor(sec % 60)}s` : `${Math.max(0, Math.floor(sec))}s`;
