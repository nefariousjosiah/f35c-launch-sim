# Cameras

Real launch footage is shot by people standing on a moving ship, and most CG launch
footage gives itself away with a perfect, weightless camera. The shot director
(`Source/F35CLaunch/Private/ShotDirector.cpp`) operates every camera the way a person
or a fixed mount would, on simulation time, so a recorded sequence is the same frame
for frame.

## The edit

The cut follows Navy combat-camera B-roll of a launch: an establishing shot, the shot
itself, the jet rushing past, the climb-out. The three set-ups cover nearly all real
launch footage (Docs/Reference.md).

| Cut at (s) | Shot | Lens | Position (deck frame) | Operation |
|---|---|---|---|---|
| start | Island telephoto | 120 mm | 70 m aft, 42 m to starboard, 24 m up ("Vulture's Row") | Slow, steady pan; small shake magnified by the lens |
| −2.5 | Deck handheld | 35 mm | 16 m aft, 26 m to starboard, eye height | Hard pan with lag and overshoot, hand wander, catapult thump |
| +1.9 | Bow, locked off | 24 mm | 1 m from the bow edge, 15 m to starboard, 0.8 m up | Fixed; vibrates when the catapult fires |
| +2.7 | Island telephoto | 120 mm | as the first shot | Follows the climb-out |

Every camera's operator runs all the time, so a cut always lands on a camera that is
already following the jet.

## The operator model

- **Reaction delay.** The operator aims at where the jet was 0.12 s (handheld) to
  0.18 s (telephoto) earlier.
- **Pan and tilt as an under-damped spring.** Natural frequency 0.9 to 1.3 Hz, damping
  ratio 0.65 to 0.8. Combined with the delay, this produces the lag behind a
  3 g acceleration and the small overshoot when the jet slows relative to the pan.
- **Focus pull** that follows the subject with a slight lag (manual focus at f/8).
- **Hand wander:** three octaves of Perlin noise in yaw, pitch and roll. The same
  angular wander is scaled down for a long lens, which still magnifies it.
- **The catapult's thump:** an 11 to 13 Hz rumble that decays over about half a second
  from the moment the launch valve opens, strongest on deck-level cameras.
- **Ship motion:** the deck, the jet and every ship-mounted camera roll (±0.35°, 11 s)
  and pitch (±0.2°, 7.5 s) together about a point below the waterline, so the horizon
  drifts the way it does in real deck footage.

## Lens and exposure

Physical exposure, locked per shot so nothing "breathes": f/8, 1/200 s, ISO 100. A 180°
shutter (motion blur 0.5) with film grain, a touch of chromatic aberration and a
vignette. The cine cameras use a Super 35 sensor cropped to 16:9.

## Recording

`-LaunchRecord=<folder>` steps exactly 1/30 s of simulation per rendered frame and saves
a numbered PNG sequence to `Saved/Captures/<folder>`, holding the timeline while shaders
compile:

```bash
UnrealEditor.exe F35CLaunch.uproject -game -windowed -ResX=1920 -ResY=1080 \
  -LaunchRecord=take1 -LaunchRecordFrom=-6 -LaunchRecordTo=9 -LaunchRecordFPS=30
ffmpeg -framerate 30 -i Saved/Captures/take1/frame_%05d.png -c:v libx264 -crf 18 -pix_fmt yuv420p take1.mp4
```

The simulation, the steam and the camera operators all run on simulation time, so slow
motion is a re-record at a higher rate, not interpolation. The README's quarter-speed
take is 120 simulated frames per second played back at 30. `-LaunchRecordPreRoll`
(default 1.5 s) runs the scene before the first saved frame so the steam has built up:

```bash
UnrealEditor.exe F35CLaunch.uproject -game -windowed -ResX=1920 -ResY=1080 \
  -LaunchRecord=slowmo -LaunchRecordFrom=-0.4 -LaunchRecordTo=2.7 \
  -LaunchRecordFPS=120 -LaunchRecordPreRoll=6
```

`-LaunchCapture=<file.png> -LaunchCaptureTime=<s>` saves a single frame.
