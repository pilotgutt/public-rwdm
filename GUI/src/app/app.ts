import { Component, signal } from '@angular/core';
import { Sensorvalues } from './shared/components/sensorvalues/sensorvalues';
import { Videofeed } from './shared/components/videofeed/videofeed';
import { Topbar } from './shared/components/topbar/topbar';
import { MantisImuModelComponent} from './shared/components/mantis-imumodel/mantis-imumodel';
import { Overview } from './shared/components/overview/overview';
import { StressIndicatorComponent } from './shared/components/stress-indicator/stress-indicator';
import { SignalIndicatorComponent } from './shared/components/signal-indicator/signal-indicator';
import { EnvironmentSensors} from './shared/components/environment-sensors/environment-sensors';


@Component({
  selector: 'app-root',
  standalone: true, 
  templateUrl: './app.html',
  styleUrls: ['./app.css'],
  imports: [
    Sensorvalues,
    Videofeed,
    Topbar,
    MantisImuModelComponent,
    Overview,
    StressIndicatorComponent,
    SignalIndicatorComponent,
    EnvironmentSensors,
  
  ],
})
export class App {
  protected readonly title = signal('diver-ui');
}
