#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IslandBuilder.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;

/** One exhibit on the island. */
struct FPixelStation
{
	int32 Index = 0;
	FString Title;
	FString Body;       // on-screen annotation: what to try and what to look for
	FVector Pad = FVector::ZeroVector;
	FVector Focus = FVector::ZeroVector;
	float Radius = 140.f;
};

/** Builds the whole demo island at runtime from engine basic shapes and the pixel material. */
UCLASS()
class PIXELART3D_API AIslandBuilder : public AActor
{
	GENERATED_BODY()

public:
	AIslandBuilder();
	virtual void BeginPlay() override;

	TArray<FPixelStation> Stations;

	/** The three beams of the cycle exhibit; index 0 can be pushed by the player. */
	UPROPERTY() TArray<TObjectPtr<AActor>> CycleBeams;
	FVector CycleBeamBase = FVector::ZeroVector;

private:
	AActor* SpawnBlock(const FVector& Center, const FVector& SizeCm, int32 TileTop, int32 TileSide, FLinearColor Tint, bool bSprite, FName Label, UStaticMesh* Mesh = nullptr, float Density = 1.f);
	/** KayKit static mesh (/Game/KayKit/<Folder>/<Name>), base on the ground at At. Collide: a box from its bounds.
	 *  Label NAME_None registers it as ground (no sprite sorting), for flat stones and decals. */
	AActor* SpawnProp(const TCHAR* Folder, const TCHAR* Name, const FVector& At, float Yaw, FName Label, bool bCollide = true, float Scale = 1.f);
	/** An idle KayKit adventurer running the pixel anim instance (stepped keys, bone snap). */
	AActor* SpawnNpc(const TCHAR* Name, const TCHAR* Texture, const FVector& At, float Yaw);
	/** Cobblestone path from A to B (two stones wide when Wide), stopping short of both ends. */
	void LayPath(const FVector& A, const FVector& B, float StartGap, float EndGap, bool bWide = false);
	/** One instance of a KayKit Halloween mesh as ground (instanced, not sorted, no collision). */
	void AddGround(const TCHAR* Name, const FVector& At, float Yaw, float Scale = 1.f);
	void BuildTerrain();
	void BuildStations();

	UPROPERTY() TObjectPtr<UStaticMesh> Cube;
	UPROPERTY() TObjectPtr<UStaticMesh> Cylinder;
	UPROPERTY() TMap<FName, TObjectPtr<UInstancedStaticMeshComponent>> Ground;
};
