# CLEARANCE TtsServer

Local Piper TTS HTTP service. The sim launches this on session start and calls
it for every pilot readback. Per-airline voices give each callsign a different
sound (Speedbird British, Lufthansa German, etc.).

## Setup

1. Make sure Python 3.10+ is installed and on PATH.
2. Install dependencies:
   ```
   python -m pip install -r requirements.txt
   ```
3. Download voice models into `./voices/`. Recommended set:
   - `en_GB-northern_english_male-medium` (British male)
   - `en_US-libritts-high` (US, multi-speaker, high quality)
   - `de_DE-thorsten-medium` (German male)
   - `fr_FR-siwis-medium` (French female)

   Voices are on Hugging Face under `rhasspy/piper-voices`. Each voice is two
   files: `<name>.onnx` and `<name>.onnx.json`. Put both in `./voices/`.

   Direct links (each pair is ~30-60 MB):
   - https://huggingface.co/rhasspy/piper-voices/tree/main/en/en_GB/northern_english_male/medium
   - https://huggingface.co/rhasspy/piper-voices/tree/main/en/en_US/libritts/high
   - https://huggingface.co/rhasspy/piper-voices/tree/main/de/de_DE/thorsten/medium
   - https://huggingface.co/rhasspy/piper-voices/tree/main/fr/fr_FR/siwis/medium

## Run

```
python tts_server.py --port 8123
```

Health check:
```
curl http://127.0.0.1:8123/health
```

Test synthesis (writes hello.wav):
```
curl -X POST http://127.0.0.1:8123/speak ^
  -H "Content-Type: application/json" ^
  -d "{\"text\":\"Speedbird one zero one, heading two seven zero.\",\"voice\":\"en_GB\"}" ^
  -o hello.wav
```

## Voice mapping

The Unreal side picks a voice tag based on the callsign prefix:

| Callsign | Voice tag | Style              |
|----------|-----------|--------------------|
| BAW      | en_GB     | British (Speedbird)|
| DLH      | de_DE     | German (Lufthansa) |
| AFR      | fr_FR     | French (Air France)|
| AAL      | en_US     | US (American)      |
| UAL      | en_US     | US (United)        |
| UAE      | en_GB     | British-ish        |
| VIPER    | en_US     | US military        |
| (other)  | en_US     | US fallback        |

Missing models fall back to `en_US`. Missing `en_US` returns 500 with an
explanation - download at least that one before the first session.
