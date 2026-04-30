import { Component, Input } from '@angular/core';
import { CommonModule } from '@angular/common';

@Component({
  selector: 'app-stress-indicator',
  standalone: true,
  imports: [CommonModule],
  templateUrl: './stress-indicator.html',
  styleUrls: ['./stress-indicator.css'],
})
export class StressIndicatorComponent {
  @Input() pulseBpm: number | null = null;
  @Input() oxyPercent: number | null = null;
  @Input() bodyTemp: number | null = null;

  private clamp(v: number, min: number, max: number): number {
    return Math.max(min, Math.min(max, v));
  }

  /**
   * Returns 0..1 depending on how far the value is outside the normal range.
   * 0 = inside range
   * 1 = far outside range
   */
  private rangeDeviation(value: number, low: number, high: number, spanScale = 1): number {
    if (value >= low && value <= high) return 0;

    const span = Math.max(0.001, (high - low) * spanScale);

    if (value < low) {
      return this.clamp((low - value) / span, 0, 1);
    }

    return this.clamp((value - high) / span, 0, 1);
  }

  /**
   * Computes stress score from:
   * - pulse: strongest signal
   * - bodyTemp: mild signal
   * - SpO2: weak stress signal, mostly a strain/safety indicator
   */
  get value(): number | null {
    if (
      this.pulseBpm == null &&
      this.oxyPercent == null &&
      this.bodyTemp == null
    ) {
      return null;
    }

    const pulseScore =
      this.pulseBpm != null
        ? this.rangeDeviation(this.pulseBpm, 60, 95, 1.2)
        : 0;

    const tempScore =
      this.bodyTemp != null
        ? this.rangeDeviation(this.bodyTemp, 36.1, 37.4, 1.0)
        : 0;

    let o2Score = 0;
    if (this.oxyPercent != null && this.oxyPercent < 95) {
      o2Score = this.clamp((95 - this.oxyPercent) / 5, 0, 1);
    }

    const total =
      pulseScore * 0.65 +
      tempScore * 0.20 +
      o2Score * 0.15;

    return Math.round(total * 100);
  }

  get clamped(): number {
    if (this.value === null || Number.isNaN(this.value)) return 0;
    return this.clamp(this.value, 0, 100);
  }

  get label(): string {
    if (this.value === null) return 'Waiting…';
    if (this.clamped < 20) return 'Low stress level';
    if (this.clamped < 45) return 'Moderate stress level';
    if (this.clamped < 70) return 'High stress level';
    return 'Very high stress level';
  }

  get fillColor(): string {
    if (this.value === null) return 'rgb(187, 187, 187)';
    if (this.clamped < 20) return 'rgb(200, 200, 200)';
    if (this.clamped < 45) return 'rgb(243, 156, 18)';
    return 'rgb(231, 76, 60)';
  }

  get markerLeftPct(): number {
    return this.clamped;
  }

  limits = [
    { pct: 20, label: '20' },
    { pct: 45, label: '45' },
    { pct: 70, label: '70' },
  ];
}