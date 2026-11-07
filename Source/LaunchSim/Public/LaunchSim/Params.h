// Copyright (c) 2026 nefariousjosiah. MIT License.
#pragma once

// Every number that shapes the launch, in SI units.
//
// Provenance tags (see Docs/LaunchModel.md for the sources):
//   [S] sourced from published material
//   [D] derived from sourced values by calculation
//   [E] engineering estimate; plausible but unpublished
//   [T] to be tuned against frame-timed reference footage (DVIDS 372940)
//
// Frames: the deck frame has its origin on the deck surface at the catapult
// shuttle's start position, +X down the track towards the bow, +Y up, +Z to
// starboard. The body frame is +X nose, +Y up, +Z right wing. Both are
// right-handed. See VecMath.h.

#include <initializer_list>

#include "LaunchSim/VecMath.h"

namespace launchsim {

inline constexpr double kGravity = 9.80665;
inline constexpr double kKnot = 0.514444;  // m/s
inline constexpr double kPound = 0.45359237;  // kg
inline constexpr double kPoundForce = 4.4482216;  // N
inline constexpr double kAirDensity = 1.225;  // kg/m^3, sea level

/// One landing-gear leg, modelled as a nonlinear air-oil strut with a tyre.
struct GearParams {
    Vec3 attach;                 ///< top of the strut, body frame relative to the CG, m
    double extendedLength = 1.0; ///< strut top to axle at full extension, m
    double tyreRadius = 0.35;    ///< m
    double maxStroke = 0.45;     ///< m, beyond this the bump stop engages
    double springLinear = 4.0e5; ///< N/m
    double springCubic = 2.0e6;  ///< N/m^3; the air spring stiffens as it compresses
    double dampCompression = 3.0e4;  ///< N/(m/s)^2, square-law orifice damping
    double dampRebound = 9.0e4;      ///< N/(m/s)^2, rebound is restricted more
    double dampLinear = 6.0e3;       ///< N/(m/s), seal friction; lets small motions settle
    double bumpStiffness = 5.0e7;    ///< N/m beyond maxStroke
    double rollingFriction = 0.02;   ///< brakes off
    double brakingFriction = 0.5;    ///< brakes on
    double sideFriction = 0.8;
};

struct AeroParams {
    double wingArea = 62.1;      ///< m^2 [S] (668 ft^2)
    double span = 13.1;          ///< m [S] (43 ft)
    double chord = 4.74;         ///< m [D] mean geometric chord S/b
    double cl0 = 0.45;           ///< full launch flaps [E][T]; tuned with SimTests/tools/rotation_sweep.cpp
    double clAlpha = 3.5;        ///< per rad, low-aspect-ratio wing [E]
    double clMax = 1.45;         ///< [E]
    double cd0 = 0.035;          ///< gear and flaps down [E]
    double cd0Clean = 0.020;     ///< [E]
    double inducedK = 0.144;     ///< 1/(pi e AR), AR 2.76, e 0.8 [D]
    double cm0 = 0.0;            ///< trimmed at launch flap setting [E]
    double cmAlpha = -0.30;      ///< per rad; FCS-augmented stability [E]
    double cmQ = -6.0;           ///< pitch damping, per rad of qc/2V [E]
    double cmStab = 0.55;        ///< per rad of stabilator, trailing edge up = nose up [E]
    double cyBeta = -0.9;        ///< side force per rad of sideslip [E]
    double clBeta = -0.08;       ///< dihedral effect [E]
    double clP = -0.30;          ///< roll damping [E]
    double clAileron = 0.06;     ///< per rad [E]
    double cnBeta = 0.10;        ///< weathercock stability [E]
    double cnR = -0.25;          ///< yaw damping [E]
    double cnRudder = 0.05;      ///< per rad [E]
    double groundEffectLift = 0.10;  ///< extra lift fraction deep in ground effect [E]
};

struct EngineParams {
    double thrustIdle = 8.0e3;              ///< N [E]
    double thrustMil = 28000.0 * kPoundForce;   ///< ~125 kN [S] (sources give 25-28 klbf)
    double thrustMax = 43000.0 * kPoundForce;   ///< ~191 kN, afterburner [S] (40-43 klbf)
    double spoolTimeConstant = 1.4;         ///< s, idle to MIL in ~4-5 s [E]
    double afterburnerLightTime = 0.6;      ///< s [E]
};

struct AircraftParams {
    double mass = 55000.0 * kPound;  ///< 24,948 kg; DT-II test points were 55k and 60k lb [S]
    /// Principal moments of inertia, body frame (x roll, y yaw, z pitch), kg m^2.
    /// Scaled from published F-16 values by mass and size [E].
    Vec3 inertia{45.0e3, 300.0e3, 265.0e3};
    double cgHeightNominal = 2.12;   ///< m above deck at rest [D] from the hero model

    GearParams noseGear;
    GearParams leftMainGear;
    GearParams rightMainGear;

    /// Launch bar: hinged on the nose gear just ahead of and above the axle; its tip
    /// rides in the shuttle at roughly 25 degrees.
    Vec3 launchBarHinge{5.85, -1.61, 0.0};  ///< body frame [E]
    double launchBarLength = 1.2;            ///< m [E]
    /// Holdback fitting on the nose gear, aft of the axle.
    Vec3 holdbackFitting{5.30, -1.85, 0.0};  ///< body frame [E]

    AeroParams aero;
    EngineParams engine;

    AircraftParams();
};

/// Defaults for an F-35C at a 55,000 lb launch weight. Gear geometry is measured
/// from the hero model scaled to the published 15.7 m length (Tools/Blender/
/// export_f35_parts.py): 6.36 m wheelbase, 4.38 m main-gear track. Extended lengths
/// are the modelled strut length plus the static compression.
inline AircraftParams::AircraftParams() {
    // Nose gear: twin-wheel, carrier-strength, carries ~12% of the weight at rest.
    noseGear.attach = {5.60, -0.57, 0.0};
    noseGear.extendedLength = 1.26;
    noseGear.tyreRadius = 0.40;
    noseGear.maxStroke = 0.42;
    noseGear.springLinear = 2.5e5;
    noseGear.springCubic = 3.0e6;
    noseGear.dampCompression = 2.0e4;
    noseGear.dampRebound = 6.0e4;
    noseGear.dampLinear = 2.5e3;   // light: the F-35C nose is known to oscillate on the stroke

    // Main gear, 4.38 m track, 0.76 m aft of the CG.
    for (GearParams* gear : {&leftMainGear, &rightMainGear}) {
        gear->extendedLength = 1.63;
        gear->tyreRadius = 0.54;
        gear->maxStroke = 0.45;
        gear->springLinear = 6.0e5;
        gear->springCubic = 2.0e6;
        gear->dampCompression = 4.0e4;
        gear->dampRebound = 1.2e5;
        gear->dampLinear = 8.0e3;
    }
    leftMainGear.attach = {-0.76, -0.115, -2.19};
    rightMainGear.attach = {-0.76, -0.115, 2.19};
}

/// The C-13 steam catapult on a Nimitz-class carrier.
struct CatapultParams {
    double powerStroke = 94.4;       ///< m, C-13-1: 309 ft 8.75 in [S]
    double trackLength = 99.0;       ///< m, 324 ft 10 in [S]
    /// End speed relative to the deck; the capacity selector valve setting picks it.
    double endSpeed = 135.0 * kKnot; ///< [E][T] calibrate from footage
    /// Shuttle force falls along the stroke as steam expands: linear from
    /// peakRatio x mean at the start to (2 - peakRatio) x mean at the end.
    double peakRatio = 1.3;          ///< [E][T]
    double pressureRiseTime = 0.12;  ///< s, launch valve opening to full force [E]
    double tensionForce = 15.0e3;    ///< N, shuttle tension before the shot [E]
    /// Repeatable-release holdback: lets go when its load exceeds this.
    /// Must exceed full-afterburner thrust + tension (~206 kN) so the jet stays put
    /// at run-up; the shot breaks it within the first ~0.1 s of pressure rise.
    double holdbackReleaseLoad = 230.0e3;  ///< N [E][T]; the 2017 fix lowered it
    double holdbackStiffness = 2.0e7;      ///< N/m [E]
    double waterBrakeDeceleration = 600.0; ///< m/s^2, shuttle stopping in the water brake [E]
};

struct EnvironmentParams {
    double windOverDeck = 27.0 * kKnot;  ///< along the catapult axis, from the bow [E] (25-30 kt)
    double bowDistance = 101.0;          ///< m from shuttle start to the bow edge [E]
    double deckHeight = 18.0;            ///< m, flight deck above the waterline [E]
};

/// Flight control law for the hands-off launch: holds a launch trim on the
/// stroke, then rotates to and holds a climb attitude.
struct ControlLawParams {
    double launchTrimStab = degToRad(6.0);  ///< trailing edge up on the stroke [E]
    double climbAttitude = degToRad(12.0);  ///< target pitch after the bow [E][T]
    double rotationRate = degToRad(10.0);   ///< attitude command slew, per s [E][T]
    double pitchGain = 3.5;                 ///< rad of stab per rad of attitude error
    double pitchRateGain = 1.3;             ///< rad of stab per rad/s
    double maxStab = degToRad(25.0);
    double rollGain = 2.0;
    double rollRateGain = 0.6;
    double maxAileron = degToRad(25.0);
    double yawDamperGain = 1.5;
    double maxRudder = degToRad(30.0);
};

/// When each step of the launch happens. Times are relative to the launch valve
/// opening (t = 0). The order is sourced; the durations are estimates [E][T].
struct SequenceParams {
    double start = -40.0;       ///< simulation starts hooked up, holdback fitted, JBD raised
    double tension = -35.0;     ///< director: off brakes + take tension
    double runUp = -30.0;       ///< shooter: run up to MIL
    double wipeoutStart = -24.0;
    double wipeoutEnd = -16.0;  ///< full-travel control check
    double salute = -10.0;      ///< pilot salutes, right hand to the towel rack
    double shooterSignal = -6.0;///< shooter returns salute, touches the deck, points forward
    double fire = 0.0;          ///< deck-edge operator fires; launch valve opens
    double gearUpDelay = 5.0;   ///< after leaving the bow [E]
    double gearTransitTime = 5.0;
    bool afterburner = false;   ///< MIL is the 2014 default; AB on the shooter's signal
};

struct LaunchParams {
    AircraftParams aircraft;
    CatapultParams catapult;
    EnvironmentParams environment;
    ControlLawParams controlLaw;
    SequenceParams sequence;
};

}  // namespace launchsim
