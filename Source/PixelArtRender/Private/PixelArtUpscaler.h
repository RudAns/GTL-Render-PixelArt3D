#pragma once

#include "CoreMinimal.h"
#include "PostProcess/PostProcessUpscale.h"

struct FPixelUpscalerParams
{
	FIntPoint Inner = FIntPoint::ZeroValue;          // displayed texels
	FIntPoint ExpectedRender = FIntPoint::ZeroValue; // render size the game thread asked for
	FVector2f SubTexel = FVector2f::ZeroVector;      // snap remainder in texels (x right, y up)
	bool bBilinear = false;
	bool bShowGrid = false;
	int32 SteadyLayerId = 0;
	bool bPlayerShadow = false;
	FTextureResource* WorldShadow = nullptr;
	FTextureResource* PlayerShadow = nullptr;
	FVector3f LightOrigin, LightRight, LightUp, LightDir;
	float LightExtent = 1.f;
	FVector3f PlayerLightOrigin;
	float PlayerLightExtent = 1.f;
	float ShadowBias = 11.f;
	FLinearColor ShadowTint = FLinearColor::White;
};

/**
 * Replaces the engine's primary spatial upscale: integer-scale nearest (or bilinear for comparison)
 * from the inner window of the low-res render, shifted by the camera snap remainder, letterboxed.
 */
class FPixelUpscaler final : public ISpatialUpscaler
{
public:
	explicit FPixelUpscaler(const FPixelUpscalerParams& InParams) : Params(InParams) {}

	virtual const TCHAR* GetDebugName() const override { return TEXT("PixelArt3D.Upscale"); }
	virtual ISpatialUpscaler* Fork_GameThread(const FSceneViewFamily& ViewFamily) const override { return new FPixelUpscaler(Params); }
	virtual FScreenPassTexture AddPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FInputs& PassInputs) const override;

private:
	FPixelUpscalerParams Params;
};
