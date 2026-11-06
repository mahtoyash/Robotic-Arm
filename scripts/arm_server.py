#!/usr/bin/env python3
"""
RoboArm Local Voice Server for Android (Termux)
Optimized for Qualcomm Snapdragon 8 Gen 2 (iQOO Neo 9 Pro)
Supports 3 Terminal Modes:
  [1] INMP441 Mic (ESP32 Hardware Mic over Wi-Fi)
  [2] Phone Built-in Mic (Direct Termux Push-to-Talk) [DEFAULT]
  [3] Dual Mode (Both active simultaneously!)
"""

import os
import sys
import time
import string
import socket
import threading
import subprocess
import requests
from concurrent.futures import ThreadPoolExecutor
from flask import Flask, request, jsonify

app = Flask(__name__)
esp32_ip = "192.168.1.39"
active_mode = "2"

COMMAND_MAP = {
    # ── Wake Word & Awaken ──
    "hey robo": "wake",
    "ok robo": "wake",
    "hello robo": "wake",
    "hi robo": "wake",
    "robo": "wake",
    "rubo": "wake",
    "robu": "wake",
    "roba": "wake",
    "ruba": "wake",
    "robot": "wake",
    "wake up": "wake",
    "wake": "wake",
    "utho": "wake",
    "jag": "wake",

    # ── Sleep / Rest ──
    "sleep": "sleep",
    "be slept": "sleep",
    "slept": "sleep",
    "so jao": "sleep",
    "rest": "sleep",
    "relax": "sleep",

    # ── Gripper ──
    "grab": "grab",
    "pakad": "grab",
    "pakdo": "grab",
    "close": "grab",
    "hold": "grab",
    "band": "grab",
    "open": "open",
    "khol": "open",
    "kholo": "open",
    "chhod": "open",
    "release": "open",

    # ── Base (Continuous 360°) ──
    "clockwise": "base_right",
    "cw": "base_right",
    "move clockwise": "base_right",
    "turn clockwise": "base_right",
    "rotate clockwise": "base_right",
    "anticlockwise": "base_left",
    "anti clockwise": "base_left",
    "counterclockwise": "base_left",
    "counter clockwise": "base_left",
    "ccw": "base_left",
    "move anticlockwise": "base_left",
    "turn anticlockwise": "base_left",
    "rotate anticlockwise": "base_left",
    "move base left": "base_left",
    "move left": "base_left",
    "base left": "base_left",
    "turn left": "base_left",
    "rotate left": "base_left",
    "left": "base_left",
    "baaye": "base_left",
    "move base right": "base_right",
    "move right": "base_right",
    "base right": "base_right",
    "turn right": "base_right",
    "rotate right": "base_right",
    "right": "base_right",
    "daaye": "base_right",

    # ── Shoulder ──
    "shoulder front": "shoulder_front",
    "shoulder friend": "shoulder_front",
    "shoulder forward": "shoulder_front",
    "shoulder down": "shoulder_front",
    "front": "shoulder_front",
    "friend": "shoulder_front",
    "forward": "shoulder_front",
    "push": "shoulder_front",
    "aage": "shoulder_front",
    "shoulder reverse": "shoulder_back",
    "reverse": "shoulder_back",
    "rivers": "shoulder_back",
    "river": "shoulder_back",
    "shoulder rivers": "shoulder_back",
    "shoulder pull": "shoulder_back",
    "pull": "shoulder_back",
    "shoulder bag": "shoulder_back",
    "shoulder pack": "shoulder_back",
    "shoulder back": "shoulder_back",
    "shoulder up": "shoulder_back",
    "back": "shoulder_back",
    "peeche": "shoulder_back",

    # ── Elbow (Explicit Elbow Commands) ──
    "elbow up": "elbow_up",
    "elbow raise": "elbow_up",
    "elbow upar": "elbow_up",
    "elbow lift": "elbow_up",
    "elbow down": "elbow_down",
    "elbow lower": "elbow_down",
    "elbow niche": "elbow_down",

    # ── Vertical Tilt / Wrist (Raise & Lower) ──
    "raise": "wrist_up",
    "lift": "wrist_up",
    "up": "wrist_up",
    "upar": "wrist_up",
    "wrist up": "wrist_up",
    "pitch up": "wrist_up",
    "patch up": "wrist_up",
    "pich up": "wrist_up",
    "lower": "wrist_down",
    "drop": "wrist_down",
    "down": "wrist_down",
    "neeche": "wrist_down",
    "niche": "wrist_down",
    "wrist down": "wrist_down",
    "pitch down": "wrist_down",
    "patch down": "wrist_down",
    "pich down": "wrist_down",
    "roll left": "roll_left",
    "roll right": "roll_right",

    # ── Stop ──
    "stop": "stop",
    "ruko": "stop",

    # ── Micro Commands (5°-7° Precision Movements) ──
    # Micro Base:
    "micro left": "micro_base_left",
    "thoda left": "micro_base_left",
    "nudge left": "micro_base_left",
    "micro base left": "micro_base_left",
    "micro right": "micro_base_right",
    "thoda right": "micro_base_right",
    "nudge right": "micro_base_right",
    "micro base right": "micro_base_right",

    # Micro Shoulder:
    "micro front": "micro_shoulder_front",
    "micro forward": "micro_shoulder_front",
    "thoda front": "micro_shoulder_front",
    "thoda aage": "micro_shoulder_front",
    "nudge front": "micro_shoulder_front",
    "micro back": "micro_shoulder_back",
    "micro reverse": "micro_shoulder_back",
    "thoda back": "micro_shoulder_back",
    "thoda peeche": "micro_shoulder_back",
    "nudge back": "micro_shoulder_back",

    # ── Elbow (Must explicitly say 'elbow') ──
    "elbow up": "elbow_up",
    "micro elbow up": "micro_elbow_up",
    "thoda elbow up": "micro_elbow_up",
    "thoda elbow upar": "micro_elbow_up",
    "nudge elbow up": "micro_elbow_up",
    "elbow down": "elbow_down",
    "micro elbow down": "micro_elbow_down",
    "thoda elbow down": "micro_elbow_down",
    "thoda elbow niche": "micro_elbow_down",
    "nudge elbow down": "micro_elbow_down",

    # ── Wrist Pitch (Controls 'up' and 'down') ──
    "up": "wrist_up",
    "upar": "wrist_up",
    "lift": "wrist_up",
    "pitch up": "wrist_up",
    "wrist up": "wrist_up",
    "micro up": "micro_wrist_up",
    "thoda up": "micro_wrist_up",
    "thoda upar": "micro_wrist_up",
    "nudge up": "micro_wrist_up",
    "micro pitch up": "micro_wrist_up",
    "micro wrist up": "micro_wrist_up",
    "thoda wrist upar": "micro_wrist_up",

    "down": "wrist_down",
    "niche": "wrist_down",
    "neeche": "wrist_down",
    "lower": "wrist_down",
    "pitch down": "wrist_down",
    "wrist down": "wrist_down",
    "micro down": "micro_wrist_down",
    "thoda down": "micro_wrist_down",
    "thoda niche": "micro_wrist_down",
    "nudge down": "micro_wrist_down",
    "micro pitch down": "micro_wrist_down",
    "micro wrist down": "micro_wrist_down",
    "thoda wrist niche": "micro_wrist_down",

    # Micro Wrist Roll:
    "micro roll left": "micro_roll_left",
    "thoda roll left": "micro_roll_left",
    "micro roll right": "micro_roll_right",
    "thoda roll right": "micro_roll_right"
}

def check_single_ip(ip):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(0.35)
    res = sock.connect_ex((ip, 80))
    sock.close()
    if res == 0:
        try:
            r = requests.get(f"http://{ip}/", timeout=0.8)
            if "RoboArm" in r.text:
                return ip
        except Exception:
            pass
    return None

def auto_discover_esp32():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        my_ip = s.getsockname()[0]
        s.close()
        prefix = ".".join(my_ip.split(".")[:3]) + "."
        print(f"🔍 Auto-scanning local network {prefix}1..254 for ESP32 RoboArm...")
        with ThreadPoolExecutor(max_workers=50) as ex:
            ips = [f"{prefix}{i}" for i in range(1, 255)]
            for found in ex.map(check_single_ip, ips):
                if found:
                    print(f"🎯 Auto-Discovered ESP32 RoboArm at: {found}!")
                    return found
    except Exception as e:
        print(f"⚠️ Auto-scan notice: {e}")
    return None

def dispatch_to_esp32(action):
    global esp32_ip
    targets = []
    if esp32_ip:
        targets.append(esp32_ip)
    if "roboarm.local" not in targets:
        targets.append("roboarm.local")
    if "192.168.4.1" not in targets:
        targets.append("192.168.4.1")

    for target in targets:
        url = f"http://{target}/cmd?action={action}"
        try:
            print(f"🚀 Sending [{action}] to ESP32 ({target})...")
            resp = requests.get(url, timeout=2.5)
            if resp.status_code == 200:
                print(f"✅ ESP32 Response: {resp.text.strip()}")
                esp32_ip = target
                return True
        except Exception:
            pass

    # If all configured targets fail, run subnet scan
    print(f"⚠️ Could not reach ESP32 on {targets}. Running auto-network scan...")
    discovered = auto_discover_esp32()
    if discovered:
        esp32_ip = discovered
        try:
            url = f"http://{discovered}/cmd?action={action}"
            resp = requests.get(url, timeout=3.0)
            if resp.status_code == 200:
                print(f"✅ ESP32 Response from {discovered}: {resp.text.strip()}")
                return True
        except Exception:
            pass

    print(f"❌ Failed to reach ESP32 on local network.")
    return False

def find_whisper_binary():
    candidates = [
        os.path.expanduser("~/whisper.cpp/build/bin/whisper-cli"),
        os.path.expanduser("~/whisper.cpp/build/bin/main"),
        os.path.expanduser("~/whisper.cpp/main"),
        os.path.expanduser("~/whisper.cpp/whisper-cli"),
        "./build/bin/whisper-cli",
        "./build/bin/main",
        "./main",
        "./whisper-cli",
    ]
    for c in candidates:
        if os.path.isfile(c):
            return os.path.abspath(c)
    return "./build/bin/whisper-cli"

def find_whisper_model():
    candidates = [
        os.path.expanduser("~/whisper.cpp/models/ggml-base.bin"),
        os.path.expanduser("~/whisper.cpp/models/ggml-tiny.bin"),
        os.path.expanduser("~/whisper.cpp/models/ggml-small.bin"),
        "models/ggml-base.bin",
        "models/ggml-tiny.bin",
        "models/ggml-small.bin",
    ]
    for c in candidates:
        if os.path.isfile(c):
            return os.path.abspath(c)
# Speech Recognition Engine Integration
try:
    import speech_recognition as sr
    HAS_GOOGLE_SR = True
except ImportError:
    HAS_GOOGLE_SR = False

active_engine = "1" # [1] Google STT (Default), [2] Whisper Offline, [3] Auto Hybrid

def find_whisper_binary():
    candidates = [
        os.path.expanduser("~/whisper.cpp/build/bin/whisper-cli"),
        os.path.expanduser("~/whisper.cpp/build/bin/main"),
        os.path.expanduser("~/whisper.cpp/main"),
        os.path.expanduser("~/whisper.cpp/whisper-cli"),
        "./build/bin/whisper-cli",
        "./build/bin/main",
        "./main",
        "./whisper-cli",
    ]
    for c in candidates:
        if os.path.isfile(c):
            return os.path.abspath(c)
    return "./build/bin/whisper-cli"

def find_whisper_model():
    candidates = [
        os.path.expanduser("~/whisper.cpp/models/ggml-base.bin"),
        os.path.expanduser("~/whisper.cpp/models/ggml-tiny.bin"),
        os.path.expanduser("~/whisper.cpp/models/ggml-small.bin"),
        "models/ggml-base.bin",
        "models/ggml-tiny.bin",
        "models/ggml-small.bin",
    ]
    for c in candidates:
        if os.path.isfile(c):
            return os.path.abspath(c)
    return "models/ggml-base.bin"

WHISPER_BIN = find_whisper_binary()
WHISPER_MODEL = find_whisper_model()

def transcribe_with_google(audio_path):
    """Transcribes audio using Google Speech API (optimized for Indian English en-IN)"""
    if not HAS_GOOGLE_SR:
        return None, "speech_recognition library not installed (run: pip install SpeechRecognition)"
    r = sr.Recognizer()
    r.energy_threshold = 300
    r.dynamic_energy_threshold = True
    try:
        with sr.AudioFile(audio_path) as source:
            audio_data = r.record(source)
        # en-IN accurately understands Indian English accents and short robot keywords
        text = r.recognize_google(audio_data, language="en-IN")
        return text.strip().lower(), "OK"
    except sr.UnknownValueError:
        return "", "No speech detected"
    except sr.RequestError as e:
        return None, f"Network/API error: {e}"
    except Exception as e:
        return None, f"Error: {e}"

def transcribe_with_whisper(audio_path):
    """Runs local Whisper offline on Snapdragon CPU"""
    cmd = f'"{WHISPER_BIN}" -m "{WHISPER_MODEL}" -f "{audio_path}" -t 4 -nt -l en'
    res = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    raw = res.stdout.strip()
    cleaned = raw.replace("[BLANK_AUDIO]", "").replace("[Music]", "").replace("[music]", "").strip().lower()
    for noise in ["*sad music*", "*music*", "*boom*", "sad music", "music", "boom"]:
        cleaned = cleaned.replace(noise, "")
    cleaned = cleaned.translate(str.maketrans("", "", string.punctuation))
    return " ".join(cleaned.split()), raw

def process_voice_transcription(audio_path, source="ESP32"):
    """Decodes voice using chosen engine (Google STT or Whisper) and dispatches to ESP32"""
    global active_engine
    text = ""
    engine_used = ""

    # ── 1. Try Google Speech Recognition ──
    if active_engine in ["1", "3"]: # Google or Hybrid
        if HAS_GOOGLE_SR:
            g_text, g_status = transcribe_with_google(audio_path)
            if g_text is not None:
                text = g_text
                engine_used = "Google STT (en-IN)"
                print(f"[{source}] 🌐 Google Speech Output: \"{text}\"")
            else:
                print(f"[{source}] ⚠️ Google STT unavailable ({g_status}), falling back to Whisper...")

    # ── 2. Fallback to Local Whisper if Google wasn't used or failed ──
    if not text and (active_engine == "2" or not engine_used):
        w_text, raw = transcribe_with_whisper(audio_path)
        text = w_text
        engine_used = "Whisper Offline"
        print(f"[{source}] 📴 Whisper Output: \"{raw}\"")

    # Clean punctuation and whitespace
    text = text.translate(str.maketrans("", "", string.punctuation)).strip().lower()
    print(f"[{source}] 🗣️ [{engine_used}] Heard: \"{text}\"")

    if text in ["music", "none", ""]:
        print(f"🔇 Ignored background noise token: \"{text}\"")
        return text, None

    # Match in command dictionary
    matched = None
    for kw in sorted(COMMAND_MAP.keys(), key=len, reverse=True):
        if kw in text:
            matched = COMMAND_MAP[kw]
            break

    if matched:
        if matched == "wake":
            print(f"✨ [WAKE DETECTED from {source}] Moving to Wake Position!")
        else:
            print(f"🎯 [COMMAND DETECTED from {source}] Action [{matched}] -> Executing & Resetting 20s Timer!")
        dispatch_to_esp32(matched)
    else:
        print(f"❓ [{source}] No robot command matched in: \"{text}\"")

    return text, matched

@app.route('/audio', methods=['POST'])
def handle_audio():
    global esp32_ip, active_mode
    if active_mode == "2":
        # Mode 2 is pure Phone Mic, ignore ESP32 mic audio
        return jsonify({"status": "ignored", "reason": "Phone Mic Mode Active"}), 200

    if request.remote_addr and request.remote_addr != "127.0.0.1":
        esp32_ip = request.remote_addr

    audio_bytes = request.data
    size = len(audio_bytes) if audio_bytes else 0
    print(f"\n📦 [INMP441 AUDIO] Size: {size} bytes from ESP32 ({esp32_ip})")

    if not audio_bytes or size < 1000:
        return jsonify({"status": "error", "message": "Audio too short"}), 400

    temp_path = "temp_esp32.wav"
    with open(temp_path, "wb") as f:
        f.write(audio_bytes)

    text, action = process_voice_transcription(temp_path, source="ESP32 (INMP441)")
    return jsonify({"status": "ok", "heard": text, "action": action})

@app.route('/', methods=['GET'])
def serve_web_studio():
    studio_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web_voice_studio.html")
    if os.path.exists(studio_path):
        with open(studio_path, "r", encoding="utf-8") as f:
            return f.read(), 200, {"Content-Type": "text/html; charset=utf-8"}
    return "<h1>RoboArm Web Voice Studio HTML not found</h1>", 404

@app.route('/cmd', methods=['GET', 'POST'])
def proxy_cmd():
    act = request.args.get('action', '').strip().lower()
    if not act:
        return jsonify({"status": "error", "message": "Missing action"}), 400
    if act in ["shoulder_reverse", "reverse", "pull", "shoulder_pull", "shoulder_bag", "shoulder_pack", "bag"]:
        act = "shoulder_back"
    elif act in ["shoulder_forward", "forward", "push"]:
        act = "shoulder_front"
    elif act in ["up", "pitch_up", "patch_up"]:
        act = "wrist_up"
    elif act in ["down", "pitch_down", "patch_down"]:
        act = "wrist_down"
    
    success = dispatch_to_esp32(act)
    status_code = 200 if success else 500
    msg = f"Executed {act} on ESP32 ({esp32_ip})" if success else f"Cannot reach ESP32 at {esp32_ip}! Check Arduino Serial Monitor for IP."
    return jsonify({"status": "ok" if success else "failed", "message": msg, "action": act, "target_esp32": esp32_ip}), status_code

@app.route('/target_ip', methods=['GET', 'POST'])
def handle_target_ip():
    global esp32_ip
    new_ip = request.args.get('ip')
    if new_ip:
        esp32_ip = new_ip.strip()
        print(f"🎯 Target ESP32 IP updated to: {esp32_ip}")
        return jsonify({"status": "ok", "esp32_ip": esp32_ip})
    return jsonify({"esp32_ip": esp32_ip})

def record_phone_mic(duration=2.5, filename="temp_phone.wav"):
    """Records audio directly from the Android phone microphone using termux-microphone-record"""
    raw_file = "temp_phone_raw.wav"
    for f in [raw_file, filename]:
        if os.path.exists(f):
            try: os.remove(f)
            except: pass

    # Record from phone mic
    rec_cmd = f"termux-microphone-record -f {raw_file} -l {int(duration + 0.5)}"
    subprocess.run(rec_cmd, shell=True, capture_output=True)
    time.sleep(duration + 0.4)
    subprocess.run("termux-microphone-record -q", shell=True, capture_output=True)

    # Convert to 16kHz Mono 16-bit WAV using ffmpeg
    if os.path.exists(raw_file):
        conv_cmd = f"ffmpeg -y -i {raw_file} -ar 16000 -ac 1 -c:a pcm_s16le {filename}"
        res = subprocess.run(conv_cmd, shell=True, capture_output=True)
        if res.returncode == 0 and os.path.exists(filename):
            try: os.remove(raw_file)
            except: pass
            return filename
        else:
            return raw_file
    return None

def phone_mic_loop(prompt_label="PHONE MIC"):
    """Continuous Hands-Free Listening Loop for Phone Microphone (No Enter Key Needed!)"""
    print(f"\n=======================================================")
    print(f"📱 {prompt_label} ACTIVE (100% HANDS-FREE)")
    print(f"🎙️ Continuously listening... Just speak directly into your phone!")
    print(f"👉 Say: 'ROBO', 'SHOULDER FRONT', 'ELBOW UP', 'GRAB', 'OPEN'...")
    print(f"🛑 Press Ctrl+C anytime to stop.")
    print(f"=======================================================\n")

    while True:
        try:
            audio_path = record_phone_mic(duration=2.2)
            if audio_path and os.path.exists(audio_path) and os.path.getsize(audio_path) > 1000:
                text, matched = process_voice_transcription(audio_path, source="Phone Mic")
                if matched:
                    # Brief 0.4s cooldown after moving so robot servo sound doesn't get picked up
                    time.sleep(0.4)
            else:
                time.sleep(0.1)
        except (KeyboardInterrupt, EOFError):
            print("\n👋 Stopping Phone Mic loop.")
            break

if __name__ == '__main__':
    print("="*60)
    print("🤖 ROBO UNIVERSAL VOICE CONTROLLER (Snapdragon 8 Gen 2)")
    print(f"🔧 Whisper Engine:  {WHISPER_BIN}")
    print(f"🌐 Google STT:      {'✅ Installed' if HAS_GOOGLE_SR else '⚠️ Not installed (pip install SpeechRecognition)'}")
    print("="*60)

    # Check CLI flags if any, otherwise default directly to Google STT + Hands-Free Phone Mic
    if "--whisper" in sys.argv:
        active_engine = "2"
    elif "--hybrid" in sys.argv:
        active_engine = "3"
    else:
        active_engine = "1" # Default: Google Speech (en-IN)

    if "--esp32" in sys.argv:
        active_mode = "1"
    elif "--dual" in sys.argv:
        active_mode = "3"
    else:
        active_mode = "2" # Default: Phone Built-in Mic

    for i, arg in enumerate(sys.argv):
        if arg == "--ip" and i + 1 < len(sys.argv):
            esp32_ip = sys.argv[i + 1]

    if "--ip" not in sys.argv:
        def bg_detect():
            global esp32_ip
            discovered = auto_discover_esp32()
            if discovered:
                esp32_ip = discovered
                print(f"✨ Auto-Locked ESP32 IP to: {esp32_ip}")
        threading.Thread(target=bg_detect, daemon=True).start()

    # Start Flask Wi-Fi server in background thread for ESP32
    def run_flask():
        import logging
        log = logging.getLogger('werkzeug')
        log.setLevel(logging.ERROR)
        app.run(host='0.0.0.0', port=5000, debug=False, use_reloader=False)

    flask_thread = threading.Thread(target=run_flask, daemon=True)
    flask_thread.start()
    time.sleep(0.5)

    engine_name = "Google Speech (en-IN)" if active_engine == "1" else ("Whisper Offline" if active_engine == "2" else "Auto Hybrid")
    print(f"🚀 System Online! Engine: [{engine_name}] | Mic: [Phone Built-in Mic]")
    print(f"📡 Background Wi-Fi Server live on port 5000 (ESP32: {esp32_ip})")

    if active_mode == "1":
        print("\n✅ Mode [1] ACTIVE: Listening exclusively via Robot's INMP441 Mic.")
        try:
            while True: time.sleep(1)
        except KeyboardInterrupt:
            print("\n👋 Server stopped.")
    elif active_mode in ["2", "3"]:
        phone_mic_loop(prompt_label=f"PHONE MIC ({engine_name})")

