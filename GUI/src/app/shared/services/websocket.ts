import { Injectable } from '@angular/core';
import { Observable } from 'rxjs';
import { io, Socket } from 'socket.io-client';

@Injectable({ providedIn: 'root' })
export class WebSocketService {
  private socket: Socket;

  constructor() {
    this.socket = io('http://localhost:3001');
    this.socket.on('connect', () => console.log('[WS] connected', this.socket.id));
    this.socket.on('connect_error', (err) => console.error('[WS] connect_error', err));
    this.socket.on('disconnect', (reason) => console.warn('[WS] disconnected', reason));

    // DEBUG: prove events arrive
    this.socket.on('temp', (v) => console.log('[WS] temp event', v));
    this.socket.on('pressure', (v) => console.log('[WS] pressure event', v));
    this.socket.on('ekg', (v) => console.log('[WS] ekg event', v));
    this.socket.on('bodyTemp', (v) => console.log('[WS] bodyTemp event', v));
    this.socket.on('signal-packet', (v) => console.log('[WS] signal-packet event', v));
    this.socket.on('sensor-packet', (v) => console.log('[WS] sensor-packet event', v));
  }


  sensorPacket$(): Observable<any> {
    return new Observable(observer => {
      const h = (pkt: any) => observer.next(pkt);
      console.log('[WS] subscribing to sensor-packet');
      this.socket.on('sensor-packet', h);
      return () => this.socket.off('sensor-packet', h);
    });
  }

  signalPacket$(): Observable<any> {
    return new Observable(observer => {
      const h = (pkt: any) => observer.next(pkt);
      console.log('[WS] subscribing to signal-packet');
      this.socket.on('signal-packet', h);
      return () => this.socket.off('signal-packet', h);
    });
  } 

}

