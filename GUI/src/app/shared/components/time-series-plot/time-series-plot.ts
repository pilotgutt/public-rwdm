import {
  AfterViewInit,
  Component,
  ElementRef,
  Input,
  OnChanges,
  OnDestroy,
  SimpleChanges,
  ViewChild,
} from '@angular/core';
import { CommonModule } from '@angular/common';

export type TimePoint = { t: number; v: number };

@Component({
  selector: 'app-time-series-plot',
  standalone: true,
  imports: [CommonModule],
  templateUrl: './time-series-plot.html',
  styleUrls: ['./time-series-plot.css'],
})
export class TimeSeriesPlotComponent implements AfterViewInit, OnDestroy, OnChanges {
  @ViewChild('c', { static: true }) canvasRef!: ElementRef<HTMLCanvasElement>;

  @Input() series: TimePoint[] = [];
  @Input() title = 'Plot';
  @Input() unit = '';
  @Input() secondsOnScreen = 60;
  @Input() yTicks = 5;
  @Input() xTicks = 4;

  // If you want timestamps (HH:MM:SS) instead of "seconds ago"
  @Input() xAxisMode: 'secondsAgo' | 'timestamp' = 'secondsAgo';

  private ctx!: CanvasRenderingContext2D;
  private rafId: number | null = null;
  private ro!: ResizeObserver;

  ngAfterViewInit(): void {
    const ctx = this.canvasRef.nativeElement.getContext('2d');
    if (!ctx) throw new Error('Canvas 2D context not available');
    this.ctx = ctx;

    this.ro = new ResizeObserver(() => this.resizeCanvas());
    this.ro.observe(this.canvasRef.nativeElement);

    this.resizeCanvas();
    this.startLoop();
  }

  ngOnChanges(_: SimpleChanges): void {
    // no-op; drawing loop reads latest this.series
  }

  ngOnDestroy(): void {
    if (this.rafId !== null) cancelAnimationFrame(this.rafId);
    this.rafId = null;
    if (this.ro) this.ro.disconnect();
  }

  private startLoop() {
    const loop = () => {
      this.draw();
      this.rafId = requestAnimationFrame(loop);
    };
    this.rafId = requestAnimationFrame(loop);
  }

  private resizeCanvas() {
    const canvas = this.canvasRef.nativeElement;
    const rect = canvas.getBoundingClientRect();
    const w = Math.max(1, Math.floor(rect.width));
    const h = Math.max(1, Math.floor(rect.height));
    const dpr = window.devicePixelRatio || 1;

    canvas.width = Math.floor(w * dpr);
    canvas.height = Math.floor(h * dpr);
    this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  }

  private draw() {
    const canvas = this.canvasRef.nativeElement;
    const rect = canvas.getBoundingClientRect();
    const w = Math.max(1, Math.floor(rect.width));
    const h = Math.max(1, Math.floor(rect.height));
    const ctx = this.ctx;

    ctx.clearRect(0, 0, w, h);

    // Title
    ctx.fillStyle = 'rgba(0,0,0,0.8)';
    ctx.font = '14px sans-serif';
    ctx.fillText(this.title, 12, 18);

    // Plot area
    const left = 50, right = 12, top = 28, bottom = 30;
    const pw = w - left - right;
    const ph = h - top - bottom;

    // Axes frame
    ctx.strokeStyle = 'rgba(0,0,0,0.25)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(left, top);
    ctx.lineTo(left, top + ph);
    ctx.lineTo(left + pw, top + ph);
    ctx.stroke();

    // Filter to last N seconds
    const now = Date.now();
    const cutoff = now - this.secondsOnScreen * 1000;
    const data = this.series.filter(p => p.t >= cutoff);

    if (data.length < 2) {
      ctx.fillStyle = 'rgba(0,0,0,0.7)';
      ctx.font = '13px sans-serif';
      ctx.fillText('Not enough data yet', left + 10, top + 30);
      return;
    }

    const tMin = data[0].t;
    const tMax = data[data.length - 1].t;

    let vMin = Infinity, vMax = -Infinity;
    for (const p of data) { vMin = Math.min(vMin, p.v); vMax = Math.max(vMax, p.v); }
    vMin = Math.floor(vMin - 2);
    vMax = Math.ceil(vMax + 2);
    if (vMax === vMin) { vMax = vMin + 1; }

    const xOf = (t: number) => left + ((t - tMin) / (tMax - tMin)) * pw;
    const yOf = (v: number) => top + (1 - (v - vMin) / (vMax - vMin)) * ph;

    // Y ticks + grid
    ctx.font = '12px sans-serif';
    ctx.fillStyle = 'rgba(0,0,0,0.75)';
    for (let i = 0; i <= this.yTicks; i++) {
      const frac = i / this.yTicks;
      const v = vMax - frac * (vMax - vMin);
      const y = top + frac * ph;

      ctx.strokeStyle = 'rgba(0,0,0,0.10)';
      ctx.beginPath();
      ctx.moveTo(left, y);
      ctx.lineTo(left + pw, y);
      ctx.stroke();

      ctx.fillText(v.toFixed(0), 8, y + 4);
    }

    // X ticks + grid
    for (let i = 0; i <= this.xTicks; i++) {
      const frac = i / this.xTicks;
      const t = tMin + frac * (tMax - tMin);
      const x = left + frac * pw;

      ctx.strokeStyle = 'rgba(0,0,0,0.10)';
      ctx.beginPath();
      ctx.moveTo(x, top);
      ctx.lineTo(x, top + ph);
      ctx.stroke();

      let label: string;
      if (this.xAxisMode === 'timestamp') {
        const d = new Date(t);
        label = d.toLocaleTimeString([], { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' });
      } else {
        const secondsAgo = (tMax - t) / 1000;
        label = `${secondsAgo.toFixed(0)}s`;
      }

      ctx.fillText(label, x - 18, top + ph + 20);
    }

    // Unit label
    if (this.unit) ctx.fillText(this.unit, 8, top + 14);

    // Plot line
    ctx.strokeStyle = '#0acbe9';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(xOf(data[0].t), yOf(data[0].v));
    for (let i = 1; i < data.length; i++) {
      ctx.lineTo(xOf(data[i].t), yOf(data[i].v));
    }
    ctx.stroke();
  }
}
