// Copyright (c) 2026 nefariousjosiah. MIT License.

#include "ShotDirector.h"

#include "CatapultDeck.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "F35CLaunchActor.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "SimConversions.h"

namespace
{
	FLaunchShot MakeShot(FName Name, double CutTime, const FVector& Location, double FocalLength,
	                     ELaunchShotStyle Style, double TrackingHz, double Damping, double Delay, double Shake)
	{
		FLaunchShot Shot;
		Shot.Name = Name;
		Shot.CutTime = CutTime;
		Shot.Location = Location;
		Shot.FocalLength = FocalLength;
		Shot.Style = Style;
		Shot.TrackingHz = TrackingHz;
		Shot.TrackingDamping = Damping;
		Shot.ReactionDelay = Delay;
		Shot.ShakeDegrees = Shake;
		return Shot;
	}

	/// Sum of Perlin octaves, roughly -1..1: slow drift, mid wobble, fine tremor.
	double HandNoise(double T, double Seed)
	{
		return 0.6 * FMath::PerlinNoise1D(static_cast<float>(T * 0.35 + Seed)) +
		       0.3 * FMath::PerlinNoise1D(static_cast<float>(T * 1.1 + Seed * 2.1)) +
		       0.1 * FMath::PerlinNoise1D(static_cast<float>(T * 4.0 + Seed * 3.7));
	}

	double Unwrap(double Angle, double Reference)
	{
		return Reference + FMath::UnwindDegrees(Angle - Reference);
	}
}

AShotDirector::AShotDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// The edit follows real launch B-roll: an establishing telephoto from the island while
	// the jet is on the catapult, a handheld deck camera through the shot, the locked-off
	// bow camera as the jet rushes past, and the island telephoto again for the climb-out.
	// Deck-frame positions in cm (+X down the track, +Y starboard, +Z up).
	Shots.Add(MakeShot(TEXT("IslandTelephoto"), -7.0, FVector(-7000.0, 4200.0, 2400.0), 120.0,
	                   ELaunchShotStyle::Telephoto, 0.9, 0.8, 0.18, 0.35));
	Shots.Add(MakeShot(TEXT("DeckHandheld"), -2.5, FVector(-1600.0, 2600.0, 170.0), 35.0, ELaunchShotStyle::Handheld,
	                   1.3, 0.65, 0.12, 0.35));
	FLaunchShot Bow = MakeShot(TEXT("BowLockedOff"), 1.9, FVector(9900.0, 1500.0, 80.0), 24.0,
	                           ELaunchShotStyle::LockedOff, 0.0, 1.0, 0.0, 0.0);
	Bow.LockedTarget = FVector(4000.0, -100.0, 250.0);
	Shots.Add(Bow);
	Shots.Add(MakeShot(TEXT("IslandTelephotoClimb"), 2.7, FVector(-7000.0, 4200.0, 2400.0), 120.0,
	                   ELaunchShotStyle::Telephoto, 0.9, 0.8, 0.18, 0.35));
}

void AShotDirector::BeginPlay()
{
	Super::BeginPlay();
	if (Launch == nullptr)
	{
		for (TActorIterator<AF35CLaunchActor> It(GetWorld()); It; ++It)
		{
			Launch = *It;
			break;
		}
	}
	if (Launch == nullptr)
	{
		return;
	}
	Deck = Launch->Deck;
	if (Deck == nullptr)
	{
		for (TActorIterator<ACatapultDeck> It(GetWorld()); It; ++It)
		{
			Deck = *It;
			break;
		}
	}
	DeckRest = Deck != nullptr ? Deck->GetActorTransform() : FTransform::Identity;
	AddTickPrerequisiteActor(Launch);  // read the jet's pose after it has been updated
	Launch->bAimViewCamera = false;    // this director operates the cameras
	CreateCameras();
}

void AShotDirector::CreateCameras()
{
	Cameras.Reset();
	Operators.Reset();
	for (const FLaunchShot& Shot : Shots)
	{
		FActorSpawnParameters Params;
		Params.Name = MakeUniqueObjectName(GetWorld(), ACineCameraActor::StaticClass(), Shot.Name);
		ACineCameraActor* Camera = GetWorld()->SpawnActor<ACineCameraActor>(Params);
		ConfigureCamera(Camera, Shot);
		Cameras.Add(Camera);
	}
	Operators.SetNum(Shots.Num());
}

void AShotDirector::ConfigureCamera(ACineCameraActor* Camera, const FLaunchShot& Shot) const
{
	UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
	Lens->Filmback.SensorWidth = 24.89f;  // Super 35, cropped to 16:9
	Lens->Filmback.SensorHeight = 14.0f;
	Lens->CurrentFocalLength = static_cast<float>(Shot.FocalLength);
	Lens->CurrentAperture = static_cast<float>(FStop);
	// The cine camera only feeds its aperture into exposure while focus is active.
	Lens->FocusSettings.FocusMethod = ECameraFocusMethod::Manual;
	Lens->FocusSettings.ManualFocusDistance = 3000.0f;

	FPostProcessSettings& PP = Lens->PostProcessSettings;
	PP.bOverride_AutoExposureMethod = true;
	PP.AutoExposureMethod = AEM_Manual;
	PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PP.AutoExposureApplyPhysicalCameraExposure = 1;
	PP.bOverride_CameraISO = true;
	PP.CameraISO = static_cast<float>(ISO);
	PP.bOverride_CameraShutterSpeed = true;
	PP.CameraShutterSpeed = static_cast<float>(ShutterSpeed);
	PP.bOverride_DepthOfFieldFstop = true;
	PP.DepthOfFieldFstop = static_cast<float>(FStop);
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = 0.0f;
	PP.bOverride_MotionBlurAmount = true;
	PP.MotionBlurAmount = 0.5f;  // 180 degree shutter
	PP.bOverride_FilmGrainIntensity = true;
	PP.FilmGrainIntensity = static_cast<float>(FilmGrain);
	PP.bOverride_SceneFringeIntensity = true;
	PP.SceneFringeIntensity = static_cast<float>(ChromaticAberration);
	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = static_cast<float>(Vignette);
	Lens->PostProcessBlendWeight = 1.0f;
}

FTransform AShotDirector::ShipMotion(double Time) const
{
	// Roll and pitch about a point well below the flight deck, as a ship moves.
	const double Roll = ShipRollDegrees * FMath::Sin(2.0 * PI * Time / ShipRollPeriod);
	const double Pitch = ShipPitchDegrees * FMath::Sin(2.0 * PI * Time / ShipPitchPeriod + 1.3);
	const FQuat Rotation = FRotator(Pitch, 0.0, Roll).Quaternion();
	const FVector Pivot(0.0, 0.0, -2500.0);
	return FTransform(Rotation, Pivot - Rotation.RotateVector(Pivot));
}

int32 AShotDirector::ShotIndexAt(double Time) const
{
	int32 Index = 0;
	for (int32 i = 0; i < Shots.Num(); ++i)
	{
		if (Time >= Shots[i].CutTime)
		{
			Index = i;
		}
	}
	return Index;
}

void AShotDirector::StepOperator(int32 Index, double Time, double Dt, const FVector& Target)
{
	const FLaunchShot& Shot = Shots[Index];
	FOperator& Op = Operators[Index];

	if (Shot.Style == ELaunchShotStyle::LockedOff)
	{
		const FVector Dir = Shot.LockedTarget - Shot.Location;
		Op.Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
		Op.Pitch = FMath::RadiansToDegrees(FMath::Atan2(Dir.Z, Dir.Size2D()));
		Op.Focus = static_cast<double>(Dir.Size());
		Op.bInitialised = true;
		return;
	}

	// Reaction delay: the operator responds to where the jet was a moment ago.
	Op.TargetHistory.Add(TPair<double, FVector>(Time, Target));
	while (Op.TargetHistory.Num() > 2 && Op.TargetHistory[1].Key <= Time - Shot.ReactionDelay)
	{
		Op.TargetHistory.RemoveAt(0);
	}
	const FVector Seen = Op.TargetHistory[0].Value;
	const FVector Dir = Seen - Shot.Location;
	double DesiredYaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
	const double DesiredPitch = FMath::RadiansToDegrees(FMath::Atan2(Dir.Z, Dir.Size2D()));

	if (!Op.bInitialised)
	{
		Op.Yaw = DesiredYaw;
		Op.Pitch = DesiredPitch;
		Op.YawRate = Op.PitchRate = 0.0;
		Op.Focus = (Target - Shot.Location).Size();
		Op.bInitialised = true;
		return;
	}
	DesiredYaw = Unwrap(DesiredYaw, Op.Yaw);

	// Pan/tilt as an under-damped spring: lags the jet, then overshoots slightly.
	const double W = 2.0 * PI * Shot.TrackingHz;
	const int32 SubSteps = FMath::Max(1, FMath::CeilToInt(Dt / 0.004));
	const double H = Dt / SubSteps;
	for (int32 i = 0; i < SubSteps; ++i)
	{
		const double YawAcc = W * W * (DesiredYaw - Op.Yaw) - 2.0 * Shot.TrackingDamping * W * Op.YawRate;
		const double PitchAcc = W * W * (DesiredPitch - Op.Pitch) - 2.0 * Shot.TrackingDamping * W * Op.PitchRate;
		Op.YawRate += YawAcc * H;
		Op.PitchRate += PitchAcc * H;
		Op.Yaw += Op.YawRate * H;
		Op.Pitch += Op.PitchRate * H;
	}
	// Focus pulls after the subject too.
	Op.Focus += ((Target - Shot.Location).Size() - Op.Focus) * (1.0 - FMath::Exp(-4.0 * Dt));
}

void AShotDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Launch == nullptr || Cameras.Num() != Shots.Num())
	{
		return;
	}

	const launchsim::SystemsSnapshot& S = Launch->Snapshot();
	const double Time = S.time;
	double Dt = bHaveLastTime ? Time - LastTime : 0.0;
	if (Dt < 0.0)  // the launch looped: start the operators over
	{
		for (FOperator& Op : Operators)
		{
			Op = FOperator();
		}
		Dt = 0.0;
	}
	LastTime = Time;
	bHaveLastTime = true;

	// Ship motion carries the deck, the jet and every ship-mounted camera together.
	const FTransform DeckWorld = ShipMotion(Time) * DeckRest;
	if (Deck != nullptr)
	{
		Deck->SetActorTransform(DeckWorld);
	}
	Launch->SetActorTransform(DeckWorld);

	const FVector Jet = SimToUnreal::Location(S.body.position);
	for (int32 i = 0; i < Shots.Num(); ++i)
	{
		const FLaunchShot& Shot = Shots[i];
		StepOperator(i, Time, Dt, Jet + Shot.AimOffset);
		const FOperator& Op = Operators[i];

		// Handheld wander (long lenses are steadier in degrees, but still magnify it),
		// plus the catapult's thump through the deck at the shot.
		const double Seed = 13.7 * (i + 1);
		const double Hand = Shot.Style == ELaunchShotStyle::Handheld   ? Shot.ShakeDegrees
		                    : Shot.Style == ELaunchShotStyle::Telephoto ? Shot.ShakeDegrees * 0.15
		                                                                : 0.0;
		const double Thump = Time >= 0.0 ? FMath::Exp(-2.2 * Time) : 0.0;
		const double Rumble = Thump * (Shot.Style == ELaunchShotStyle::LockedOff   ? 0.10
		                               : Shot.Style == ELaunchShotStyle::Telephoto ? 0.04
		                                                                           : 0.30);
		const double ShakeYaw = Hand * HandNoise(Time, Seed) + Rumble * FMath::Sin(2.0 * PI * 13.0 * Time + Seed);
		const double ShakePitch =
			Hand * HandNoise(Time, Seed + 41.0) + Rumble * FMath::Sin(2.0 * PI * 11.0 * Time + 2.0 * Seed);
		const double ShakeRoll = 0.5 * Hand * HandNoise(Time * 0.7, Seed + 97.0);

		const FQuat Local = FRotator(Op.Pitch + ShakePitch, Op.Yaw + ShakeYaw, ShakeRoll).Quaternion();
		Cameras[i]->SetActorLocationAndRotation(DeckWorld.TransformPosition(Shot.Location),
		                                        DeckWorld.GetRotation() * Local);
		Cameras[i]->GetCineCameraComponent()->FocusSettings.ManualFocusDistance = static_cast<float>(Op.Focus);
	}

	const int32 Wanted = ShotIndexAt(Time);
	if (Wanted != ActiveShot)
	{
		ActiveShot = Wanted;
		if (APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0))
		{
			Controller->SetViewTargetWithBlend(Cameras[ActiveShot], 0.0f);  // hard cut
		}
		UE_LOG(LogTemp, Display, TEXT("ShotDirector: t=%.2f cut to %s"), Time, *Shots[ActiveShot].Name.ToString());
	}
}
