import { ChangeDetectorRef, Component, NgZone, OnDestroy } from '@angular/core';
import { CommonModule } from '@angular/common';
import { WebSocketService } from '../../services/websocket';
import { WebrtcVideoComponent } from '../webrtc-video/webrtc-video.component';
import { Subscription } from 'rxjs';
import { MantisImuModelComponent } from '../mantis-imumodel/mantis-imumodel';

@Component({
  selector: 'app-videofeed',
  standalone: true,
  imports: [CommonModule, MantisImuModelComponent, WebrtcVideoComponent],
  templateUrl: './videofeed.html',
  styleUrls: ['./videofeed.css'],
})
export class Videofeed implements OnDestroy{
  waterTemp: number | null = null;
  pressuremBar: number | null = null;
  depth_m: number | null = null;

  private pressureToDepth(pressure_mbar: number): number {
  const surfacePressure = 1013.25; // mbar
  const depth = (pressure_mbar - surfacePressure) / 100;
  return Math.max(0, depth); // avoid negative depth above water
  }

  private sub = new Subscription();

  constructor(
    private ws: WebSocketService,
    private zone: NgZone,
    private cdr: ChangeDetectorRef) {

  this.sub.add(this.ws.sensorPacket$().subscribe(pkt => {
    this.zone.run(() => {

      this.waterTemp = typeof pkt.ms_temperature === 'number'
        ? pkt.ms_temperature
        : null;

      this.pressuremBar = typeof pkt.ms_pressure === 'number'
        ? pkt.ms_pressure
        : null;

      if (typeof pkt.ms_pressure === 'number') {
        this.depth_m = this.pressureToDepth(pkt.ms_pressure);
      } else {
        this.depth_m = null;
      }

      queueMicrotask(() => this.cdr.detectChanges());
    });
  }));
  
  }

  ngOnDestroy(): void {
    this.sub.unsubscribe();
  }
}
