// Copyright (c) 2026 nefariousjosiah. MIT License.

#include "AirframeRig.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	// Blender export frame (metres, +X nose, +Y left, +Z up) to Unreal (cm, +Y right).
	FVector RigPoint(const TArray<TSharedPtr<FJsonValue>>& V)
	{
		return FVector(V[0]->AsNumber(), -V[1]->AsNumber(), V[2]->AsNumber()) * 100.0;
	}

	FVector RigDirection(const TArray<TSharedPtr<FJsonValue>>& V)
	{
		return FVector(V[0]->AsNumber(), -V[1]->AsNumber(), V[2]->AsNumber()).GetSafeNormal();
	}
}

FAirframeRig FAirframeRig::Load(const FString& JsonPath)
{
	FAirframeRig Rig;
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *JsonPath))
	{
		return Rig;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		return Rig;
	}

	const TSharedPtr<FJsonObject>* Parts = nullptr;
	if (Root->TryGetObjectField(TEXT("parts"), Parts))
	{
		for (const auto& Pair : (*Parts)->Values)
		{
			const TSharedPtr<FJsonObject> Entry = Pair.Value->AsObject();
			FAirframeRigPart Part;
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (Entry->TryGetArrayField(TEXT("pivot"), Values) && Values->Num() == 3)
			{
				Part.Pivot = RigPoint(*Values);
				Part.bHasPivot = true;
			}
			if (Entry->TryGetArrayField(TEXT("axis"), Values) && Values->Num() == 3)
			{
				Part.Axis = RigDirection(*Values);
				Part.bHasAxis = true;
			}
			Rig.Parts.Add(FName(*Pair.Key), Part);
		}
	}

	const TSharedPtr<FJsonObject>* Wheels = nullptr;
	const TSharedPtr<FJsonObject>* Tops = nullptr;
	if (Root->TryGetObjectField(TEXT("wheels"), Wheels) && Root->TryGetObjectField(TEXT("strut_tops"), Tops))
	{
		const TCHAR* WheelNames[3] = {TEXT("noseWheel"), TEXT("leftWheel"), TEXT("rightWheel")};
		const TCHAR* TopNames[3] = {TEXT("noseGear"), TEXT("leftGear"), TEXT("rightGear")};
		for (int32 i = 0; i < 3; ++i)
		{
			const TSharedPtr<FJsonObject>* Wheel = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Top = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Axle = nullptr;
			if ((*Wheels)->TryGetObjectField(WheelNames[i], Wheel) && (*Tops)->TryGetArrayField(TopNames[i], Top) &&
			    (*Wheel)->TryGetArrayField(TEXT("axle"), Axle))
			{
				Rig.AuthoredStrutLength[i] = RigPoint(*Top).Z - RigPoint(*Axle).Z;
			}
		}
	}
	Rig.bValid = Rig.Parts.Num() > 0;
	return Rig;
}

FTransform FAirframeRig::Hinge(const FAirframeRigPart& Part, double Angle, const FVector& ReferenceDirection,
                               const FVector& DesiredMotion, const FVector& Offset)
{
	if (!Part.bHasPivot || !Part.bHasAxis)
	{
		return FTransform(Offset);
	}
	const double Sign = FVector::DotProduct(FVector::CrossProduct(Part.Axis, ReferenceDirection), DesiredMotion) >= 0.0
		                    ? 1.0
		                    : -1.0;
	const FQuat Rotation(Part.Axis, Sign * Angle);
	// Vertices are in the airframe frame: v' = R (v - p) + p + offset.
	return FTransform(Rotation, Part.Pivot - Rotation.RotateVector(Part.Pivot) + Offset);
}
