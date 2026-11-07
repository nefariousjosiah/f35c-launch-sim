// Copyright (c) 2026 nefariousjosiah. MIT License.
#pragma once

#include <array>
#include <cstdint>

#include "LaunchSim/Params.h"

#if defined(LAUNCHSIM_API) && !defined(DLLEXPORT)
// Built by UnrealBuildTool: LAUNCHSIM_API expands to DLLEXPORT/DLLIMPORT, which the
// engine's platform header defines. This is the only engine header the module uses.
#include "HAL/Platform.h"
#endif
#ifndef LAUNCHSIM_API
#define LAUNCHSIM_API  // empty in the standalone CMake build
#endif

namespace launchsim {

/// Where we are in the launch. The order follows the documented deck procedure.
enum class LaunchPhase : std::uint8_t {
    Hookup,         ///< on the catapult, launch bar in the shuttle, holdback fitted, JBD up
    Tension,        ///< off the brakes, shuttle takes tension, the jet squats
    RunUp,          ///< engine to MIL (or afterburner on signal)
    Wipeout,        ///< full-travel control check
    FinalChecks,    ///< white-shirt checkers, thumbs-up to the shooter
    Salute,         ///< pilot salutes, right hand to the towel rack
    ShooterSignal,  ///< shooter returns salute, touches the deck, points down the track
    Stroke,         ///< launch valve open, catapult stroke
    OverTheBow,     ///< launch bar off the shuttle, control-law rotation
    Climbout,       ///< pilot has the jet, gear coming up
};

/// What the deck crew should be signalling right now (drives crew animation).
enum class CrewCue : std::uint8_t {
    Standby,
    TakeTension,     ///< director: palm up off the brakes, other arm pointing forward
    RunUp,           ///< shooter: vigorous two-finger wave
    Afterburner,     ///< shooter: open palm "raise the roof"
    FinalChecks,     ///< checkers watch the controls, then thumbs-up
    ReturnSalute,    ///< shooter returns the salute, points to each check item
    LaunchSignal,    ///< shooter touches the deck and points down the track
    Launched,
};

struct BodyState {
    Vec3 position;         ///< CG, deck frame, m
    Vec3 velocity;         ///< deck frame, m/s
    Quat orientation;      ///< body -> deck
    Vec3 angularVelocity;  ///< body frame, rad/s
};

struct GearSnapshot {
    double compression = 0.0;      ///< m
    double compressionRate = 0.0;  ///< m/s
    double load = 0.0;             ///< N
    double wheelAngle = 0.0;       ///< rad, accumulated spin for animation
    bool inContact = false;
};

/// Everything the renderer, animation, effects, audio and cameras need, one per step.
struct SystemsSnapshot {
    double time = 0.0;  ///< s relative to the launch valve opening
    LaunchPhase phase = LaunchPhase::Hookup;
    CrewCue crewCue = CrewCue::Standby;
    BodyState body;

    std::array<GearSnapshot, 3> gear;  ///< nose, left main, right main
    double gearExtension = 1.0;        ///< 1 down and locked, 0 retracted
    double wingFold = 0.0;             ///< 0 spread (the sim starts spread)

    // Control surfaces, radians (actuator positions, rate limited).
    double stabilator = 0.0;  ///< + trailing edge up
    double aileron = 0.0;     ///< + roll right
    double rudder = 0.0;      ///< + nose right
    double flaps = degToRad(25.0);

    // Engine.
    double thrust = 0.0;              ///< N
    double afterburner = 0.0;         ///< 0..1 burner intensity
    double nozzleGlow = 0.0;          ///< 0..1 for the exhaust material

    // Catapult hardware.
    double shuttlePosition = 0.0;     ///< m along the track
    double shuttleVelocity = 0.0;
    double towForce = 0.0;            ///< N along the launch bar
    double holdbackLoad = 0.0;        ///< N
    bool launchBarEngaged = true;
    bool holdbackAttached = true;
    double launchBarRaise = 0.0;      ///< 0 in the shuttle, 1 stowed

    // Derived readouts.
    double airspeed = 0.0;            ///< m/s true airspeed
    double angleOfAttack = 0.0;       ///< rad
    double longitudinalG = 0.0;       ///< body-x specific force, g (what the pilot feels)
    double heightAboveDeck = 0.0;     ///< CG height above the deck plane, m
    bool ditched = false;
};

/// Numbers the tests and the calibration tool read after a launch.
struct LaunchMetrics {
    double releaseTime = -1.0;      ///< holdback let go, s after the valve opened
    double separationTime = -1.0;   ///< launch bar left the shuttle
    double endSpeed = 0.0;          ///< deck-relative speed at separation, m/s
    double peakLongitudinalG = 0.0;
    double minSinkBelowDeck = 0.0;  ///< lowest CG height relative to its height at the bow, m (<= 0)
    double bowCrossingTime = -1.0;
    double peakNoseCompression = 0.0;
    double tensionNoseCompression = 0.0;
};

/// Deterministic F-35C catapult launch, stepped at a fixed 1 kHz in the deck frame.
/// The carrier is the reference frame; wind over deck arrives as a constant headwind.
class LAUNCHSIM_API LaunchSimulation {
public:
    static constexpr std::int64_t kStepNanoseconds = 1'000'000;
    static constexpr double kTimeStep = 1.0e-3;
    static constexpr std::int64_t kMaxFrameNanoseconds = 100'000'000;

    explicit LaunchSimulation(const LaunchParams& params = {});

    /// Back to the start of the sequence: hooked up, settled on the struts, holdback fitted.
    void reset();
    /// Runs as many fixed steps as fit in the elapsed wall time. Returns the step count.
    int advance(double frameSeconds);
    /// Exactly one fixed step.
    void step();

    const LaunchParams& params() const { return params_; }
    const SystemsSnapshot& snapshot() const { return snapshot_; }
    const LaunchMetrics& metrics() const { return metrics_; }
    const BodyState& body() const { return body_; }
    double time() const { return time_; }

    /// Mean shuttle tow force the capacity selector chose for this shot, N.
    double meanTowForce() const { return meanTowForce_; }

private:
    struct Forces {
        Vec3 force;   ///< deck frame, N
        Vec3 moment;  ///< body frame about the CG, N m
    };

    Forces computeForces(const BodyState& s, double t, std::array<GearSnapshot, 3>* gearOut,
                         double* towOut, double* holdbackOut) const;
    void integrate(double dt);
    void updateSequence();
    void updateEngine(double dt);
    void updateControls(double dt);
    void updateCatapultEvents();
    void publishSnapshot(const Vec3& previousVelocity);
    void settleOnStruts();
    Vec3 launchBarHingeWorld(const BodyState& s) const;
    double launchBarTipX(const BodyState& s) const;

    LaunchParams params_;
    BodyState body_;
    double time_ = 0.0;
    std::int64_t accumulatorNs_ = 0;
    std::int64_t stepCount_ = 0;

    // Discrete state, constant across one integration step.
    bool fired_ = false;
    bool launchBarEngaged_ = true;
    bool holdbackAttached_ = true;
    bool brakes_ = true;
    double meanTowForce_ = 0.0;
    Vec3 holdbackAnchor_;
    double holdbackRestLength_ = 0.0;
    double thrust_ = 0.0;
    double afterburner_ = 0.0;
    double stab_ = 0.0;
    double aileron_ = 0.0;
    double rudder_ = 0.0;
    double attitudeCommand_ = 0.0;
    double gearExtension_ = 1.0;
    double shuttlePosition_ = 0.0;
    double shuttleVelocity_ = 0.0;
    double launchBarRaise_ = 0.0;
    double bowHeight_ = 0.0;
    std::array<double, 3> wheelAngles_{};

    SystemsSnapshot snapshot_;
    LaunchMetrics metrics_;
};

}  // namespace launchsim
