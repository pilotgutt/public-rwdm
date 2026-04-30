import { ComponentFixture, TestBed } from '@angular/core/testing';

import { Sensorvalues } from './sensorvalues';

describe('Sensorvalues', () => {
  let component: Sensorvalues;
  let fixture: ComponentFixture<Sensorvalues>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [Sensorvalues]
    })
    .compileComponents();

    fixture = TestBed.createComponent(Sensorvalues);
    component = fixture.componentInstance;
    await fixture.whenStable();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
