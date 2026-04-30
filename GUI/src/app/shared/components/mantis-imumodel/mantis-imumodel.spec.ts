import { ComponentFixture, TestBed } from '@angular/core/testing';

import { MantisImuModelComponent } from './mantis-imumodel';

describe('MantisImuModelComponent', () => {
  let component: MantisImuModelComponent;
  let fixture: ComponentFixture<MantisImuModelComponent>;     
  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [MantisImuModelComponent]
    })
    .compileComponents();

    fixture = TestBed.createComponent(MantisImuModelComponent);
    component = fixture.componentInstance;
    await fixture.whenStable();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
