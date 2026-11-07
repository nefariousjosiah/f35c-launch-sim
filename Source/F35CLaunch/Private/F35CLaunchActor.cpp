// Copyright (c) 2026 nefariousjosiah. MIT License.

#include "F35CLaunchActor.h"

#include "CatapultDeck.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "SimConversions.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"

namespace
{
	FVector BodyToUnreal(const launchsim::Vec3& V) { return SimToUnreal::Location(V); }

	// Parts exported by Tools/Blender/export_f35_parts.py.
	const TCHAR* const ModelPartNames[] = {
		TEXT("Body"), TEXT("Canopy"),
		TEXT("StabilatorLeft"), TEXT("StabilatorRight"), TEXT("FlaperonLeft"), TEXT("FlaperonRight"),
		TEXT("RudderLeft"), TEXT("RudderRight"), TEXT("SlatLeft"), TEXT("SlatRight"),
		TEXT("NoseGearUpper"), TEXT("NoseGearLower"), TEXT("NoseWheel"),
		TEXT("LeftGearUpper"), TEXT("LeftGearLower"), TEXT("LeftWheel"),
		TEXT("RightGearUpper"), TEXT("RightGearLower"), TEXT("RightWheel"),
	};

	// Gear parts per leg, in LaunchSim order (nose, left main, right main).
	const TCHAR* const GearUpper[3] = {TEXT("NoseGearUpper"), TEXT("LeftGearUpper"), TEXT("RightGearUpper")};
	const TCHAR* const GearLower[3] = {TEXT("NoseGearLower"), TEXT("LeftGearLower"), TEXT("RightGearLower")};
	const TCHAR* const GearWheel[3] = {TEXT("NoseWheel"), TEXT("LeftWheel"), TEXT("RightWheel")};

	/// Material for each source material slot, by surface type (Docs/Reference.md).
	const TCHAR* MaterialForSlot(const FString& Slot, bool bGearPart)
	{
		if (Slot.StartsWith(TEXT("livery"))) return TEXT("MI_Airframe_FS36170");
		if (Slot.StartsWith(TEXT("darkmet"))) return TEXT("MI_DarkMetal");
		// The source uses one "light metal" for gear legs and the nozzle petals alike;
		// on the real jet the legs are white and the nozzle is dark, heat-tinted metal.
		if (Slot.StartsWith(TEXT("lightmet"))) return bGearPart ? TEXT("MI_GearWhite") : TEXT("MI_DarkMetal");
		if (Slot.StartsWith(TEXT("window"))) return TEXT("MI_CanopyTint");
		if (Slot.Contains(TEXT("Red")) || Slot == TEXT("red")) return TEXT("MI_NavRed");
		if (Slot.Contains(TEXT("Green")) || Slot == TEXT("green")) return TEXT("MI_NavGreen");
		if (Slot == TEXT("yellow") || Slot == TEXT("warning")) return TEXT("MI_WarningYellow");
		return TEXT("MI_CockpitDark");
	}

	const FVector AxisAft(-1.0, 0.0, 0.0);
	const FVector AxisForward(1.0, 0.0, 0.0);
	const FVector AxisUp(0.0, 0.0, 1.0);
	const FVector AxisDown(0.0, 0.0, -1.0);
	const FVector AxisRight(0.0, 1.0, 0.0);
}

AF35CLaunchActor::AF35CLaunchActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Airframe = CreateDefaultSubobject<USceneComponent>(TEXT("Airframe"));
	Airframe->SetupAttachment(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	CubeMesh = Cube.Object;
	CylinderMesh = Cylinder.Object;
}

UMaterialInterface* AF35CLaunchActor::ProjectMaterial(const TCHAR* Name) const
{
	return LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Materials/%s.%s"), Name, Name));
}

UStaticMeshComponent* AF35CLaunchActor::AddMeshComponent(FName Name, UStaticMesh* Mesh, USceneComponent* Parent)
{
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(
		this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), Name), RF_Transient);
	Component->SetStaticMesh(Mesh);
	Component->SetupAttachment(Parent);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->RegisterComponent();
	AirframeComponents.Add(Component);
	return Component;
}

void AF35CLaunchActor::ClearAirframe()
{
	for (UStaticMeshComponent* Old : AirframeComponents)
	{
		if (Old != nullptr)
		{
			Old->DestroyComponent();
		}
	}
	AirframeComponents.Reset();
	ModelParts.Reset();
	BlockoutWheels.Reset();
	BlockoutStruts.Reset();
	LaunchBar = nullptr;
}

bool AF35CLaunchActor::BuildModelAirframe()
{
	const FString RigPath = FPaths::ProjectContentDir() /
	                        ModelFolder.Replace(TEXT("/Game/"), TEXT("")) / TEXT("rig.json");
	Rig = FAirframeRig::Load(RigPath);
	if (!Rig.bValid)
	{
		return false;
	}

	for (const TCHAR* PartName : ModelPartNames)
	{
		const FString Path = FString::Printf(TEXT("%s/%s.%s"), *ModelFolder, PartName, PartName);
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
		if (Mesh == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("F35CLaunch: model part %s missing, using the blockout"), *Path);
			return false;
		}
		// Parts were baked into the centre-of-gravity frame, so they assemble with identity transforms.
		UStaticMeshComponent* Part = AddMeshComponent(PartName, Mesh, Airframe);
		const TArray<FStaticMaterial>& Slots = Mesh->GetStaticMaterials();
		const bool bWheel = FString(PartName).EndsWith(TEXT("Wheel"));
		const bool bGearPart = FString(PartName).Contains(TEXT("Gear"));
		for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
		{
			const TCHAR* Material =
				bWheel ? TEXT("MI_Tyre") : MaterialForSlot(Slots[Slot].MaterialSlotName.ToString(), bGearPart);
			Part->SetMaterial(Slot, ProjectMaterial(Material));
		}
		ModelParts.Add(PartName, Part);
	}
	return true;
}

UStaticMeshComponent* AF35CLaunchActor::AddBlockoutPart(FName Name, UStaticMesh* Mesh, const FVector& CenterMeters,
                                                        const FVector& SizeMeters, const TCHAR* Material,
                                                        USceneComponent* Parent, const FRotator& Rotation)
{
	// Centre and size are in LaunchSim body axes (x nose, y up, z right wing), metres.
	UStaticMeshComponent* Part = AddMeshComponent(Name, Mesh, Parent);
	Part->SetMaterial(0, ProjectMaterial(Material));
	Part->SetRelativeLocation(BodyToUnreal({CenterMeters.X, CenterMeters.Y, CenterMeters.Z}));
	Part->SetRelativeRotation(Rotation);
	Part->SetRelativeScale3D(FVector(SizeMeters.X, SizeMeters.Z, SizeMeters.Y));
	return Part;
}

void AF35CLaunchActor::BuildPlaceholderAirframe()
{
	if (CubeMesh == nullptr || CylinderMesh == nullptr)
	{
		return;
	}
	// Blockout of the F-35C planform: 15.7 m long, 13.1 m span. Centre/size in body axes.
	const TCHAR* Skin = TEXT("MI_Airframe_FS36170");
	AddBlockoutPart(TEXT("Fuselage"), CubeMesh, FVector(0.2, 0.0, 0.0), FVector(13.4, 1.5, 2.6), Skin, Airframe);
	AddBlockoutPart(TEXT("Nose"), CubeMesh, FVector(7.6, -0.15, 0.0), FVector(2.3, 0.9, 1.3), Skin, Airframe);
	AddBlockoutPart(TEXT("Canopy"), CubeMesh, FVector(4.4, 0.95, 0.0), FVector(3.2, 0.55, 1.0), TEXT("MI_CanopyTint"), Airframe);
	AddBlockoutPart(TEXT("Wing"), CubeMesh, FVector(-1.4, -0.15, 0.0), FVector(5.2, 0.22, 13.1), Skin, Airframe);
	AddBlockoutPart(TEXT("Stabilators"), CubeMesh, FVector(-6.8, -0.05, 0.0), FVector(2.6, 0.14, 8.0), Skin, Airframe);
	for (const double Side : {-1.35, 1.35})
	{
		AddBlockoutPart(TEXT("Fin"), CubeMesh, FVector(-6.0, 1.75, Side), FVector(3.0, 2.6, 0.14), Skin, Airframe,
		                FRotator(0.0, 0.0, Side > 0 ? 20.0 : -20.0));
	}
	const launchsim::AircraftParams Aircraft;
	for (const launchsim::GearParams* Gear : {&Aircraft.noseGear, &Aircraft.leftMainGear, &Aircraft.rightMainGear})
	{
		const double Diameter = Gear->tyreRadius * 2.0;
		BlockoutStruts.Add(AddBlockoutPart(TEXT("Strut"), CubeMesh, FVector::ZeroVector, FVector(0.12, 1.0, 0.12),
		                                   TEXT("MI_GearWhite"), Airframe));
		// Cylinder axis is local Z; size (x, y, z) maps to scale (x, z, y), so this is
		// diameter x diameter x 0.35 m before the roll that turns the axle sideways.
		BlockoutWheels.Add(AddBlockoutPart(TEXT("Wheel"), CylinderMesh, FVector::ZeroVector,
		                                   FVector(Diameter, 0.35, Diameter), TEXT("MI_Tyre"), Airframe,
		                                   FRotator(0.0, 0.0, 90.0)));
	}
}

void AF35CLaunchActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ClearAirframe();
	bUsingModel = BuildModelAirframe();
	if (!bUsingModel)
	{
		ClearAirframe();
		BuildPlaceholderAirframe();
	}
	LaunchBar = AddMeshComponent(TEXT("LaunchBar"), CubeMesh, Root);
	LaunchBar->SetMaterial(0, ProjectMaterial(TEXT("MI_DarkMetal")));
	ResetLaunch();
	ApplySnapshot();  // show the hooked-up pose in the editor viewport
}

void AF35CLaunchActor::ResetLaunch()
{
	launchsim::LaunchParams Params;
	Params.catapult.endSpeed = EndSpeedKnots * launchsim::kKnot;
	Params.aircraft.mass = GrossWeightPounds * launchsim::kPound;
	Params.sequence.afterburner = bAfterburner;
	Sim = MakeUnique<launchsim::LaunchSimulation>(Params);
	while (Sim->time() < StartTime)
	{
		Sim->step();
	}
}

void AF35CLaunchActor::BeginPlay()
{
	Super::BeginPlay();

	if (Deck == nullptr)
	{
		for (TActorIterator<ACatapultDeck> It(GetWorld()); It; ++It)
		{
			Deck = *It;
			break;
		}
	}
	if (Deck != nullptr)
	{
		SetActorTransform(Deck->GetActorTransform());  // the deck actor's origin is the sim's deck frame
	}

	FParse::Value(FCommandLine::Get(), TEXT("LaunchCapture="), CaptureFile);
	FParse::Value(FCommandLine::Get(), TEXT("LaunchCaptureTime="), CaptureTime);

	ConfigureCameraExposure();
	if (ViewCamera != nullptr)
	{
		ViewCamera->AddTickPrerequisiteActor(this);  // camera reads the pose after it is updated
		if (APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0))
		{
			Controller->SetViewTarget(ViewCamera);
		}
	}
	ResetLaunch();
	ApplySnapshot();
	AimCamera(0.0f, true);
}

void AF35CLaunchActor::ConfigureCameraExposure()
{
	ACineCameraActor* Cine = Cast<ACineCameraActor>(ViewCamera);
	if (Cine == nullptr)
	{
		return;
	}
	// Manual physical exposure, set like a real camera, so nothing auto-adjusts frame to
	// frame (auto exposure "breathing" is a classic CG tell).
	UCineCameraComponent* Camera = Cine->GetCineCameraComponent();
	Camera->CurrentAperture = static_cast<float>(FStop);
	// The cine camera only feeds its aperture into exposure while focus is active, so focus
	// manually on the jet the way an operator would (f/11 keeps the deck in focus too).
	Camera->FocusSettings.FocusMethod = ECameraFocusMethod::Manual;
	Camera->FocusSettings.ManualFocusDistance =
		static_cast<float>((Airframe->GetComponentLocation() - Cine->GetActorLocation()).Size());
	FPostProcessSettings& PP = Camera->PostProcessSettings;
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
	Camera->PostProcessBlendWeight = 1.0f;
	UE_LOG(LogTemp, Display, TEXT("F35CLaunch: camera f/%.1f 1/%.0f s ISO %.0f -> EV100 %.2f"), FStop, ShutterSpeed, ISO,
	       FMath::Log2(FStop * FStop * ShutterSpeed * 100.0 / ISO));
}

void AF35CLaunchActor::AimCamera(float DeltaSeconds, bool bSnap)
{
	if (ViewCamera == nullptr || !bAimViewCamera)
	{
		return;
	}
	const FRotator Desired = (Airframe->GetComponentLocation() - ViewCamera->GetActorLocation()).Rotation();
	ViewCamera->SetActorRotation(bSnap ? Desired
	                                   : FMath::RInterpTo(ViewCamera->GetActorRotation(), Desired, DeltaSeconds,
	                                                      static_cast<float>(CameraTrackingSpeed)));
}

void AF35CLaunchActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Sim)
	{
		return;
	}
	if (!CaptureFile.IsEmpty())
	{
		if (UpdateCapture())
		{
			return;
		}
		Sim->advance(1.0 / 60.0);  // fixed timeline when capturing, independent of frame rate
		DeltaSeconds = 1.0f / 60.0f;
	}
	else
	{
		Sim->advance(DeltaSeconds);
		if (Sim->time() > LoopAfter)
		{
			ResetLaunch();
		}
	}
	ApplySnapshot();
	AimCamera(DeltaSeconds, false);
}

bool AF35CLaunchActor::UpdateCapture()
{
	// Hold the timeline while shaders compile, so the captured frame shows final materials.
	if (GShaderCompilingManager != nullptr && GShaderCompilingManager->GetNumRemainingJobs() > 0)
	{
		return true;
	}
	if (FramesSinceCapture >= 0)
	{
		if (++FramesSinceCapture > 30)
		{
			FPlatformMisc::RequestExit(false);
		}
		return true;
	}
	if (Sim->time() >= CaptureTime)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Captures") / CaptureFile);
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogTemp, Display, TEXT("LaunchCapture: t=%.3f s -> %s"), Sim->time(), *Path);
		FramesSinceCapture = 0;
		return true;
	}
	return false;
}

void AF35CLaunchActor::PoseModel(const launchsim::SystemsSnapshot& S)
{
	const auto Set = [this](const TCHAR* Name, const FTransform& Transform) {
		if (UStaticMeshComponent** Part = ModelParts.Find(Name))
		{
			(*Part)->SetRelativeTransform(Transform);
		}
	};
	const auto RigPart = [this](const TCHAR* Name) -> FAirframeRigPart {
		const FAirframeRigPart* Part = Rig.Parts.Find(Name);
		return Part != nullptr ? *Part : FAirframeRigPart();
	};

	// Control surfaces. Stabilators: + trailing edge up. Flaperons: trailing edge down
	// for flaps; roll right lowers the left one. Rudders: + nose right swings both
	// trailing edges to starboard. Leading-edge flaps droop for take-off.
	const double Flap = FMath::DegreesToRadians(TakeoffFlaperonDeg);
	const double LeadingEdge = FMath::DegreesToRadians(TakeoffLeadingEdgeDeg);
	Set(TEXT("StabilatorLeft"), FAirframeRig::Hinge(RigPart(TEXT("StabilatorLeft")), S.stabilator, AxisAft, AxisUp));
	Set(TEXT("StabilatorRight"), FAirframeRig::Hinge(RigPart(TEXT("StabilatorRight")), S.stabilator, AxisAft, AxisUp));
	Set(TEXT("FlaperonLeft"), FAirframeRig::Hinge(RigPart(TEXT("FlaperonLeft")), Flap + S.aileron, AxisAft, AxisDown));
	Set(TEXT("FlaperonRight"), FAirframeRig::Hinge(RigPart(TEXT("FlaperonRight")), Flap - S.aileron, AxisAft, AxisDown));
	Set(TEXT("RudderLeft"), FAirframeRig::Hinge(RigPart(TEXT("RudderLeft")), S.rudder, AxisAft, AxisRight));
	Set(TEXT("RudderRight"), FAirframeRig::Hinge(RigPart(TEXT("RudderRight")), S.rudder, AxisAft, AxisRight));
	Set(TEXT("SlatLeft"), FAirframeRig::Hinge(RigPart(TEXT("SlatLeft")), LeadingEdge, AxisForward, AxisDown));
	Set(TEXT("SlatRight"), FAirframeRig::Hinge(RigPart(TEXT("SlatRight")), LeadingEdge, AxisForward, AxisDown));

	// Gear: the sliding half of each strut follows the simulated extension; the wheel
	// rides with it and rolls with deck speed. The model was authored at static
	// compression, which is where the physics settles at rest.
	const launchsim::AircraftParams& Aircraft = Sim->params().aircraft;
	const launchsim::GearParams* Gears[3] = {&Aircraft.noseGear, &Aircraft.leftMainGear, &Aircraft.rightMainGear};
	const bool bGearDown = S.gearExtension > 0.5;
	for (int32 i = 0; i < 3; ++i)
	{
		const double ExtensionCm = (Gears[i]->extendedLength - S.gear[i].compression) * 100.0;
		FVector StrutUp = RigPart(GearLower[i]).Axis;
		if (StrutUp.Z < 0.0)
		{
			StrutUp = -StrutUp;
		}
		const FVector Slide = StrutUp * (Rig.AuthoredStrutLength[i] - ExtensionCm);
		Set(GearLower[i], FTransform(Slide));
		Set(GearWheel[i], FAirframeRig::Hinge(RigPart(GearWheel[i]), S.gear[i].wheelAngle, AxisUp, AxisForward, Slide));
		for (const TCHAR* Name : {GearUpper[i], GearLower[i], GearWheel[i]})
		{
			if (UStaticMeshComponent** Part = ModelParts.Find(Name))
			{
				(*Part)->SetVisibility(bGearDown);
			}
		}
	}
}

void AF35CLaunchActor::PosePlaceholder(const launchsim::SystemsSnapshot& S)
{
	const launchsim::AircraftParams& Aircraft = Sim->params().aircraft;
	const launchsim::GearParams* Gears[3] = {&Aircraft.noseGear, &Aircraft.leftMainGear, &Aircraft.rightMainGear};
	const bool bGearDown = S.gearExtension > 0.5;
	for (int32 i = 0; i < 3 && i < BlockoutWheels.Num(); ++i)
	{
		const launchsim::GearParams& G = *Gears[i];
		const double Extension = G.extendedLength - S.gear[i].compression;
		const launchsim::Vec3 Axle = G.attach + launchsim::Vec3{0.0, -Extension, 0.0};
		BlockoutWheels[i]->SetRelativeLocation(BodyToUnreal(Axle));
		BlockoutWheels[i]->SetRelativeRotation(FRotator(-FMath::RadiansToDegrees(S.gear[i].wheelAngle), 0.0, 90.0));
		BlockoutStruts[i]->SetRelativeLocation(BodyToUnreal(G.attach + launchsim::Vec3{0.0, -Extension * 0.5, 0.0}));
		BlockoutStruts[i]->SetRelativeScale3D(FVector(0.12, 0.12, Extension));
		BlockoutWheels[i]->SetVisibility(bGearDown);
		BlockoutStruts[i]->SetVisibility(bGearDown);
	}
}

void AF35CLaunchActor::ApplySnapshot()
{
	const launchsim::SystemsSnapshot& S = Sim->snapshot();
	Airframe->SetRelativeLocationAndRotation(SimToUnreal::Location(S.body.position), SimToUnreal::Rotation(S.body.orientation));
	if (bUsingModel)
	{
		PoseModel(S);
	}
	else
	{
		PosePlaceholder(S);
	}

	// Launch bar: hinge on the nose gear down to the shuttle while engaged, stowed up after.
	const launchsim::AircraftParams& Aircraft = Sim->params().aircraft;
	if (LaunchBar != nullptr)
	{
		const launchsim::Vec3 Hinge =
			S.body.position + launchsim::rotate(S.body.orientation, Aircraft.launchBarHinge);
		const launchsim::Vec3 Shuttle{S.shuttlePosition, 0.0, 0.0};
		const launchsim::Vec3 Stowed =
			Hinge + launchsim::rotate(S.body.orientation, launchsim::Vec3{0.6, 0.8, 0.0}) * Aircraft.launchBarLength;
		const launchsim::Vec3 Tip = S.launchBarEngaged ? Shuttle : Shuttle + (Stowed - Shuttle) * S.launchBarRaise;
		const launchsim::Vec3 Mid = (Hinge + Tip) * 0.5;
		const FVector Along = SimToUnreal::Direction(launchsim::normalized(Tip - Hinge));
		LaunchBar->SetRelativeLocationAndRotation(SimToUnreal::Location(Mid), Along.Rotation());
		LaunchBar->SetRelativeScale3D(FVector(launchsim::length(Tip - Hinge), 0.1, 0.08));
		LaunchBar->SetVisibility(S.gearExtension > 0.5 && S.launchBarRaise < 0.99);
	}

	if (Deck != nullptr)
	{
		Deck->SetShuttlePosition(S.shuttlePosition);
		// JBD stays up through the shot, then lowers once the jet is clear.
		const double SinceShot = S.time;
		Deck->SetBlastDeflectorRaise(SinceShot < 4.0 ? 1.0 : FMath::Clamp(1.0 - (SinceShot - 4.0) / 3.0, 0.0, 1.0));
	}
}
