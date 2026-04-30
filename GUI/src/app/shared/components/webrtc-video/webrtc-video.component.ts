import {
  AfterViewInit,
  ChangeDetectorRef,
  Component,
  ElementRef,
  OnDestroy,
  ViewChild,
} from '@angular/core';
import { CommonModule } from '@angular/common';
import { Subscription } from 'rxjs';
import { WebrtcVideoService } from '../../services/webrtc-video.service';

@Component({
  selector: 'app-webrtc-video',
  standalone: true,
  imports: [CommonModule],
  templateUrl: './webrtc-video.component.html',
  styleUrls: ['./webrtc-video.component.css'],
})
export class WebrtcVideoComponent implements AfterViewInit, OnDestroy {
  @ViewChild('videoElement', { static: true })
  videoRef!: ElementRef<HTMLVideoElement>;

  private streamSub?: Subscription;

  constructor(private webrtcService: WebrtcVideoService, private cdr: ChangeDetectorRef) {queueMicrotask(() => this.cdr.detectChanges());}

  ngAfterViewInit(): void {
    // Start signaling
    this.webrtcService.connect();

    // Subscribe to stream changes
    this.streamSub = this.webrtcService.remoteStream$.subscribe((stream: MediaStream | null) => {
      const video = this.videoRef.nativeElement;
      console.log('[webrtc-video] stream subscription ->', stream);
      if (stream) {
        if (video.srcObject !== stream) {
          console.log('[webrtc-video] attaching stream, tracks=', stream.getVideoTracks(), stream.getAudioTracks());
          video.srcObject = stream;
          // Ensure playback starts (autoplay may require a play() promise)
          video.muted = true;
          video.autoplay = true;
          video.playsInline = true;
          video.play().catch((err) => console.warn('[webrtc-video] play() failed', err));

          // Ensure Angular runs change detection after we attach the stream
          queueMicrotask(() => this.cdr.detectChanges());

          // Log track settings and video element dimensions after a short delay
          const track = stream.getVideoTracks()[0];
          if (track) {
            try {
              console.log('[webrtc-video] video track settings:', track.getSettings());
            } catch (e) {
              console.log('[webrtc-video] could not read track settings', e);
            }
          }

          setTimeout(() => {
            console.log('[webrtc-video] video element size', video.videoWidth, video.videoHeight, 'readyState', video.readyState);
          }, 500);
        }
      } else {
        video.srcObject = null;
      }
    });
  }

  ngOnDestroy(): void {
    this.streamSub?.unsubscribe();
    this.webrtcService.disconnect();
  }
}