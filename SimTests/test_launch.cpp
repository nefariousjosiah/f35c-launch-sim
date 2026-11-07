// Copyright (c) 2026 nefariousjosiah. MIT License.
//
// Acceptance tests for the launch physics. The expected ranges come from the
// research in Docs/LaunchModel.md: a 94.4 m C-13-1 power stroke taking about
// 2.2-2.8 s, MIL power held against the holdback, a nose strut that squats under
// tension and oscillates on the stroke, and little or no sink off the bow.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "LaunchSim/LaunchSimulation.h"

using namespace launchsim;
using Catch::Approx;

namespace {

/// Runs the simulation until `untilTime` (seconds relative to the launch valve opening).
void runUntil(LaunchSimulation& sim, double untilTime, std::vector<SystemsSnapshot>* trace = nullptr) {
    while (sim.time() < untilTime - 0.5 * LaunchSimulation::kTimeStep) {
        sim.step();
        if (trace != nullptr) {
            trace->push_back(sim.snapshot());
        }
    }
}

}  // namespace

TEST_CASE("The jet starts at rest on its struts with the launch bar in the shuttle", "[setup]") {
    LaunchSimulation sim;
    const SystemsSnapshot& s = sim.snapshot();
    REQUIRE(s.phase == LaunchPhase::Hookup);
    REQUIRE(length(s.body.velocity) < 1e-9);
    for (const GearSnapshot& g : s.gear) {
        REQUIRE(g.inContact);
    }
    REQUIRE(s.gear[0].compression > 0.05);
    REQUIRE(s.gear[0].compression < 0.20);
    REQUIRE(s.gear[1].compression == Approx(s.gear[2].compression).margin(1e-6));  // symmetric
    REQUIRE(s.shuttlePosition == Approx(0.0).margin(1e-6));
    REQUIRE(std::abs(radToDeg(toEuler(s.body.orientation).pitch)) < 3.0);
}

TEST_CASE("The holdback holds the jet at MIL power until the shot", "[holdback]") {
    LaunchSimulation sim;
    const double startX = sim.body().position.x;
    runUntil(sim, -0.01);
    const SystemsSnapshot& s = sim.snapshot();
    REQUIRE(s.holdbackAttached);
    REQUIRE(s.thrust > 0.95 * sim.params().aircraft.engine.thrustMil);
    // Stationary, though the CG shifts a few cm forward as the nose squats.
    REQUIRE(length(sim.body().velocity) < 0.01);
    REQUIRE(std::abs(sim.body().position.x - startX) < 0.10);
    REQUIRE(s.holdbackLoad < sim.params().catapult.holdbackReleaseLoad);
}

TEST_CASE("Afterburner run-up is also held", "[holdback]") {
    LaunchParams params;
    params.sequence.afterburner = true;
    LaunchSimulation sim(params);
    runUntil(sim, -0.01);
    REQUIRE(sim.snapshot().holdbackAttached);
    REQUIRE(sim.snapshot().afterburner > 0.99);
}

TEST_CASE("Taking tension and running up squats the nose strut", "[gear]") {
    LaunchSimulation sim;
    const double atRest = sim.snapshot().gear[0].compression;
    runUntil(sim, -1.0);
    REQUIRE(sim.metrics().tensionNoseCompression > atRest + 0.02);
}

TEST_CASE("The holdback breaks in the first moments of the shot", "[holdback]") {
    LaunchSimulation sim;
    runUntil(sim, 1.0);
    REQUIRE(sim.metrics().releaseTime >= 0.0);
    REQUIRE(sim.metrics().releaseTime < 0.2);
}

TEST_CASE("The stroke reaches the commanded end speed in about 2.5 s", "[catapult]") {
    LaunchSimulation sim;
    runUntil(sim, 5.0);
    const LaunchMetrics& m = sim.metrics();
    const double strokeTime = m.separationTime - m.releaseTime;
    INFO("stroke " << strokeTime << " s, end speed " << m.endSpeed / kKnot << " kt, peak "
                   << m.peakLongitudinalG << " g");
    REQUIRE(m.separationTime > 0.0);
    REQUIRE(m.endSpeed == Approx(sim.params().catapult.endSpeed).epsilon(0.03));
    REQUIRE(strokeTime > 2.2);
    REQUIRE(strokeTime < 2.8);
    REQUIRE(m.peakLongitudinalG > 3.0);
    REQUIRE(m.peakLongitudinalG < 4.6);
}

TEST_CASE("The end speed is the same whether launched at MIL or in afterburner", "[catapult]") {
    // The capacity selector accounts for the extra thrust.
    LaunchParams params;
    params.sequence.afterburner = true;
    LaunchSimulation sim(params);
    runUntil(sim, 5.0);
    REQUIRE(sim.metrics().endSpeed == Approx(params.catapult.endSpeed).epsilon(0.03));
}

TEST_CASE("The nose oscillates on the stroke", "[gear]") {
    LaunchSimulation sim;
    std::vector<SystemsSnapshot> trace;
    runUntil(sim, 3.0, &trace);
    int reversals = 0;
    double previous = 0.0;
    for (const SystemsSnapshot& s : trace) {
        if (s.phase != LaunchPhase::Stroke || !s.gear[0].inContact) {
            continue;
        }
        const double rate = s.gear[0].compressionRate;
        if (std::abs(rate) > 0.05) {
            if (previous != 0.0 && (rate > 0.0) != (previous > 0.0)) {
                ++reversals;
            }
            previous = rate;
        }
    }
    INFO("nose strut reversals on the stroke: " << reversals);
    REQUIRE(reversals >= 4);
    // Acceleration lifts the nose while the launch bar pulls it down, so the strut stays
    // loaded through the stroke rather than unloading.
    REQUIRE(sim.metrics().peakNoseCompression > 0.8 * sim.metrics().tensionNoseCompression);
}

TEST_CASE("After the bow the control law rotates the jet into a climb with little sink", "[flight]") {
    LaunchSimulation sim;
    runUntil(sim, 9.0);
    const LaunchMetrics& m = sim.metrics();
    REQUIRE(m.bowCrossingTime > 0.0);
    const SystemsSnapshot& s = sim.snapshot();
    INFO("sink " << m.minSinkBelowDeck << " m, pitch " << radToDeg(toEuler(s.body.orientation).pitch)
                 << " deg, climb " << s.body.velocity.y << " m/s");
    REQUIRE_FALSE(s.ditched);
    REQUIRE(m.minSinkBelowDeck > -3.0);
    REQUIRE(radToDeg(toEuler(s.body.orientation).pitch) == Approx(12.0).margin(3.0));
    REQUIRE(s.body.velocity.y > 2.0);
    REQUIRE(std::abs(radToDeg(toEuler(s.body.orientation).roll)) < 2.0);
    REQUIRE(s.phase == LaunchPhase::Climbout);
}

TEST_CASE("Gear retracts after the bow", "[flight]") {
    LaunchSimulation sim;
    runUntil(sim, 15.0);
    REQUIRE(sim.snapshot().gearExtension == Approx(0.0));
}

TEST_CASE("The sequence follows the documented order", "[sequence]") {
    LaunchSimulation sim;
    std::vector<LaunchPhase> order;
    while (sim.time() < 10.0) {
        sim.step();
        if (order.empty() || order.back() != sim.snapshot().phase) {
            order.push_back(sim.snapshot().phase);
        }
    }
    const std::vector<LaunchPhase> expected{
        LaunchPhase::Hookup,        LaunchPhase::Tension, LaunchPhase::RunUp,      LaunchPhase::Wipeout,
        LaunchPhase::FinalChecks,   LaunchPhase::Salute,  LaunchPhase::ShooterSignal, LaunchPhase::Stroke,
        LaunchPhase::OverTheBow,    LaunchPhase::Climbout,
    };
    REQUIRE(order == expected);
}

TEST_CASE("Two runs are bit-identical", "[determinism]") {
    LaunchSimulation a;
    LaunchSimulation b;
    runUntil(a, 6.0);
    runUntil(b, 6.0);
    REQUIRE(a.body().position == b.body().position);
    REQUIRE(a.body().orientation == b.body().orientation);
    REQUIRE(a.metrics().separationTime == b.metrics().separationTime);
}

TEST_CASE("Frame-time chunking does not change the result", "[determinism]") {
    LaunchSimulation a;
    LaunchSimulation b;
    while (a.time() < 4.0) {
        a.advance(1.0 / 60.0);
    }
    while (b.time() < a.time() - 1e-9) {
        b.advance(1.0 / 144.0);
    }
    while (a.time() < b.time() - 1e-9) {
        a.step();
    }
    REQUIRE(a.body().position == b.body().position);
}
