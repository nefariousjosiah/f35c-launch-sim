// Copyright (c) 2026 nefariousjosiah. MIT License.
#pragma once

#include "CoreMinimal.h"

/// Hinge and strut data for the imported aircraft model, read from the rig.json
/// written by Tools/Blender/export_f35_parts.py.
///
/// All parts are imported in the centre-of-gravity frame, so a part at rest has an
/// identity transform relative to the airframe; animation is a rotation about the
/// part's pivot, or a slide along its strut axis. Values here are in Unreal space
/// (centimetres; the Blender export's +Y left maps to Unreal -Y).
struct FAirframeRigPart
{
	FVector Pivot = FVector::ZeroVector;
	FVector Axis = FVector::ZeroVector;
	bool bHasPivot = false;
	bool bHasAxis = false;
};

struct FAirframeRig
{
	TMap<FName, FAirframeRigPart> Parts;
	/// Modelled strut length, strut top to axle, for nose / left / right (cm).
	double AuthoredStrutLength[3] = {0.0, 0.0, 0.0};
	/// Engine nozzle exit centre (cm, airframe frame) and radius (cm), for exhaust effects.
	FVector NozzleExit = FVector(-450.0, 0.0, 20.0);
	double NozzleRadius = 60.0;
	bool bValid = false;

	static FAirframeRig Load(const FString& JsonPath);

	/// Relative transform that rotates a part by Angle (radians) about its hinge.
	/// The sign is chosen so that a positive angle moves ReferenceDirection (from the
	/// pivot towards, e.g., the trailing edge) along DesiredMotion, which keeps every
	/// surface's sign convention explicit regardless of how the model's axes point.
	static FTransform Hinge(const FAirframeRigPart& Part, double Angle, const FVector& ReferenceDirection,
	                        const FVector& DesiredMotion, const FVector& Offset = FVector::ZeroVector);
};
