#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PixelTypes.h"
#include "PixelArtRender.h"
#include "PixelArtSubsystem.generated.h"

class UMaterialParameterCollection;
class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;
class ASceneCapture2D;
class UPrimitiveComponent;

/** One layer participant of the sprite sort. */
struct FPixelSprite
{
	TWeakObjectPtr<AActor> Actor;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Prims;
	uint8 Id = 0;
	bool bDynamic = false;
	bool bCharacter = false;
	FName Label;

	// per-frame
	FBox Box = FBox(ForceInit);
	FVector Pivot = FVector::ZeroVector;
	FBox2D Rect = FBox2D(ForceInit); // screen plane rect, texels
	int32 Node = INDEX_NONE;         // condensed graph node (merge groups share one)
	int32 Level = 0;
};

struct FPixelSortEdge
{
	int32 Back = 0;
	int32 Front = 0;
	float Area = 0.f; // overlap area in texels^2
	bool bCut = false;
	bool bMerged = false;
};

struct FPixelSortStats
{
	int32 Sprites = 0;
	int32 Pairs = 0;
	int32 Cycles = 0;
	int32 Cuts = 0;
	int32 Merged = 0;
	int32 Levels = 0;
	double Ms = 0.0;
};

/** Camera state as actually used for the frame (written by APixelCameraManager). */
struct FPixelCameraState
{
	FVector Position = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	FVector Right = FVector::RightVector;
	FVector Up = FVector::UpVector;
	float Yaw = 0.f;      // yaw currently shown (eases toward the target when rotating)
	double Texel = 6.25;  // world cm per texel
	FVector2D Remainder = FVector2D::ZeroVector; // snap remainder (texels)
	FPixelArtLayout Layout;
	FVector2D Parity = FVector2D::ZeroVector;    // 0.5 where the render size is odd
	bool bValid = false;
};

UCLASS()
class PIXELART3D_API UPixelArtSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UPixelArtSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	FPixelSettings Settings;
	FPixelCameraState Camera;

	// --- sprites ---------------------------------------------------------------------------
	void RegisterSprite(AActor* Actor, bool bDynamic, bool bCharacter, FName Label);
	void RegisterTerrain(UPrimitiveComponent* Prim);
	const TArray<FPixelSprite>& GetSprites() const { return Sprites; }
	const TArray<FPixelSortEdge>& GetEdges() const { return Edges; }
	const FPixelSortStats& GetSortStats() const { return SortStats; }

	// --- snapping ----------------------------------------------------------------------------
	/** Snap a world point onto the screen-aligned texel grid anchored at the world origin. */
	FVector SnapWorld(const FVector& World) const;

	// --- materials -----------------------------------------------------------------------------
	UMaterialInstanceDynamic* MakeSurfaceMaterial(UObject* Outer, int32 Tile, FLinearColor Tint, bool bUseTexture);
	/** Same lighting/sort path, albedo from an imported mesh's own UVs and palette texture. */
	UMaterialInstanceDynamic* MakeMeshMaterial(UObject* Outer, UTexture* Texture);
	/** Applies MakeMeshMaterial to every material slot of a mesh component. */
	void ApplyMeshMaterial(class UMeshComponent* Mesh, UTexture* Texture);
	UTextureRenderTarget2D* GetShadowTarget() const { return ShadowTarget; }

	// --- light ---------------------------------------------------------------------------------
	float LightYawDeg = -35.f;
	float LightPitchDeg = -50.f;

	// --- metrics -------------------------------------------------------------------------------
	float FlickerPercent = -1.f; // frame-to-frame differing texels after best integer alignment
	bool bMeasureFlicker = false;
	/** Per-frame motion samples (texels): x,y = player on screen, z,w = world scroll offset. Filled by the camera. */
	bool bMotionProbe = false;
	TArray<FVector4d> MotionSamples;

private:
	void UpdateSort();
	void ApplySortToPrims();
	void UpdateMaterialParameters();
	void UpdateShadowCapture();
	void MeasureFlicker();
	bool InFront(const FBox& A, const FBox& B) const;

	TArray<FPixelSprite> Sprites;
	TArray<FPixelSortEdge> Edges;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Terrain;
	TSet<uint64> LastCuts; // hysteresis memory: (back<<32 | front) of edges cut last frame
	FPixelSortStats SortStats;
	uint8 NextId = 2;      // 0 = background, 1 = terrain

	UPROPERTY() TObjectPtr<UMaterialParameterCollection> Collection;
	UPROPERTY() TObjectPtr<UMaterialInterface> SurfaceMaterial;
	UPROPERTY() TObjectPtr<UTexture> Atlas;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> ShadowTarget;
	UPROPERTY() TObjectPtr<ASceneCapture2D> ShadowCapture;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Mids;

	FVector ShadowOrigin = FVector::ZeroVector;
	FVector ShadowForward = FVector::ForwardVector;
	FVector ShadowRight = FVector::RightVector;
	FVector ShadowUp = FVector::UpVector;
	float ShadowExtent = 4800.f;

	TArray<FColor> PrevFrame;
	FIntPoint PrevSize = FIntPoint::ZeroValue;
	int32 FrameCounter = 0;
	TArray<float> FlickerHistory;
};
