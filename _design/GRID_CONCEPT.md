# grid

## spec

- 2 track x 3 "note"

## wants

- [ ] define a euclidean track
- [ ] layer explicit steps on top of euclidean steps
- [ ] condition triggers (percentage chance, every N, fill, not fill)
- [x] variable length track
- [ ] tempo set by grid specification (i.e. 120 bpm)
- [ ] tempo set by ii
- [ ] explicit swing / micro timing
- [ ] variable gate length
- [ ] mute groups
- [ ] per step ratchet
- [ ] live record of pattern?
- [ ] quantize recorded pattern?
- [ ] multiple patterns per track
- [x] base clock output (on out)
- [ ] note priority (in 2 track mode)
- [ ] clock in is reset or manually advance track 2

- [ ] per track random modulation with slewing of rate for rushing/dragging of triggers
- [ ] each track tries to follow the rate of the other (not the primary rate)
- [ ] adjustable frequency/magnitude of rate modulation
- [ ] adjustable follow strength
- [ ] use meter to perturb rate less around quarter notes? (stronger beat)
- [ ] use voice; lower voice get perturbed less?

- [ ] ii leader, send actively held step to tt for use in data entry (in tracker with encoder)
- [ ] ii follower
  - [ ] change pattern
  - [ ] cue pattern
  - [ ] reset
  - [ ] mute voice

- [ ] 4th trigger out (per track)
  - [ ] end of measure
  - [ ] end of pattern
  - [ ] fill state

## edit interaction

- [ ] if one track is shorter than the other show a ghosted version of the
      repeated trigs on other pages
## performance interaction

- [ ] press for fill
- [ ] quantized / unquantized mute of track
- [ ] momentary press to mute, press to play
- [x] reset by grid
- [ ] reset by tr
- [ ] reset by ii
- [ ] end of cycle tr
- [ ] quantized switch of pattern
