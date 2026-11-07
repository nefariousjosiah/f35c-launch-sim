// Copyright (c) 2026 nefariousjosiah. MIT License.

#include "CatapultDeck.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "LaunchSim/Params.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Material instances built by Tools/Editor/build_materials.py (values in Docs/Reference.md).
	const TCHAR* const NonSkid = TEXT("MI_NonSkid");
	const TCHAR* const TrackSteel = TEXT("MI_TrackSteel");
	const TCHAR* const SlotShadow = TEXT("MI_SlotShadow");
	const TCHAR* const ShuttleSteel = TEXT("MI_ShuttleSteel");
	const TCHAR* const DeflectorGrey = TEXT("MI_DeflectorGrey");
	const TCHAR* const MarkingWhite = TEXT("MI_MarkingWhite");
	const TCHAR* const MarkingRed = TEXT("MI_MarkingRed");
	const TCHAR* const MarkingYellow = TEXT("MI_MarkingYellow");
	const TCHAR* const HullGrey = TEXT("MI_HullGrey");

	constexpr double DeckThickness = 0.5;      // m
	constexpr double DeckWidth = 40.0;         // m, the hero section only; the purchased carrier supplies the rest
	constexpr double DeckAftExtent = 60.0;     // m aft of the shuttle start
	constexpr int32 BlastDeflectorPanels = 4;
	constexpr double BlastDeflectorPanelWidth = 3.3;   // m [estimate]
	constexpr double BlastDeflectorPanelLength = 4.2;  // m [estimate]
}

ACatapultDeck::ACatapultDeck()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CubeMesh = Cube.Object;
}

void ACatapultDeck::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildDeck();
}

UMaterialInterface* ACatapultDeck::MaterialFor(const TCHAR* Name) const
{
	return LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Materials/%s.%s"), Name, Name));
}

UStaticMeshComponent* ACatapultDeck::AddBox(FName Name, const FVector& CenterMeters, const FVector& SizeMeters,
                                            const TCHAR* Material, USceneComponent* Parent)
{
	// Engine cube is 100 cm on a side, centred. Positions here are in LaunchSim deck
	// coordinates (x along the track, y up, z starboard), converted to Unreal axes.
	// Transient: rebuilt from parameters on every construction, never saved into the level.
	UStaticMeshComponent* Box = NewObject<UStaticMeshComponent>(
		this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), Name), RF_Transient);
	Box->SetStaticMesh(CubeMesh);
	Box->SetMaterial(0, MaterialFor(Material));
	Box->SetupAttachment(Parent != nullptr ? Parent : Root.Get());
	Box->SetRelativeLocation(FVector(CenterMeters.X, CenterMeters.Z, CenterMeters.Y) * 100.0);
	Box->SetRelativeScale3D(FVector(SizeMeters.X, SizeMeters.Z, SizeMeters.Y));
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->RegisterComponent();
	BuiltComponents.Add(Box);
	return Box;
}

void ACatapultDeck::BuildDeck()
{
	for (UStaticMeshComponent* Old : BuiltComponents)
	{
		if (Old != nullptr)
		{
			Old->DestroyComponent();
		}
	}
	BuiltComponents.Reset();
	for (USceneComponent* Hinge : BlastDeflectorHinges)
	{
		if (Hinge != nullptr)
		{
			Hinge->DestroyComponent();
		}
	}
	BlastDeflectorHinges.Reset();
	if (CubeMesh == nullptr)
	{
		return;
	}

	const launchsim::LaunchParams Defaults;
	const double Stroke = Defaults.catapult.powerStroke;
	const double Track = Defaults.catapult.trackLength;
	const double Bow = Defaults.environment.bowDistance;
	const double Freeboard = Defaults.environment.deckHeight;

	// Deck plate from aft of the JBD to the bow edge; top surface at y = 0.
	const double DeckLength = Bow + DeckAftExtent;
	AddBox(TEXT("DeckPlate"), FVector(Bow - DeckLength * 0.5, -DeckThickness * 0.5, 0.0),
	       FVector(DeckLength, DeckThickness, DeckWidth), NonSkid);

	// Hull below the bow edge, so crossing the bow reads as a drop to the sea.
	AddBox(TEXT("BowHull"), FVector(Bow - 6.0, -Freeboard * 0.5, 0.0), FVector(12.0, Freeboard - 1.0, DeckWidth * 0.8),
	       HullGrey);

	// Catapult track: the slot the launch bar runs in, flanked by steel cover plates.
	const double TrackCenter = Track * 0.5 - 1.5;
	AddBox(TEXT("TrackSlot"), FVector(TrackCenter, 0.002, 0.0), FVector(Track + 3.0, 0.004, 0.08), SlotShadow);
	for (const double Side : {-0.3, 0.3})
	{
		AddBox(*FString::Printf(TEXT("TrackPlate%s"), Side < 0 ? TEXT("Port") : TEXT("Starboard")),
		       FVector(TrackCenter, 0.003, Side), FVector(Track + 3.0, 0.006, 0.44), TrackSteel);
	}
	// End of the power stroke, marked the way the deck crew sees it.
	AddBox(TEXT("StrokeEndMark"), FVector(Stroke, 0.004, 0.0), FVector(0.15, 0.004, 2.0), MarkingYellow);

	// Shuttle: a low spreader just proud of the slot.
	Shuttle = AddBox(TEXT("Shuttle"), FVector(0.0, 0.06, 0.0), FVector(0.7, 0.12, 0.22), ShuttleSteel);

	// Catapult centreline, dashed white.
	for (int32 Dash = 0; Dash < 40; ++Dash)
	{
		const double X = -30.0 + Dash * 4.0;
		if (X > Bow - 2.0)
		{
			break;
		}
		AddBox(*FString::Printf(TEXT("Centreline%d"), Dash), FVector(X, 0.002, 0.9), FVector(2.0, 0.004, 0.12),
		       MarkingWhite);
	}

	// Foul line: alternating red and white stripes along the deck edge.
	for (int32 Stripe = 0; Stripe < 40; ++Stripe)
	{
		const double X = -DeckAftExtent + 1.0 + Stripe * 2.0;
		AddBox(*FString::Printf(TEXT("FoulLine%d"), Stripe), FVector(X, 0.002, -DeckWidth * 0.5 + 2.5),
		       FVector(2.0, 0.004, 0.3), (Stripe % 2 == 0) ? MarkingRed : MarkingWhite);
	}

	// Jet blast deflector: panels hinged at their forward edge, raised aft end up.
	const double SpanStart = -0.5 * BlastDeflectorPanels * BlastDeflectorPanelWidth;
	for (int32 Panel = 0; Panel < BlastDeflectorPanels; ++Panel)
	{
		const double Z = SpanStart + (Panel + 0.5) * BlastDeflectorPanelWidth;
		const FName HingeName(*FString::Printf(TEXT("JBDHinge%d"), Panel));
		USceneComponent* Hinge = NewObject<USceneComponent>(
			this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), HingeName), RF_Transient);
		Hinge->SetupAttachment(Root);
		Hinge->SetRelativeLocation(FVector(BlastDeflectorHingeX, Z, 0.0) * 100.0);
		Hinge->RegisterComponent();
		BlastDeflectorHinges.Add(Hinge);
		AddBox(*FString::Printf(TEXT("JBDPanel%d"), Panel), FVector(-BlastDeflectorPanelLength * 0.5, 0.12, 0.0),
		       FVector(BlastDeflectorPanelLength, 0.24, BlastDeflectorPanelWidth - 0.08), DeflectorGrey, Hinge);
	}
	SetBlastDeflectorRaise(1.0);
}

void ACatapultDeck::SetShuttlePosition(double Meters)
{
	if (Shuttle != nullptr)
	{
		Shuttle->SetRelativeLocation(FVector(Meters * 100.0, 0.0, 6.0));
	}
}

void ACatapultDeck::SetBlastDeflectorRaise(double Amount)
{
	// Panels extend aft (-X) from the hinge; negative pitch lifts their aft edge.
	const double Angle = FMath::Clamp(Amount, 0.0, 1.0) * BlastDeflectorRaisedAngle;
	for (USceneComponent* Hinge : BlastDeflectorHinges)
	{
		if (Hinge != nullptr)
		{
			Hinge->SetRelativeRotation(FRotator(-Angle, 0.0, 0.0));
		}
	}
}
