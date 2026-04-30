import {
  AfterViewInit,
  Component,
  ElementRef,
  ViewChild,
  OnDestroy,
  NgZone,
  ChangeDetectorRef
} from '@angular/core';
import * as THREE from 'three';
import { GLTFLoader } from 'three/examples/jsm/Addons.js';
import { WebSocketService } from '../../services/websocket';
import { Subscription } from 'rxjs';

@Component({
  selector: 'app-mantis-imumodel',
  templateUrl: './mantis-imumodel.html',
  styleUrls: ['./mantis-imumodel.css']
})
export class MantisImuModelComponent
  implements AfterViewInit, OnDestroy {

  roll: number = 0;
  pitch: number = 0;
  yaw: number = 0;

  @ViewChild('host', { static: true })
  host!: ElementRef<HTMLDivElement>;

  private scene!: THREE.Scene;
  private camera!: THREE.PerspectiveCamera;
  private renderer!: THREE.WebGLRenderer;
  private frameId = 0;

  private imuGroup!: THREE.Group;
  private model!: THREE.Object3D;

  private loadModel(): void {
  const loader = new GLTFLoader();

  // Load the GLB model
  loader.load(
    'assets/models/diver.glb',
    (gltf) => {
      this.model = gltf.scene;

      // Create IMU group
      this.imuGroup = new THREE.Group();
      this.imuGroup.add(this.model);
      this.scene.add(this.imuGroup);

      // Optional: center model
      const box = new THREE.Box3().setFromObject(this.model);
      const center = box.getCenter(new THREE.Vector3());
      this.model.position.sub(center);

      // Optional: scale tweak (should already be correct if you applied transforms)
      this.model.scale.setScalar(1.0);
    },
    undefined,
    (error) => {
      console.error('GLB load error', error);
    }
  );
}

  private sub = new Subscription();

  constructor(
    private ws: WebSocketService,
    private zone: NgZone,
    private cdr: ChangeDetectorRef) {
    
    this.sub.add(this.ws.sensorPacket$().subscribe(pkt => {
      this.zone.run(() => {
        this.roll = typeof pkt.roll_deg === 'number' ? pkt.roll_deg : 0;
        this.pitch = typeof pkt.pitch_deg === 'number' ? pkt.pitch_deg : 0;
        this.yaw = typeof pkt.heading_deg === 'number' ? pkt.heading_deg : 0;

        //console.log('IMU roll:', this.roll, 'pitch:', this.pitch, 'yaw:', this.yaw);

        this.cdr.detectChanges();
      });
    }));
  }

  ngAfterViewInit(): void {
    this.initThree();
    this.zone.runOutsideAngular(() => this.animate());
  }

  ngOnDestroy(): void {
    cancelAnimationFrame(this.frameId);
    this.renderer.dispose();
  }

  private initThree(): void {
    const el = this.host.nativeElement;

    /* SCENE */
    this.scene = new THREE.Scene();

    /* CAMERA */
    this.camera = new THREE.PerspectiveCamera(
      45,
      el.clientWidth / el.clientHeight,
      0.1,
      100
    );
    this.camera.position.z = 3;

    /* RENDERER — TRANSPARENT */
    this.renderer = new THREE.WebGLRenderer({
      alpha: true,
      antialias: true
    });
    this.renderer.setClearColor(0x000000, 0);
    this.renderer.setSize(el.clientWidth, el.clientHeight);
    el.appendChild(this.renderer.domElement);

    /* LIGHTS */
    this.scene.add(new THREE.AmbientLight(0xffffff, 0.5));

    const light = new THREE.DirectionalLight(0xffffff, 1);
    light.position.set(2, 2, 2);
    this.scene.add(light);

    this.loadModel();

    /* PLACEHOLDER TRANSDUCER */

  }
  
  private animate = (): void => {
    if (this.imuGroup) {
    // Convert deg -> rad
    const correctedRoll = this.roll + 90;   // shift -90° to 0°
    const correctedPitch = this.pitch;
    const correctedYaw = this.yaw;

    const rollRad  = THREE.MathUtils.degToRad(correctedRoll);
    const pitchRad = THREE.MathUtils.degToRad(correctedPitch);
    const yawRad   = THREE.MathUtils.degToRad(correctedYaw);

    // x, y, z
    this.imuGroup.rotation.set(-rollRad, -yawRad, -pitchRad);
    }

    this.renderer.render(this.scene, this.camera);
    this.frameId = requestAnimationFrame(this.animate);
  };



}
