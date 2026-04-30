import { ComponentFixture, TestBed } from '@angular/core/testing';

import { TimeSeriesPlot } from './time-series-plot';

describe('TimeSeriesPlot', () => {
  let component: TimeSeriesPlot;
  let fixture: ComponentFixture<TimeSeriesPlot>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [TimeSeriesPlot]
    })
    .compileComponents();

    fixture = TestBed.createComponent(TimeSeriesPlot);
    component = fixture.componentInstance;
    await fixture.whenStable();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
