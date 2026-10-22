#pragma once

#include "CoreMinimal.h"

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
	PIXELARTRENDER_API void SetTexelReadback(bool bEnable);
	PIXELARTRENDER_API bool FetchTexels(TArray<FColor>& Out, FIntPoint& OutSize, uint64* OutFrame = nullptr);
}
