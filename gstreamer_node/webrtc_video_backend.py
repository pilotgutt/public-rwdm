#!/usr/bin/env python3
import asyncio
import json
import logging
import os
import sys

import websockets
from websockets.server import WebSocketServerProtocol

import gi
gi.require_version("Gst", "1.0")
gi.require_version("GObject", "2.0")
gi.require_version("GstWebRTC", "1.0")
gi.require_version("GstSdp", "1.0")
from gi.repository import Gst, GObject, GstWebRTC, GstSdp

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("webrtc-backend")

Gst.init(None)

# Configuration
UDP_PORT = 8699              # Where we receive RTP/H.264 (video from 192.168.11.99)
WEBSOCKET_PORT = 8765        # Where Angular (or test client) connects for signaling
STUN_SERVER = "stun.l.google.com:19302"  # Public STUN, okay for dev


class WebRTCServer:
    def __init__(self, loop: asyncio.AbstractEventLoop):
        self.pipeline = None
        self.webrtc = None
        self.ws: WebSocketServerProtocol | None = None
        self.loop = loop  # main asyncio loop
        # Prevent repeated offers while waiting for an answer
        self.waiting_for_answer = False

    # ---------- GStreamer setup ----------

    def create_pipeline(self):
        """
        Build the GStreamer pipeline:
        udpsrc -> rtph264depay -> h264parse -> rtph264pay -> webrtcbin
        """
        # ── Pipeline A: decode → re-encode (robust, normalises any camera quirks)
        #    Requires gst-libav (avdec_h264). Install full GStreamer if missing.
        # pipeline_desc = f"""
        #     udpsrc port={UDP_PORT} caps=application/x-rtp,media=video,encoding-name=H264,payload=102,clock-rate=90000
        #         ! rtph264depay
        #         ! h264parse
        #         ! avdec_h264
        #         ! videoconvert
        #         ! video/x-raw,format=I420
        #         ! x264enc tune=zerolatency bitrate=2000 speed-preset=veryfast key-int-max=30
        #         ! video/x-h264,profile=constrained-baseline
        #         ! rtph264pay config-interval=1 pt=96 aggregate-mode=zero-latency
        #         ! queue max-size-time=100000000
        #         ! webrtcbin name=webrtcbin stun-server=stun://{STUN_SERVER}
        # """
        # ── Pipeline B: passthrough (lower CPU, no gst-libav needed)
        pipeline_desc = f"""
            udpsrc port={UDP_PORT} caps=application/x-rtp,media=video,encoding-name=H264,payload=102,clock-rate=90000
                ! rtph264depay
                ! h264parse config-interval=1
                ! video/x-h264,stream-format=byte-stream,alignment=au
                ! rtph264pay config-interval=1 pt=96 aggregate-mode=zero-latency
                ! queue max-size-time=100000000
                ! webrtcbin name=webrtcbin stun-server=stun://{STUN_SERVER}
        """
        # pipeline_desc = f"""
        # udpsrc port={UDP_PORT} caps=application/x-rtp,media=video,encoding-name=VP8,payload=97
        #     ! rtpvp8depay
        #     ! rtpvp8pay pt=97
        #     ! queue
        #     ! webrtcbin name=webrtcbin stun-server=stun://{STUN_SERVER}
        # """

        logger.info("Creating pipeline: %s", pipeline_desc)
        self.pipeline = Gst.parse_launch(pipeline_desc)
        self.webrtc = self.pipeline.get_by_name("webrtcbin")
        if not self.webrtc:
            raise RuntimeError("Failed to get webrtcbin from pipeline")

        # Connect webrtcbin signals
        self.webrtc.connect("on-negotiation-needed", self.on_negotiation_needed)
        self.webrtc.connect("on-ice-candidate", self.on_ice_candidate)

        # Bus for error messages
        bus = self.pipeline.get_bus()
        bus.add_signal_watch()
        bus.connect("message", self.on_bus_message)

        self.pipeline.set_state(Gst.State.PLAYING)
        logger.info("Pipeline set to PLAYING")

    def on_bus_message(self, bus, message):
        msg_type = message.type
        if msg_type == Gst.MessageType.ERROR:
            err, debug = message.parse_error()
            logger.error("GStreamer error: %s (%s)", err, debug)
        elif msg_type == Gst.MessageType.WARNING:
            w, debug = message.parse_warning()
            logger.warning("GStreamer warning: %s (%s)", w, debug)
        elif msg_type == Gst.MessageType.EOS:
            logger.info("GStreamer EOS")

    # ---------- WebRTC signaling <-> GStreamer ----------

    def send_json(self, data: dict):
        """
        Schedule sending JSON over WebSocket from GStreamer thread, using
        the main asyncio loop stored on the server object.
        """
        if not self.ws:
            logger.warning("No WebSocket connected; cannot send %s", data)
            return

        async def _async_send():
            try:
                await self.ws.send(json.dumps(data))
            except Exception as e:
                logger.error("Failed to send over WebSocket: %s", e)

        # Use asyncio from GObject main loop
        # asyncio.run_coroutine_threadsafe(_async_send(), asyncio.get_event_loop())
        # Use the stored main loop rather than get_event_loop()
        asyncio.run_coroutine_threadsafe(_async_send(), self.loop)




# ---------- WebRTC negotiation ----------
    def on_negotiation_needed(self, element):
        logger.info("on-negotiation-needed")
        # Debounce: if an offer is already outstanding, skip
        if getattr(self, "waiting_for_answer", False):
            logger.info("Negotiation already in progress, skipping")
            return

        self.waiting_for_answer = True
        promise = Gst.Promise.new_with_change_func(
            self.on_offer_created, element
        )
        element.emit("create-offer", None, promise)

    def on_offer_created(self, promise, element):
        logger.info("Offer created")

        reply = promise.get_reply()
        offer = reply.get_value("offer")  # Already a WebRTCSessionDescription now

        # Extract SDP text
        try:
            sdp_text = offer.get_sdp().as_text()
        except AttributeError:
            # Fallback for builds exposing .sdp directly
            sdp_text = offer.sdp.as_text()

        logger.info("Local SDP offer length: %d chars", len(sdp_text))

        # Set local description
        element.emit("set-local-description", offer, None)

        # --- This is the ONLY offer we actually send to the client ---
        self.send_json({
            "type": "offer",
            "sdp": sdp_text,
        })
        logger.info("Offer SDP sent to client")

        # --- Optional debug: inspect local-description, but DON'T send again ---

        local_desc = element.get_property("local-description")
        if local_desc is None:
            logger.warning("Debug: local-description is still None after set-local-description")
        else:
            logger.info("Debug: local-description is set (type=%s)", type(local_desc))
            # If you ever want to compare, you can log this too:
            # try:
            #     local_sdp_text = local_desc.get_sdp().as_text()
            #     logger.debug("Debug: local-description SDP length: %d chars", len(local_sdp_text))
            # except Exception as e:
            #     logger.warning("Debug: could not read SDP from local-description: %s", e)


# ---------- ICE candidate handling ----------
    def on_ice_candidate(self, element, mlineindex, candidate):
        logger.info("New ICE candidate from webrtcbin: %s", candidate)
        self.send_json({
            "type": "ice",
            "candidate": candidate,
            "sdpMLineIndex": mlineindex,
        })

    async def handle_client_message(self, message: str):
        """
        Handle signaling messages from WebSocket client.
        Expect JSON: {type: "answer"|"ice"|...}
        """
        logger.info("Received message from client: %s", message)
        data = json.loads(message)

        if data["type"] == "answer":
            logger.info("Received answer SDP from client")
            sdp_text = data["sdp"]

            # Parse SDP text into a GstSdp.SDPMessage and wrap into
            # a GstWebRTC.WebRTCSessionDescription so webrtcbin receives
            # the proper typed object.
            try:
                # Prefer sdp_message_new_from_text when available (simpler binding),
                # otherwise fall back to parse_buffer variants.
                sdp = None

                if hasattr(GstSdp, 'sdp_message_new_from_text'):
                    try:
                        maybe = GstSdp.sdp_message_new_from_text(sdp_text)
                        # Some bindings return (result, sdp_message), some return SDPMessage
                        if isinstance(maybe, tuple):
                            # (result, sdp_message)
                            _, maybe_sdp = maybe
                            if isinstance(maybe_sdp, GstSdp.SDPMessage):
                                sdp = maybe_sdp
                        elif isinstance(maybe, GstSdp.SDPMessage):
                            sdp = maybe
                    except Exception:
                        sdp = None

                if sdp is None:
                    # Try parse_buffer with explicit output SDPMessage (common binding)
                    buf = bytes(sdp_text.encode('utf-8'))
                    sdp = GstSdp.SDPMessage.new()
                    try:
                        ret = GstSdp.sdp_message_parse_buffer(buf, sdp)
                    except TypeError:
                        # Some bindings return (ret, sdp)
                        parsed = GstSdp.sdp_message_parse_buffer(buf)
                        if isinstance(parsed, tuple):
                            ret, maybe_sdp = parsed
                            if isinstance(maybe_sdp, GstSdp.SDPMessage):
                                sdp = maybe_sdp
                        else:
                            ret = parsed

                    if not isinstance(sdp, GstSdp.SDPMessage) or ret != GstSdp.SDPResult.OK:
                        logger.error("Failed to parse SDP from client, result=%s sdp=%s", getattr(locals(), 'ret', None), sdp)
                        return

                answer = GstWebRTC.WebRTCSessionDescription.new(
                    GstWebRTC.WebRTCSDPType.ANSWER,
                    sdp,
                )

                # set-remote-description expects (WebRTCSessionDescription, promise)
                self.webrtc.emit('set-remote-description', answer, None)
                logger.info('Remote description (answer) applied via WebRTCSessionDescription')
                # Allow new negotiations after we have applied the answer
                try:
                    self.waiting_for_answer = False
                except Exception:
                    pass
            except Exception as e:
                logger.error('Failed to set remote description from SDP: %s', e)
                return

#-----

        elif data["type"] == "ice":
            candidate = data["candidate"]
            sdp_mline_index = data.get("sdpMLineIndex", 0)
            logger.info("Adding ICE candidate from client: %s", candidate)
            self.webrtc.emit("add-ice-candidate", sdp_mline_index, candidate)

        elif data["type"] == "start":
            # Placeholder if you want the client to explicitly start negotiation.
            # With current setup, negotiation will occur when pipeline starts,
            # so this could be used as a trigger to call on_negotiation_needed
            logger.info("Client requested start; (negotiation usually auto-triggers)")

        else:
            logger.warning("Unknown message type: %s", data["type"])

    # ---------- WebSocket server ----------

    async def ws_handler(self, websocket: WebSocketServerProtocol):
        logger.info("WebSocket client connected")
        self.ws = websocket

        # Create pipeline on first client
        if not self.pipeline:
            self.create_pipeline()

        try:
            async for message in websocket:
                await self.handle_client_message(message)
        except websockets.exceptions.ConnectionClosed:
            logger.info("WebSocket closed")
        finally:
            self.ws = None
            # clear any outstanding negotiation state
            try:
                self.waiting_for_answer = False
            except Exception:
                pass

    async def run(self):
        async with websockets.serve(self.ws_handler, "0.0.0.0", WEBSOCKET_PORT):
            logger.info(f"Signaling server listening on ws://0.0.0.0:{WEBSOCKET_PORT}")
            await asyncio.Future()  # run forever


def main():
    GObject.threads_init()

    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    server = WebRTCServer(loop)

    # Integrate GObject main loop for GStreamer
    gobject_loop = GObject.MainLoop()
    loop.run_in_executor(None, gobject_loop.run)

    try:
        loop.run_until_complete(server.run())
    except KeyboardInterrupt:
        logger.info("Stopping...")
    finally:
        if server.pipeline:
            server.pipeline.set_state(Gst.State.NULL)
        gobject_loop.quit()
        loop.stop()
        loop.close()


if __name__ == "__main__":
    main()