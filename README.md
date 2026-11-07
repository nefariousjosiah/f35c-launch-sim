# F-35C Catapult Launch

A real-time recreation of an F-35C steam-catapult launch from a Nimitz-class carrier,
modelled on the jet's first carrier sea trials aboard USS Nimitz in November 2014.
Built in Unreal Engine 5.5 with C++.

The aim is a shot that passes for real footage. Everything a viewer's eye checks is
taken from published sources: the deck crew's signal sequence, the ~2.5 s, 94 m
catapult stroke, satin grey coating, daylight heat shimmer instead of flame, steam
blowing aft, and handheld cameras exposed for the dark deck. Values that were never
published (peak g, holdback release load, strut rates) are tunable parameters, to be
calibrated against frame-timed Navy footage.

> **Status:** early development. The launch physics is complete and tested; the deck
> kit, aircraft and look-dev are in progress.

## What's here

- **`Source/LaunchSim/`: the launch physics.** Pure, deterministic C++ with no engine
  dependency: a six-degree-of-freedom rigid body at a fixed 1 kHz in the deck frame,
  nonlinear landing-gear struts, launch bar and holdback, a front-loaded 94.4 m
  catapult stroke, aerodynamics with ground effect lost at the bow, and the hands-off
  flight-control rotation. See [Docs/LaunchModel.md](Docs/LaunchModel.md).
- **`SimTests/`: acceptance tests** for the physics, built with CMake and Catch2 and
  run in CI without Unreal.
- **`Source/F35CLaunch/`: the Unreal module** that drives animation, effects, crew and
  cameras from the simulation.
- **`Docs/`:** the [launch model](Docs/LaunchModel.md), [render settings](Docs/RenderSettings.md)
  and [reference material](Docs/Reference.md).

### One simulated launch

| | Simulated | Expected |
|---|---|---|
| Stroke time | 2.58 s | 2.2 to 2.8 s for a 94.4 m stroke |
| End speed, deck-relative | 132 kt | 135 kt commanded |
| Peak longitudinal acceleration | 3.2 g | 3.0 to 4.6 g |
| Holdback release after the valve opens | 0.05 s | under 0.2 s |
| Sink off the bow | 1.7 m | little or none |
| Nose-strut oscillation on the stroke | 6 reversals | present (documented F-35C behaviour) |

## Building

**Unreal project:** Unreal Engine 5.5.4 and Visual Studio 2022 with the MSVC
14.38.33130 toolset. Right-click `F35CLaunch.uproject`, choose *Generate Visual
Studio project files*, then build the `F35CLaunchEditor` target. Third-party art is
not in the repo; see [ASSETS.md](ASSETS.md). Without it, the project still builds
and the simulation runs with placeholders.

**Physics tests only** (any platform, no Unreal needed):

```bash
cmake -S SimTests -B SimTests/build
cmake --build SimTests/build --parallel
ctest --test-dir SimTests/build --output-on-failure
```

## Assets and licensing

Code and original content: [MIT](LICENSE) © 2026 nefariousjosiah. Purchased models
and materials are used under their own licenses and are not redistributed; see
[ASSETS.md](ASSETS.md) and [ATTRIBUTION.md](ATTRIBUTION.md).

This is an independent portfolio project. It is not affiliated with, sponsored by or
endorsed by Lockheed Martin, the U.S. Navy or the U.S. Department of Defense. The
aircraft and ship are depicted factually; no official logos are used.
