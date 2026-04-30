import { Component, OnDestroy, NgZone, ChangeDetectorRef } from '@angular/core';
import { CommonModule } from '@angular/common';

@Component({
  selector: 'app-topbar',
  standalone: true,
  templateUrl: './topbar.html',
  styleUrls: ['./topbar.css'],
  imports: [CommonModule],
})
export class Topbar implements OnDestroy {
  now = new Date();
  private timerId: number;

  constructor(private ngZone: NgZone, private cdRef: ChangeDetectorRef) {
    this.timerId = window.setInterval(() => {
      this.ngZone.run(() => {
        this.now = new Date();
        queueMicrotask(() => this.cdRef.detectChanges());
      });
    }, 1000);
  }

  ngOnDestroy(): void {
    clearInterval(this.timerId);
  }
}