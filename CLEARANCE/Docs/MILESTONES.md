# CLEARANCE — Milestone Video Tracker

Portfolio progress videos. Two cadences:
- **Weekly clip** (30-90s) — quick "what's new this week", for discipline + raw
  footage. Don't over-edit; it's material.
- **MILESTONE** videos — the keepers, filmed at capability jumps. These get cut
  into the final demo reel.

Tips: keep raw clips, short, dated, consistent framing. Record high-res
(downscale later, never up). Name like `2026-Wk03_first-flight.mp4`.

## Milestone videos

| ID | Capability shown | Needs | Recorded? |
|----|------------------|-------|-----------|
| M0 | Kickoff — narrate the design docs + architecture intent | nothing (optional baseline) | ⬜ |
| M1 | First flight — aircraft spawn and fly realistically on the debug radar | DONE: full backend + C++ debug overlay running in PIE (2026-05-24) | 🎥 READY TO RECORD |
| M2 | In control — vectoring an aircraft (heading/alt/speed), it responds | DONE via console commands; aircraft banks/pitches/accelerates correctly | ✅ RECORDED 2026-05-25 |
| M3 | Safety net — two aircraft converge, conflict alert fires | Conflict Detector (6) + alert UI | ⬜ |
| M4 | Full loop — traffic, conflicts, go-arounds, scoring all running together | Steps 6-9 integrated | ⬜ |
| M5 | Demo reel — polished radar, a tense scenario, the money shot | UI polish + tuning | ⬜ |

M1 is the first real portfolio asset; everything before it is a bonus/baseline.

| MV | **Voice ATC** — hold-to-talk, speak a clearance, aircraft obeys. Offline (whisper.cpp), self-contained (game auto-launches its own server). | DONE 2026-05-25 | 🎥 RECORD THIS |

## Weekly clips log

| Week | Date | What's new this week | Clip file |
|------|------|----------------------|-----------|
| Wk1 | 2026-05-24 | Full 9-system backend built AND running in PIE: aircraft spawn, fly realistically, conflicts detected, scoring live. Debug radar overlay. | (superseded by M2) |
| Wk1 | 2026-05-25 | M2 recorded: player commands an aircraft (climb/descend, turn+bank, speed up/down) — it responds correctly. First milestone video. | ✅ M2 clip |
| Wk1 | 2026-05-25 | VOICE working end-to-end: push-to-talk -> whisper.cpp -> phraseology parser -> aircraft responds. Offline + self-contained (auto-launched bundled server). | 🎥 record MV |
