import { 
  Component, 
  OnDestroy, 
  NgZone, 
  ChangeDetectorRef,
 } from '@angular/core';
import { CommonModule } from '@angular/common';
import { WebSocketService } from '../../services/websocket';
import { Subscription } from 'rxjs';
import { TimeSeriesPlotComponent } from '../time-series-plot/time-series-plot';
import { FormsModule } from '@angular/forms';
import { StressIndicatorComponent } from '../stress-indicator/stress-indicator';

type SensorKey = 'pulse' | 'o2' | 'bodyTemp';

type Limits = {
low: number;
high: number;
};

type SensorConfig = {
key: SensorKey;
name: string;
unit: string;
limits: Limits;
};

const STORAGE_KEY = 'biometric_limits_v1';



@Component({
  selector: 'app-sensorvalues',
  standalone: true,
  imports: [CommonModule, TimeSeriesPlotComponent, FormsModule, StressIndicatorComponent],
  templateUrl: './sensorvalues.html',
  styleUrls: ['./sensorvalues.css'],
})


export class Sensorvalues implements OnDestroy {
  pulseBpm: number | null = null;
  oxyPercent: number | null = null;
  bodyTemp: number | null = null;

  private dragging = false;
  private dragOffsetX = 0;
  private dragOffsetY = 0;

  

  pulseSeries: { t: number; v: number }[] = [];
  o2Series: { t: number; v: number }[] = [];
  bodyTempSeries: { t: number; v: number }[] = [];
  maxSeconds = 60; // keep last 60 seconds

  activeTitle = '';
  activeUnit = '';
  activeSeries: { t: number; v: number }[] = [];

  startDrag(ev: MouseEvent, dialog: HTMLDialogElement) {
  this.dragging = true;

  const rect = dialog.getBoundingClientRect();
  this.dragOffsetX = ev.clientX - rect.left;
  this.dragOffsetY = ev.clientY - rect.top;

  // remove centering transform once user drags
  dialog.style.transform = 'none';

  const onMove = (e: MouseEvent) => {
    if (!this.dragging) return;
    dialog.style.left = `${e.clientX - this.dragOffsetX}px`;
    dialog.style.top  = `${e.clientY - this.dragOffsetY}px`;
  };

  const onUp = () => {
    this.dragging = false;
    window.removeEventListener('mousemove', onMove);
    window.removeEventListener('mouseup', onUp);
  };

  window.addEventListener('mousemove', onMove);
  window.addEventListener('mouseup', onUp);
}


  private addSample(series: { t: number; v: number }[], v: number) {
    const now = Date.now();
    series.push({ t: now, v });

    const cutoff = now - this.maxSeconds * 1000;
    while (series.length && series[0].t < cutoff) series.shift();
  }

  // --- NEW: config for each sensor (defaults) ---
  sensors: SensorConfig[] = [
    { key: 'bodyTemp', name: 'Skin temperature', unit: '°C',  limits: { low: 36.0, high: 38.0 } },
    { key: 'pulse',    name: 'Pulse',    unit: 'bpm', limits: { low: 50,  high: 110 } },
    { key: 'o2',       name: 'SpO₂',      unit: '%',   limits: { low: 92,  high: 100 } },
  ];

  // --- NEW: dialog editing state ---
  editingSensor: SensorConfig | null = null;
  editLow = 0;
  editHigh = 0;

  private sub = new Subscription();

  private round1(value: number): number {
  return Math.round(value * 10) / 10;
  }

  constructor(private ws: WebSocketService,
     private zone: NgZone,
     private cdr: ChangeDetectorRef) {

    
    this.sub.add(this.ws.sensorPacket$().subscribe(pkt => {
      this.zone.run(() => {
      if (typeof pkt.heart_rate === 'number') {
        this.pulseBpm = this.round1(pkt.heart_rate);
        this.addSample(this.pulseSeries, pkt.heart_rate);
      } else this.pulseBpm = null;

      if (typeof pkt.spo2 === 'number') {
        this.oxyPercent = this.round1(pkt.spo2);
        this.addSample(this.o2Series, pkt.spo2);
      } else this.oxyPercent = null;

      if (typeof pkt.tmp_temperature === 'number') {
        this.bodyTemp = this.round1(pkt.tmp_temperature);
        this.addSample(this.bodyTempSeries, pkt.tmp_temperature);
      } else this.bodyTemp = null;
      


        queueMicrotask(() => this.cdr.detectChanges());
      });
    }));
  
    this.loadLimits();
    queueMicrotask(() => this.cdr.detectChanges());
  }

  // ---------- read current sensor value by key ----------
  getSensorValue(key: SensorKey): number | null {
    switch (key) {
      case 'pulse': return this.pulseBpm;
      case 'o2': return this.oxyPercent;
      case 'bodyTemp': return this.bodyTemp;
    }
  }

  // ---------- scaling for indicator bar ----------
  /** Adds padding so the marker doesn't sit at the edges all the time */
  private getDisplayRange(limits: Limits): { min: number; max: number } {
    const span = Math.max(1e-6, limits.high - limits.low);
    const pad = span * 0.25; // 25% padding on each side
    return { min: limits.low - pad, max: limits.high + pad };
  }

  pctForValue(v: number, limits: Limits): number {
    const r = this.getDisplayRange(limits);
    const pct = ((v - r.min) / (r.max - r.min)) * 100;
    return Math.max(0, Math.min(100, pct));
  }

  pctForLimit(which: 'low' | 'high', limits: Limits): number {
    return this.pctForValue(which === 'low' ? limits.low : limits.high, limits);
  }

  statusFor(v: number, limits: Limits): 'low' | 'ok' | 'high' {
    if (v < limits.low) return 'low';
    if (v > limits.high) return 'high';
    return 'ok';
  }

  // ---------- limits dialog ----------
  openLimits(dialog: HTMLDialogElement, s: SensorConfig, ev?: MouseEvent) {
    ev?.stopPropagation(); // don’t open the plot when clicking the gear
    this.editingSensor = s;
    this.editLow = s.limits.low;
    this.editHigh = s.limits.high;
    dialog.showModal();
  }

  saveLimits(dialog: HTMLDialogElement) {
    if (!this.editingSensor) return;

    // basic validation: low < high
    const low = Number(this.editLow);
    const high = Number(this.editHigh);
    if (!Number.isFinite(low) || !Number.isFinite(high) || low >= high) return;

    this.editingSensor.limits = { low, high };
    this.persistLimits();
    dialog.close();
  }

  closeLimits(dialog: HTMLDialogElement, ev?: Event) {
    ev?.stopPropagation();
    dialog.close();
  }

  private persistLimits() {
    const map: Record<string, Limits> = {};
    for (const s of this.sensors) map[s.key] = s.limits;
    localStorage.setItem(STORAGE_KEY, JSON.stringify(map));
  }

  private loadLimits() {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return;
    try {
      const map = JSON.parse(raw) as Record<string, Limits>;
      for (const s of this.sensors) {
        const saved = map[s.key];
        if (saved && typeof saved.low === 'number' && typeof saved.high === 'number' && saved.low < saved.high) {
          s.limits = saved;
        }
      }
    } catch { /* ignore */ }
  }

  openPlot(dialog: HTMLDialogElement, title: string, unit: string, series: { t: number; v: number }[]) {
  this.activeTitle = title;
  this.activeUnit = unit;
  this.activeSeries = series;
  dialog.showModal();
  }

  closePlot(dialog: HTMLDialogElement, ev?: Event) {
    ev?.stopPropagation();
    dialog.close();
  }


  ngOnDestroy(): void {
    this.sub.unsubscribe();
  }
}
