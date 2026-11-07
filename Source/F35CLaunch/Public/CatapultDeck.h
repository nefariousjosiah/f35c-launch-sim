// Copyright (c) 2026 nefariousjosiah. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CatapultDeck.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;

/// The hero section of flight deck around one C-13 catapult: deck plate, track slot
/// and cover plates, shuttle, jet blast deflector panels and the foul line.
///
/// The actor's origin is the shuttle's start position on the deck surface, which is
/// also the origin of the LaunchSim deck frame, so simulation coordinates map
/// straight onto it. Geometry is laid out from the same parameters the physics
/// uses (stroke and track length, bow distance), so the two always agree.
///
/// Geometry is engine primitives at real dimensions; surfaces use the project's
/// physically based material instances. Detail meshes come during look-dev.
UCLASS()
class F35CLAUNCH_API ACatapultDeck : public AActor
{
	GENERATED_BODY()

public:
	ACatapultDeck();

	/// Moves the shuttle along the track (metres from its start).
	void SetShuttlePosition(double Meters);

	/// 0 = flush with the deck, 1 = fully raised.
	void SetBlastDeflectorRaise(double Amount);

	/// Deck-frame X of the jet blast deflector hinge line, metres (aft of the shuttle, so negative).
	UPROPERTY(EditAnywhere, Category = "Deck")
	double BlastDeflectorHingeX = -19.0;

	/// Raised angle of the JBD panels, degrees [estimate].
	UPROPERTY(EditAnywhere, Category = "Deck")
	double BlastDeflectorRaisedAngle = 55.0;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	UStaticMeshComponent* AddBox(FName Name, const FVector& CenterMeters, const FVector& SizeMeters,
	                             const TCHAR* Material, USceneComponent* Parent = nullptr);
	UMaterialInterface* MaterialFor(const TCHAR* Name) const;
	void BuildDeck();

	UPROPERTY(VisibleAnywhere, Category = "Deck")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Shuttle;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> BlastDeflectorHinges;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> BuiltComponents;

	UPROPERTY()
	TObjectPtr<class UStaticMesh> CubeMesh;

};
