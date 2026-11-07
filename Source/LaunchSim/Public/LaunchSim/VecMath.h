#pragma once

// Minimal double-precision vector and quaternion math for the simulator core.
//
// Frame conventions (see docs/architecture.md):
//   World frame: right-handed, +Y up. The drone starts at the origin facing +X.
//   Body frame:  +X forward (nose), +Y up (thrust direction), +Z right.
//   Quaternions are Hamilton, unit length, and rotate body-frame vectors into the world frame.

#include <algorithm>
#include <cmath>

namespace launchsim {

inline constexpr double kPi = 3.14159265358979323846;

constexpr double degToRad(double degrees) { return degrees * kPi / 180.0; }
constexpr double radToDeg(double radians) { return radians * 180.0 / kPi; }

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    constexpr Vec3& operator+=(const Vec3& o) {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    constexpr Vec3& operator-=(const Vec3& o) {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        return *this;
    }
    constexpr Vec3& operator*=(double s) {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
    constexpr bool operator==(const Vec3&) const = default;
};

constexpr Vec3 operator*(double s, const Vec3& v) { return v * s; }
constexpr double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
/// Component-wise product. Used to apply a diagonal inertia tensor.
constexpr Vec3 hadamard(const Vec3& a, const Vec3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline double length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalized(const Vec3& v) {
    const double len = length(v);
    return len > 0.0 ? v / len : Vec3{};
}

/// Unit vectors of the body frame, expressed in body coordinates.
namespace body {
inline constexpr Vec3 kForward{1.0, 0.0, 0.0};
inline constexpr Vec3 kUp{0.0, 1.0, 0.0};
inline constexpr Vec3 kRight{0.0, 0.0, 1.0};
}  // namespace body

struct Quat {
    double w = 1.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    /// Hamilton product: (a * b) applies b first, then a.
    constexpr Quat operator*(const Quat& o) const {
        return {
            w * o.w - x * o.x - y * o.y - z * o.z,
            w * o.x + x * o.w + y * o.z - z * o.y,
            w * o.y - x * o.z + y * o.w + z * o.x,
            w * o.z + x * o.y - y * o.x + z * o.w,
        };
    }
    constexpr Quat operator+(const Quat& o) const { return {w + o.w, x + o.x, y + o.y, z + o.z}; }
    constexpr Quat operator*(double s) const { return {w * s, x * s, y * s, z * s}; }
    constexpr bool operator==(const Quat&) const = default;
};

constexpr Quat conjugate(const Quat& q) { return {q.w, -q.x, -q.y, -q.z}; }
inline double norm(const Quat& q) { return std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z); }
inline Quat normalized(const Quat& q) {
    const double n = norm(q);
    return n > 0.0 ? q * (1.0 / n) : Quat{};
}

/// Rotation of `angle` radians about `axis` (right-hand rule).
inline Quat fromAxisAngle(const Vec3& axis, double angle) {
    const Vec3 n = normalized(axis);
    const double s = std::sin(0.5 * angle);
    return {std::cos(0.5 * angle), n.x * s, n.y * s, n.z * s};
}

/// Rotates v by unit quaternion q (q v q*), without building a matrix.
constexpr Vec3 rotate(const Quat& q, const Vec3& v) {
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = 2.0 * cross(u, v);
    return v + q.w * t + cross(u, t);
}
constexpr Vec3 inverseRotate(const Quat& q, const Vec3& v) { return rotate(conjugate(q), v); }

/// Aircraft-style attitude angles in radians.
///   roll  > 0: right side down
///   pitch > 0: nose up
///   yaw   > 0: nose turned left (counter-clockwise seen from above, i.e. about +Y)
struct EulerAngles {
    double roll = 0.0;
    double pitch = 0.0;
    double yaw = 0.0;
};

/// Builds yaw (about Y) * pitch (about body Z) * roll (about body X).
inline Quat fromEuler(const EulerAngles& e) {
    return fromAxisAngle(body::kUp, e.yaw) * fromAxisAngle(body::kRight, e.pitch) *
           fromAxisAngle(body::kForward, e.roll);
}

/// Inverse of fromEuler. Roll is ill-defined when pitched straight up or down.
inline EulerAngles toEuler(const Quat& q) {
    const Vec3 f = rotate(q, body::kForward);
    const Vec3 r = rotate(q, body::kRight);
    const Vec3 u = rotate(q, body::kUp);
    return {std::atan2(-r.y, u.y), std::asin(std::clamp(f.y, -1.0, 1.0)), std::atan2(-f.z, f.x)};
}

/// Cosine of the angle between body up and world up: 1 = level, 0 = on its side, < 0 = inverted.
constexpr double tiltCosine(const Quat& q) { return rotate(q, body::kUp).y; }

}  // namespace launchsim
