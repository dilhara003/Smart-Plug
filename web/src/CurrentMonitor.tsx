import { useEffect, useId, useMemo, useRef, useState } from 'react';
import { Activity, Clock3, Radio, Zap } from 'lucide-react';
import { age, online } from './lib';
import type { Device, Sample } from './types';
import './current-monitor.css';

type Reading = { time: number; amps: number };
const DAY = 86_400_000;
const windows = [{ label: '15 min', ms: 900_000 }, { label: '1 hour', ms: 3_600_000 }, { label: '24 hours', ms: DAY }];
const validAmps = (amps: unknown): amps is number => typeof amps === 'number' && Number.isFinite(amps) && amps >= 0;
const timeLabel = (time: number, seconds = false) => new Date(time).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', ...(seconds ? { second: '2-digit' } : {}) });

// Retain actual device reports while navigating. Cloud minute history fills
// earlier periods; never create readings by interpolating the current value.
export function useCurrentReports(device: Device | undefined) {
  const [reports, setReports] = useState<{ id: string; readings: Reading[] }>({ id: '', readings: [] });
  const id = device?.id, timestamp = device?.last_seen, amps = device?.state.current_amps;
  useEffect(() => {
    if (!id || !timestamp || !validAmps(amps)) return;
    const time = Date.parse(timestamp);
    if (!Number.isFinite(time)) return;
    setReports(previous => {
      const readings = previous.id === id ? previous.readings : [];
      if (readings.at(-1)?.time === time) return previous;
      return { id, readings: [...readings.filter(r => r.time >= time - DAY && r.time !== time), { time, amps }].slice(-9000) };
    });
  }, [id, timestamp, amps]);
  return reports.id === id ? reports.readings : [];
}

export function CurrentMonitor({ device, samples, reports, now, demo }: { device: Device; samples: Sample[]; reports: Reading[]; now: number; demo: boolean }) {
  const [windowMs, setWindowMs] = useState(windows[0].ms);
  const [unit, setUnit] = useState<'A' | 'mA'>('A');
  const [inspected, setInspected] = useState<number | null>(null);
  const chart = useRef<HTMLDivElement>(null);
  const [chartWidth, setChartWidth] = useState(760);
  useEffect(() => {
    const observer = new ResizeObserver(entries => setChartWidth(Math.max(220, Math.round(entries[0].contentRect.width))));
    if (chart.current) observer.observe(chart.current);
    return () => observer.disconnect();
  }, []);
  const gradient = useId().replace(/:/g, '');
  const connected = online(device.last_seen, now);
  const current = validAmps(device.state.current_amps) ? device.state.current_amps : null;
  const format = (amps: number) => unit === 'A' ? amps.toFixed(3) : (amps * 1000).toFixed(0);
  const allReadings = useMemo(() => {
    const liveMinutes = new Set(reports.map(r => Math.floor(r.time / 60_000)));
    const readings = samples.filter(s => s.device_id === device.id && validAmps(s.current_amps))
      .map(s => ({ time: Date.parse(s.minute), amps: s.current_amps }))
      .filter(r => Number.isFinite(r.time) && !liveMinutes.has(Math.floor(r.time / 60_000)));
    const byTime = new Map([...readings, ...reports].map(r => [r.time, r]));
    return [...byTime.values()].sort((a, b) => a.time - b.time);
  }, [samples, reports, device.id]);
  // A new report can arrive between the parent's one-second clock ticks.
  const end = Math.max(now, Date.now());
  const start = end - windowMs;
  const readings = allReadings.filter(r => r.time >= start && r.time <= end);
  const peak = readings.length ? Math.max(...readings.map(r => r.amps)) : null;
  const average = readings.length ? readings.reduce((sum, r) => sum + r.amps, 0) / readings.length : null;
  const minimum = readings.length ? Math.min(...readings.map(r => r.amps)) : null;
  const top = (() => {
    const value = peak ? Math.max(0.001, peak * 1.15) : 0.1;
    const magnitude = 10 ** Math.floor(Math.log10(value));
    return Math.ceil(value / magnitude / 0.5) * magnitude * 0.5;
  })();
  const axisScale = unit === 'A' ? 1 : 1000;
  const axisDecimals = Math.max(0, Math.min(6, -Math.floor(Math.log10(top * axisScale / 4))));
  const width = chartWidth, height = chartWidth < 420 ? 180 : 250, left = 54, right = 14, bottom = 28, plotHeight = height - bottom - 18;
  const x = (time: number) => left + (time - start) / windowMs * (width - left - right);
  const y = (amps: number) => 18 + plotHeight * (1 - amps / top);
  const segments: Reading[][] = [];
  for (const reading of readings) {
    const last = segments.at(-1);
    if (!last || reading.time - last.at(-1)!.time > 90_000) segments.push([reading]);
    else last.push(reading);
  }
  const selectedIndex = inspected === null ? readings.length - 1 : Math.max(0, readings.findIndex(r => r.time >= inspected));
  const selected = readings[selectedIndex];
  const selectedPercent = selected ? x(selected.time) / width * 100 : 50;
  const inspect = (clientX: number, bounds: DOMRect) => {
    if (!readings.length) return;
    const target = start + ((clientX - bounds.left) / bounds.width * width - left) / (width - left - right) * windowMs;
    const nearest = readings.reduce((a, b) => Math.abs(a.time - target) <= Math.abs(b.time - target) ? a : b);
    setInspected(nearest.time);
  };

  return <section className="panel current-monitor" id="current-monitor" aria-label="Current draw monitor">
    <header className="current-heading">
      <div><span className="eyebrow"><Activity size={13} /> YOUR ELECTRICAL PULSE</span><h2>Current draw</h2></div>
      <span className={`current-connection ${connected ? 'is-live' : ''}`}><span className="dot" />{demo ? 'Demo readings' : connected ? 'Live device reports' : 'Device offline'}</span>
    </header>
    <div className="current-layout">
      <div className="current-reading">
        <span className="current-caption">{connected ? 'Latest reported current' : 'Last reported current'}</span>
        <div className="current-number" data-testid="current-reading"><strong>{current === null ? '—' : format(current)}</strong><span>{unit}</span></div>
        <div className="current-unit-switch" role="group" aria-label="Current display unit">{(['A', 'mA'] as const).map(u => <button key={u} aria-pressed={unit === u} onClick={() => setUnit(u)}>{u === 'A' ? 'Amps' : 'Milliamps'}</button>)}</div>
        <p className="current-freshness"><Clock3 size={13} /> Updated {age(device.last_seen, now)}</p>
        <div className="current-reading-note"><Zap size={16} /><p>{current === null ? 'Waiting for a current measurement from this device.' : !connected ? 'Showing the last report. New readings resume when the device reconnects.' : current === 0 ? 'The device is reporting zero current. Connect a load to see a change if the current sensor is installed.' : 'Watch the reading change as your connected load draws current.'}</p></div>
      </div>
      <div className="current-history">
        <div className="current-chart-toolbar"><span>Current over time <small>· {unit}</small></span><div className="current-periods" role="group" aria-label="Current chart time range">{windows.map(w => <button key={w.ms} aria-pressed={windowMs === w.ms} onClick={() => { setWindowMs(w.ms); setInspected(null); }}>{w.label}</button>)}</div></div>
        <div className="current-chart" ref={chart} onPointerLeave={() => setInspected(null)}>
          {inspected !== null && selected && <div className="current-tooltip" style={{ left: `${Math.max(22, Math.min(78, selectedPercent))}%` }} role="status"><strong>{format(selected.amps)} {unit}</strong><span>{timeLabel(selected.time, true)}</span></div>}
          <svg viewBox={`0 0 ${width} ${height}`} role="img" aria-label={`Current measurements over the last ${windows.find(w => w.ms === windowMs)?.label}, in ${unit}`} onPointerMove={e => inspect(e.clientX, e.currentTarget.getBoundingClientRect())}>
            <defs><linearGradient id={gradient} x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stopColor="#59a47e" stopOpacity=".26" /><stop offset="100%" stopColor="#59a47e" stopOpacity=".015" /></linearGradient></defs>
            {[0, .25, .5, .75, 1].map(fraction => <g key={fraction}><line x1={left} x2={width - right} y1={y(top * fraction)} y2={y(top * fraction)} stroke="#e2eae3" strokeDasharray="3 5" /><text x={left - 10} y={y(top * fraction) + 4} textAnchor="end">{(top * fraction * axisScale).toFixed(axisDecimals)}</text></g>)}
            {[0, .5, 1].map(fraction => <text key={fraction} x={x(start + windowMs * fraction)} y={height - 5} textAnchor={fraction === 0 ? 'start' : fraction === 1 ? 'end' : 'middle'}>{timeLabel(start + windowMs * fraction)}</text>)}
            {segments.map((segment, i) => <g key={i}>{segment.length > 1 && <><path d={`M ${x(segment[0].time)} ${y(0)} ${segment.map(r => `L ${x(r.time)} ${y(r.amps)}`).join(' ')} L ${x(segment.at(-1)!.time)} ${y(0)} Z`} fill={`url(#${gradient})`} /><polyline points={segment.map(r => `${x(r.time)},${y(r.amps)}`).join(' ')} fill="none" stroke="#287958" strokeWidth="2.5" strokeLinejoin="round" strokeLinecap="round" /></>}{segment.length === 1 && <circle cx={x(segment[0].time)} cy={y(segment[0].amps)} r="3.5" fill="#287958" />}</g>)}
            {selected && <g><line x1={x(selected.time)} x2={x(selected.time)} y1={18} y2={y(0)} stroke="#91b89f" strokeDasharray="4 5" /><circle cx={x(selected.time)} cy={y(selected.amps)} r="5" fill="#287958" stroke="white" strokeWidth="2.5" /></g>}
          </svg>
          {!readings.length && <div className="current-chart-empty"><Activity size={23} /><strong>No readings in this time range</strong><span>{connected ? 'The first device report will appear here.' : 'Choose a longer range to view earlier readings.'}</span></div>}
        </div>
        {readings.length > 0 && <div className="current-inspector"><span>Hover, touch or use the slider to inspect</span><input type="range" aria-label="Inspect current measurements" min={0} max={Math.max(0, readings.length - 1)} value={selectedIndex} aria-valuetext={selected ? `${format(selected.amps)} ${unit} at ${timeLabel(selected.time, true)}` : undefined} onChange={e => setInspected(readings[Number(e.target.value)].time)} /></div>}
        <div className="current-statistics">{[['Average', average], ['Peak', peak], ['Lowest', minimum]].map(([label, value]) => <div key={label as string}><span>{label}</span><strong>{value === null ? '—' : format(value as number)} <small>{unit}</small></strong></div>)}<div><span>Recorded readings</span><strong>{readings.length.toLocaleString()}</strong></div></div>
      </div>
    </div>
    <footer className="current-monitor-foot"><span><Radio size={13} />{demo ? 'Sample data' : 'Device reports about every 10 seconds'} · Older history stored per minute</span><span>Approximate sensor readings · Not a billing meter</span></footer>
  </section>;
}
