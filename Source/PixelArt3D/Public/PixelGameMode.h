#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "PixelGameMode.generated.h"

UCLASS()
class PIXELART3D_API APixelGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	APixelGameMode();
	virtual void StartPlay() override;
	/** The map has no PlayerStart; without this the pawn spawns at the origin, buried in the terrain. */
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;
};

/** Canvas overlay: precedence graph (V) and station markers. */
UCLASS()
class PIXELART3D_API APixelDebugHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;
};
