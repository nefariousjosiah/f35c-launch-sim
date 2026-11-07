// Copyright (c) 2026 nefariousjosiah. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "AirframeRig.h"
#include "GameFramework/Actor.h"
#include "LaunchSim/LaunchSimulation.h"
#include "F35CLaunchActor.generated.h"

class ACatapultDeck;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UCatapultSteamComponent;

/// Runs one catapult launch and drives everything visible from it.
///
/// The launch physics lives in LaunchSim (pure C++, unit-tested outside Unreal).
/// This actor advances it at the fixed 1 kHz rate from the frame time, then poses
/// the airframe and its moving parts from the per-step SystemsSnapshot: control
/// surfaces about their hinge lines, gear struts sliding with the simulated
/// compression, wheels rolling with deck speed, plus the launch bar, shuttle and jet
/// blast deflector. It sits at the deck actor's origin, the simulation's deck frame.
///
/// The aircraft model is third-party and not in the repository (see ASSETS.md). If it
/// is missing, a blockout made of engine primitives stands in.
///
/// Command line, for unattended output (files go to Saved/Captures):
///   -LaunchCapture=<file.png> -LaunchCaptureTime=<s>   one frame at that time
///   -LaunchRecord=<folder> [-LaunchRecordFrom=-8 -LaunchRecordTo=9 -LaunchRecordFPS=30]
///       a numbered frame sequence on a fixed timeline, for encoding to video
UCLASS()
class F35CLAUNCH_API AF35CLaunchActor : public AActor
{
	GENERATED_BODY()

public:
	AF35CLaunchActor();
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Launch")
	TObjectPtr<ACatapultDeck> Deck;

	/// Camera to view through when play starts (a Cine Camera Actor in the level).
	UPROPERTY(EditAnywhere, Category = "Launch")
	TObjectPtr<AActor> ViewCamera;

	/// Content folder holding the imported aircraft parts and their rig.json.
	UPROPERTY(EditAnywhere, Category = "Launch")
	FString ModelFolder = TEXT("/Game/ThirdParty/F35B");

	/// Where in the sequence play begins, seconds relative to the shot (the sim starts at -40 s).
	UPROPERTY(EditAnywhere, Category = "Launch")
	double StartTime = -8.0;

	/// Deck-relative end speed the catapult is set for, knots [tune against footage].
	UPROPERTY(EditAnywhere, Category = "Launch", meta = (ClampMin = "100", ClampMax = "170"))
	double EndSpeedKnots = 135.0;

	UPROPERTY(EditAnywhere, Category = "Launch", meta = (ClampMin = "45000", ClampMax = "70000"))
	double GrossWeightPounds = 55000.0;

	/// Military power is the default; afterburner launches happen on the shooter's signal.
	UPROPERTY(EditAnywhere, Category = "Launch")
	bool bAfterburner = false;

	/// Take-off flaperon droop, degrees trailing edge down [estimate].
	UPROPERTY(EditAnywhere, Category = "Launch")
	double TakeoffFlaperonDeg = 15.0;

	/// Take-off leading-edge flap droop, degrees [estimate].
	UPROPERTY(EditAnywhere, Category = "Launch")
	double TakeoffLeadingEdgeDeg = 12.0;

	/// Aim the view camera at the jet with operator-like lag (placeholder for the camera system).
	UPROPERTY(EditAnywhere, Category = "Launch")
	bool bAimViewCamera = true;

	/// Physical exposure of the view camera. Theory says "sunny 16" (EV100 ~15); with the
	/// Sky Atmosphere also attenuating the 35-degree sun, f/8 at 1/200 s, ISO 100
	/// (EV100 ~13.6) matches the brightness of real deck photos. Calibrate against
	/// DVIDS frames during look-dev.
	UPROPERTY(EditAnywhere, Category = "Camera")
	double FStop = 8.0;

	UPROPERTY(EditAnywhere, Category = "Camera")
	double ShutterSpeed = 200.0;  // 1/s

	UPROPERTY(EditAnywhere, Category = "Camera")
	double ISO = 100.0;

	/// How quickly the operator catches up with the jet; lower lags more.
	UPROPERTY(EditAnywhere, Category = "Launch", meta = (ClampMin = "0.5", ClampMax = "20"))
	double CameraTrackingSpeed = 5.0;

	/// Restart the launch this many seconds after the shot.
	UPROPERTY(EditAnywhere, Category = "Launch")
	double LoopAfter = 12.0;

	const launchsim::SystemsSnapshot& Snapshot() const { return Sim->snapshot(); }

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	void ResetLaunch();
	void ApplySnapshot();
	void ClearAirframe();
	bool BuildModelAirframe();
	void BuildPlaceholderAirframe();
	void PoseModel(const launchsim::SystemsSnapshot& S);
	void PosePlaceholder(const launchsim::SystemsSnapshot& S);
	UStaticMeshComponent* AddMeshComponent(FName Name, UStaticMesh* Mesh, USceneComponent* Parent);
	UStaticMeshComponent* AddBlockoutPart(FName Name, UStaticMesh* Mesh, const FVector& CenterMeters,
	                                      const FVector& SizeMeters, const TCHAR* Material, USceneComponent* Parent,
	                                      const FRotator& Rotation = FRotator::ZeroRotator);
	UMaterialInterface* ProjectMaterial(const TCHAR* Name) const;
	bool UpdateCapture();
	bool UpdateRecording();
	void AimCamera(float DeltaSeconds, bool bSnap);
	void ConfigureCameraExposure();
	void BuildExhaustEffects();
	void UpdateEffects(const launchsim::SystemsSnapshot& S, float SimDeltaSeconds);

	UPROPERTY(VisibleAnywhere, Category = "Launch")
	TObjectPtr<USceneComponent> Root;

	/// Pose of the aircraft CG in the deck frame. Every airframe part attaches here.
	UPROPERTY(VisibleAnywhere, Category = "Launch")
	TObjectPtr<USceneComponent> Airframe;

	/// Owns every generated component (model parts or blockout) for garbage collection.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> AirframeComponents;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> LaunchBar;

	/// Catapult steam, in the deck frame.
	UPROPERTY(VisibleAnywhere, Category = "Effects")
	TObjectPtr<UCatapultSteamComponent> Steam;

	/// Refraction-only heat haze: a cone from the nozzle, and a rising sheet over the JBD.
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ExhaustHaze;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> DeflectorHaze;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ExhaustHazeMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DeflectorHazeMaterial;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	/// Model parts by name (raw pointers; lifetime held by AirframeComponents).
	TMap<FName, UStaticMeshComponent*> ModelParts;
	/// Blockout gear, nose / left / right, used only without the model.
	TArray<UStaticMeshComponent*> BlockoutWheels;
	TArray<UStaticMeshComponent*> BlockoutStruts;

	FAirframeRig Rig;
	bool bUsingModel = false;
	double LastEffectsTime = 0.0;
	bool bBarWasEngaged = true;

	TUniquePtr<launchsim::LaunchSimulation> Sim;

	FString CaptureFile;
	FString RecordFolder;
	double RecordFrom = -8.0;
	double RecordTo = 9.0;
	double RecordFps = 30.0;
	int32 RecordFrame = 0;
	int32 RecordTail = -1;
	double CaptureTime = 1.0;
	int32 FramesSinceCapture = -1;
};
