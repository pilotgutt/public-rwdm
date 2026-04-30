import { ComponentFixture, TestBed } from '@angular/core/testing';

import { EnvironmentSensors } from './environment-sensors';

describe('EnvironmentSensors', () => {
  let component: EnvironmentSensors;
  let fixture: ComponentFixture<EnvironmentSensors>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [EnvironmentSensors]
    })
    .compileComponents();

    fixture = TestBed.createComponent(EnvironmentSensors);
    component = fixture.componentInstance;
    await fixture.whenStable();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
