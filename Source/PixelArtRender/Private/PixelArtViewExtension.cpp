#include "PixelArtViewExtension.h"
#include "PixelArtUpscaler.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "SceneView.h"
#include "UnrealClient.h"

namespace
{
	IConsoleVariable* ScreenPercentageCVar()
	{
		static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
		return CVar;
	}
}

FPixelArtViewExtension::FPixelArtViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

bool FPixelArtViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	// Game viewport only: scene captures (shadow map, debug captures) keep their own resolution.
	return GEngine && GEngine->GameViewport && Context.Viewport != nullptr
		&& Context.Viewport == GEngine->GameViewport->Viewport;
}

void FPixelArtViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	FrameState = PixelArtRender::GetFrame();
	bFrameActive = false;

	IConsoleVariable* CVar = ScreenPercentageCVar();
	if (!CVar || !InViewFamily.RenderTarget)
	{
		return;
	}

	if (!FrameState.bEnabled)
	{
		if (bOwnsScreenPercentage)
		{
			CVar->Set(100.0f, ECVF_SetByCode);
			bOwnsScreenPercentage = false;
		}
		PixelArtRenderPrivate::StoreAppliedLayout(FPixelArtLayout());
		return;
	}

	FrameLayout = PixelArtRender::ComputeLayout(InViewFamily.RenderTarget->GetSizeXY(), FrameState.Scale);
	if (!FrameLayout.bValid)
	{
		return;
	}

	// ApplyResolutionFraction ceils W*f and H*f; the small epsilon keeps H*f just under Render.Y.
	const float Fraction = (FrameLayout.Render.Y - 0.001f) / FrameLayout.Viewport.Y;
	CVar->Set(Fraction * 100.0f, ECVF_SetByCode);
	bOwnsScreenPercentage = true;
	bFrameActive = true;
	PixelArtRenderPrivate::StoreAppliedLayout(FrameLayout);
}

void FPixelArtViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	if (!bFrameActive || InViewFamily.Views.Num() != 1 || InViewFamily.GetPrimarySpatialUpscalerInterface() != nullptr)
	{
		return;
	}
	const FSceneView* View = InViewFamily.Views[0];
	if (View->PrimaryScreenPercentageMethod != EPrimaryScreenPercentageMethod::SpatialUpscale)
	{
		return; // TSR / TAAU own the upscale; the report explains why AA must be off.
	}

	FPixelUpscalerParams Params;
	Params.Inner = FrameLayout.Inner;
	Params.ExpectedRender = FrameLayout.Render;
	Params.SubTexel = FrameState.SubTexel;
	Params.bBilinear = FrameState.Filter == EPixelArtUpscale::Bilinear;
	Params.bShowGrid = FrameState.bShowGrid;
	Params.SteadyLayerId = FrameState.SteadyLayerId;
	InViewFamily.SetPrimarySpatialUpscalerInterface(new FPixelUpscaler(Params));
}
