// Copyright (c) 2026 nefariousjosiah. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "CatapultSteamComponent.generated.h"

/// Catapult steam: a small deterministic particle simulation drawn as camera-facing
/// instanced quads with a lit translucent puff material.
///
/// Matches the reference behaviour (Docs/Reference.md):
///   - low wisps leaking from the track slot before the shot,
///   - a burst that "unzips" behind the shuttle as it runs down the stroke,
///   - a heavier puff where the spent steam vents at the end of the stroke,
/// all advected aft by the wind over the deck and growing as they dissipate.
///
/// The component lives in the deck frame (Unreal axes: +X down the track, +Z up).
/// Particles are seeded from a fixed stream, so captured frames are reproducible.
UCLASS(ClassGroup = (Effects), meta = (BlueprintSpawnableComponent))
class F35CLAUNCH_API UCatapultSteamComponent : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	UCatapultSteamComponent();

	/// Advances the simulation by DeltaSeconds of simulation time.
	/// ShotTime is seconds since the launch valve opened (negative before the shot).
	void Step(float DeltaSeconds, double ShotTime, double ShuttleX, bool bShuttleRunning, bool bJustSeparated,
	          const FVector& CameraLocal);

	void ResetSteam();

	/// Wind over the deck, m/s; steam drifts aft at this speed.
	UPROPERTY(EditAnywhere, Category = "Steam")
	float WindOverDeck = 13.9f;

	UPROPERTY(EditAnywhere, Category = "Steam")
	float PowerStroke = 94.4f;

	UPROPERTY(EditAnywhere, Category = "Steam")
	float LeakRate = 8.0f;  // wisps per second along the slot

	UPROPERTY(EditAnywhere, Category = "Steam")
	float StrokeRate = 160.0f;  // puffs per second behind the moving shuttle

	UPROPERTY(EditAnywhere, Category = "Steam")
	int32 EndOfStrokePuffs = 45;

private:
	struct FPuff
	{
		FVector Position;  // metres, deck frame (Unreal axes)
		FVector Velocity;  // m/s
		float Age = 0.0f;
		float Life = 1.0f;
		float Size0 = 1.0f;  // metres
		float Growth = 1.0f; // size multiplier per second
		float Phase = 0.0f;  // turbulence phase
		bool bAlive = false;
	};

	void Spawn(const FVector& Position, const FVector& Velocity, float Size, float Life, float Growth);
	void RefreshInstances(const FVector& CameraLocal);

	static constexpr int32 MaxPuffs = 700;
	TArray<FPuff> Puffs;
	int32 NextSlot = 0;
	float LeakAccumulator = 0.0f;
	float StrokeAccumulator = 0.0f;
	FRandomStream Random;
	bool bPoolReady = false;
};
