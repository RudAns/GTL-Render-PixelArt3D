#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "PixelCameraManager.generated.h"

/**
 * Orthographic pixel camera. Computes the frame's view, snaps it onto the screen-aligned texel grid
 * (talk p.65), and hands the snap remainder to the upscaler for sub-texel compensation. Doing both in
 * UpdateViewTarget guarantees the remainder belongs to exactly the view that gets rendered.
 */
UCLASS()
class PIXELART3D_API APixelCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	APixelCameraManager();

	virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) override;

	/** World point the camera frames (smoothed pawn position plus optional pan). */
	FVector Focus = FVector::ZeroVector;
	FVector PanOffset = FVector::ZeroVector;
	bool bHasFocus = false;
	float PanTime = 0.f;
	float ShownYaw = 0.f;
	bool bHasYaw = false;

	/** Optional override target (stations frame their exhibit). */
	TOptional<FVector> FocusOverride;

	/** Locked follow: eased offset from the player's snapped anchor (zero while simply following). */
	FVector FollowOffset = FVector::ZeroVector;

private:
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> PostMid;
};
