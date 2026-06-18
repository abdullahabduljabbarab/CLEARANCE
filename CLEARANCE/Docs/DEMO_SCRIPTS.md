# CLEARANCE - Demo Reel Scripts

Seven scenarios, each a 60-second portfolio cut. Each script reads
screenplay-style:

- **YOU** = your spoken push-to-talk transmissions (Left Alt held)
- **PILOT** = the aircraft TTS voice reading back through the radio FX chain
- **AWACS / ACC / TOWER / MET / GCI** = the scenario's scripted voice injects
- **NARRATION** = optional voiceover (record separately, overlay in editing)

Phraseology follows the parser - test any line with `clearance.say <line>` in
the console before going on tape. Numbers can be digits or spoken
(`270` or `two seven zero`); both parse.

Recommended hero reel (3 min): **Baltic Intercept** -> **Hijack Response**
-> **Mass Divert**.

Extended reel (7 min):
1. Baltic Intercept (GCI / sensor fusion)
2. Cold War Probe (multi-bandit GCI)
3. Hijack Response (7500 + SHADOW)
4. Mayday Engine Fire (7700 + priority traffic)
5. NORDO Inbound (7600 + lost-comms autopilot)
6. Mass Divert (weather + fuel cascade)
7. Mixed Ops (restricted airspace planning)

---

## 1. Baltic Intercept (60s)

**Tagline:** *Distributed sensor fusion + GCI doctrine in one operator picture.*

### Timeline

**T+0:00 | NARRATION**
> "Civilian flight Scandinavian two three eight, westbound at flight level
> three three zero through the Baltic corridor. Multi-sensor radar fusion
> active. AWACS overhead."

**T+0:25 | AWACS (auto)**
> "Magic, new contact, east of the sector, in trail of Scandinavian, low
> altitude, no IFF, closing from behind."

**T+0:30** — UNKNOWN01 appears east of SK238 on the scope.

**T+0:33 | YOU**
> "Interrogate unknown zero one."

**T+0:35 | RADIO** — static burst, no IFF response.

**T+0:40 | NARRATION**
> "No IFF, pursuing a civilian airliner. That's enough for a classification."

**T+0:43 | YOU**
> "Declare unknown zero one hostile."

**T+0:46 | SYSTEM** — UNKNOWN01 reclassified, threat tag flips red.

**T+0:50 | YOU**
> "Scramble unknown zero one."

**T+0:52 | SYSTEM** — 3-ship of VIPER fighters spawns at the east boundary.

**T+0:55 | SYSTEM** — Vipers join up; UNKNOWN01 turns outbound (`JOIN-UP
3-ship` banner).

**T+0:58 | NARRATION**
> "Bandit escorted out. Civilian safe. Every layer - the sensor model, the
> phraseology parser, the lead-pursuit intercept, the scoring loop - in C++
> on Unreal."

**T+1:00 | CUT**

### Key visible captures
- `RDR fleet centre:ON placed:N active:N` diagnostic line
- `RDR <cs> [N/M]` confidence tags on tracks
- AWACS voice line + your three voice commands on the audio
- 3-ship spawn animation
- Bandit's outbound turn after join-up

---

## 2. Cold War Probe (60s)

**Tagline:** *Multi-bandit GCI - classify each contact correctly, don't mis-ID the probes.*

### Timeline

**T+0:00 | NARRATION**
> "Three unknown contacts inbound across the ADIZ. One civilian transit
> overhead. Two of the unknowns are probes, one is a real threat. Operator
> doesn't know which is which yet."

**T+0:20 | AWACS (auto)**
> "Magic, multiple unknown contacts north of the ADIZ line, low altitude,
> all squawking dark. Stand by for vectors."

**T+0:30** — UNKNOWN01 spawns NW.
**T+0:45** — UNKNOWN02 spawns N.

**T+0:50 | YOU**
> "Interrogate unknown zero two."

**T+0:52 | RADIO** — static.

**T+1:00** — UNKNOWN03 spawns NE. Three unknowns on scope.

**T+1:05 | AWACS (auto)**
> "Magic, three unknowns established. Two believed probes - typical pattern
> is to turn back at the line. One is pressing south on a steady course.
> Identify and prioritise."

**T+1:10 | NARRATION**
> "Unknown zero two is the only one closing on the civilian. The other two
> are tracking parallel - probes. Don't mis-ID them."

**T+1:15 | YOU**
> "Declare unknown zero two hostile."

**T+1:18 | YOU**
> "Scramble unknown zero two."

**T+1:25** — UNKNOWN01 and 03 break off automatically (scripted breakOff
verb), turning back outbound. No mis-ID penalty.

**T+1:35** — Vipers join up on UNKNOWN02. Civilian safe.

**T+1:40 | CUT**

### Key visible captures
- Three simultaneous unknowns on the scope
- The interrogate static cue audible
- Two probes turning outbound on their own (no scramble called on them)
- One correct declare + scramble
- Final scoreboard with `+intercept` and no `-misID` penalty

---

## 3. Hijack Response (60s)

**Tagline:** *Emergency doctrine without mis-identification - SHADOW intercept on a 7500.*

### Timeline

**T+0:00 | NARRATION**
> "Five aircraft transiting the sector. Speedbird, Lufthansa, Air France,
> Ryanair, KLM. Standard squawks, standard separation."

**T+0:15 | ACC (auto)**
> "Five aircraft transiting the sector, all squawking standard codes.
> Maintain standard separation."

**T+0:45** — BAW472 squawk flips 4721 -> 7500. Emergency tag.

**T+0:50 | ACC (auto)**
> "Speedbird four seven two squawking seven five zero zero. Hijack code.
> Activate doctrine, recommend immediate SHADOW intercept."

**T+0:55 | NARRATION**
> "Seven five zero zero is hijack code. Pilot's under duress. Doctrine is
> SHADOW - military escort, no hostile declaration. Mis-IDing the airliner
> as hostile costs a thousand points."

**T+1:00 | YOU**
> "Shadow speedbird four seven two."

**T+1:03 | SYSTEM** — 3-ship SHADOW flight spawns, vectors onto BAW472.

**T+1:08 | YOU**
> "Air France one one zero nine, turn right heading two seven zero."

**T+1:11 | PILOT (AFR1109)**
> "Right heading two seven zero, Air France one one zero nine."

**T+1:15 | YOU**
> "Ryanair two two four zero, turn left heading one eight zero."

**T+1:18 | PILOT (RYR2240)**
> "Left heading one eight zero, Ryanair two two four zero."

**T+1:25** — SHADOW formation settles on BAW472 and escorts it outbound.

**T+1:30 | NARRATION**
> "Civilian under duress, escorted by friendlies, all other traffic clear.
> Right outcome by doctrine."

**T+1:35 | CUT**

### Key visible captures
- 7500 squawk transition on the readout
- Distinct ACC voice (en-GB-Ryan) vs the earlier AWACS in Baltic
- SHADOW launch with NO red "DECLARED HOSTILE" banner (proves doctrine win)
- Two civilian readbacks (proves phraseology bandwidth)
- Hijack flying with escort, not under fighter pursuit

---

## 4. Mayday Engine Fire (60s)

**Tagline:** *Single-aircraft priority handling under traffic pressure.*

### Timeline

**T+0:00 | NARRATION**
> "Six aircraft transiting the sector at varying altitudes and headings.
> One of them - Speedbird three nine four - is about to lose an engine."

**T+0:25** — BAW394 squawk flips 1394 -> 7700. Emergency tag.

**T+0:28 | ACC (auto)**
> "Speedbird three nine four declaring Mayday, engine fire, smoke in
> cockpit, requesting immediate landing. Clear all traffic from his approach
> corridor."

**T+0:33 | NARRATION**
> "Mayday gets absolute priority. Every other contact gets a heading change.
> The emergency goes straight to the runway."

**T+0:38 | YOU**
> "Lufthansa eight one two, turn right heading three six zero."

**T+0:41 | PILOT (DLH812)**
> "Right heading three six zero, Lufthansa eight one two."

**T+0:44 | YOU**
> "Easyjet six seven zero, turn left heading zero nine zero."

**T+0:47 | PILOT (EZY670)**
> "Left heading zero nine zero, Easyjet six seven zero."

**T+0:50 | YOU**
> "Air France two two one, climb flight level two eight zero."

**T+0:53 | PILOT (AFR221)**
> "Climb flight level two eight zero, Air France two two one."

**T+0:56 | YOU**
> "Speedbird three nine four, cleared ILS approach."

**T+0:59 | PILOT (BAW394)** *(panic voice, radio FX)*
> "Cleared ILS approach, Speedbird three nine four. Confirm fire crew on
> standby."

**T+1:03 | NARRATION**
> "Mayday flying the localiser, traffic clear, no separation breakdowns.
> Real-time priority handling under load."

**T+1:05 | CUT**

### Key visible captures
- Squawk 1394 -> 7700 transition
- Pilot panic voice (different from synthesized ACC) on the audio
- Four heading changes in 30 seconds (proves voice throughput)
- Clear approach corridor visible on the scope
- Score: +emergency-handled at touchdown

---

## 5. NORDO Inbound (60s)

**Tagline:** *Two aircraft go radio-out simultaneously - the system flies them home; the operator manages everyone else.*

### Timeline

**T+0:00 | NARRATION**
> "Six aircraft in the sector. Two of them are about to lose their radios.
> The published lost-comms procedure will fly them home autonomously - the
> operator's job is everyone else."

**T+0:30** — BAW118 squawks 7600. Lost-comms autopilot engages (turns for
runway, descends to 3000).

**T+0:33 | ACC (auto)**
> "Speedbird one one eight squawking seven six zero zero. Comms failure. He
> will fly the published lost comms procedure to the active runway."

**T+0:40 | NARRATION**
> "Can't talk to him. Vector everyone else clear of his track."

**T+0:45 | YOU**
> "Air France seven seven six, turn right heading one eight zero."

**T+0:48 | PILOT (AFR776)**
> "Right heading one eight zero, Air France seven seven six."

**T+0:55 | YOU**
> "KLM five zero nine, descend flight level one two zero."

**T+0:58 | PILOT (KLM509)**
> "Descend flight level one two zero, KLM five zero nine."

**T+1:00** — DLH445 also squawks 7600. Second NORDO active.

**T+1:04 | ACC (auto)**
> "Lufthansa four four five also squawking seven six zero zero. Second
> NORDO inbound. Keep the other traffic clear of both tracks."

**T+1:10 | YOU**
> "Ryanair three two zero, turn left heading two seven zero."

**T+1:13 | PILOT (RYR320)**
> "Left heading two seven zero, Ryanair three two zero."

**T+1:18 | YOU**
> "Easyjet eight eight two, turn right heading zero four five."

**T+1:21 | PILOT (EZY882)**
> "Right heading zero four five, Easyjet eight eight two."

**T+1:25 | NARRATION**
> "Both NORDOs autonomously flying the approach. Four talking aircraft
> manually routed clear. Lost-comms doctrine end-to-end in software."

**T+1:30 | CUT** (cut from 90s real take to 60s with hard cuts)

### Key visible captures
- TWO emergency squawks flipping within 30 seconds
- NORDOs visibly turning for the runway WITHOUT any voice command
- Player's four vectors on the talking aircraft
- ILS capture on the NORDOs (autopilot proof)

---

## 6. Mass Divert (60s)

**Tagline:** *Six inbound aircraft, runway closed, five minutes to divert. Pure controller tempo.*

### Timeline

**T+0:00 | NARRATION**
> "Six aircraft inbound to the airport. Wind two five zero at fourteen,
> active runway two seven zero. Standard day. About to become a very
> non-standard day."

**T+0:15 | MET (auto)**
> "Weather advisory. Severe storm cell tracking toward the field. Wind
> shear and crosswind expected to exceed runway limits within ninety
> seconds."

**T+0:30** — Wind flips 250 -> 090 at 38kts. Crosswind beyond runway limits.

**T+0:35 | TOWER (auto)**
> "All traffic, the runway is closed due to severe crosswind. Divert all
> inbound aircraft to alternate. Repeat, runway closed, divert to alternate."

**T+0:40 | NARRATION**
> "Five minutes before fuel reserves cascade. Every aircraft has to be out
> of the sector by then."

**T+0:43 | YOU**
> "Speedbird one two three, divert."

**T+0:46 | PILOT (BAW123)**
> "Diverting, Speedbird one two three."

**T+0:49 | YOU**
> "Lufthansa four five six, divert."

**T+0:52 | PILOT (DLH456)**
> "Diverting, Lufthansa four five six."

**T+0:55 | YOU**
> "Air France seven eight nine, divert."

**T+0:58 | PILOT (AFR789)**
> "Diverting, Air France seven eight nine."

**T+1:01 | YOU**
> "KLM two three four, divert."

**T+1:04 | YOU**
> "Ryanair five six seven, divert."

**T+1:07 | YOU**
> "Easyjet eight nine zero, divert."

**T+1:10 | SYSTEM** — `SCENARIO STOPPED: Mass Divert after Ns (X/Y events)`

**T+1:13 | NARRATION**
> "Six aircraft diverted, zero fuel emergencies. Pure controller tempo on
> the same scenario engine as everything before it."

**T+1:15 | CUT** (cut to 60 by tightening the divert sequence)

### Key visible captures
- Six contacts on screen at once (cognitive load)
- Wind indicator flipping 250 -> 090
- Six rapid divert commands + six readbacks
- Live `SCENARIO T+MM:SS events N/M` ticking up
- Scenario "completed" banner

---

## 7. Mixed Ops (60s)

**Tagline:** *Eight civilians, multiple restricted zones, zero hostile activity. Pure spatial planning.*

### Timeline

**T+0:00 | NARRATION**
> "Eight civilian aircraft transiting a continental sector at cruise.
> Multiple restricted areas active - military training zones, protected
> sites. No emergencies, no hostiles. Just airspace."

**T+0:15 | ACC (auto)**
> "Active restricted areas published in the sector. Eight civilian
> transits. Maintain standard separation and route around all P areas."

**T+0:20 | NARRATION**
> "Each aircraft has to thread between the zones. A bust scores minus a
> hundred and fifty. Eight aircraft, multiple zones, zero margin."

**T+0:25 | YOU**
> "Speedbird seven zero one, turn left heading two five zero."

**T+0:28 | PILOT (BAW701)**
> "Left heading two five zero, Speedbird seven zero one."

**T+0:33 | YOU**
> "Lufthansa two zero five, turn right heading one zero zero."

**T+0:36 | PILOT (DLH205)**
> "Right heading one zero zero, Lufthansa two zero five."

**T+0:42 | YOU**
> "KLM four four two, turn left heading zero two zero."

**T+0:45 | PILOT (KLM442)**
> "Left heading zero two zero, KLM four four two."

**T+0:50 | YOU**
> "Ryanair nine one eight, turn right heading two zero zero."

**T+0:53 | PILOT (RYR918)**
> "Right heading two zero zero, Ryanair nine one eight."

**T+0:58 | YOU**
> "Vueling eight one two, turn left heading two nine zero."

**T+1:01 | PILOT (VLG812)**
> "Left heading two nine zero, Vueling eight one two."

**T+1:06 | NARRATION**
> "Eight aircraft, multiple obstacles, zero busts. Defence-grade spatial
> discipline."

**T+1:10 | CUT**

### Key visible captures
- Eight contacts plus the drawn restricted-zone rings
- Sustained voice command rate (~5 vectors in 60s)
- No emergency banners, no scramble banners - just controller technique
- Score at end: zero `-busted` penalties

---

## Production notes

**Voice setup:** push-to-talk = Left Alt. Mic gain matched to AWACS / ACC /
TOWER / MET levels.

**Camera:** lock to top-down sector view. Free-cam reads as a game; top-down
reads as a system.

**Sim scale:** default is 10x. For recording drop to 4-5x via the controller
property `SimulationTimeScale` so the on-screen tempo is readable.

**Audio bed:** no music. The radio chatter IS the soundtrack.

**README placement:** embed the three hero videos (or all seven, with the
hero set as the top of the page) above the architecture diagrams. First
thing a defence recruiter sees is operational footage, not class diagrams.

**Pre-take check:** run `clearance.say <line>` for every line in the script
to confirm the parser accepts it. If a readback comes back "say again," the
line isn't in the parser yet - fix the parser, don't fudge the script.

---

*Living document. Refresh as new action verbs land (EW jamming, MIL-STD-
2525C symbology, instructor live-injects) - demos should show the new
capability.*
