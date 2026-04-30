import { ComponentFixture, TestBed } from '@angular/core/testing';

import { StressIndicator } from './stress-indicator';

describe('StressIndicator', () => {
  let component: StressIndicator;
  let fixture: ComponentFixture<StressIndicator>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [StressIndicator]
    })
    .compileComponents();

    fixture = TestBed.createComponent(StressIndicator);
    component = fixture.componentInstance;
    await fixture.whenStable();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
