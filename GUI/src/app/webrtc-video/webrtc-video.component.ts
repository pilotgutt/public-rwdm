import {
  AfterViewInit,
  Component,
  ElementRef,
  OnDestroy,
  ViewChild,
} from '@angular/core';
import { CommonModule } from '@angular/common';
import { Subscription } from 'rxjs';
import { WebrtcVideoService } from '../shared/services/webrtc-video.service';

@Component({
  selector: 'app-webrtc-video',
  standalone: true,
  imports: [CommonModule],
  template: `
    <div class="webrtc-video-container">
      <video #videoElement class="webrtc-video" playsinline autoplay controls muted></video>
    </div>
  `,
  styles: [
    `.webrtc-video-container { width:100%; height:100%; position:relative; display:flex; justify-content:center; align-items:center; }`,
    `.webrtc-video { width:100%; height:100%; max-width:100%; display:block; object-fit:contain; background:black; }`,
  ],
})
export class WebrtcVideoComponent implements AfterViewInit, OnDestroy {
  @ViewChild('videoElement', { static: true })
  videoRef!: ElementRef<HTMLVideoElement>;

  private streamSub?: Subscription;

  constructor(private webrtcService: WebrtcVideoService) {}

  ngAfterViewInit(): void {
    // Start signaling
    this.webrtcService.connect();

    // Subscribe to stream changes
    this.streamSub = this.webrtcService.remoteStream$.subscribe((stream: MediaStream | null) => {
      const video = this.videoRef.nativeElement;
      if (stream) {
        if (video.srcObject !== stream) {
          video.srcObject = stream;
          // Ensure playback starts (autoplay may require a play() promise)
          video.play().catch(() => {});
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