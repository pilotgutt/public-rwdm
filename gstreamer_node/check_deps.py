#!/usr/bin/env python3
"""Dependency check for webrtc_video_backend.py"""

import sys
import shutil

OK = "\033[92m✔\033[0m"
FAIL = "\033[91m✘\033[0m"
passed = 0
failed = 0


def check(name, fn):
    global passed, failed
    try:
        detail = fn()
        print(f"  {OK} {name}" + (f"  ({detail})" if detail else ""))
        passed += 1
    except Exception as e:
        print(f"  {FAIL} {name}  — {e}")
        failed += 1


# --- Python version ---
print("\n[Python]")
check(f"Python >= 3.10 (have {sys.version.split()[0]})",
      lambda: None if sys.version_info >= (3, 10) else (_ for _ in ()).throw(
          RuntimeError("Python 3.10+ required")))

# --- pip packages ---
print("\n[Python packages]")
check("websockets", lambda: __import__("websockets").__version__)

# --- GObject Introspection ---
print("\n[GObject Introspection]")
check("PyGObject (gi)", lambda: __import__("gi").__version__)

import gi  # noqa: E402

GI_MODULES = {
    "Gst":        "1.0",
    "GObject":    "2.0",
    "GstWebRTC":  "1.0",
    "GstSdp":     "1.0",
}

for mod, ver in GI_MODULES.items():
    def _check(m=mod, v=ver):
        gi.require_version(m, v)
        ns = getattr(__import__("gi.repository", fromlist=[m]), m)
        if m == "Gst":
            ns.init(None)
            return ns.version_string()
        return None
    check(f"gi.repository.{mod} ({ver})", _check)

# --- GStreamer elements ---
print("\n[GStreamer elements]")

ELEMENTS = [
    "udpsrc",
    "rtph264depay",
    "h264parse",
    "rtph264pay",
    "queue",
    "webrtcbin",
]

from gi.repository import Gst  # noqa: E402
Gst.init(None)

for elem in ELEMENTS:
    def _check_elem(e=elem):
        factory = Gst.ElementFactory.find(e)
        if factory is None:
            raise RuntimeError(f"element '{e}' not found — install the plugin that provides it")
        return factory.get_metadata("long-name")
    check(elem, _check_elem)

# --- Optional: avdec_h264 for Pipeline A (decode→re-encode) ---
print("\n[Optional — only needed for decode→re-encode pipeline]")
for opt in ["avdec_h264", "x264enc", "videoconvert", "videoscale"]:
    def _check_opt(e=opt):
        factory = Gst.ElementFactory.find(e)
        if factory is None:
            raise RuntimeError(f"not found (install gst-libav / gst-plugins-ugly)")
        return factory.get_metadata("long-name")
    check(opt, _check_opt)

# --- Network tool ---
print("\n[Network]")
check("gst-launch-1.0 on PATH", lambda: (
    None if shutil.which("gst-launch-1.0")
    else (_ for _ in ()).throw(RuntimeError("not on PATH"))))

# --- Summary ---
total = passed + failed
print(f"\n{'='*40}")
print(f"  {passed}/{total} checks passed", end="")
if failed:
    print(f"  ({failed} failed)")
else:
    print("  — all good, ready to run!")
print()
sys.exit(1 if failed else 0)
