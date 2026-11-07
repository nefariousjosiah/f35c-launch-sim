// Copyright (c) 2026 nefariousjosiah. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShotDirector.generated.h"

class ACineCameraActor;
class ACatapultDeck;
class AF35CLaunchActor;

/// How a shot is operated, after the three camera set-ups that cover nearly all real
/// carrier launch footage (Docs/Reference.md, Docs/Cameras.md).
UENUM()
enum class ELaunchShotStyle : uint8
{
	/// Deck or catwalk photographer: wide-to-normal lens, panning hard with lag and overshoot.
	Handheld,
	/// Long lens from the island ("Vulture's Row"): steadier, but every wobble is magnified.
	Telephoto,
	/// Fixed camera (bow or deck-edge): no operator; vibrates when the catapult fires.
	LockedOff,
};

USTRUCT()
struct FLaunchShot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Shot")
	FName Name;

	/// Cut to this shot at this time, seconds relative to the launch valve opening.
	UPROPERTY(EditAnywhere, Category = "Shot")
	double CutTime = 0.0;

	/// Camera position in the deck frame (cm; +X down the track, +Y starboard, +Z up).
	UPROPERTY(EditAnywhere, Category = "Shot")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Shot")
	double FocalLength = 35.0;

	UPROPERTY(EditAnywhere, Category = "Shot")
	ELaunchShotStyle Style = ELaunchShotStyle::Handheld;

	/// Locked-off shots look at this deck-frame point (cm) instead of following the jet.
	UPROPERTY(EditAnywhere, Category = "Shot")
	FVector LockedTarget = FVector::ZeroVector;

	/// Operator: natural frequency (Hz) and damping ratio of the pan/tilt response, and
	/// reaction delay (s). Under-damped with a delay gives the lag and overshoot of a
	/// human following a fast subject.
	UPROPERTY(EditAnywhere, Category = "Shot")
	double TrackingHz = 1.2;

	UPROPERTY(EditAnywhere, Category = "Shot")
	double TrackingDamping = 0.72;

	UPROPERTY(EditAnywhere, Category = "Shot")
	double ReactionDelay = 0.12;

	/// Handheld wander, degrees (scaled down automatically for long lenses).
	UPROPERTY(EditAnywhere, Category = "Shot")
	double ShakeDegrees = 0.35;

	/// Aim point relative to the jet's centre of gravity, cm (operators frame a little ahead).
	UPROPERTY(EditAnywhere, Category = "Shot")
	FVector AimOffset = FVector(150.0, 0.0, 50.0);
};

/// Cuts between cameras and operates them like people would.
///
/// Each camera has its own simulated operator running all the time, so cutting back to
/// a camera finds it already following the jet. Operators react after a short delay and
/// track with an under-damped spring (lag, then a little overshoot), add handheld noise,
/// and get kicked by the catapult's thump. Ship-mounted cameras roll and pitch with the
/// ship, so the horizon drifts. Exposure is physical and locked per camera; focus follows
/// the jet with a slight lag. Everything advances on simulation time, so recorded
/// sequences are reproducible frame for frame.
UCLASS()
class F35CLAUNCH_API AShotDirector : public AActor
{
	GENERATED_BODY()

public:
	AShotDirector();
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Director")
	TObjectPtr<AF35CLaunchActor> Launch;

	UPROPERTY(EditAnywhere, Category = "Director")
	TArray<FLaunchShot> Shots;

	/// Physical exposure shared by all cameras (see Docs/RenderSettings.md).
	UPROPERTY(EditAnywhere, Category = "Director")
	double FStop = 8.0;

	UPROPERTY(EditAnywhere, Category = "Director")
	double ShutterSpeed = 200.0;

	UPROPERTY(EditAnywhere, Category = "Director")
	double ISO = 100.0;

	/// Ship motion in a calm Southern California Bight sea: slow roll and pitch.
	UPROPERTY(EditAnywhere, Category = "Director")
	double ShipRollDegrees = 0.35;

	UPROPERTY(EditAnywhere, Category = "Director")
	double ShipRollPeriod = 11.0;

	UPROPERTY(EditAnywhere, Category = "Director")
	double ShipPitchDegrees = 0.2;

	UPROPERTY(EditAnywhere, Category = "Director")
	double ShipPitchPeriod = 7.5;

	/// Lens character: film grain, chromatic aberration (fringe), vignette.
	UPROPERTY(EditAnywhere, Category = "Director")
	double FilmGrain = 0.18;

	UPROPERTY(EditAnywhere, Category = "Director")
	double ChromaticAberration = 0.25;

	UPROPERTY(EditAnywhere, Category = "Director")
	double Vignette = 0.35;

protected:
	virtual void BeginPlay() override;

private:
	struct FOperator
	{
		double Yaw = 0.0;      // degrees, unwrapped
		double Pitch = 0.0;
		double YawRate = 0.0;  // degrees per second
		double PitchRate = 0.0;
		double Focus = 2000.0; // cm
		TArray<TPair<double, FVector>> TargetHistory;  // (sim time, deck-frame target)
		bool bInitialised = false;
	};

	void CreateCameras();
	void ConfigureCamera(ACineCameraActor* Camera, const FLaunchShot& Shot) const;
	void StepOperator(int32 Index, double Time, double Dt, const FVector& Target);
	FTransform ShipMotion(double Time) const;
	int32 ShotIndexAt(double Time) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ACineCameraActor>> Cameras;

	UPROPERTY(Transient)
	TObjectPtr<ACatapultDeck> Deck;

	TArray<FOperator> Operators;
	FTransform DeckRest;
	int32 ActiveShot = -1;
	double LastTime = 0.0;
	bool bHaveLastTime = false;
};
