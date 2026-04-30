import {
  Component,
  OnDestroy,
  NgZone,
  ChangeDetectorRef,
} from '@angular/core';
import { CommonModule } from '@angular/common';
import { Subscription } from 'rxjs';
import { WebSocketService } from '../../services/websocket';

type SignalLevel = 'Excellent' | 'Good' | 'Fair' | 'Weak';

@Component({
  selector: 'app-signal-indicator',
  standalone: true,
  imports: [CommonModule],
  templateUrl: './signal-indicator.html',
  styleUrls: ['./signal-indicator.css'],
})
export class SignalIndicatorComponent implements OnDestroy {
  signalStrength: number | null = null;

  private sub = new Subscription();

  constructor(
    private ws: WebSocketService,
    private zone: NgZone,
    private cdr: ChangeDetectorRef
  ) {
    this.sub.add(
      this.ws.signalPacket$().subscribe((pkt) => {
        this.zone.run(() => {
          if (pkt && typeof pkt.signal_strength === 'number' && Number.isFinite(pkt.signal_strength)) {
            this.signalStrength = pkt.signal_strength;
          } else {
            this.signalStrength = null;
          }

          console.log('Signal strength:', this.signalStrength);

          queueMicrotask(() => this.cdr.detectChanges());
        });
      })
    );
  }

  get level(): SignalLevel {
    const v = this.signalStrength;
    if (v === null || Number.isNaN(v)) return 'Weak';

    // Tune these thresholds to your real acoustic-link values
    if (v >= 0.85) return 'Excellent';
    if (v >= 0.78) return 'Good';
    if (v >= 0.74) return 'Fair';
    return 'Weak';
  }

  get bars(): number {
    switch (this.level) {
      case 'Excellent': return 3;
      case 'Good': return 2;
      case 'Fair': return 1;
      case 'Weak': return 0;
    }
  }

  get color(): string {
    return '#000000';
  }

  get rangeText(): string {
    const v = this.signalStrength;
    if (v === null || Number.isNaN(v)) return '—';
    return v.toFixed(3);
  }

  get guidelineText(): string {
    switch (this.level) {
      case 'Excellent': return '>= 3.0';
      case 'Good': return '2.0 - 2.9';
      case 'Fair': return '1.0 - 1.9';
      case 'Weak': return 'No signal';
    }
  }

  ngOnDestroy(): void {
    this.sub.unsubscribe();
  }
}