// Runs one launch with the default parameters and writes its telemetry as CSV.
// Feeds Tools/Plots/plot_telemetry.py, which draws Docs/Media/telemetry*.png.
//
//   launchsim_telemetry [out.csv]      (stdout when no path is given)

#include <cmath>
#include <cstdio>

#include "LaunchSim/LaunchSimulation.h"

using namespace launchsim;

int main(int argc, char** argv) {
    std::FILE* out = argc > 1 ? std::fopen(argv[1], "w") : stdout;
    if (!out) {
        std::perror(argv[1]);
        return 1;
    }

    LaunchSimulation sim;
    const double deckHeight = sim.snapshot().heightAboveDeck;

    std::fprintf(out, "time_s,deck_speed_kt,airspeed_kt,longitudinal_g,nose_compression_m,pitch_deg,"
                      "height_change_m,tow_force_kN,holdback_kN,phase\n");
    while (sim.time() < 6.0) {
        sim.step();
        const SystemsSnapshot& s = sim.snapshot();
        const long ms = std::lround(s.time * 1000.0);
        if (s.time < -1.0 || ms % 10 != 0) continue;
        std::fprintf(out, "%.3f,%.2f,%.2f,%.4f,%.4f,%.3f,%.4f,%.1f,%.1f,%d\n", s.time, s.body.velocity.x / kKnot,
                     s.airspeed / kKnot, s.longitudinalG, s.gear[0].compression,
                     radToDeg(toEuler(s.body.orientation).pitch), s.heightAboveDeck - deckHeight, s.towForce / 1000.0,
                     s.holdbackLoad / 1000.0, static_cast<int>(s.phase));
    }

    const LaunchMetrics& m = sim.metrics();
    std::fprintf(stderr,
                 "holdback release %.3f s, launch bar off the shuttle %.3f s at %.1f kt, bow %.3f s, "
                 "peak %.2f g, sink %.2f m\n",
                 m.releaseTime, m.separationTime, m.endSpeed / kKnot, m.bowCrossingTime, m.peakLongitudinalG,
                 m.minSinkBelowDeck);
    if (out != stdout) std::fclose(out);
    return 0;
}
