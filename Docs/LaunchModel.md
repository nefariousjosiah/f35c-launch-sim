# Launch model

How the catapult launch is simulated, and where every number comes from. The code
is in `Source/LaunchSim/` (pure C++, no engine dependency) and the parameters are
in [`Params.h`](../Source/LaunchSim/Public/LaunchSim/Params.h), each tagged:

- **[S]** sourced from published material
- **[D]** derived from sourced values by calculation
- **[E]** engineering estimate, plausible but unpublished
- **[T]** to be tuned against frame-timed reference footage

## The event being recreated

The F-35C's first carrier sea trials (Developmental Test I) aboard USS Nimitz off
San Diego. The first arrested landing was on 3 November 2014 (test jet CF-03); the
first catapult launches followed on 4 November. DT-I finished on 14 November with
124 catapult launches and 124 arrested landings. The trials published no launch
speeds or weights, which is why those values are parameters rather than constants.

## Sequence

The order of events is documented; the durations are estimates. Times are relative
to the launch valve opening.

| t (s) | Phase | What happens | Crew signal |
|---|---|---|---|
| −40 | Hookup | Launch bar in the shuttle, holdback fitted, jet blast deflector up | — |
| −35 | Tension | Brakes off, shuttle takes tension; the nose strut squats | Director: palm up, other arm forward |
| −30 | Run-up | Engine to military power (afterburner only on a separate signal) | Shooter: two-finger wave |
| −24 | Wipeout | Full-travel control check: aft, forward, left, right, rudder | — |
| −16 | Final checks | White-shirt checkers watch surfaces, panels, leaks | Thumbs-up to the shooter |
| −10 | Salute | Pilot salutes; right hand to the canopy "towel rack" (hands-off launch) | — |
| −6 | Shooter signal | Shooter returns the salute, touches the deck, points down the track | Touch deck, point forward |
| 0 | Stroke | Deck-edge operator fires; holdback breaks; ~2.5 s stroke | — |
| +2.6 | Over the bow | Launch bar leaves the shuttle; control law rotates the jet | — |
| +5.8 | Climbout | Pilot takes the jet; gear comes up | — |

## Physics

- **Rigid body.** Six degrees of freedom in the deck frame, double precision, RK4 at
  a fixed 1 kHz. The carrier is the reference frame and the wind over deck arrives
  as a steady headwind.
- **Landing gear.** Three struts as nonlinear air-oil springs (linear plus cubic
  term) with square-law orifice damping, more restrictive on rebound, a bump stop,
  and regularised tyre friction. The nose strut is lightly damped, which is what
  produces the documented F-35C nose oscillation on the stroke.
- **Launch bar.** Tow force acts along the bar, from its hinge on the nose gear
  down to the shuttle in the deck slot. Its downward component holds the nose down
  against the nose-up pitch that acceleration produces.
- **Holdback.** A stiff, tension-only link from the nose-gear fitting to the deck.
  It releases when its load exceeds the release load, set above full-afterburner
  thrust plus tension so the jet stays put at run-up.
- **Catapult.** A 94.4 m power stroke (C-13-1). The capacity selector valve picks a
  mean tow force that reaches the commanded end speed net of thrust and drag; the
  force is front-loaded (1.3× mean at the start, 0.7× at the end, as steam expands)
  and ramps up over 0.12 s as the launch valve opens.
- **Aerodynamics.** Linear lift with a cap, parabolic drag, sideslip and damping
  derivatives, with ground effect that disappears abruptly when the jet crosses
  the bow and the surface drops 18 m to the sea.
- **Flight control.** Hands-off: launch trim on the stroke, then a rate-limited
  attitude command to 12° with pitch-rate damping, wings-level hold and a yaw damper.

## Why the stroke takes ~2.5 s, not 2

The often-quoted "about 150 knots in about 2 seconds" does not fit a Nimitz
catapult. At constant acceleration, 150 kt over 94.4 m takes 2.45 s; reaching it in
2.0 s would need a peak near 6.6 g. "150 kt in 2 s" is exactly the constant-
acceleration result for a ~76 m stroke, the length of the shorter C-13-0 and C-13-3.
With a front-loaded force profile and a 135 kt deck-relative end speed, the model
gives a 2.58 s stroke and a 3.2 g peak, inside the 2.2 to 2.8 s window derived for a
94.4 m stroke.

## Tuning

`SimTests/tools/rotation_sweep.cpp` sweeps the control-law rotation settings and
reports sink off the bow, pitch rate and angle of attack. The defaults (launch-flap
CL₀ 0.45, 10°/s rotation) give a 1.7 m settle off the bow, a 10°/s peak pitch rate
and an 11° peak angle of attack.

Still to calibrate against the DVIDS B-roll of the first launch (DVIDS 372940):
stroke time, signal-to-motion delay, and from those the end speed and holdback
release load.

## Key sources

- C-13 stroke and track lengths: Navy training manual NAVEDTRA 14001.
- DT-I dates and totals: U.S. Pacific Fleet news releases, November 2014.
- MIL vs afterburner launches, modified JBDs in DT-II: Code One magazine.
- Hands-off launch and the "towel rack": naval aviator accounts (Fightersweep).
- Nose-gear oscillation, its pilot ratings and the 2017 holdback fix: The War Zone,
  USNI News, Defense News.
- F-35C dimensions and weights: Lockheed Martin fast facts; U.S. Naval Academy.
