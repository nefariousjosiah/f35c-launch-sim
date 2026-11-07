# Render settings

The project keeps two profiles: real time on the development machine (AMD RX 6650 XT,
8 GB, 1440p) and final frames through Movie Render Queue. Look-dev happens in real
time, so the two must agree on everything that affects the look: light levels,
exposure, atmosphere and materials.

## Project defaults (`Config/DefaultEngine.ini`)

| Setting | Value | Why |
|---|---|---|
| RHI | DX12, SM6 | Required for hardware ray tracing and Nanite; several UE 5.5 issues are Vulkan-only |
| Global illumination / reflections | Lumen | Fully dynamic daylight; no baked lighting (`r.AllowStaticLighting=False`) |
| Shadows | Virtual Shadow Maps | Sharp sun shadows on the deck and airframe |
| Hardware ray tracing | Compiled in, off in real time | Software Lumen is faster live; MRQ turns HWRT on |
| Extended luminance range | On | Must be on before any look-dev; switching later changes every exposure value |
| Auto exposure | Off by default | Exposure is set per camera like a real one (see below) |
| Sky atmosphere drives height fog | On | Marine haze takes its colour from the atmosphere |
| Anti-aliasing | TSR | Tuned for RDNA's packed 16-bit maths |
| MSVC toolset | 14.38.33130, pinned in the targets | UE 5.5's preferred compiler |

## Light and exposure

- **Sun:** 100,000 to 120,000 lux, source angle 0.545° (the real sun's diameter).
- **Exposure:** manual with physical camera exposure, EV100 ≈ 15 (sunny 16), with
  Min EV100 = Max EV100 so nothing "breathes". Shooting at f/4 and a 180° shutter
  means emulating an ND filter by lowering ISO.
- **Motion blur:** amount 0.5, a 180° shutter.

The exhaust flame's emissive is authored once against that locked exposure. At
EV 15 it fades to almost nothing, leaving the heat shimmer, as in real daylight
footage; at dusk exposure the same flame reads as orange.

## Profiles

| | Real time | Movie Render Queue |
|---|---|---|
| Lumen | Software ray tracing, scalability High | Hardware ray tracing, hit-lighting reflections, Cinematic |
| Shadows | VSM | VSM, directional LOD bias −1 |
| Resolution | TSR at 67% | Native, AA off, 16 to 32 temporal samples |
| Bloom | Standard, low | Convolution, with lens dirt |
| Output | Viewport | Linear EXR, tone curve off, graded downstream |

## UE 5.5 notes

- Stay on 5.5.4: it fixes water flicker, translucency-volume shadowing and an
  offscreen Movie Render Queue crash.
- Materials with moving textures (heat haze, steam, water) need "Has Pixel
  Animation" or TSR smears them.
- Niagara Fluids (Beta), MegaLights (Experimental) and Substrate (Beta) are not used.
