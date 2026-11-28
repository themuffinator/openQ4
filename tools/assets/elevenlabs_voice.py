#!/usr/bin/env python3
"""Generate openQ4 voice lines with the ElevenLabs API and process them for the game.

The lines, their voices and the voice profiles live in
docs/dev/voice-generation-lines.json; docs/dev/voice-generation-breakdown.md
explains them. Everything this tool writes goes under .tmp/voice/<voice>/ and
nothing is installed: a chosen take is integrated by hand.

  python tools/assets/elevenlabs_voice.py list   --voice valkaryne
  python tools/assets/elevenlabs_voice.py design --voice valkaryne [--seed N] [--previews-only]
  python tools/assets/elevenlabs_voice.py create --voice valkaryne --preview <generated_voice_id> [--name NAME]
  python tools/assets/elevenlabs_voice.py speak  --voice valkaryne [--takes 3] [--only ID ...] [--dry-run]
  python tools/assets/elevenlabs_voice.py process --voice valkaryne [--input DIR] [--only ID ...]

design, create and speak call the API and spend the account's credits; they
read the key from ELEVENLABS_API_KEY, or else from the file
~/.elevenlabs/api_key (ELEVENLABS_API_KEY_FILE names another), and never
print it. --dry-run and
process make no requests. Voice ids are account-specific and are kept in
.tmp/voice/<voice>/voice.json, never in the repository.

Never clone or remix a recording of a real performer: voices come from Voice
Design (docs/dev/voice-generation-breakdown.md, "Decisions before generating").
"""

from __future__ import annotations

import argparse
import base64
import json
import math
import os
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

import numpy as np
from scipy import signal
from scipy.io import wavfile

ROOT = Path(__file__).resolve().parents[2]
LINES = ROOT / "docs" / "dev" / "voice-generation-lines.json"
API = "https://api.elevenlabs.io"
GAME_RATE = 44100           # Awakening voice: 44.1 kHz, 16-bit mono WAV


# ---- the line list -------------------------------------------------------------

def load(voice: str) -> tuple[dict, list[dict]]:
    data = json.loads(LINES.read_text(encoding="utf-8"))
    profile = data.get("voices", {}).get(voice)
    if profile is None:
        sys.exit(f"{LINES.name}: no voice profile '{voice}'")
    lines = [l for l in data["lines"] if l["voice"].split(" ")[0] == voice]
    return profile, lines


def out_dir(voice: str) -> Path:
    path = ROOT / ".tmp" / "voice" / voice
    path.mkdir(parents=True, exist_ok=True)
    return path


def select(lines: list[dict], only: list[str] | None) -> list[dict]:
    if not only:
        return lines
    unknown = set(only) - {l["id"] for l in lines}
    if unknown:
        sys.exit(f"unknown ids: {', '.join(sorted(unknown))}")
    return [l for l in lines if l["id"] in only]


# ---- the API ------------------------------------------------------------------

KEY_FILE = Path.home() / ".elevenlabs" / "api_key"


def api_key() -> str:
    """ELEVENLABS_API_KEY, or a key file outside the repository; never printed."""
    key = os.environ.get("ELEVENLABS_API_KEY")
    if key:
        return key.strip()
    path = Path(os.environ.get("ELEVENLABS_API_KEY_FILE") or KEY_FILE)
    if path.is_file():
        return path.read_text(encoding="utf-8").strip()
    sys.exit(f"no API key: set ELEVENLABS_API_KEY, or put the key alone in {path} "
             "(set it yourself; this tool never asks for it)")


def request(method: str, path: str, body: dict | None = None, query: dict | None = None,
            expect_json: bool = True, attempts: int = 5):
    key = api_key()
    url = API + path
    if query:
        url += "?" + "&".join(f"{k}={v}" for k, v in query.items())
    data = json.dumps(body).encode("utf-8") if body is not None else None
    for attempt in range(attempts):
        req = urllib.request.Request(url, data=data, method=method,
                                     headers={"xi-api-key": key, "Content-Type": "application/json",
                                              "Accept": "application/json" if expect_json else "*/*"})
        try:
            with urllib.request.urlopen(req, timeout=180) as response:
                payload = response.read()
                return json.loads(payload) if expect_json else payload
        except urllib.error.HTTPError as error:
            detail = error.read().decode("utf-8", "replace")[:400]
            if error.code in (409, 429, 500, 502, 503) and attempt + 1 < attempts:
                time.sleep(2 ** attempt * 2)
                continue
            sys.exit(f"{method} {path}: HTTP {error.code}: {detail}")
        except urllib.error.URLError as error:
            if attempt + 1 < attempts:
                time.sleep(2 ** attempt * 2)
                continue
            sys.exit(f"{method} {path}: {error.reason}")
    raise AssertionError("unreachable")


def pcm_to_game(pcm: bytes, rate: int) -> np.ndarray:
    audio = np.frombuffer(pcm, dtype="<i2").astype(np.float64) / 32768.0
    if rate != GAME_RATE:
        g = math.gcd(GAME_RATE, rate)
        audio = signal.resample_poly(audio, GAME_RATE // g, rate // g)
    return audio


def write_wav(path: Path, audio: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    wavfile.write(str(path), GAME_RATE, (np.clip(audio, -1.0, 1.0) * 32767.0).astype(np.int16))


def read_wav(path: Path) -> np.ndarray:
    rate, data = wavfile.read(str(path))
    audio = data.astype(np.float64)
    if data.dtype == np.int16:
        audio /= 32768.0
    if audio.ndim > 1:
        audio = audio.mean(axis=1)
    if rate != GAME_RATE:
        g = math.gcd(GAME_RATE, rate)
        audio = signal.resample_poly(audio, GAME_RATE // g, rate // g)
    return audio


# ---- commands -----------------------------------------------------------------

def cmd_list(args) -> None:
    profile, lines = load(args.voice)
    speech = [l for l in lines if l.get("kind") != "sfx"]
    print(f"{args.voice}: {len(speech)} lines ({sum(len(l['text']) for l in speech)} characters), "
          f"{len(lines) - len(speech)} sound effects")
    for l in lines:
        extra = f" [{l.get('preset')}{', hive' if l.get('hive') else ''}]"
        print(f"  {l['id']:24s} {l['tier']:9s}{extra:16s} {l['text']}")


def cmd_design(args) -> None:
    profile, _ = load(args.voice)
    body = {"voice_description": profile["description"], "text": profile["preview_text"],
            "model_id": profile.get("design_model", "eleven_ttv_v3"),
            "guidance_scale": profile.get("guidance_scale", 5)}
    if args.seed is not None:
        body["seed"] = args.seed
    result = request("POST", "/v1/text-to-voice/design", body)
    folder = out_dir(args.voice) / "design"
    folder.mkdir(exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    record = []
    for i, preview in enumerate(result.get("previews", [])):
        path = folder / f"{stamp}_{i}.mp3"
        path.write_bytes(base64.b64decode(preview["audio_base_64"]))
        record.append({"file": path.name, "generated_voice_id": preview["generated_voice_id"],
                       "duration_secs": preview.get("duration_secs")})
        print(f"preview {i}: {path}  generated_voice_id={preview['generated_voice_id']}")
    log = folder / "previews.json"
    history = json.loads(log.read_text()) if log.exists() else []
    history.append({"time": stamp, "seed": args.seed, "body": {k: v for k, v in body.items() if k != "text"},
                    "previews": record})
    log.write_text(json.dumps(history, indent=1))
    print("Listen, then save one: create --preview <generated_voice_id>")


def cmd_create(args) -> None:
    profile, _ = load(args.voice)
    result = request("POST", "/v1/text-to-voice", {
        "voice_name": args.name or f"openQ4 {args.voice}",
        "voice_description": profile["description"],
        "generated_voice_id": args.preview,
        "labels": {"project": "openQ4", "character": args.voice},
    })
    state = out_dir(args.voice) / "voice.json"
    state.write_text(json.dumps({"voice_id": result["voice_id"], "generated_voice_id": args.preview,
                                 "name": result.get("name")}, indent=1))
    print(f"voice saved: {result['voice_id']} (recorded in {state})")


def cmd_speak(args) -> None:
    profile, lines = load(args.voice)
    lines = select(lines, args.only)
    speech = [l for l in lines if l.get("kind") != "sfx"]
    effects = [l for l in lines if l.get("kind") == "sfx"] if args.sfx else []
    characters = sum(len(l["text"]) for l in speech) * args.takes
    print(f"{len(speech)} lines x {args.takes} takes = {characters} characters"
          + (f", {len(effects)} sound effects x {args.takes}" if effects else "")
          + f"; output {args.format}")
    if args.dry_run:
        for l in speech + effects:
            print(f"  {l['id']}: {l['text']}")
        return
    state = out_dir(args.voice) / "voice.json"
    voice_id = args.voice_id or (json.loads(state.read_text())["voice_id"] if state.exists() else None)
    if speech and not voice_id:
        sys.exit("no voice id: run create, or pass --voice-id")
    rate = int(args.format.split("_")[1])
    dry = out_dir(args.voice) / "dry"
    for l in speech:
        for take in range(args.takes):
            path = dry / f"{l['id']}.take{take}.wav"
            if path.exists() and not args.force:
                continue
            body = {"text": l["text"], "model_id": args.model or profile.get("tts_model", "eleven_v3"),
                    "voice_settings": profile.get("voice_settings", {}), "seed": args.seed + take}
            pcm = request("POST", f"/v1/text-to-speech/{voice_id}", body, {"output_format": args.format},
                          expect_json=False)
            write_wav(path, pcm_to_game(pcm, rate))
            print(f"  {path.relative_to(ROOT)}")
    for l in effects:
        for take in range(args.takes):
            path = dry / f"{l['id']}.take{take}.wav"
            if path.exists() and not args.force:
                continue
            body = {"text": l["text"], "duration_seconds": l.get("duration_seconds"),
                    "loop": bool(l.get("loop")), "prompt_influence": 0.5,
                    "model_id": profile.get("sfx_model", "eleven_text_to_sound_v2")}
            pcm = request("POST", "/v1/sound-generation", body, {"output_format": args.format}, expect_json=False)
            write_wav(path, pcm_to_game(pcm, rate))
            print(f"  {path.relative_to(ROOT)}")


# ---- processing ----------------------------------------------------------------
#
# Presets (docs/dev/voice-generation-breakdown.md, "The Valkaryne re-voiced"):
#   nexus    her global taunts: vocoder-led machine voice, dark 1.3 s space
#   monitor  Cryofac's screens: the same voice through a 300 Hz-3.5 kHz speaker
#   creature non-speech vocalisations: metal and grit without a vocoder
# "hive" lines add detuned, delayed copies: the Strogg speaking through her.

def butter(kind: str, freq, order: int = 4):
    return signal.butter(order, freq, btype=kind, fs=GAME_RATE, output="sos")


def stretch(x: np.ndarray, rate: float, n_fft: int = 2048, hop: int = 512) -> np.ndarray:
    """Phase-vocoder time stretch (rate > 1 shortens)."""
    window = np.hanning(n_fft)
    frames = [np.fft.rfft(window * x[i:i + n_fft]) for i in range(0, max(1, len(x) - n_fft), hop)]
    if len(frames) < 2:
        return x.copy()
    spec = np.array(frames)
    steps = np.arange(0, len(spec) - 1, rate)
    expected = 2 * np.pi * hop * np.arange(spec.shape[1]) / n_fft
    phase = np.angle(spec[0])
    out = np.zeros(int(len(steps) * hop + n_fft))
    for k, t in enumerate(steps):
        i = int(t)
        frac = t - i
        mag = (1 - frac) * np.abs(spec[i]) + frac * np.abs(spec[i + 1])
        frame = np.fft.irfft(mag * np.exp(1j * phase), n_fft)
        out[k * hop:k * hop + n_fft] += window * frame
        delta = np.angle(spec[i + 1]) - np.angle(spec[i]) - expected
        delta -= 2 * np.pi * np.round(delta / (2 * np.pi))
        phase += expected + delta
    return out / (np.sum(window ** 2) / hop)


def pitch_shift(x: np.ndarray, semitones: float) -> np.ndarray:
    factor = 2.0 ** (semitones / 12.0)
    longer = stretch(x, 1.0 / factor)
    shifted = signal.resample(longer, max(1, int(round(len(longer) / factor))))
    return np.pad(shifted, (0, max(0, len(x) - len(shifted))))[:len(x)]


def f0_contour(x: np.ndarray, hop: int = 441, frame: int = 2048) -> np.ndarray:
    """Per-hop fundamental (Hz, 0 when unvoiced), by normalised autocorrelation."""
    lo, hi = int(GAME_RATE / 500), int(GAME_RATE / 70)
    out = []
    for i in range(0, len(x), hop):
        f = x[i:i + frame]
        if len(f) < frame or np.sqrt(np.mean(f ** 2)) < 0.01:
            out.append(0.0)
            continue
        f = f - f.mean()
        ac = np.fft.irfft(np.abs(np.fft.rfft(f, 2 * frame)) ** 2)[:frame]
        ac /= ac[0] + 1e-12
        lag = lo + int(np.argmax(ac[lo:hi]))
        out.append(GAME_RATE / lag if ac[lag] > 0.45 else 0.0)
    return np.array(out)


def carrier(x: np.ndarray, shift: float = -5.0, contour: float = 0.35) -> np.ndarray:
    """A sawtooth chord (root, fifth, octave) that follows a flattened, lowered pitch."""
    hop = 441
    f0 = f0_contour(x, hop)
    voiced = f0[f0 > 0]
    centre = float(np.median(voiced)) if len(voiced) else 180.0
    held, last = np.empty_like(f0), centre
    for i, f in enumerate(f0):
        last = f if f > 0 else last
        held[i] = last
    target = centre * 2 ** (shift / 12) * (held / centre) ** contour
    target = signal.medfilt(target, 5)
    per_sample = np.interp(np.arange(len(x)), np.arange(len(target)) * hop, target)
    out = np.zeros(len(x))
    for ratio, gain in ((1.0, 1.0), (1.5, 0.6), (2.0, 0.45), (1.004, 0.5)):
        phase = np.cumsum(per_sample * ratio / GAME_RATE) % 1.0
        out += gain * (2 * phase - 1)
    noise = np.random.default_rng(7).standard_normal(len(x))
    return out / 2.5 + 0.35 * signal.sosfilt(butter("highpass", 3000), noise)


def vocoder(modulator: np.ndarray, carrier_signal: np.ndarray, bands: int = 28) -> np.ndarray:
    edges = np.geomspace(80, 9000, bands + 1)
    smooth = butter("lowpass", 60, order=2)
    out = np.zeros(len(modulator))
    for low, high in zip(edges[:-1], edges[1:]):
        band = butter("bandpass", [low, high])
        envelope = signal.sosfilt(smooth, np.abs(signal.sosfilt(band, modulator)))
        out += signal.sosfilt(band, carrier_signal) * np.maximum(envelope, 0)
    return out


def comb(x: np.ndarray, delay_ms: float = 0.84, feedback: float = 0.65) -> np.ndarray:
    d = max(1, int(round(delay_ms * GAME_RATE / 1000)))
    a = np.zeros(d + 1)
    a[0], a[d] = 1.0, -feedback
    return signal.lfilter([1.0 - feedback], a, x)


def crush(x: np.ndarray, bits: int = 6, hold: int = 4) -> np.ndarray:
    held = np.repeat(x[::hold], hold)[:len(x)]
    levels = 2 ** (bits - 1)
    return np.round(held * levels) / levels


def space(x: np.ndarray, seconds: float, wet: float, cutoff: float) -> np.ndarray:
    n = int(seconds * GAME_RATE)
    t = np.arange(n) / GAME_RATE
    ir = np.random.default_rng(11).standard_normal(n) * np.exp(-6.9 * t / seconds)
    ir = signal.sosfilt(butter("lowpass", cutoff), ir)
    ir /= np.sqrt(np.sum(ir ** 2)) + 1e-12
    tail = np.zeros(len(x) + n)
    wet_signal = signal.fftconvolve(x, ir)[:len(tail)]
    tail[:len(wet_signal)] = wet_signal
    dry = np.pad(x, (0, n))
    return (1 - wet) * dry + wet * tail


def level(x: np.ndarray, active_db: float = -11.6, ceiling_db: float = -0.5) -> np.ndarray:
    """The game's levels: about -13 dBFS while speaking (the limiter costs ~1.4 dB), peaks under -0.5 dBFS."""
    frames = x[:len(x) // 1024 * 1024].reshape(-1, 1024)
    rms = np.sqrt(np.mean(frames ** 2, axis=1))
    active = rms[rms > rms.max() * 0.1] if len(rms) else np.array([1e-3])
    y = x * 10 ** (active_db / 20) / (np.sqrt(np.mean(active ** 2)) + 1e-12)
    ceiling = 10 ** (ceiling_db / 20)
    return ceiling * np.tanh(y / ceiling)


def trim(x: np.ndarray, threshold: float = 0.003, pad: float = 0.05) -> np.ndarray:
    loud = np.where(np.abs(x) > threshold)[0]
    if not len(loud):
        return x
    p = int(pad * GAME_RATE)
    return x[max(0, loud[0] - p):loud[-1] + p]


def process(x: np.ndarray, preset: str, hive: bool) -> np.ndarray:
    x = trim(x)
    x = x / (np.max(np.abs(x)) + 1e-12)
    if preset == "creature":
        core = pitch_shift(x, -3)
        ring = core * (0.7 + 0.3 * np.sin(2 * np.pi * 45 * np.arange(len(core)) / GAME_RATE))
        y = comb(ring, 0.84, 0.55) + 0.15 * crush(signal.sosfilt(butter("bandpass", [2000, 8000]), ring))
        return level(space(y, 0.6, 0.15, 5000))
    core = pitch_shift(x, -5)
    voc = vocoder(x, carrier(x))
    voc /= np.max(np.abs(voc)) + 1e-12
    sub = pitch_shift(x, -12)
    ring = core * (0.7 + 0.3 * np.sin(2 * np.pi * 45 * np.arange(len(core)) / GAME_RATE))
    y = 0.70 * voc + 0.30 * ring + 0.40 * sub
    if hive:
        for cents, delay_ms in ((15, 12), (-20, 19), (25, 27)):
            copy = pitch_shift(voc, cents / 100)
            d = int(delay_ms * GAME_RATE / 1000)
            y += 0.35 * np.pad(copy, (d, 0))[:len(y)]
    y = comb(y, 0.84, 0.65)
    y = y + 0.12 * crush(signal.sosfilt(butter("bandpass", [2000, 8000]), y))
    if preset == "monitor":
        y = signal.sosfilt(butter("bandpass", [300, 3500]), y)
        y = space(y, 0.3, 0.15, 4000)
    else:
        y = space(y, 1.3, 0.28, 3500)
    return level(y)


def cmd_process(args) -> None:
    _, lines = load(args.voice)
    lines = select(lines, args.only)
    source = Path(args.input) if args.input else out_dir(args.voice) / "dry"
    picks_path = out_dir(args.voice) / "picks.json"
    picks = json.loads(picks_path.read_text()) if picks_path.exists() else {}
    target = out_dir(args.voice) / ("processed" if not args.input else "audition")
    done = 0
    for l in lines:
        candidates = [source / f"{l['id']}.take{picks.get(l['id'], 0)}.wav", source / f"{l['id']}.wav",
                      source / f"{l['id'][3:]}.wav", source / f"{l['id']}.take0.wav"]
        found = next((c for c in candidates if c.exists()), None)
        if found is None:
            continue
        audio = process(read_wav(found), l.get("preset", "nexus"), bool(l.get("hive")))
        path = target / f"{l['id']}.wav"
        write_wav(path, audio)
        done += 1
        print(f"  {found.name} -> {path.relative_to(ROOT)} ({len(audio) / GAME_RATE:.2f} s)")
    print(f"{done} processed into {target.relative_to(ROOT)}"
          + ("" if done else f" (no takes in {source}; picks.json maps an id to its chosen take)"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("list", "design", "create", "speak", "process"):
        p = sub.add_parser(name)
        p.add_argument("--voice", default="valkaryne")
        if name == "design":
            p.add_argument("--seed", type=int)
        if name == "create":
            p.add_argument("--preview", required=True, help="a generated_voice_id from design")
            p.add_argument("--name")
        if name in ("speak", "process"):
            p.add_argument("--only", nargs="+")
        if name == "speak":
            p.add_argument("--takes", type=int, default=3)
            p.add_argument("--seed", type=int, default=1000)
            p.add_argument("--model")
            p.add_argument("--voice-id")
            p.add_argument("--format", default="pcm_24000",
                           help="pcm_24000 works on every plan; pcm_44100 needs Pro")
            p.add_argument("--sfx", action="store_true", help="also generate the optional vocalisations")
            p.add_argument("--force", action="store_true")
            p.add_argument("--dry-run", action="store_true")
        if name == "process":
            p.add_argument("--input", help="a folder of <id>.wav or <id>.takeN.wav files (local auditions)")
    args = parser.parse_args()
    {"list": cmd_list, "design": cmd_design, "create": cmd_create, "speak": cmd_speak,
     "process": cmd_process}[args.command](args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
