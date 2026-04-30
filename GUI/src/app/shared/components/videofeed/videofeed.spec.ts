import { ComponentFixture, TestBed } from '@angular/core/testing';

import { Videofeed } from './videofeed';

describe('Videofeed', () => {
  let component: Videofeed;
  let fixture: ComponentFixture<Videofeed>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [Videofeed]
    })
    .compileComponents();

    fixture = TestBed.createComponent(Videofeed);
    component = fixture.componentInstance;
    await fixture.whenStable();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
