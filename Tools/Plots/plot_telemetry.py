"""Draws one simulated launch's telemetry as small multiples, light and dark.

    SimTests/build/launchsim_telemetry telemetry.csv
    python Tools/Plots/plot_telemetry.py telemetry.csv Docs/Media

Writes telemetry_light.png and telemetry_dark.png; the README picks one by the
viewer's colour scheme. Needs matplotlib.
"""

import csv
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

THEMES = {
    "light": dict(surface="#fcfcfb", ink="#0b0b0b", ink2="#52514e", muted="#898781",
                  grid="#e1e0d9", axis="#c3c2b7", wash="#f0efec", series="#2a78d6"),
    "dark": dict(surface="#1a1a19", ink="#f0efec", ink2="#c3c2b7", muted="#898781",
                 grid="#2c2c2a", axis="#383835", wash="#262624", series="#3987e5"),
}

PANELS = [
    ("deck_speed_kt", 1.0, "Speed along the deck (kt)"),
    ("longitudinal_g", 1.0, "Acceleration felt by the pilot (g)"),
    ("nose_compression_m", 100.0, "Nose-gear strut compression (cm)"),
    ("pitch_deg", 1.0, "Pitch attitude (°)"),
]


def load(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    cols = {k: [float(r[k]) for r in rows] for k in rows[0]}
    return cols


def events(cols):
    """Holdback release (tension drops to zero) and launch-bar separation (tow force ends)."""
    t, hold, tow = cols["time_s"], cols["holdback_kN"], cols["tow_force_kN"]
    release = next(t[i] for i in range(len(t)) if t[i] >= 0 and hold[i] <= 0.0)
    separation = next(t[i] for i in range(len(t)) if t[i] > release + 0.5 and tow[i] <= 0.0)
    return release, separation


def draw(cols, theme, out):
    c = THEMES[theme]
    plt.rcParams.update({
        "font.family": ["Segoe UI", "DejaVu Sans"],
        "font.size": 10,
        "axes.edgecolor": c["axis"],
        "axes.labelcolor": c["ink2"],
        "xtick.color": c["muted"],
        "ytick.color": c["muted"],
        "text.color": c["ink"],
    })
    release, separation = events(cols)
    t = cols["time_s"]

    fig, axes = plt.subplots(2, 2, figsize=(12, 6.6), dpi=150, sharex=True)
    fig.patch.set_facecolor(c["surface"])
    for ax, (key, scale, title) in zip(axes.flat, PANELS):
        ax.set_facecolor(c["surface"])
        ax.axvspan(release, separation, color=c["wash"], lw=0, zorder=0)
        for x in (0.0, separation):
            ax.axvline(x, color=c["muted"], lw=0.8, ls=(0, (3, 3)), zorder=1)
        ax.plot(t, [v * scale for v in cols[key]], color=c["series"], lw=2.0, solid_capstyle="round", zorder=3)
        ax.set_title(title, loc="left", fontsize=11, color=c["ink"], pad=8)
        ax.grid(axis="y", color=c["grid"], lw=0.6)
        ax.set_axisbelow(True)
        for side in ("top", "right", "left"):
            ax.spines[side].set_visible(False)
        ax.tick_params(length=0, pad=4)
        ax.set_xlim(t[0], t[-1])

    top = axes[0, 0]
    ymax = top.get_ylim()[1]
    top.text(0.08, ymax * 0.97, "Catapult fires", color=c["ink2"], fontsize=9, va="top")
    top.text((release + separation) / 2, ymax * 0.06, f"Stroke, {separation - release:.2f} s",
             color=c["ink2"], fontsize=9, ha="center")
    top.text(separation + 0.08, ymax * 0.97, "Off the shuttle", color=c["ink2"], fontsize=9, va="top")
    for ax in axes[1]:
        ax.set_xlabel("Seconds from the launch valve opening")

    fig.suptitle("One simulated F-35C launch, 1 kHz deterministic physics", x=0.012, ha="left",
                 fontsize=13, color=c["ink"], y=0.995)
    fig.tight_layout(rect=(0, 0, 1, 0.97), h_pad=2.2, w_pad=3.0)
    fig.savefig(out, facecolor=c["surface"])
    plt.close(fig)


def main():
    csv_path, out_dir = sys.argv[1], Path(sys.argv[2])
    cols = load(csv_path)
    for theme in THEMES:
        draw(cols, theme, out_dir / f"telemetry_{theme}.png")
    print("events: release %.3f s, separation %.3f s" % events(cols))


if __name__ == "__main__":
    main()
