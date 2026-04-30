import { Injectable, NgZone } from '@angular/core';
import { BehaviorSubject } from 'rxjs';

interface SignalMessage {
  type: 'offer' | 'answer' | 'ice' | 'start';
  sdp?: string;
  candidate?: string;
  sdpMLineIndex?: number;
}

@Injectable({
  providedIn: 'root',
})
export class WebrtcVideoService {
  private ws: WebSocket | null = null;
  private pc: RTCPeerConnection | null = null;
  private statsInterval: number | null = null;

  // Expose the remote MediaStream to components
  private remoteStreamSubject = new BehaviorSubject<MediaStream | null>(null);
  remoteStream$ = this.remoteStreamSubject.asObservable();

  // Adjust STUN to match backend if needed; can also leave default
  private readonly rtcConfig: RTCConfiguration = {
    iceServers: [
      { urls: 'stun:stun.l.google.com:19302' },
    ],
  };

  constructor(private zone: NgZone) {}

  connect(): void {
    if (this.ws) {
      return; // already connected or connecting
    }

    this.ws = new WebSocket('ws://localhost:8765');

    this.ws.onopen = () => {
      console.log('[WebRTC] WebSocket connected');
      // Optional: tell backend we’re ready, though it auto-negotiates already
      this.sendSignal({ type: 'start' });
    };

    this.ws.onmessage = async (event) => {
      const data: SignalMessage = JSON.parse(event.data);
      console.log('[WebRTC] Received signaling message:', data);

      switch (data.type) {
        case 'offer':
          await this.handleOffer(data.sdp!);
          break;
        case 'answer':
          // In this flow backend is the offerer, so we don't expect 'answer'
          console.warn('[WebRTC] Unexpected answer from backend');
          break;
        case 'ice':
          await this.handleRemoteIce(data);
          break;
        default:
          console.warn('[WebRTC] Unknown message type:', data.type);
      }
    };

    this.ws.onclose = () => {
      console.log('[WebRTC] WebSocket closed');
      this.ws = null;
      this.teardownPeerConnection();
      // Optionally, emit null stream
      this.zone.run(() => this.remoteStreamSubject.next(null));
    };

    this.ws.onerror = (err) => {
      console.error('[WebRTC] WebSocket error', err);
    };
  }

  disconnect(): void {
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
    this.teardownPeerConnection();
    this.zone.run(() => this.remoteStreamSubject.next(null));
  }

  // ---------- Internal helpers ----------

  private sendSignal(msg: SignalMessage): void {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      console.warn('[WebRTC] Tried to send signaling message but WebSocket not open', msg);
      return;
    }
    this.ws.send(JSON.stringify(msg));
  }

  private ensurePeerConnection(): RTCPeerConnection {
    if (this.pc) {
      return this.pc;
    }

    this.pc = new RTCPeerConnection(this.rtcConfig);

    // ICE candidates from browser -> backend
    this.pc.onicecandidate = (event) => {
      if (event.candidate) {
        const cand = event.candidate;
        console.log('[WebRTC] Local ICE candidate:', cand);
        this.sendSignal({
          type: 'ice',
          candidate: cand.candidate,
          sdpMLineIndex: cand.sdpMLineIndex ?? 0,
        });
      } else {
        console.log('[WebRTC] ICE gathering complete');
      }
    };

    // Remote track(s) from backend -> MediaStream
    this.pc.ontrack = (event) => {
      console.log('[WebRTC] Remote track received', event.streams);
      const [stream] = event.streams;
      if (stream) {
        // Ensure change detection runs in Angular zone
        this.zone.run(() => {
          this.remoteStreamSubject.next(stream);
        });
      }
    };

    this.pc.onconnectionstatechange = () => {
      console.log('[WebRTC] Connection state:', this.pc?.connectionState);
    };

    // Start periodic stats polling to help diagnose freezes
    try {
      this.statsInterval = window.setInterval(() => this.logStats(), 5000);
    } catch (e) {
      // window may be undefined in some test environments
    }

    return this.pc;
  }

  private teardownPeerConnection(): void {
    if (this.pc) {
      this.pc.onicecandidate = null;
      this.pc.ontrack = null;
      this.pc.onconnectionstatechange = null;
      this.pc.close();
      if (this.statsInterval) {
        clearInterval(this.statsInterval);
        this.statsInterval = null;
      }
      this.pc = null;
    }
  }

  private async handleOffer(sdp: string): Promise<void> {
    const pc = this.ensurePeerConnection();

    const offerDesc = new RTCSessionDescription({
      type: 'offer',
      sdp,
    });

    console.log('[WebRTC] Setting remote offer');
    await pc.setRemoteDescription(offerDesc);

    console.log('[WebRTC] Creating answer');
    const answer = await pc.createAnswer();
    await pc.setLocalDescription(answer);

    console.log('[WebRTC] Sending answer');
    this.sendSignal({
      type: 'answer',
      sdp: answer.sdp ?? '',
    });
  }

  private async handleRemoteIce(msg: SignalMessage): Promise<void> {
    if (!msg.candidate) {
      return;
    }
    const pc = this.ensurePeerConnection();

    const ice = new RTCIceCandidate({
      candidate: msg.candidate,
      sdpMLineIndex: msg.sdpMLineIndex ?? 0,
    });

    try {
      console.log('[WebRTC] Adding remote ICE candidate:', ice);
      await pc.addIceCandidate(ice);
    } catch (e) {
      console.error('[WebRTC] Error adding remote ICE candidate', e);
    }
  }

  private async logStats(): Promise<void> {
    const pc = this.pc;
    if (!pc) return;

    try {
      const stats = await pc.getStats();
      stats.forEach(report => {
        // Look for inbound-rtp reports which hold frames/packets info
        if ((report as any).type === 'inbound-rtp' || (report as any).type === 'inbound-rtp' ) {
          const r: any = report as any;
          console.log('[WebRTC stats] inbound-rtp', {
            id: r.id,
            kind: r.kind,
            packetsLost: r.packetsLost,
            packetsReceived: r.packetsReceived,
            framesDecoded: r.framesDecoded,
            framesDropped: r.framesDropped,
            frameWidth: r.frameWidth,
            frameHeight: r.frameHeight,
            bytesReceived: r.bytesReceived,
            jitter: r.jitter,
          });
        }
      });
    } catch (e) {
      console.warn('[WebRTC] getStats() failed', e);
    }
  }
}