"""
CLEARANCE TTS service - speaks the pilot side of every radio exchange.

A small FastAPI process that wraps Piper TTS and exposes one endpoint:

    POST /speak  { "text": "...", "voice": "en_GB" }  ->  audio/wav

The Unreal sim launches this on session start (same pattern as WhisperServer)
and calls it after every Phraseology readback. Per-airline voices come from
the voice tag - BAW gets en_GB, DLH gets de_DE, etc.

Piper was picked over edge-tts so the whole pipeline stays offline (a
defence-sim hard requirement) and over Windows SAPI so the voices don't sound
like Stephen Hawking.

Run:
    python -m pip install -r requirements.txt
    # download voice models into ./voices/ (see README)
    python tts_server.py --port 8123
"""

from __future__ import annotations

import argparse
import asyncio
import io
import logging
import os
import sys
import wave
from pathlib import Path
from typing import Dict, Optional

try:
    from piper import PiperVoice
except ImportError:
    print("piper-tts not installed. Run: pip install -r requirements.txt", file=sys.stderr)
    raise

# Edge backend (Microsoft Edge neural voices, free, online). Optional - the
# server still runs if these aren't installed, you just can't pick Edge voices.
try:
    import edge_tts
    from imageio_ffmpeg import get_ffmpeg_exe
    FFMPEG_EXE = get_ffmpeg_exe()
    EDGE_AVAILABLE = True
except ImportError:
    EDGE_AVAILABLE = False
    FFMPEG_EXE = None

from fastapi import FastAPI, HTTPException
from fastapi.responses import Response
from pydantic import BaseModel
import uvicorn


# Map of voice tag -> filename stem in ./voices/ . Tags are short codes the
# Unreal side knows about (BAW maps to en_GB, etc). Anything missing falls
# back to "default". - TripleA
VOICE_FILES: Dict[str, str] = {
    "en_GB": "en_GB-cori-high",
    "en_US": "en_US-lessac-high",
}


VOICES_DIR = Path(__file__).parent / "voices"
_LOADED: Dict[str, PiperVoice] = {}


def load_voice(tag: str) -> PiperVoice:
    """Lazy-load a voice model. Cached after first use."""
    if tag in _LOADED:
        return _LOADED[tag]
    stem = VOICE_FILES.get(tag) or VOICE_FILES.get("en_US")
    if not stem:
        raise HTTPException(status_code=400, detail=f"No voice for tag {tag!r}")
    model_path = VOICES_DIR / f"{stem}.onnx"
    config_path = VOICES_DIR / f"{stem}.onnx.json"
    if not model_path.exists() or not config_path.exists():
        raise HTTPException(
            status_code=500,
            detail=(
                f"Voice model {stem!r} not found. Expected:\n"
                f"  {model_path}\n  {config_path}\n"
                "See TtsServer/README.md for download links."
            ),
        )
    voice = PiperVoice.load(str(model_path), config_path=str(config_path))
    _LOADED[tag] = voice
    logging.info("loaded voice %s from %s", tag, model_path)
    return voice


def synth_wav(voice: PiperVoice, text: str) -> bytes:
    """Run synthesis and return a complete WAV byte blob (header + PCM).

    Piper's Python API has shifted between minor versions - handle whichever
    one is actually installed:
      * 1.0/1.1: voice.synthesize(text, wav)               - writes to wav
      * 1.2+:    voice.synthesize_wav(text, wav)           - explicit name
      * recent:  for chunk in voice.synthesize(text): ...  - iterator
    """
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wav:
        # Try the explicit synthesize_wav first (newer, unambiguous).
        if hasattr(voice, "synthesize_wav"):
            try:
                voice.synthesize_wav(text, wav)
                return buf.getvalue()
            except Exception as e:
                logging.debug("synthesize_wav failed (%s), trying fallback", e)

        # Old-style: synthesize(text, wav) writes frames into the wave file.
        try:
            result = voice.synthesize(text, wav)
            # If it returned something it's probably an iterator, drain it.
            if result is not None and not isinstance(result, (bytes, bytearray)):
                try:
                    for chunk in result:
                        data = getattr(chunk, "audio_int16_bytes", None)
                        if data is None and isinstance(chunk, (bytes, bytearray)):
                            data = chunk
                        if data:
                            wav.writeframes(data)
                except TypeError:
                    pass
            return buf.getvalue()
        except TypeError:
            pass

        # Iterator-style: voice.synthesize(text) yields audio chunks.
        sr = getattr(getattr(voice, "config", None), "sample_rate", 22050)
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sr)
        for chunk in voice.synthesize(text):
            data = getattr(chunk, "audio_int16_bytes", None)
            if data is None and isinstance(chunk, (bytes, bytearray)):
                data = chunk
            if data:
                wav.writeframes(data)

    return buf.getvalue()


def is_edge_voice(tag: str) -> bool:
    """Edge voice names look like 'en-GB-RyanNeural' - language-region-NameNeural,
    distinguishable from Piper's 'en_GB' / 'en_US' style by the hyphens."""
    return bool(tag) and tag.count("-") >= 2 and "_" not in tag


async def synth_edge(text: str, voice: str) -> bytes:
    """Hit Microsoft Edge's neural TTS endpoint and return a WAV blob. edge-tts
    returns MP3 frames; we pipe them through the bundled ffmpeg directly to get
    16-bit mono PCM in a WAV the UE side can decode. Skipping pydub avoids its
    ffprobe dependency - imageio-ffmpeg only bundles ffmpeg. - TripleA"""
    if not EDGE_AVAILABLE:
        raise HTTPException(
            status_code=500,
            detail="edge-tts / imageio-ffmpeg not installed. Run pip install -r requirements.txt",
        )
    comm = edge_tts.Communicate(text, voice)
    mp3 = bytearray()
    async for chunk in comm.stream():
        if chunk.get("type") == "audio":
            mp3.extend(chunk["data"])
    if not mp3:
        raise HTTPException(status_code=502, detail="edge-tts returned no audio")

    # Direct ffmpeg pipe: stdin=mp3, stdout=RAW PCM. We then wrap that PCM in a
    # WAV header ourselves with the correct sizes - ffmpeg piped WAV writes
    # 0xFFFFFFFF for the size fields (can't seek a pipe) which the UE WAV
    # parser rejects. - TripleA
    import struct
    import subprocess
    sample_rate = 22050
    channels = 1
    bits = 16
    cmd = [
        FFMPEG_EXE,
        "-loglevel", "error",
        "-f", "mp3", "-i", "pipe:0",
        "-f", "s16le", "-acodec", "pcm_s16le",
        "-ac", str(channels), "-ar", str(sample_rate),
        "pipe:1",
    ]
    proc = await asyncio.to_thread(
        subprocess.run, cmd, input=bytes(mp3), capture_output=True
    )
    if proc.returncode != 0:
        raise HTTPException(status_code=500, detail=f"ffmpeg failed: {proc.stderr.decode(errors='ignore')[:300]}")

    pcm = proc.stdout
    if not pcm:
        raise HTTPException(status_code=500, detail="ffmpeg produced no audio")

    data_len = len(pcm)
    byte_rate = sample_rate * channels * (bits // 8)
    block_align = channels * (bits // 8)
    header = (
        b"RIFF" + struct.pack("<I", 36 + data_len) + b"WAVE"
        + b"fmt " + struct.pack("<IHHIIHH", 16, 1, channels, sample_rate, byte_rate, block_align, bits)
        + b"data" + struct.pack("<I", data_len)
    )
    return header + pcm


class SpeakRequest(BaseModel):
    text: str
    voice: Optional[str] = "en_US"


app = FastAPI(title="CLEARANCE TTS")


@app.get("/health")
def health():
    return {
        "ok": True,
        "piper_voices": list(VOICE_FILES.keys()),
        "edge_available": EDGE_AVAILABLE,
    }


@app.post("/speak")
async def speak(req: SpeakRequest):
    text = (req.text or "").strip()
    if not text:
        raise HTTPException(status_code=400, detail="empty text")
    try:
        if is_edge_voice(req.voice or ""):
            wav = await synth_edge(text, req.voice)
        else:
            voice = load_voice(req.voice or "en_US")
            wav = synth_wav(voice, text)
        return Response(content=wav, media_type="audio/wav")
    except HTTPException:
        raise
    except Exception as e:
        logging.exception("synthesis failed: text=%r voice=%r", text, req.voice)
        raise HTTPException(status_code=500, detail=str(e))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8123)
    parser.add_argument("--log-level", default="info")
    args = parser.parse_args()

    logging.basicConfig(
        level=args.log_level.upper(),
        format="%(asctime)s [%(levelname)s] %(message)s",
    )

    if not VOICES_DIR.exists():
        VOICES_DIR.mkdir(parents=True)
        logging.warning("created voices dir at %s - download models there", VOICES_DIR)

    # Eagerly load the default voice so the first /speak isn't slow.
    try:
        load_voice("en_US")
    except HTTPException as e:
        logging.warning("default voice not loaded yet: %s", e.detail)

    uvicorn.run(app, host=args.host, port=args.port, log_level=args.log_level)


if __name__ == "__main__":
    main()
