// Copyright (c) 2026 nefariousjosiah. MIT License.

#include "LaunchSim/LaunchSimulation.h"

#include <algorithm>
#include <cmath>

namespace launchsim {

namespace {

constexpr Vec3 kWorldUp{0.0, 1.0, 0.0};

double smoothStep(double edge0, double edge1, double x) {
    const double u = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return u * u * (3.0 - 2.0 * u);
}

double moveTowards(double current, double target, double maxDelta) {
    return current + std::clamp(target - current, -maxDelta, maxDelta);
}

struct Derivative {
    Vec3 dPosition;
    Vec3 dVelocity;
    Quat dOrientation{0.0, 0.0, 0.0, 0.0};
    Vec3 dAngularVelocity;
};

BodyState offsetState(const BodyState& s, const Derivative& d, double h) {
    BodyState out;
    out.position = s.position + d.dPosition * h;
    out.velocity = s.velocity + d.dVelocity * h;
    out.orientation = s.orientation + d.dOrientation * h;
    out.angularVelocity = s.angularVelocity + d.dAngularVelocity * h;
    return out;
}

/// Ground reaction of one strut against the deck plane (y = 0, x < bow).
Vec3 gearReaction(const GearParams& g, const BodyState& s, double gearExtension, bool brakes, double bowX,
                  Vec3& contactPoint, GearSnapshot& out) {
    out.inContact = false;
    out.compression = 0.0;
    out.compressionRate = 0.0;
    out.load = 0.0;
    if (gearExtension < 0.999) {
        return {};
    }
    const Quat q = normalized(s.orientation);
    const Vec3 top = s.position + rotate(q, g.attach);
    const Vec3 axis = rotate(q, Vec3{0.0, -1.0, 0.0});
    if (axis.y > -0.3 || top.x > bowX) {
        return {};  // inverted, or past the end of the deck
    }
    const double distance = top.y / -axis.y;  // strut top to the deck along the strut axis
    const double compression = g.extendedLength + g.tyreRadius - distance;
    if (compression <= 0.0) {
        return {};
    }

    const Vec3 omegaWorld = rotate(q, s.angularVelocity);
    const Vec3 topVelocity = s.velocity + cross(omegaWorld, top - s.position);
    const double compressionRate = topVelocity.y / axis.y;

    const double stroke = std::min(compression, g.maxStroke);
    double load = g.springLinear * stroke + g.springCubic * stroke * stroke * stroke;
    if (compression > g.maxStroke) {
        load += g.bumpStiffness * (compression - g.maxStroke);
    }
    load += compressionRate > 0.0 ? g.dampCompression * compressionRate * compressionRate
                                  : -g.dampRebound * compressionRate * compressionRate;
    load += g.dampLinear * compressionRate;
    load = std::max(load, 0.0);

    contactPoint = top + axis * distance;
    const Vec3 contactVelocity = s.velocity + cross(omegaWorld, contactPoint - s.position);
    const Vec3 nose = rotate(q, body::kForward);
    const Vec3 heading = normalized(Vec3{nose.x, 0.0, nose.z});
    const Vec3 side{-heading.z, 0.0, heading.x};
    const double rolling = dot(contactVelocity, heading);
    const double sliding = dot(contactVelocity, side);
    const double muLong = brakes ? g.brakingFriction : g.rollingFriction;
    const double longForce = -muLong * load * std::tanh(rolling / 0.05);
    const double sideForce = -g.sideFriction * load * std::tanh(sliding / 0.15);

    out.inContact = true;
    out.compression = compression;
    out.compressionRate = compressionRate;
    out.load = load;
    return kWorldUp * load + heading * longForce + side * sideForce;
}

}  // namespace

LaunchSimulation::LaunchSimulation(const LaunchParams& params) : params_(params) { reset(); }

Vec3 LaunchSimulation::launchBarHingeWorld(const BodyState& s) const {
    return s.position + rotate(normalized(s.orientation), params_.aircraft.launchBarHinge);
}

double LaunchSimulation::launchBarTipX(const BodyState& s) const {
    const Vec3 hinge = launchBarHingeWorld(s);
    const double length = params_.aircraft.launchBarLength;
    const double drop = std::clamp(hinge.y, 0.0, length * 0.99);
    return hinge.x + std::sqrt(length * length - drop * drop);
}

void LaunchSimulation::reset() {
    const SequenceParams& seq = params_.sequence;
    time_ = seq.start;
    accumulatorNs_ = 0;
    stepCount_ = 0;
    fired_ = false;
    launchBarEngaged_ = true;
    holdbackAttached_ = true;
    brakes_ = true;
    meanTowForce_ = 0.0;
    thrust_ = params_.aircraft.engine.thrustIdle;
    afterburner_ = 0.0;
    stab_ = aileron_ = rudder_ = 0.0;
    attitudeCommand_ = 0.0;
    gearExtension_ = 1.0;
    shuttleVelocity_ = 0.0;
    launchBarRaise_ = 0.0;
    wheelAngles_ = {};
    metrics_ = {};

    settleOnStruts();

    // Slide the jet along the track so the launch bar tip sits in the shuttle at x = 0.
    body_.position.x -= launchBarTipX(body_);
    shuttlePosition_ = 0.0;
    bowHeight_ = body_.position.y;

    // The holdback bar runs aft and down from the nose-gear fitting to a deck anchor.
    const Vec3 fitting = body_.position + rotate(body_.orientation, params_.aircraft.holdbackFitting);
    holdbackAnchor_ = {fitting.x - 1.5, 0.0, 0.0};
    holdbackRestLength_ = length(fitting - holdbackAnchor_);

    publishSnapshot(body_.velocity);
}

void LaunchSimulation::settleOnStruts() {
    // Drop the jet onto its gear with heavy artificial damping so the sequence starts at rest.
    body_ = {};
    body_.position = {0.0, params_.aircraft.cgHeightNominal, 0.0};
    const bool savedHoldback = holdbackAttached_;
    holdbackAttached_ = false;
    for (int i = 0; i < 6000; ++i) {
        integrate(kTimeStep);
        body_.velocity *= 0.995;
        body_.angularVelocity *= 0.995;
        body_.velocity.x = 0.0;
        body_.velocity.z = 0.0;
        body_.position.x = 0.0;
        body_.position.z = 0.0;
    }
    body_.velocity = {};
    body_.angularVelocity = {};
    holdbackAttached_ = savedHoldback;
}

int LaunchSimulation::advance(double frameSeconds) {
    const auto frameNs = static_cast<std::int64_t>(std::llround(std::max(frameSeconds, 0.0) * 1e9));
    accumulatorNs_ += std::min(frameNs, kMaxFrameNanoseconds);
    int steps = 0;
    while (accumulatorNs_ >= kStepNanoseconds) {
        step();
        accumulatorNs_ -= kStepNanoseconds;
        ++steps;
    }
    return steps;
}

void LaunchSimulation::step() {
    const Vec3 previousVelocity = body_.velocity;
    updateSequence();
    updateEngine(kTimeStep);
    updateControls(kTimeStep);
    integrate(kTimeStep);
    ++stepCount_;
    time_ = params_.sequence.start + static_cast<double>(stepCount_) * kTimeStep;
    updateCatapultEvents();
    publishSnapshot(previousVelocity);
}

void LaunchSimulation::updateSequence() {
    const SequenceParams& seq = params_.sequence;
    brakes_ = time_ < seq.tension;
    if (!fired_ && time_ >= seq.fire) {
        fired_ = true;
        // Capacity selector valve: pick the mean shuttle force that reaches the
        // commanded end speed over the power stroke, net of thrust and resistance.
        const AircraftParams& ac = params_.aircraft;
        const CatapultParams& cat = params_.catapult;
        const double v = cat.endSpeed;
        const double meanAirspeed = params_.environment.windOverDeck + 0.5 * v;
        const double drag = 0.5 * kAirDensity * meanAirspeed * meanAirspeed * ac.aero.wingArea * ac.aero.cd0;
        const double rolling = ac.mass * kGravity * ac.noseGear.rollingFriction;
        const double thrust = thrust_ + afterburner_ * (ac.engine.thrustMax - ac.engine.thrustMil);
        meanTowForce_ = ac.mass * v * v / (2.0 * cat.powerStroke) - thrust + drag + rolling;
        meanTowForce_ = std::max(meanTowForce_, 0.0);
    }
}

void LaunchSimulation::updateEngine(double dt) {
    const SequenceParams& seq = params_.sequence;
    const EngineParams& eng = params_.aircraft.engine;
    const bool runUp = time_ >= seq.runUp;
    const double target = runUp ? eng.thrustMil : eng.thrustIdle;
    thrust_ += (target - thrust_) * (1.0 - std::exp(-dt / eng.spoolTimeConstant));
    // Afterburner only on the shooter's signal, a few seconds into the run-up.
    const bool burner = seq.afterburner && time_ >= seq.runUp + 4.0;
    afterburner_ = moveTowards(afterburner_, burner ? 1.0 : 0.0, dt / eng.afterburnerLightTime);
}

void LaunchSimulation::updateControls(double dt) {
    const SequenceParams& seq = params_.sequence;
    const ControlLawParams& cl = params_.controlLaw;
    const BodyState& s = body_;
    const EulerAngles e = toEuler(s.orientation);
    const Vec3& w = s.angularVelocity;

    double stabCmd = 0.0;
    double aileronCmd = 0.0;
    double rudderCmd = 0.0;

    if (time_ >= seq.wipeoutStart && time_ < seq.wipeoutEnd) {
        // Full-travel control wipeout: aft, forward, left, right, rudder left, right.
        const double u = (time_ - seq.wipeoutStart) / (seq.wipeoutEnd - seq.wipeoutStart) * 6.0;
        const int segment = std::min(static_cast<int>(u), 5);
        const double pulse = std::sin((u - segment) * kPi);
        switch (segment) {
            case 0: stabCmd = cl.maxStab * pulse; break;
            case 1: stabCmd = -cl.maxStab * pulse; break;
            case 2: aileronCmd = -cl.maxAileron * pulse; break;
            case 3: aileronCmd = cl.maxAileron * pulse; break;
            case 4: rudderCmd = -cl.maxRudder * pulse; break;
            default: rudderCmd = cl.maxRudder * pulse; break;
        }
    } else if (time_ >= seq.salute && launchBarEngaged_) {
        stabCmd = cl.launchTrimStab;  // hands off: the control law holds launch trim on the stroke
    } else if (!launchBarEngaged_) {
        // Control-law rotation to the climb attitude, then attitude hold.
        attitudeCommand_ = moveTowards(attitudeCommand_, cl.climbAttitude, cl.rotationRate * dt);
        stabCmd = cl.launchTrimStab + cl.pitchGain * (attitudeCommand_ - e.pitch) - cl.pitchRateGain * w.z;
        aileronCmd = -(cl.rollGain * e.roll + cl.rollRateGain * w.x);
        rudderCmd = cl.yawDamperGain * w.y;
    }

    const double rate = degToRad(60.0) * dt;  // actuator rate limit
    stab_ = moveTowards(stab_, std::clamp(stabCmd, -cl.maxStab, cl.maxStab), rate);
    aileron_ = moveTowards(aileron_, std::clamp(aileronCmd, -cl.maxAileron, cl.maxAileron), rate);
    rudder_ = moveTowards(rudder_, std::clamp(rudderCmd, -cl.maxRudder, cl.maxRudder), rate);
}

LaunchSimulation::Forces LaunchSimulation::computeForces(const BodyState& s, double t,
                                                         std::array<GearSnapshot, 3>* gearOut, double* towOut,
                                                         double* holdbackOut) const {
    const AircraftParams& ac = params_.aircraft;
    const CatapultParams& cat = params_.catapult;
    const EnvironmentParams& env = params_.environment;
    const AeroParams& aero = ac.aero;
    const Quat q = normalized(s.orientation);

    Vec3 force{0.0, -ac.mass * kGravity, 0.0};
    Vec3 momentWorld;  // accumulated in the deck frame, converted at the end
    const auto applyAt = [&](const Vec3& point, const Vec3& f) {
        force += f;
        momentWorld += cross(point - s.position, f);
    };

    // Landing gear.
    const std::array<const GearParams*, 3> gears{&ac.noseGear, &ac.leftMainGear, &ac.rightMainGear};
    for (std::size_t i = 0; i < gears.size(); ++i) {
        GearSnapshot snap;
        Vec3 contact;
        const Vec3 reaction = gearReaction(*gears[i], s, gearExtension_, brakes_, env.bowDistance, contact, snap);
        if (snap.inContact) {
            applyAt(contact, reaction);
        }
        if (gearOut != nullptr) {
            (*gearOut)[i] = snap;
        }
    }

    // Launch bar: tow force along the bar, from the hinge down to the shuttle in the slot.
    double tow = 0.0;
    if (launchBarEngaged_ && t >= params_.sequence.tension) {
        tow = cat.tensionForce;
        if (fired_) {
            const double travel = std::clamp(launchBarTipX(s) / cat.powerStroke, 0.0, 1.0);
            const double shape = cat.peakRatio - 2.0 * (cat.peakRatio - 1.0) * travel;
            const double rise = smoothStep(params_.sequence.fire, params_.sequence.fire + cat.pressureRiseTime, t);
            tow = std::max(tow, meanTowForce_ * shape * rise);
        }
        const Vec3 hinge = launchBarHingeWorld(s);
        const Vec3 tip{launchBarTipX(s), 0.0, 0.0};
        applyAt(hinge, normalized(tip - hinge) * tow);
    }
    if (towOut != nullptr) {
        *towOut = tow;
    }

    // Repeatable-release holdback: a stiff tension-only link to the deck.
    double holdback = 0.0;
    if (holdbackAttached_) {
        const Vec3 fitting = s.position + rotate(q, ac.holdbackFitting);
        const Vec3 toAnchor = holdbackAnchor_ - fitting;
        const double stretch = length(toAnchor) - holdbackRestLength_;
        if (stretch > 0.0) {
            const Vec3 dir = normalized(toAnchor);
            const Vec3 fittingVelocity = s.velocity + cross(rotate(q, s.angularVelocity), fitting - s.position);
            const double stretchRate = -dot(fittingVelocity, dir);
            const double damping = 2.0 * 0.3 * std::sqrt(cat.holdbackStiffness * ac.mass);
            holdback = std::max(cat.holdbackStiffness * stretch + damping * stretchRate, 0.0);
            applyAt(fitting, dir * holdback);
        }
    }
    if (holdbackOut != nullptr) {
        *holdbackOut = holdback;
    }

    // Engine thrust along the body axis through the CG.
    const EngineParams& eng = ac.engine;
    const double thrust = thrust_ + afterburner_ * (eng.thrustMax - eng.thrustMil);
    force += rotate(q, Vec3{thrust, 0.0, 0.0});

    // Aerodynamics, in the body frame. Wind over deck is a headwind along the track.
    const Vec3 relativeWind = s.velocity + Vec3{env.windOverDeck, 0.0, 0.0};
    const Vec3 vb = inverseRotate(q, relativeWind);
    const double airspeed = length(vb);
    Vec3 aeroForceBody;
    Vec3 aeroMoment;
    if (airspeed > 1.0) {
        const double alpha = std::atan2(-vb.y, vb.x);
        const double beta = std::asin(std::clamp(vb.z / airspeed, -1.0, 1.0));
        const double dyn = 0.5 * kAirDensity * airspeed * airspeed;
        // Ground effect over the deck, lost abruptly at the bow where the surface drops away.
        const double surface = s.position.x < env.bowDistance ? 0.0 : -env.deckHeight;
        const double h = std::max(s.position.y - surface, 0.1);
        const double ratio = 16.0 * h / aero.span;
        const double inducedFactor = ratio * ratio / (1.0 + ratio * ratio);
        const double groundLift = 1.0 + aero.groundEffectLift * (1.0 - inducedFactor);

        double cl = (aero.cl0 + aero.clAlpha * alpha) * groundLift;
        if (std::abs(cl) > aero.clMax) {
            cl = std::copysign(aero.clMax, cl);
        }
        const double cd0 = aero.cd0Clean + (aero.cd0 - aero.cd0Clean) * gearExtension_;
        const double cd = cd0 + aero.inducedK * inducedFactor * cl * cl;
        const Vec3 liftDir{std::sin(alpha), std::cos(alpha), 0.0};
        const Vec3 dragDir = vb * (-1.0 / airspeed);
        aeroForceBody = liftDir * (dyn * aero.wingArea * cl) + dragDir * (dyn * aero.wingArea * cd);
        aeroForceBody.z += dyn * aero.wingArea * aero.cyBeta * beta;

        const Vec3& w = s.angularVelocity;
        const double b2v = aero.span / (2.0 * airspeed);
        const double c2v = aero.chord / (2.0 * airspeed);
        const double rollCoeff = aero.clBeta * beta + aero.clP * w.x * b2v + aero.clAileron * aileron_;
        const double yawCoeff = -aero.cnBeta * beta + aero.cnR * w.y * b2v - aero.cnRudder * rudder_;
        const double pitchCoeff = aero.cm0 + aero.cmAlpha * alpha + aero.cmQ * w.z * c2v + aero.cmStab * stab_;
        aeroMoment = {dyn * aero.wingArea * aero.span * rollCoeff, dyn * aero.wingArea * aero.span * yawCoeff,
                      dyn * aero.wingArea * aero.chord * pitchCoeff};
    }
    force += rotate(q, aeroForceBody);

    return {force, inverseRotate(q, momentWorld) + aeroMoment};
}

void LaunchSimulation::integrate(double dt) {
    const AircraftParams& ac = params_.aircraft;
    const auto derivative = [&](const BodyState& state, double t) {
        const Forces f = computeForces(state, t, nullptr, nullptr, nullptr);
        Derivative d;
        d.dPosition = state.velocity;
        d.dVelocity = f.force / ac.mass;
        const Vec3& w = state.angularVelocity;
        d.dOrientation = (state.orientation * Quat{0.0, w.x, w.y, w.z}) * 0.5;
        const Vec3 net = f.moment - cross(w, hadamard(ac.inertia, w));
        d.dAngularVelocity = {net.x / ac.inertia.x, net.y / ac.inertia.y, net.z / ac.inertia.z};
        return d;
    };

    const BodyState& s = body_;
    const Derivative k1 = derivative(s, time_);
    const Derivative k2 = derivative(offsetState(s, k1, 0.5 * dt), time_ + 0.5 * dt);
    const Derivative k3 = derivative(offsetState(s, k2, 0.5 * dt), time_ + 0.5 * dt);
    const Derivative k4 = derivative(offsetState(s, k3, dt), time_ + dt);

    const double h = dt / 6.0;
    BodyState next;
    next.position = s.position + (k1.dPosition + k2.dPosition * 2.0 + k3.dPosition * 2.0 + k4.dPosition) * h;
    next.velocity = s.velocity + (k1.dVelocity + k2.dVelocity * 2.0 + k3.dVelocity * 2.0 + k4.dVelocity) * h;
    next.orientation = normalized(
        s.orientation + (k1.dOrientation + k2.dOrientation * 2.0 + k3.dOrientation * 2.0 + k4.dOrientation) * h);
    next.angularVelocity =
        s.angularVelocity +
        (k1.dAngularVelocity + k2.dAngularVelocity * 2.0 + k3.dAngularVelocity * 2.0 + k4.dAngularVelocity) * h;
    body_ = next;
}

void LaunchSimulation::updateCatapultEvents() {
    const CatapultParams& cat = params_.catapult;
    const SequenceParams& seq = params_.sequence;
    double tow = 0.0;
    double holdback = 0.0;
    std::array<GearSnapshot, 3> gear;
    computeForces(body_, time_, &gear, &tow, &holdback);

    if (holdbackAttached_ && holdback > cat.holdbackReleaseLoad) {
        holdbackAttached_ = false;
        metrics_.releaseTime = time_;
    }

    if (launchBarEngaged_) {
        shuttlePosition_ = std::max(launchBarTipX(body_), 0.0);
        shuttleVelocity_ = body_.velocity.x;
        if (fired_ && shuttlePosition_ >= cat.powerStroke) {
            launchBarEngaged_ = false;
            metrics_.separationTime = time_;
            metrics_.endSpeed = body_.velocity.x;
            attitudeCommand_ = toEuler(body_.orientation).pitch;
        }
    } else {
        // The shuttle slams into the water brake within the run-out at the end of the track.
        const double runout = std::max(cat.trackLength - cat.powerStroke, 0.5);
        const double decel = std::max(cat.waterBrakeDeceleration,
                                      metrics_.endSpeed * metrics_.endSpeed / (2.0 * runout));
        shuttleVelocity_ = std::max(shuttleVelocity_ - decel * kTimeStep, 0.0);
        shuttlePosition_ = std::min(shuttlePosition_ + shuttleVelocity_ * kTimeStep, cat.trackLength);
        // Launch bar comes up once the jet is clear.
        launchBarRaise_ = std::min(launchBarRaise_ + kTimeStep / 1.5, 1.0);
    }

    // Gear retraction after the jet is clear of the bow.
    if (metrics_.bowCrossingTime >= 0.0 && time_ > metrics_.bowCrossingTime + seq.gearUpDelay) {
        gearExtension_ = std::max(gearExtension_ - kTimeStep / seq.gearTransitTime, 0.0);
    }

    // Wheel spin for animation.
    const std::array<const GearParams*, 3> gears{&params_.aircraft.noseGear, &params_.aircraft.leftMainGear,
                                                 &params_.aircraft.rightMainGear};
    for (std::size_t i = 0; i < 3; ++i) {
        if (gear[i].inContact) {
            const Vec3 nose = rotate(body_.orientation, body::kForward);
            wheelAngles_[i] += dot(body_.velocity, normalized(Vec3{nose.x, 0.0, nose.z})) / gears[i]->tyreRadius *
                               kTimeStep;
        }
    }
}

void LaunchSimulation::publishSnapshot(const Vec3& previousVelocity) {
    const SequenceParams& seq = params_.sequence;
    const EnvironmentParams& env = params_.environment;
    SystemsSnapshot& out = snapshot_;
    out.time = time_;
    out.body = body_;

    double tow = 0.0;
    double holdback = 0.0;
    computeForces(body_, time_, &out.gear, &tow, &holdback);
    for (std::size_t i = 0; i < 3; ++i) {
        out.gear[i].wheelAngle = wheelAngles_[i];
    }
    out.towForce = tow;
    out.holdbackLoad = holdback;
    out.launchBarEngaged = launchBarEngaged_;
    out.holdbackAttached = holdbackAttached_;
    out.launchBarRaise = launchBarRaise_;
    out.shuttlePosition = shuttlePosition_;
    out.shuttleVelocity = shuttleVelocity_;
    out.gearExtension = gearExtension_;
    out.stabilator = stab_;
    out.aileron = aileron_;
    out.rudder = rudder_;

    const EngineParams& eng = params_.aircraft.engine;
    out.thrust = thrust_ + afterburner_ * (eng.thrustMax - eng.thrustMil);
    out.afterburner = afterburner_;
    out.nozzleGlow = std::clamp((thrust_ - eng.thrustIdle) / (eng.thrustMil - eng.thrustIdle), 0.0, 1.0);

    const Vec3 vb = inverseRotate(body_.orientation, body_.velocity + Vec3{env.windOverDeck, 0.0, 0.0});
    out.airspeed = length(vb);
    out.angleOfAttack = out.airspeed > 1.0 ? std::atan2(-vb.y, vb.x) : 0.0;
    const Vec3 accel = (body_.velocity - previousVelocity) / kTimeStep;
    const Vec3 specificForce = accel + Vec3{0.0, kGravity, 0.0};
    out.longitudinalG = inverseRotate(body_.orientation, specificForce).x / kGravity;
    out.heightAboveDeck = body_.position.y;
    out.ditched = body_.position.y < -env.deckHeight + 1.0;

    // Phase and crew cue from the sequence clock and the catapult events.
    if (!launchBarEngaged_) {
        out.phase = (metrics_.bowCrossingTime >= 0.0 && time_ > metrics_.bowCrossingTime + 3.0)
                        ? LaunchPhase::Climbout
                        : LaunchPhase::OverTheBow;
        out.crewCue = CrewCue::Launched;
    } else if (fired_) {
        out.phase = LaunchPhase::Stroke;
        out.crewCue = CrewCue::Launched;
    } else if (time_ >= seq.shooterSignal) {
        out.phase = LaunchPhase::ShooterSignal;
        out.crewCue = CrewCue::LaunchSignal;
    } else if (time_ >= seq.salute) {
        out.phase = LaunchPhase::Salute;
        out.crewCue = CrewCue::ReturnSalute;
    } else if (time_ >= seq.wipeoutEnd) {
        out.phase = LaunchPhase::FinalChecks;
        out.crewCue = CrewCue::FinalChecks;
    } else if (time_ >= seq.wipeoutStart) {
        out.phase = LaunchPhase::Wipeout;
        out.crewCue = CrewCue::FinalChecks;
    } else if (time_ >= seq.runUp) {
        out.phase = LaunchPhase::RunUp;
        out.crewCue = (seq.afterburner && time_ >= seq.runUp + 3.5) ? CrewCue::Afterburner : CrewCue::RunUp;
    } else if (time_ >= seq.tension) {
        out.phase = LaunchPhase::Tension;
        out.crewCue = CrewCue::TakeTension;
    } else {
        out.phase = LaunchPhase::Hookup;
        out.crewCue = CrewCue::Standby;
    }

    // Metrics.
    if (out.phase == LaunchPhase::Stroke) {
        metrics_.peakLongitudinalG = std::max(metrics_.peakLongitudinalG, out.longitudinalG);
        metrics_.peakNoseCompression = std::max(metrics_.peakNoseCompression, out.gear[0].compression);
    }
    if (out.phase == LaunchPhase::RunUp) {
        metrics_.tensionNoseCompression = std::max(metrics_.tensionNoseCompression, out.gear[0].compression);
    }
    if (metrics_.bowCrossingTime < 0.0 && body_.position.x >= env.bowDistance) {
        metrics_.bowCrossingTime = time_;
        bowHeight_ = body_.position.y;
    }
    if (metrics_.bowCrossingTime >= 0.0) {
        metrics_.minSinkBelowDeck = std::min(metrics_.minSinkBelowDeck, body_.position.y - bowHeight_);
    }
}

}  // namespace launchsim
