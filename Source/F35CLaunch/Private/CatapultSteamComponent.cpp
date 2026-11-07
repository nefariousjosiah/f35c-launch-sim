// Copyright (c) 2026 nefariousjosiah. MIT License.

#include "CatapultSteamComponent.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

UCatapultSteamComponent::UCatapultSteamComponent()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Materials/M_SteamPuff.M_SteamPuff"));
	SetStaticMesh(Plane.Object);
	if (Material.Object != nullptr)
	{
		SetMaterial(0, Material.Object);
	}
	NumCustomDataFloats = 1;  // per-puff opacity
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
	Random.Initialize(2014);  // 4 November 2014
}

void UCatapultSteamComponent::ResetSteam()
{
	Puffs.SetNum(MaxPuffs);
	for (FPuff& Puff : Puffs)
	{
		Puff.bAlive = false;
	}
	NextSlot = 0;
	LeakAccumulator = StrokeAccumulator = 0.0f;
	Random.Initialize(2014);
	if (!bPoolReady)
	{
		ClearInstances();
		TArray<FTransform> Hidden;
		Hidden.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), MaxPuffs);
		AddInstances(Hidden, false);
		bPoolReady = true;
	}
}

void UCatapultSteamComponent::Spawn(const FVector& Position, const FVector& Velocity, float Size, float Life, float Growth)
{
	FPuff& Puff = Puffs[NextSlot];
	NextSlot = (NextSlot + 1) % MaxPuffs;
	Puff.Position = Position;
	Puff.Velocity = Velocity;
	Puff.Age = 0.0f;
	Puff.Life = Life;
	Puff.Size0 = Size;
	Puff.Growth = Growth;
	Puff.Phase = Random.FRandRange(0.0f, 2.0f * PI);
	Puff.bAlive = true;
}

void UCatapultSteamComponent::Step(float DeltaSeconds, double ShotTime, double ShuttleX, bool bShuttleRunning,
                                   bool bJustSeparated, const FVector& CameraLocal)
{
	if (Puffs.Num() != MaxPuffs)
	{
		ResetSteam();
	}
	const float Dt = FMath::Clamp(DeltaSeconds, 0.0f, 0.1f);

	// Slow leak from the slot seals along the track, before and between shots.
	LeakAccumulator += LeakRate * Dt;
	while (LeakAccumulator >= 1.0f)
	{
		LeakAccumulator -= 1.0f;
		const float X = Random.FRandRange(-1.0f, PowerStroke);
		Spawn(FVector(X, Random.FRandRange(-0.05f, 0.05f), 0.05f),
		      FVector(0.0f, Random.FRandRange(-0.2f, 0.2f), Random.FRandRange(0.2f, 0.6f)),
		      Random.FRandRange(0.25f, 0.5f), Random.FRandRange(1.5f, 3.0f), 1.4f);
	}

	// The stroke unzips the slot: steam bursts out just behind the moving shuttle.
	if (bShuttleRunning && ShotTime > 0.0)
	{
		StrokeAccumulator += StrokeRate * Dt;
		while (StrokeAccumulator >= 1.0f)
		{
			StrokeAccumulator -= 1.0f;
			const float X = static_cast<float>(ShuttleX) - Random.FRandRange(0.5f, 4.0f);
			Spawn(FVector(X, Random.FRandRange(-0.15f, 0.15f), 0.1f),
			      FVector(Random.FRandRange(-1.0f, 1.0f), Random.FRandRange(-0.8f, 0.8f), Random.FRandRange(1.0f, 2.5f)),
			      Random.FRandRange(0.5f, 0.9f), Random.FRandRange(2.5f, 4.0f), 1.1f);
		}
	}

	// Spent steam vents where the shuttle stops at the end of the stroke.
	if (bJustSeparated)
	{
		for (int32 i = 0; i < EndOfStrokePuffs; ++i)
		{
			Spawn(FVector(PowerStroke + Random.FRandRange(0.0f, 4.0f), Random.FRandRange(-0.6f, 0.6f), 0.2f),
			      FVector(Random.FRandRange(2.0f, 8.0f), Random.FRandRange(-2.0f, 2.0f), Random.FRandRange(1.5f, 4.0f)),
			      Random.FRandRange(0.8f, 1.5f), Random.FRandRange(3.0f, 5.0f), 0.9f);
		}
	}

	// Advect: wind over the deck carries everything aft; slow buoyant rise; gentle swirl.
	for (FPuff& Puff : Puffs)
	{
		if (!Puff.bAlive)
		{
			continue;
		}
		Puff.Age += Dt;
		if (Puff.Age >= Puff.Life)
		{
			Puff.bAlive = false;
			continue;
		}
		const FVector Wind(-WindOverDeck, 0.0f, 0.4f);
		const float Drag = 1.0f - FMath::Exp(-2.5f * Dt);  // puffs relax towards the wind
		Puff.Velocity += (Wind - Puff.Velocity) * Drag;
		const float Swirl = Puff.Phase + Puff.Age * 2.3f;
		Puff.Position += (Puff.Velocity + FVector(0.0f, FMath::Sin(Swirl) * 0.6f, FMath::Cos(Swirl * 1.3f) * 0.3f)) * Dt;
	}
	RefreshInstances(CameraLocal);
}

void UCatapultSteamComponent::RefreshInstances(const FVector& CameraLocal)
{
	TArray<FTransform> Transforms;
	Transforms.Reserve(MaxPuffs);
	for (int32 i = 0; i < MaxPuffs; ++i)
	{
		const FPuff& Puff = Puffs[i];
		if (!Puff.bAlive)
		{
			Transforms.Add(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector));
			SetCustomDataValue(i, 0, 0.0f, false);
			continue;
		}
		const FVector Location = Puff.Position * 100.0f;
		// Billboard: the engine plane faces +Z, so point +Z at the camera.
		const FVector ToCamera = (CameraLocal - Location).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const FQuat Facing = FRotationMatrix::MakeFromZ(ToCamera).ToQuat() * FQuat(FVector::UpVector, Puff.Phase);
		const float Size = Puff.Size0 * (1.0f + Puff.Growth * Puff.Age);
		Transforms.Add(FTransform(Facing, Location, FVector(Size)));
		const float T = Puff.Age / Puff.Life;
		const float Opacity = FMath::SmoothStep(0.0f, 0.12f, T) * FMath::Pow(1.0f - T, 1.6f);
		SetCustomDataValue(i, 0, Opacity, false);
	}
	BatchUpdateInstancesTransforms(0, Transforms, false, true, true);
}
