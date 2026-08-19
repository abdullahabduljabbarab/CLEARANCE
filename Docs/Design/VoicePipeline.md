# Voice pipeline

The comms system in CLEARANCE handles the whole path from the operator
pressing PTT on the physical console through to the aircraft crew's
spoken readback. It is voice-driven ATC by construction, and the
implementation stays diegetic: every step happens because a real ATC
operator would do it the same way.

This document walks the pipeline stage by stage, then covers how the
services are packaged for a downloadable build.

## Contents

- [What ships end to end](#what-ships-end-to-end)
- [Speech recognition](#speech-recognition)
- [Phraseology parser](#phraseology-parser)
- [Instruction validator](#instruction-validator)
- [Aircraft behaviour and readback](#aircraft-behaviour-and-readback)
- [Speech synthesis and voice routing](#speech-synthesis-and-voice-routing)
- [Transcript and communications roles](#transcript-and-communications-roles)
- [Packaging](#packaging)

## What ships end to end

```
Operator PTT
   |
   v
   microphone -> whisper.cpp STT (local exe) -> text
                                                  |
                                                  v
                                        ClearanceCommsRouter
                                                  |
                                         phraseology parser
                                                  |
                             FAircraftInstruction (structured)
                                                  |
                                       instruction validator
                                                  |
                             accepted   or   rejected + spoken reason
                                                  |
                                    UClearanceAircraftBehaviour
                                                  |
                                     apply + generate readback
                                                  |
                                                  v
                     ClearanceVoiceOutput -> Piper (or Edge TTS)
                                                  |
                                                  v
                          spatialised audio + transcript row
```

Everything after the microphone is server-authoritative. Two federated
CLEARANCE instances share the same instruction pipeline and see the
same readbacks.

## Speech recognition

Local, offline speech recognition via a bundled
[whisper.cpp](https://github.com/ggerganov/whisper.cpp) server. The
game launches `WhisperServer/whisper_server.exe` on session start,
streams microphone audio at 16 kHz mono over a local WebSocket, and
receives text partials and finals.

The server runs as a separate process so that model inference cannot
block the game thread and cannot destabilise Unreal's audio device. On
end-of-utterance the final transcript reaches
`UClearanceCommsRouter::OnTranscriptFinal` on the game thread.

Whisper occasionally mishears distinctive verbs. The parser has a
short list of intent bypasses (see [Phraseology parser](#phraseology-parser))
so a mis-transcription of the leading tokens still routes to the right
handler as long as one anchor verb is present.

## Phraseology parser

`UClearanceCommsRouter` normalises the transcript text (trim,
lowercase, callsign de-elision) and hands it to the phraseology
parser. The parser resolves the callsign from the leading tokens and
then walks the remainder against a set of verb-anchored patterns:

- `heading` / `turn left heading` / `turn right heading` → heading change
- `climb` / `descend` / `maintain` + flight-level or altitude → altitude change
- `speed` / `reduce speed` / `increase speed` → speed change
- `cleared` + approach type + runway → approach clearance
- `contact` + facility + frequency → handoff
- `squawk` + code → transponder set
- `engage` + target callsign → SAM engagement bypass
- `say again`, `roger`, `wilco` → acknowledgement pass-through

Success produces an `FAircraftInstruction` with the callsign, verb,
and payload (angle, altitude, speed, runway, etc.). Failure produces
a structured "unable to parse" reply that the router speaks back with
a request to say again.

The parser is deliberately regex + token walk rather than a full
grammar. Real ATC phraseology is constrained enough that a small
verb-anchored parser catches the majority of real-world utterances,
and it is straightforward to inspect and extend.

The complete grammar is maintained in `Docs/PHRASEOLOGY.md` in the
development workspace and referenced by the parser's leading comment
so a future maintainer can extend both in lockstep.

## Instruction validator

`UClearanceInstructionValidator::Validate` accepts or rejects an
instruction against the current aircraft state before it reaches
behaviour. Validation is stateless; the validator reads
`FAircraftState`, applies performance and safety rules, and returns
an accept or a structured reject reason.

Rejects include:

- Altitude above service ceiling for the aircraft category.
- Speed below stall margin at current weight and altitude.
- Turn command through a hostile zone or restricted airspace.
- Non-finite or malformed payload.
- Approach clearance for a runway the aircraft cannot make from
  current position and configuration.

Rejected instructions produce a spoken "unable" response with the
specific reason ("unable, above service ceiling", "unable, would take
us through prohibited area November") rather than silently failing.
The operator hears why the instruction did not take.

The validator is covered by ten automation tests
(`ClearanceInstructionValidatorTests.cpp`) locking the accept path,
invalid callsign rejection, envelope violations, military envelope
bypass, non-finite rejection, and go-around bypass paths.

## Aircraft behaviour and readback

Accepted instructions land at `UClearanceAircraftBehaviour::ApplyInstruction`.
Behaviour records the new setpoint in `FAircraftState` and updates the
active clearance so the instructor scope and score report can see what
each aircraft was last told.

The readback is generated from the same instruction, not from
free-form templating. Every accepted instruction produces a canonical
readback in the crew's voice ("British Airways one-zero-one, climb
flight level three-two-zero"). Readbacks are queued through
`ClearanceVoiceOutput` and spoken with the crew's assigned voice, so
that a scenario with mixed civilian and military traffic sounds
different across callsigns.

## Speech synthesis and voice routing

`ClearanceVoiceOutput` is the game-side wrapper for the bundled TTS
server. It queues per-callsign utterances so a burst of readbacks
plays in issue order, and it holds a voice assignment map that keeps
each aircraft's crew voice stable across the session.

The TTS server exposes two backends behind a single HTTP interface:

- **Piper** (local, offline). The default. Ten voices installed with
  the packaged build. Fast, robust, no external network dependency.
- **Edge TTS** (Microsoft's online voices). Fallback when Piper does
  not have a voice with the requested accent or timbre for a scenario
  role. Requires an internet connection at request time; unavailable
  in an offline packaged build.

Voice selection prefers Piper. Edge TTS is opt-in per voice profile
and degrades cleanly to Piper if the request fails.

Playback is spatialised through the Unreal audio engine at the
speaker's diegetic location where meaningful (radio call from an
aircraft plays from the aircraft's position, tower call plays from the
tower monitor, and so on). Player and instructor speech routes to a
non-spatial channel so the local operator always hears their own PTT
cleanly.

## Transcript and communications roles

Every accepted transmission, readback, and system event lands on a
transcript panel in the instructor performance tab and on the operator
station's reference wiki. Rows are coloured by role from
`EClearanceCommsRole`:

- Operator (the trainee)
- Pilot (aircraft transmissions)
- System (automatic sim events: squawk changes, wake advisories)
- Instructor (scripted injects: emergencies declared, threats
  reclassified, scrambles ordered, checkpoints saved)
- Tower, ACC, AWACS, GCI, ATIS, MET (external facilities)

Ten rows makes the transcript scan as a real multi-facility comms log
rather than a single stream. The role split is also what the After
Action Report uses to break the score commentary down: whose call
started each incident, which facility the operator handed to, and
whether an inject was seen and acknowledged.

## Packaging

The voice services are third-party executables bundled inside the
packaged build so that a downloader does not need Python, Piper, or
any other prerequisite installed to hear voice on first launch.

- `WhisperServer/whisper_server.exe` (whisper.cpp compiled with the
  ggml runtime and the tiny.en model baked in).
- `TtsServer/tts_server.exe` (PyInstaller `--onefile` bundle of the
  Python TTS bridge, piper-tts, edge-tts, FastAPI, and every voice
  model).

Both are staged into the packaged build through
`RuntimeDependencies.Add(...)` calls in the plugin `Build.cs`. The
game-side launcher prefers the bundled executable when present and
falls back to `python whisper_server.py` / `python tts_server.py` for
developer iteration, so the source-tree workflow still works during
active development.

The launcher spawns both services off the game thread via an async
task on `ENamedThreads::AnyBackgroundThreadNormalTask`. This matters
in VR: `CreateProc` in the main thread can stall the compositor for
long enough that the headset displays a frame drop while Windows
Defender scans the newly executed binary. Moving the spawn off the
game thread keeps the compositor happy while Defender does its work.
