# Copyright (c) 2026 nefariousjosiah. MIT License.
"""Procedural textures for the flight deck and effects.

Runs inside Blender's Python (for numpy, without extra installs):

  blender -b --factory-startup --python Tools/Textures/generate_textures.py -- <out_dir>

Everything is generated from fixed seeds, so the output is reproducible and is our
own work. Tileable textures are built in the frequency domain, which makes them
wrap seamlessly.

  T_NonSkid_N     1024^2  normal map, tiles every 1 m: aluminium-oxide grit (~1 mm/px)
  T_NonSkid_D     1024^2  detail (R mottling, G roughness variation, B grit height), tiles every 4 m
  T_DeckMask      4096x1024 over the 161 x 40 m hero deck:
                  R rubber, G oil/hydraulic stains, B traffic wear, A exhaust heat
  T_HazeNoise_N   512^2   tileable normal map for heat-haze refraction
  T_SteamPuff     256^2   soft puff sprite (alpha) for catapult steam
"""

import os
import sys

import bpy
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(argv[0] if argv else "SourceArt/Generated")
os.makedirs(OUT, exist_ok=True)

# Hero deck layout (metres), shared with ACatapultDeck: deck-frame X along the track
# from -60 (aft) to +101 (bow), Z across from -20 to +20.
DECK_X0, DECK_X1 = -60.0, 101.0
DECK_Z0, DECK_Z1 = -20.0, 20.0
MAIN_TRACK = 2.19   # main wheels, either side of the centreline (from the aircraft model)
STROKE = 94.4


def spectral_noise(size, beta, seed):
    """Tileable fractal noise with a 1/f^beta power spectrum, normalised to 0..1."""
    rng = np.random.default_rng(seed)
    white = rng.standard_normal((size, size))
    fx = np.fft.fftfreq(size)[:, None]
    fy = np.fft.fftfreq(size)[None, :]
    f = np.sqrt(fx * fx + fy * fy)
    f[0, 0] = 1.0
    field = np.real(np.fft.ifft2(np.fft.fft2(white) / f ** (beta / 2.0)))
    return normalise(field)


def blur_periodic(a, sigma_px):
    size_y, size_x = a.shape
    fy = np.fft.fftfreq(size_y)[:, None]
    fx = np.fft.fftfreq(size_x)[None, :]
    kernel = np.exp(-2.0 * (np.pi * sigma_px) ** 2 * (fx * fx + fy * fy))
    return np.real(np.fft.ifft2(np.fft.fft2(a) * kernel))


def normalise(a):
    lo, hi = np.percentile(a, 0.5), np.percentile(a, 99.5)
    return np.clip((a - lo) / (hi - lo + 1e-9), 0.0, 1.0)


def height_to_normal(height, strength):
    """Tileable normal map (tangent space, +Z out) from a height field."""
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5 * strength
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5 * strength
    n = np.stack([-dx, -dy, np.ones_like(height)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n * 0.5 + 0.5


def save(name, rgba):
    """rgba: float array (h, w, 4) in 0..1, row 0 = top of the image."""
    h, w, _ = rgba.shape
    img = bpy.data.images.new(name, width=w, height=h, alpha=True, float_buffer=False)
    img.colorspace_settings.name = "Non-Color"
    img.pixels.foreach_set(np.flipud(rgba).astype(np.float32).ravel())  # Blender rows start at the bottom
    img.filepath_raw = os.path.join(OUT, name + ".png")
    img.file_format = "PNG"
    img.save()
    print("WROTE", img.filepath_raw, w, "x", h)


def rgba(r, g, b, a=None):
    return np.stack([r, g, b, np.ones_like(r) if a is None else a], axis=-1)


# --- Non-skid grit ---------------------------------------------------------------------
size = 1024
rng = np.random.default_rng(7)
grains = np.zeros((size, size))
for radius_px, count in ((1.2, 90000), (2.0, 30000), (3.0, 6000)):  # 1-3 mm aggregate
    layer = np.zeros((size, size))
    ys, xs = rng.integers(0, size, count), rng.integers(0, size, count)
    np.add.at(layer, (ys, xs), rng.uniform(0.5, 1.0, count))
    grains += blur_periodic(layer, radius_px) * radius_px ** 2
grit = normalise(grains)
binder = spectral_noise(size, 2.2, 8)                  # the coating the grit sits in
height = 0.75 * grit + 0.25 * binder
save("T_NonSkid_N", rgba(*np.moveaxis(height_to_normal(height, 9.0), -1, 0)))

mottle = spectral_noise(size, 3.0, 11)                 # metre-scale tonal patches (tile = 4 m)
rough_var = spectral_noise(size, 2.6, 12)
save("T_NonSkid_D", rgba(mottle, rough_var, height))

# --- Deck mask -------------------------------------------------------------------------
W, H = 4096, 1024
xs = np.linspace(DECK_X0, DECK_X1, W)[None, :]          # metres along the deck (U)
zs = np.linspace(DECK_Z0, DECK_Z1, H)[:, None]          # metres across (V)
streak_noise = normalise(np.real(np.fft.ifft2(np.fft.fft2(np.random.default_rng(21).standard_normal((H, W)))
                                              * np.exp(-((np.fft.fftfreq(W)[None, :] * 400) ** 2)
                                                       - ((np.fft.fftfreq(H)[:, None] * 6) ** 2)))))
patch = normalise(np.real(np.fft.ifft2(np.fft.fft2(np.random.default_rng(22).standard_normal((H, W)))
                                       * np.exp(-((np.fft.fftfreq(W)[None, :] * 120) ** 2)
                                                - ((np.fft.fftfreq(H)[:, None] * 60) ** 2)))))

# Rubber: three wheel tracks down the catapult, heaviest where tyres scrub at the start.
rubber = np.zeros((H, W))
along = np.clip(1.0 - (xs - 0.0) / (STROKE + 5.0), 0.0, 1.0) ** 1.5
scrub = np.exp(-((xs - 2.0) / 10.0) ** 2)
for center, width in ((0.0, 0.35), (MAIN_TRACK, 0.55), (-MAIN_TRACK, 0.55)):
    for wander in (-0.25, 0.0, 0.25):                  # many launches, slightly different lines
        rubber += np.exp(-((zs - center - wander) / width) ** 2) * (0.25 * along + 0.6 * scrub)
fade_in = np.clip((xs + 16.0) / 12.0, 0.0, 1.0) ** 2           # taxiing onto the catapult
fade_out = np.clip((STROKE + 6.0 - xs) / 6.0, 0.0, 1.0)
rubber *= fade_in * fade_out
rubber = np.clip(rubber * (0.55 + 0.75 * streak_noise), 0.0, 1.0)

# Oil and hydraulic stains: blotches around the hookup point and where jets wait.
oil = np.zeros((H, W))
rng = np.random.default_rng(31)
for cx, cz, spread, n in ((-1.0, 0.0, 3.0, 40), (-6.5, 0.0, 4.0, 30), (-30.0, 0.0, 6.0, 25)):
    for _ in range(n):
        px, pz = rng.normal(cx, spread), rng.normal(cz, spread * 0.6)
        r = rng.uniform(0.15, 0.9)
        oil += rng.uniform(0.3, 1.0) * np.exp(-(((xs - px) ** 2 + (zs - pz) ** 2) / (r * r)))
oil = np.clip(oil * (0.6 + 0.8 * patch), 0.0, 1.0)

# Traffic wear: lighter, smoother lanes where crews and tractors walk and drive.
wear = 0.35 * patch + 0.45 * np.exp(-((zs - 0.0) / 6.0) ** 2) * streak_noise
wear += 0.3 * np.exp(-((np.abs(zs) - 9.0) / 2.5) ** 2)
wear = np.clip(wear, 0.0, 1.0)

# Exhaust heat: the stretch of deck between the nozzle and the jet blast deflector.
heat = np.exp(-((xs + 17.0) / 3.5) ** 2) * np.exp(-(zs / 4.0) ** 2) * (0.6 + 0.4 * patch)
save("T_DeckMask", rgba(rubber, oil, wear, np.clip(heat, 0.0, 1.0)))

# --- Effects ---------------------------------------------------------------------------
haze = spectral_noise(512, 2.4, 41)
save("T_HazeNoise_N", rgba(*np.moveaxis(height_to_normal(haze, 6.0), -1, 0)))

yy, xx = np.mgrid[0:256, 0:256]
r = np.sqrt((xx - 127.5) ** 2 + (yy - 127.5) ** 2) / 127.5
billow = spectral_noise(256, 2.0, 51)
alpha = np.clip(1.0 - r, 0.0, 1.0) ** 1.6 * (0.45 + 0.75 * billow)
alpha = np.clip(alpha / alpha.max(), 0.0, 1.0)
save("T_SteamPuff", rgba(np.ones_like(alpha), np.ones_like(alpha), np.ones_like(alpha), alpha))
