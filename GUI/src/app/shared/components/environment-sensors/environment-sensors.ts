import { 
  Component, 
  OnDestroy, 
  NgZone, 
  ChangeDetectorRef,
 } from '@angular/core';
import { CommonModule } from '@angular/common';
import { WebSocketService } from '../../services/websocket';
import { Subscription } from 'rxjs';
import { MantisImuModelComponent } from '../mantis-imumodel/mantis-imumodel';

@Component({
  selector: 'app-environment-sensors',
  standalone: true,
  imports: [CommonModule, MantisImuModelComponent],
  templateUrl: './environment-sensors.html',
  styleUrl: './environment-sensors.css',
})
export class EnvironmentSensors {

  waterTemp: number | null = null;
  pressuremBar: number | null = null;
  depth_m: number | null = null;

  private sub = new Subscription();

  private pressureToDepth(pressure_mbar: number): number {
  const surfacePressure = 1030.25; // mbar
  const depth = (pressure_mbar - surfacePressure) / 100;
  return Math.max(0, depth); // avoid negative depth above water
  }

    constructor(private ws: WebSocketService,
     private zone: NgZone,
     private cdr: ChangeDetectorRef) {

    
    this.sub.add(this.ws.sensorPacket$().subscribe(pkt => {
      this.zone.run(() => {

      this.waterTemp = typeof pkt.ms_temperature === 'number'
        ? Math.round(pkt.ms_temperature * 10) / 10
        : null;

      this.pressuremBar = typeof pkt.ms_pressure === 'number'
        ? Math.round(pkt.ms_pressure * 10) / 10
        : null;

      if (typeof pkt.ms_pressure === 'number') {
        const depth = this.pressureToDepth(pkt.ms_pressure);
        this.depth_m = Math.round(depth * 10) / 10;
      } else {
        this.depth_m = null;
      }
            


        queueMicrotask(() => this.cdr.detectChanges());
      });
    }));

  }
}



