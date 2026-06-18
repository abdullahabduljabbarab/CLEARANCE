# CLEARANCE — Voice/Text Phraseology Reference

What the phraseology parser (`UClearancePhraseology::Interpret`) currently
understands. Test it with the `clearance.say <transmission>` console command.
This is the layer voice input (Whisper) will feed once the parser is solid.

## Transmission shape
`<callsign> <one or more clearances>` — e.g.
`speedbird 101 descend flight level 100 turn right heading 270 reduce speed 210`

## Callsign (comes first)
- Telephony name + number: `speedbird 101`, `lufthansa 102`, `united 103`,
  `american 104`, `emirates 105`, `air france 106`
- ICAO code + number: `baw101`, `baw 101`, `dlh 102`
- Number as numerals (`101`) or spoken digits (`one zero one`). Matched against
  live aircraft, so it's forgiving.

## Clearances
| Type | Examples |
|------|----------|
| Heading (shortest) | `heading 270`, `fly heading two seven zero` |
| Heading (turn direction honoured) | `turn left heading 180`, `turn right heading 090` |
| Relative turn | `turn left 30 degrees`, `turn right 20` |
| Climb/descend to flight level | `climb flight level 350`, `descend flight level 100` |
| Climb/descend to altitude | `descend altitude 8000`, `descend 5000`, `climb to one zero thousand` |
| Expedite (altitude) | add `expedite` anywhere: `expedite descend flight level 80` |
| Speed | `speed 210`, `reduce speed 180`, `increase speed two five zero` |
| Go around | `go around` |
| Cleared approach | `cleared approach`, `cleared ils`, `cleared to land` |
| Cleared takeoff | `cleared for takeoff` |
| Handoff / leave sector | `contact tower ...`, `leave the sector` |

Multiple clearances in one transmission are fine; you get a combined readback.

## Numbers
- Numerals: `270`, `5000`
- Spoken digits (ATC style): `two seven zero` = 270, incl. `niner`, `tree`,
  `fife`, `fower`, `oh`/`zero`
- Multipliers: `five thousand` = 5000, `two hundred` = 200

## Ignored "flavour" words
`reduce`, `increase`, `maintain`, `fly`, `cleared`, `for`, `the`, `to`, `and` —
so natural phrasing reads fine.

## Readback
The parser echoes what it accepted, e.g.
`BAW101, right heading 270, flight level 100, speed 210`
and flags anything outside the envelope:
`... -- UNABLE flight level 400`

## NOT yet supported (need backend features — see PRODUCTION_LOG)
- Holding patterns (`hold at ...`)
- ILS glidepath / actual landing profile (approach clearance only sets phase
  today) — **next planned feature**
- Direct-to / waypoints / SID / STAR (no navaid system yet)
- Squawk codes (no transponder concept)
- Teen words like `ten thousand` (use `one zero thousand` or `10000`)
