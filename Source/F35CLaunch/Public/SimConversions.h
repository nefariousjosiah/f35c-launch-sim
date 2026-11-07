// Copyright (c) 2026 nefariousjosiah. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "LaunchSim/VecMath.h"

/// Conversions from the LaunchSim deck frame to Unreal space.
///
/// LaunchSim: right-handed, metres, +X down the catapult track, +Y up, +Z starboard.
/// Unreal:    left-handed, centimetres, +X forward, +Y right, +Z up.
/// The axes map one-to-one (X->X, Z->Y, Y->Z); swapping two axes is exactly the
/// change of handedness, so no sign flips are needed for positions.
namespace SimToUnreal
{
	inline FVector Location(const launchsim::Vec3& V)
	{
		return FVector(V.x * 100.0, V.z * 100.0, V.y * 100.0);
	}

	inline FVector Direction(const launchsim::Vec3& V)
	{
		return FVector(V.x, V.z, V.y);
	}

	/// Rotations are rebuilt from the rotated body axes rather than by permuting
	/// quaternion components, which sidesteps the handedness sign trap entirely.
	inline FQuat Rotation(const launchsim::Quat& Q)
	{
		const FVector Forward = Direction(launchsim::rotate(Q, launchsim::body::kForward));
		const FVector Up = Direction(launchsim::rotate(Q, launchsim::body::kUp));
		return FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();
	}
}
