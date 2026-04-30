import { CommonModule } from '@angular/common';
import {
  Component,
  OnDestroy,
  NgZone,
  ChangeDetectorRef,
} from '@angular/core';

import { StressIndicatorComponent } from '../stress-indicator/stress-indicator';
import { SignalIndicatorComponent } from '../signal-indicator/signal-indicator';
import { WebSocketService } from '../../services/websocket';

import { Subscription } from 'rxjs';

@Component({
  selector: 'app-overview',
  standalone: true,
  imports: [
    CommonModule,
    StressIndicatorComponent,
    SignalIndicatorComponent,
  ],
  templateUrl: './overview.html',
  styleUrls: ['./overview.css'],
})
export class Overview implements OnDestroy {
  // ── Stress + vitals ──
  stress: number | null = null;
  pulseBpm: number | null = null;
  oxyPercent: number | null = null;
  bodyTemp: number | null = null;

  // ── Signal quality ──
  rssi: number | null = null;
  packetLoss = 0;
  corruptFrames = 0;

  private expectedFrameCount: number | null = null;
  private sub = new Subscription();

  constructor(
    private ws: WebSocketService,
    private zone: NgZone,
    private cdr: ChangeDetectorRef
  ) {
    // ── Sensor data (USED BY STRESS INDICATOR) ──
    this.sub.add(
      this.ws.sensorPacket$().subscribe(pkt => {
        this.zone.run(() => {
          this.pulseBpm =
            typeof pkt.heart_rate === 'number' ? pkt.heart_rate : null;

          this.oxyPercent =
            typeof pkt.spo2 === 'number' ? pkt.spo2 : null;

          this.bodyTemp =
            typeof pkt.tmp_temperature === 'number'
              ? pkt.tmp_temperature
              : null;

          // ✅ Keep stress calculation/input
          this.stress =
            typeof pkt.stresslevel === 'number' ? pkt.stresslevel : null;

          queueMicrotask(() => this.cdr.detectChanges());
        });
      })
    );

    // ── Signal quality (packet loss + corruption) ──
    this.sub.add(
      this.ws.signalPacket$().subscribe(pkt => {
        this.zone.run(() => {
          console.log('[Overview] signal packet:', pkt);
          // Frame tracking (local fallback calculation)
          if (typeof pkt.frame_count === 'number') {
            this.processFrame(pkt);
          }

          // Server-provided counters (preferred if present)
          if (typeof pkt.packetLoss === 'number') {
            this.packetLoss = pkt.packetLoss;
          }

          if (typeof pkt.corruptFrames === 'number') {
            this.corruptFrames = pkt.corruptFrames;
          }
      
          console.log('[Overview] packetLoss=', this.packetLoss, 'corruptFrames=', this.corruptFrames);


          queueMicrotask(() => this.cdr.detectChanges());
        });
      })
    );
  }

  private processFrame(pkt: any): void {
    const fc: number | undefined = pkt.frame_count;

    if (typeof fc === 'number' && Number.isFinite(fc)) {
      if (this.expectedFrameCount !== null && fc > this.expectedFrameCount) {
        this.packetLoss += fc - this.expectedFrameCount;
      }
      this.expectedFrameCount = fc + 1;
    }

    if (pkt.crc_ok === false || pkt.corrupt === true) {
      this.corruptFrames++;
    }
  }

  /** Reset counters (client-side only) */
  resetCounters(): void {
    this.packetLoss = 0;
    this.corruptFrames = 0;
    this.expectedFrameCount = null;
    this.cdr.detectChanges();
  }

  ngOnDestroy(): void {
    this.sub.unsubscribe();
  }
}