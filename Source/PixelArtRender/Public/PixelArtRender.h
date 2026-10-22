#pragma once

#include "CoreMinimal.h"

class FTextureResource;

/**
 * Geometry of one pixel-art frame.
 *
 *  Viewport : back buffer size in device pixels.
 *  Inner    : the displayed low-res image, in texels (= Viewport / Scale, floored).
 *  Render   : the rendered low-res image incl. a margin on every side. Its aspect matches the
 *             viewport exactly, so texels stay square (Render.X / Render.Y == Viewport.X / Viewport.Y).
 *  InnerOffset : where Inner sits inside Render (texels). The margin is what lets the upscaler
 *             shift the displayed window by a sub-texel amount without sampling outside the render.
 */
struct FPixelArtLayout
{
	FIntPoint Viewport = FIntPoint::ZeroValue;
	FIntPoint Render = FIntPoint::ZeroValue;
	FIntPoint Inner = FIntPoint::ZeroValue;
	FIntPoint InnerOffset = FIntPoint::ZeroValue;
	int32 Scale = 1;
	bool bSquareTexels = true;
	bool bValid = false;
};

enum class EPixelArtUpscale : uint8
{
	Nearest,
	Bilinear,
};

/** Per-frame state written by the game thread, consumed when the view family is set up. */
struct FPixelArtFrame
{
	bool bEnabled = true;          // false: full-res rendering, engine default upscale
	int32 Scale = 4;               // device pixels per texel
	FVector2f SubTexel = FVector2f::ZeroVector; // camera snap remainder, in texels (x right, y up)
	EPixelArtUpscale Filter = EPixelArtUpscale::Nearest;
	bool bShowGrid = false;        // draw the texel grid on top of the upscaled image
	int32 SteadyLayerId = 0;       // custom-stencil id shown WITHOUT the SubTexel shift (the followed character); 0 = none

	// The followed character's shadow, composited at upscale time in the character's own (steady) frame.
	// The scene's shadow map leaves the character out; a second map holds only the character.
	bool bPlayerShadow = false;
	FTextureResource* WorldShadow = nullptr;   // R32f light-space depth, everything but the character
	FTextureResource* PlayerShadow = nullptr;  // R32f light-space depth, the character only
	FVector3f LightOrigin = FVector3f::ZeroVector, LightRight = FVector3f::ZeroVector, LightUp = FVector3f::ZeroVector, LightDir = FVector3f::ZeroVector;
	float LightExtent = 1.f;
	FVector3f PlayerLightOrigin = FVector3f::ZeroVector; // the character map is centred on the character
	float PlayerLightExtent = 1.f;
	float ShadowBias = 11.f;
	FLinearColor ShadowTint = FLinearColor::White; // display-space multiplier lit ground -> shadowed ground
};

namespace PixelArtRender
{
	PIXELARTRENDER_API FPixelArtLayout ComputeLayout(FIntPoint Viewport, int32 Scale);

	PIXELARTRENDER_API void SetFrame(const FPixelArtFrame& Frame);
	PIXELARTRENDER_API FPixelArtFrame GetFrame();

	/** Layout the view extension applied to the last game viewport family (game thread). */
	PIXELARTRENDER_API FPixelArtLayout GetAppliedLayout();

	/**
	 * GPU readback of the displayed low-res texels (before upscaling), for the frame-to-frame flicker meter.
	 * Enable it, then poll: frames come out oldest first (several readbacks are in flight, so consecutive
	 * rendered frames arrive without gaps), each with Inner.X * Inner.Y colors and its render frame number.
	 */
	PIXELARTRENDER_API void SetTexelReadback(bool bEnable, bool bFinalImage = false); // final: the upscaled output instead
	PIXELARTRENDER_API bool FetchTexels(TArray<FColor>& Out, FIntPoint& OutSize, uint64* OutFrame = nullptr);
}
