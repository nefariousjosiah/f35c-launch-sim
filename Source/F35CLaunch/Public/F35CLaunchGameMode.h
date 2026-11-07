// Copyright (c) 2026 nefariousjosiah. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "F35CLaunchGameMode.generated.h"

/// The scene is watched through the level's cameras, so no player pawn is spawned
/// (the engine default would drop a spectator sphere onto the deck).
UCLASS()
class F35CLAUNCH_API AF35CLaunchGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AF35CLaunchGameMode();
};
