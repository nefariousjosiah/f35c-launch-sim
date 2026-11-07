// Sweep of control-law rotation settings: sink off the bow vs. rotation rate and gain.
#include <cstdio>
#include "LaunchSim/LaunchSimulation.h"
using namespace launchsim;
int main() {
    const double rates[] = {6, 8, 10, 12};
    const double gains[] = {2.5, 3.5};
    const double cl0s[] = {0.35, 0.45};
    for (double cl0 : cl0s) for (double g : gains) for (double r : rates) {
        LaunchParams p;
        p.aircraft.aero.cl0 = cl0;
        p.controlLaw.rotationRate = degToRad(r);
        p.controlLaw.pitchGain = g;
        LaunchSimulation sim(p);
        double maxQ = 0.0, peakAoA = 0.0;
        while (sim.time() < 9.0) {
            sim.step();
            if (sim.metrics().separationTime > 0) {
                maxQ = std::max(maxQ, radToDeg(sim.body().angularVelocity.z));
                peakAoA = std::max(peakAoA, radToDeg(sim.snapshot().angleOfAttack));
            }
        }
        std::printf("cl0 %.2f gain %.1f rate %4.1f deg/s | sink %5.2f m | pitch@9s %5.1f | climb %5.1f m/s | max q %4.1f deg/s | peak AoA %4.1f\n",
            cl0, g, r, sim.metrics().minSinkBelowDeck, radToDeg(toEuler(sim.body().orientation).pitch),
            sim.body().velocity.y, maxQ, peakAoA);
    }
}
