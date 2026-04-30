# GStreamer WebRTC Video Feed Pipeline Documentation

This document explains how your GStreamer + Python WebRTC backend works and how Angular receives and renders the stream.

## Table of Contents

1. Background Concepts & Terminology
2. Architecture Overview
3. Python Backend Walkthrough
4. Angular Frontend Walkthrough
5. End-to-End Signaling Flow
6. Terminal Commands for Testing and Real Inputs
7. UDP Integration Checklist
8. UDP Inspection and Debug Scripts
9. Runtime Debugging Guide
10. Appendix A — Detailed Line-by-Line Walkthrough
11. Appendix B — Field Troubleshooting Playbook

---

## 1) Background Concepts & Terminology

### WebRTC
WebRTC (Web Real-Time Communication) is a standard for encrypted real-time audio/video/data between peers.

### RTCPeerConnection
`RTCPeerConnection` is the browser API that handles WebRTC negotiation, ICE, DTLS, and media tracks.

### RTP Packet
RTP (Real-time Transport Protocol) wraps media into packets over UDP. Important fields:
- Payload Type (PT): codec ID (e.g., dynamic 96 for H.264)
- Sequence Number: packet ordering / loss detection
- Timestamp: media timing
- SSRC: stream identifier

### UDP
UDP is connectionless and low-latency. It does not guarantee delivery or order.

### SDP Offer / Answer
SDP is text describing media session capabilities. One side sends an offer, the other replies with an answer.

### ICE Candidate
ICE candidates are possible network addresses/paths for peer connection (host, srflx, relay).

### STUN / TURN
- STUN: discovers public-facing address through NAT
- TURN: relays traffic when direct peer-to-peer fails

### `webrtcbin`
`webrtcbin` is GStreamer’s WebRTC element. It handles SDP, ICE, DTLS-SRTP, and RTP transport.

### H.264, SPS/PPS, IDR
- H.264 is the codec
- SPS/PPS are decoder config parameter sets
- IDR is a keyframe type
If SPS/PPS are missing, browser decoding can fail.

---

## 2) Architecture Overview

Video source sends RTP/H.264 over UDP to Python backend on port `5000`.

Backend pipeline (in [gstreamer_node/webrtc_video_backend.py](gstreamer_node/webrtc_video_backend.py)):
- `udpsrc` -> `rtph264depay` -> `h264parse` -> `rtph264pay` -> `webrtcbin`

Signaling channel:
- WebSocket on port `8765`
- Backend sends SDP offer + ICE candidates
- Angular replies with SDP answer + ICE candidates

Frontend side:
- [frontend_UI/angular/src/app/shared/services/webrtc-video.service.ts](frontend_UI/angular/src/app/shared/services/webrtc-video.service.ts) creates `RTCPeerConnection`
- [frontend_UI/angular/src/app/shared/components/webrtc-video/webrtc-video.component.ts](frontend_UI/angular/src/app/shared/components/webrtc-video/webrtc-video.component.ts) binds `MediaStream` to `<video>`

---

## 3) Python Backend Walkthrough

File: [gstreamer_node/webrtc_video_backend.py](gstreamer_node/webrtc_video_backend.py)

### Key constants
- `UDP_PORT = 5000`
- `WEBSOCKET_PORT = 8765`
- `STUN_SERVER = "stun.l.google.com:19302"`

### Class: `WebRTCServer`

#### `__init__(loop)`
Stores:
- `self.pipeline`
- `self.webrtc`
- `self.ws`
- `self.loop`
- `self.waiting_for_answer`

#### `create_pipeline()`
Builds the pipeline and sets it `PLAYING`.
Connects webrtcbin signals:
- `on-negotiation-needed`
- `on-ice-candidate`
Also attaches bus watcher for warnings/errors.

#### `on_bus_message()`
Logs:
- `GStreamer error`
- `GStreamer warning`
- `GStreamer EOS`

#### `send_json(data)`
Thread-safe signaling send via `asyncio.run_coroutine_threadsafe(..., self.loop)`.

#### `on_negotiation_needed(element)`
Debounces duplicate negotiations and triggers `create-offer`.

#### `on_offer_created(promise, element)`
- extracts SDP text
- sets local description
- sends `{type: "offer", sdp: ...}`

#### `on_ice_candidate(element, mlineindex, candidate)`
Sends `{type: "ice", candidate, sdpMLineIndex}` to frontend.

#### `handle_client_message(message)`
Handles frontend messages:
- `answer`: parse SDP, wrap in `GstWebRTC.WebRTCSessionDescription`, call `set-remote-description`
- `ice`: call `add-ice-candidate`
- `start`: informational

#### `ws_handler(websocket)`
- stores client socket
- lazily creates pipeline when first client connects
- loops over incoming messages

#### `run()`
Starts WebSocket signaling server forever.

#### `main()`
- creates asyncio loop
- starts GObject loop in executor thread
- runs server
- cleans pipeline on shutdown

---

## 4) Angular Frontend Walkthrough

### Service
File: [frontend_UI/angular/src/app/shared/services/webrtc-video.service.ts](frontend_UI/angular/src/app/shared/services/webrtc-video.service.ts)

Main responsibilities:
- open WebSocket to `ws://localhost:8765`
- create/manage `RTCPeerConnection`
- process signaling messages (`offer`, `ice`)
- emit remote stream through `remoteStream$`
- periodic `getStats()` logging

Key flow:
1. `connect()` opens WS and sends `{ type: 'start' }`
2. on `offer`: `handleOffer()` sets remote description -> creates answer -> sets local description -> sends answer
3. ICE messages exchanged in both directions
4. on `ontrack`: publish stream to `BehaviorSubject`

### Video Component
File: [frontend_UI/angular/src/app/shared/components/webrtc-video/webrtc-video.component.ts](frontend_UI/angular/src/app/shared/components/webrtc-video/webrtc-video.component.ts)

- subscribes to `remoteStream$`
- sets `video.srcObject = stream`
- ensures `muted/autoplay/playsInline`
- calls `video.play()`

Template file: [frontend_UI/angular/src/app/shared/components/webrtc-video/webrtc-video.component.html](frontend_UI/angular/src/app/shared/components/webrtc-video/webrtc-video.component.html)

---

## 5) End-to-End Signaling Flow

1. Angular opens WebSocket to backend
2. Backend creates pipeline
3. webrtcbin emits `on-negotiation-needed`
4. Backend creates and sends SDP offer
5. Angular sets offer, creates answer, sends answer
6. Backend applies answer
7. ICE candidates exchanged both directions
8. connection reaches `connected`
9. browser receives remote track and renders video

---

## 6) Terminal Commands for Testing and Real Inputs

### Start backend
```bash
cd gstreamer_node
python3 webrtc_video_backend.py
```

### Start Angular
```bash
cd frontend_UI/angular
npm install
ng serve
```

### Simulated test pattern to UDP/H.264
```bash
gst-launch-1.0 videotestsrc is-live=true pattern=0 \
  ! video/x-raw,width=640,height=480,framerate=30/1 \
  ! videoconvert \
  ! x264enc tune=zerolatency bitrate=2000 speed-preset=ultrafast key-int-max=30 \
  ! rtph264pay config-interval=1 pt=96 \
  ! udpsink host=127.0.0.1 port=5000
```

### Linux laptop webcam
```bash
gst-launch-1.0 v4l2src device=/dev/video0 \
  ! video/x-raw,width=640,height=480,framerate=30/1 \
  ! videoconvert \
  ! x264enc tune=zerolatency bitrate=2000 speed-preset=ultrafast key-int-max=30 \
  ! rtph264pay config-interval=1 pt=96 \
  ! udpsink host=127.0.0.1 port=5000
```

### Windows webcam (Media Foundation)
```bash
gst-launch-1.0 mfvideosrc \
  ! video/x-raw,width=640,height=480,framerate=30/1 \
  ! videoconvert \
  ! x264enc tune=zerolatency bitrate=2000 speed-preset=ultrafast key-int-max=30 \
  ! rtph264pay config-interval=1 pt=96 \
  ! udpsink host=127.0.0.1 port=5000
```

### Remote source device (send to backend IP)
```bash
gst-launch-1.0 v4l2src device=/dev/video0 \
  ! video/x-raw,width=640,height=480,framerate=30/1 \
  ! videoconvert \
  ! x264enc tune=zerolatency bitrate=2000 speed-preset=ultrafast key-int-max=30 \
  ! rtph264pay config-interval=1 pt=96 \
  ! udpsink host=<BACKEND_IP> port=5000
```

---

## 7) UDP Integration Checklist

- [ ] Sender is running and targeting correct backend IP + UDP port
- [ ] Firewall allows inbound UDP/5000 on backend machine
- [ ] Packets observed on receiver (`tcpdump`/script)
- [ ] RTP version is 2 and PT matches expected codec
- [ ] H.264 stream contains SPS/PPS and periodic IDR
- [ ] Backend logs show offer created and answer applied
- [ ] Frontend logs show `Connection state: connected`
- [ ] WebRTC stats show increasing `packetsReceived` and `framesDecoded`

---

## 8) UDP Inspection and Debug Scripts

### Quick packet check
```bash
sudo tcpdump -i any udp port 5000 -c 20
```

### Detailed hex view
```bash
sudo tcpdump -i any udp port 5000 -c 10 -X
```

### RTP-aware view with tshark
```bash
tshark -i any -f "udp port 5000" -c 20 -d udp.port==5000,rtp -T fields \
  -e rtp.version -e rtp.p_type -e rtp.seq -e rtp.timestamp -e frame.len
```

### Suggested `udp_inspector.py` (create this file)

```python
#!/usr/bin/env python3
import argparse
import socket
import struct
import time
from collections import Counter


def classify(data: bytes):
    if len(data) >= 12 and ((data[0] >> 6) & 0x03) == 2:
        pt = data[1] & 0x7F
        seq = struct.unpack("!H", data[2:4])[0]
        ts = struct.unpack("!I", data[4:8])[0]
        return "rtp", {"pt": pt, "seq": seq, "ts": ts}

    if len(data) >= 2 and data[0] == 0x0A and data[1] == 0xFA:
        return "sensor_like", {}

    return "unknown", {}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=5000)
    p.add_argument("--duration", type=int, default=10)
    args = p.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", args.port))
    sock.settimeout(1.0)

    counts = Counter()
    start = time.time()

    while time.time() - start < args.duration:
        try:
            data, addr = sock.recvfrom(65535)
        except socket.timeout:
            continue
        t, d = classify(data)
        counts[t] += 1
        if counts.total() <= 20:
            print(f"{addr[0]}:{addr[1]} len={len(data)} type={t} details={d}")

    print("\nSummary:")
    total = sum(counts.values())
    for k, v in counts.items():
        pct = (v / total * 100) if total else 0
        print(f"  {k}: {v} ({pct:.1f}%)")

    if counts.get("rtp", 0) > 0 and counts.get("sensor_like", 0) > 0:
        print("\nMixed traffic detected: RTP + sensor-like packets on same port.")
        print("Recommendation: split ports or use a UDP demux proxy.")


if __name__ == "__main__":
    main()
```

### Run inspector
```bash
python3 udp_inspector.py --port 5000 --duration 15
```

---

## 9) Runtime Debugging Guide

### A) What healthy logs look like

#### Python backend terminal
Expected highlights:
- `Signaling server listening on ws://0.0.0.0:8765`
- `WebSocket client connected`
- `Pipeline set to PLAYING`
- `on-negotiation-needed`
- `Offer created`
- `Offer SDP sent to client`
- `Received answer SDP from client`
- `Remote description (answer) applied via WebRTCSessionDescription`

#### Browser console
Expected highlights:
- `[WebRTC] WebSocket connected`
- `[WebRTC] Setting remote offer`
- `[WebRTC] Creating answer`
- `[WebRTC] Sending answer`
- `[WebRTC] Connection state: connected`
- `[WebRTC] Remote track received`
- `[webrtc-video] attaching stream...`
- `[webrtc-video] video element size ... readyState 4`

### B) Red flags and what they usually mean

- `No WebSocket connected; cannot send ...`
  - frontend not connected yet, or disconnected

- `Failed to parse SDP from client`
  - malformed/unsupported SDP parsing path

- `GStreamer error: Internal data flow error`
  - caps mismatch, invalid RTP, codec mismatch

- Browser `Connection state: failed`
  - ICE path failure / firewall / NAT issue

- Browser receives packets but `framesDecoded` stays `0`
  - codec mismatch or missing SPS/PPS

### C) Stats fields to watch

In `[WebRTC stats] inbound-rtp`:
- `packetsReceived` should continuously increase
- `bytesReceived` should continuously increase
- `framesDecoded` should increase if decode succeeds
- `packetsLost` should stay low
- `jitter` should remain relatively low and stable

### D) Browser tools

- Chrome: `chrome://webrtc-internals`
- Firefox: `about:webrtc`
- DevTools Network tab -> WebSocket frames to inspect signaling JSON

### E) Increase GStreamer verbosity

```bash
GST_DEBUG=3 python3 webrtc_video_backend.py
GST_DEBUG=webrtcbin:5,webrtcice:5 python3 webrtc_video_backend.py
GST_DEBUG=udpsrc:5,rtph264depay:5 python3 webrtc_video_backend.py
```

Save logs:
```bash
GST_DEBUG=4 python3 webrtc_video_backend.py 2> gst_debug.log
```

---

## Practical Recommendation

For production reliability:
1. Keep video RTP on one UDP port (e.g., 5000)
2. Keep sensor packets on a separate UDP port
3. If sender cannot be changed, add a UDP demux proxy before the backend
4. Keep runtime logging enabled in both backend and frontend until field testing is stable

---

## 10) Appendix A — Detailed Line-by-Line Walkthrough

This appendix gives a deeper, near line-by-line explanation of the active backend and frontend code paths.

### A.1) Python backend detailed walk

Primary file: [gstreamer_node/webrtc_video_backend.py](gstreamer_node/webrtc_video_backend.py)

#### Header and imports block

- Shebang makes the script executable in Unix-like shells.
- `asyncio` is used for signaling server event loop.
- `websockets` powers signaling protocol transport.
- `gi.require_version(...)` pins runtime ABI for GStreamer modules.
- `Gst.init(None)` is mandatory before creating pipelines.

#### Config block

- `UDP_PORT = 5000`
  - ingest port for incoming RTP video.
- `WEBSOCKET_PORT = 8765`
  - signaling server port for browser client.
- `STUN_SERVER = "stun.l.google.com:19302"`
  - ICE discovery helper.

#### Class initialization

State model in constructor:
- `pipeline`: `None` until first client connects.
- `webrtc`: set after `parse_launch` + `get_by_name`.
- `ws`: active signaling socket for single client.
- `loop`: async loop captured to bridge thread domains.
- `waiting_for_answer`: prevents repeated overlapping offers.

This design keeps startup light and avoids opening media path before a client exists.

#### `create_pipeline()` deep details

Pipeline creation string is the critical contract between sender and receiver.

Caps details:
- `application/x-rtp`
- `media=video`
- `encoding-name=H264`
- `payload=96`

These must match sender output. If sender PT or codec differs, depayloader/parse chain can break.

Element order rationale:
1. `udpsrc`: datagram ingest.
2. `rtph264depay`: strips RTP layer.
3. `h264parse`: normalizes stream, timing, parameter sets.
4. `rtph264pay`: repacketizes for WebRTC pipeline path.
5. `queue`: threading/buffering boundary.
6. `webrtcbin`: signaling/media transport engine.

Post-create actions:
- bind negotiation and ICE callbacks
- add bus signal watch
- transition to PLAYING

#### Bus callback behavior

`on_bus_message()` is your first responder for media pipeline health.

- ERROR: immediately actionable; often caps mismatch or missing plugin.
- WARNING: may be tolerable but should be inspected if repeated.
- EOS: expected for finite file sources, suspicious for supposed live camera.

#### Thread bridge in `send_json()`

GStreamer callbacks are not running inside the asyncio thread.
`run_coroutine_threadsafe()` is therefore essential to avoid unsafe cross-thread websocket usage.

#### Negotiation callback pair

`on_negotiation_needed()`:
- acts as offer trigger gate.
- debounce avoids duplicate offers from repeated internal triggers.

`on_offer_created()`:
- extracts SDP text from offer object.
- applies local description.
- sends single canonical offer via signaling channel.

#### ICE callback

`on_ice_candidate()` sends each discovered candidate immediately.

Why immediate send matters:
- faster connectivity checks
- reduced time to first frame

#### `handle_client_message()` deeper notes

`answer` path:
- binding compatibility logic handles multiple Python GI signatures.
- result must be a valid `GstSdp.SDPMessage`.
- wraps into `WebRTCSessionDescription(ANSWER, sdp)`.
- calls `set-remote-description`.

`ice` path:
- candidate forwarded to `add-ice-candidate` with correct mline index.

`start` path:
- informational in current architecture.

Error path:
- logs unknown message type for protocol sanity checks.

#### WebSocket lifecycle

`ws_handler()` flow:
1. store client socket
2. lazy-create pipeline
3. process incoming messages until close
4. reset socket + negotiation flag on disconnect

Single-client note: later clients replace previous socket reference. For multi-client support, architecture must move to per-peer session objects.

#### Main loop integration

Two-loop architecture:
- asyncio loop for WebSocket and Python async tasks
- GObject main loop for GStreamer bus/signals

Current implementation runs GObject loop via executor thread, which is a practical bridge model for GI-based apps.

---

### A.2) Alternate backend file path (`webrtcvid.py`)

File: [gstreamer_node/webrtcvid.py](gstreamer_node/webrtcvid.py)

Pipeline path differences:
- source is `filesrc location=this.MP4`
- demux/decode stage exists (`qtdemux` + `decodebin`)
- output codec is VP8 (`vp8enc` + `rtpvp8pay pt=97`)

Use case:
- deterministic local testing with known source data
- remove external UDP sender dependency while validating signaling and rendering path

---

### A.3) Angular frontend detailed walk

Service file: [frontend_UI/angular/src/app/shared/services/webrtc-video.service.ts](frontend_UI/angular/src/app/shared/services/webrtc-video.service.ts)

#### State and observables

- `BehaviorSubject<MediaStream | null>` provides current stream snapshot to late subscribers.
- `remoteStream$` observable decouples component from low-level WebRTC internals.

#### `connect()` signal transport behavior

Operation sequence:
1. open websocket
2. send `start`
3. parse incoming signaling JSON
4. dispatch by message type
5. on close, cleanup and emit null stream

Implementation detail:
- keeps one socket instance and prevents duplicate connection attempts.

#### `ensurePeerConnection()` internals

`onicecandidate`:
- emits local browser candidates to backend.

`ontrack`:
- receives remote media stream and emits into subject.
- wrapped in `NgZone.run()` to force Angular change detection correctness.

`onconnectionstatechange`:
- critical for debugging transitions: `new` -> `connecting` -> `connected` (or `failed`).

stats interval:
- periodic telemetry for packet/decoder health.

#### `handleOffer()` critical path

Offer-answer sequence in browser:
1. `setRemoteDescription(offer)`
2. `createAnswer()`
3. `setLocalDescription(answer)`
4. send answer over WS

Any failure here prevents media establishment even if WS seems healthy.

#### `handleRemoteIce()`

Applies backend ICE candidate to browser peer connection.
Late candidates can still be valid; errors may indicate malformed candidate strings or lifecycle ordering issues.

#### Component behavior

Component file: [frontend_UI/angular/src/app/shared/components/webrtc-video/webrtc-video.component.ts](frontend_UI/angular/src/app/shared/components/webrtc-video/webrtc-video.component.ts)

Key sequence:
1. connect signaling in `ngAfterViewInit`
2. subscribe to stream
3. set `video.srcObject`
4. enforce autoplay-compatible flags
5. call `play()` and inspect runtime element dimensions

A stream can exist before visible render; `videoWidth`, `videoHeight`, and `readyState` confirm decode/render status.

---

## 11) Appendix B — Field Troubleshooting Playbook

This section is designed for on-site or tank/sea trial troubleshooting when time is limited.

### B.1) 60-second triage checklist

1. Is backend running and listening on signaling port?
2. Is frontend connected to signaling WS?
3. Are UDP packets arriving on media port?
4. Is offer sent and answer received?
5. Are ICE candidates exchanged both directions?
6. Is browser connection state `connected`?
7. Are `packetsReceived` and `framesDecoded` increasing?

If answer to any step is no, focus there first before moving forward.

### B.2) Triage commands

Backend host command set:

```bash
# confirm signaling listener
ss -ltnp | grep 8765

# confirm UDP packets arrive
sudo tcpdump -i any udp port 5000 -c 25

# confirm process is running
ps aux | grep webrtc_video_backend.py | grep -v grep
```

Frontend host quick checks:

```javascript
// in browser console
const v = document.querySelector('video');
console.log('state', v?.readyState, v?.videoWidth, v?.videoHeight);
```

### B.3) Problem-to-cause map

| Symptom | Likely cause | Fastest check |
|---|---|---|
| WS never connects | backend down / wrong URL | browser console + backend listener on 8765 |
| Offer sent, no answer | frontend signaling path broken | Network tab WS frames |
| Connection state `failed` | ICE path blocked | candidate logs + firewall/NAT |
| Packets received, no frames decoded | codec/caps mismatch or SPS/PPS issue | stats + sender encoder settings |
| `on-negotiation-needed` never fires | no media reaching pipeline | tcpdump on UDP 5000 |
| Intermittent freeze | packet loss/jitter/keyframe spacing | stats trend + sender bitrate/key-int |

### B.4) Sender tuning recommendations

For unstable links:
- lower resolution (for example 640x480)
- lower framerate (for example 15 fps)
- lower bitrate
- keep `tune=zerolatency`
- keep periodic keyframes (`key-int-max` around 15-30)
- keep SPS/PPS insertion (`config-interval=1`)

### B.5) Deployment recommendations

1. Use separate ports for video and sensor payloads.
2. Keep signaling and media logs enabled in test builds.
3. Add health watchdogs:
   - backend pipeline state heartbeat
   - frontend track/frames watchdog
4. Persist logs with timestamps during sea trials.
5. Save `chrome://webrtc-internals` dump for each failed run.

### B.6) What to archive after each failed test

- Backend terminal output
- `GST_DEBUG` log (if enabled)
- Browser console export
- webrtc-internals dump
- sender command line used
- network topology snapshot (IP/port paths)

This archive drastically speeds root-cause analysis between test runs.
