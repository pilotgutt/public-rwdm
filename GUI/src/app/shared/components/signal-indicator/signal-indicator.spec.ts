import { ComponentFixture, TestBed } from '@angular/core/testing';

import { SignalIndicator } from './signal-indicator';

describe('SignalIndicator', () => {
  let component: SignalIndicator;
  let fixture: ComponentFixture<SignalIndicator>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [SignalIndicator]
    })
    .compileComponents();

    fixture = TestBed.createComponent(SignalIndicator);
    component = fixture.componentInstance;
    await fixture.whenStable();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
