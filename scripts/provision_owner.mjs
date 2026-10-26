// Optional one-time admin setup. The server key is never written to disk.
import { createClient } from '@supabase/supabase-js';
import { randomBytes } from 'node:crypto';
import { mkdir, writeFile } from 'node:fs/promises';
const email = process.env.SMARTPLUG_OWNER_EMAIL;
const key = process.env.SUPABASE_SERVER_KEY;
if (!email || !key) throw new Error('Set SMARTPLUG_OWNER_EMAIL and SUPABASE_SERVER_KEY for this one-time command.');
const admin = createClient(process.env.SUPABASE_URL || 'https://udmbcrnrdmxyhvgxcyfe.supabase.co', key, { auth: { persistSession: false, autoRefreshToken: false } });
const { data: existing, error: listError } = await admin.auth.admin.listUsers({ page: 1, perPage: 1000 });
if (listError) throw listError;
if (existing.users.some(u => u.email?.toLowerCase() === email.toLowerCase())) {
  console.log('Owner account already exists; its password was preserved.');
} else {
  const password = randomBytes(24).toString('base64url') + '!a7';
  const { data, error } = await admin.auth.admin.createUser({ email, password, email_confirm: true, user_metadata: { display_name: 'My workspace' } });
  if (error) throw error;
  await mkdir('artifacts', { recursive: true });
  await writeFile('artifacts/owner-login.json', JSON.stringify({ email, password, userId: data.user.id }, null, 2), { mode: 0o600 });
  await writeFile('artifacts/OWNER_LOGIN.txt', `ERU SmartPlug owner login\n\nURL: http://127.0.0.1:5173/\nEmail: ${email}\nPassword: ${password}\n\nThis is your application password, separate from the database password.\nKeep this file private. It is excluded from Git and release archives.\nYou can change the password from Settings > Account in the app.\n`, { mode: 0o600 });
  console.log('Owner account created without sending an invitation email. Private login saved in artifacts/OWNER_LOGIN.txt.');
}
