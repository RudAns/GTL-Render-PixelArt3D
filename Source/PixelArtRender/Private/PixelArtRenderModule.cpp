#include "PixelArtRender.h"
#include "PixelArtViewExtension.h"

#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SceneViewExtension.h"
#include "ShaderCore.h"
#include <atomic>

namespace
{
	FCriticalSection GFrameLock;
	FPixelArtFrame GFrame;
	FPixelArtLayout GAppliedLayout;
}

FPixelArtLayout PixelArtRender::ComputeLayout(FIntPoint Viewport, int32 Scale)
{
	FPixelArtLayout L;
	L.Viewport = Viewport;
	L.Scale = FMath::Clamp(Scale, 1, 16);
	if (Viewport.X < 16 || Viewport.Y < 16)
	{
		return L;
	}

	L.Inner = FIntPoint(Viewport.X / L.Scale, Viewport.Y / L.Scale);

	// Render height must make Render.X an integer at the viewport's exact aspect, so texels are square:
	// Render.Y has to be a multiple of Viewport.Y / gcd(X, Y). Need >= 1 texel of margin on every side.
	const int32 G = FMath::GreatestCommonDivisor(Viewport.X, Viewport.Y);
	const int32 Step = Viewport.Y / G;
	const int32 MinMargin = 2;

	if (Step <= FMath::Max(8, L.Inner.Y / 4))
	{
		int32 RenderY = ((L.Inner.Y + 2 * MinMargin + Step - 1) / Step) * Step;
		int32 RenderX = int32((int64)RenderY * Viewport.X / Viewport.Y);
		while (RenderX < L.Inner.X + 2 * MinMargin)
		{
			RenderY += Step;
			RenderX = int32((int64)RenderY * Viewport.X / Viewport.Y);
		}
		L.Render = FIntPoint(RenderX, RenderY);
		L.bSquareTexels = true;
	}
	else
	{
		// Odd window size: accept a sub-percent aspect error instead of a huge margin.
		const int32 RenderY = L.Inner.Y + 2 * MinMargin;
		L.Render = FIntPoint(FMath::CeilToInt((double)RenderY * Viewport.X / Viewport.Y), RenderY);
		L.bSquareTexels = false;
	}

	L.InnerOffset = FIntPoint((L.Render.X - L.Inner.X) / 2, (L.Render.Y - L.Inner.Y) / 2);
	L.bValid = true;
	return L;
}

void PixelArtRender::SetFrame(const FPixelArtFrame& Frame)
{
	FScopeLock Lock(&GFrameLock);
	GFrame = Frame;
}

FPixelArtFrame PixelArtRender::GetFrame()
{
	FScopeLock Lock(&GFrameLock);
	return GFrame;
}

FPixelArtLayout PixelArtRender::GetAppliedLayout()
{
	FScopeLock Lock(&GFrameLock);
	return GAppliedLayout;
}

namespace
{
	FCriticalSection GTexelLock;
	TArray<FColor> GTexels;
	FIntPoint GTexelSize = FIntPoint::ZeroValue;
	bool GTexelsFresh = false;
	std::atomic<bool> GTexelReadback{ false };
}

void PixelArtRender::SetTexelReadback(bool bEnable) { GTexelReadback = bEnable; }

bool PixelArtRender::FetchTexels(TArray<FColor>& Out, FIntPoint& OutSize)
{
	FScopeLock Lock(&GTexelLock);
	if (!GTexelsFresh) return false;
	Out = GTexels; OutSize = GTexelSize; GTexelsFresh = false;
	return true;
}

bool PixelArtRenderPrivate::WantsTexelReadback() { return GTexelReadback; }

void PixelArtRenderPrivate::PublishTexels(TArray<FColor>&& Texels, FIntPoint Size)
{
	FScopeLock Lock(&GTexelLock);
	GTexels = MoveTemp(Texels); GTexelSize = Size; GTexelsFresh = true;
}

void PixelArtRenderPrivate::StoreAppliedLayout(const FPixelArtLayout& Layout)
{
	FScopeLock Lock(&GFrameLock);
	GAppliedLayout = Layout;
}

class FPixelArtRenderModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		AddShaderSourceDirectoryMapping(TEXT("/PixelArt3D"), FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders")));
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddLambda([this]()
		{
			Extension = FSceneViewExtensions::NewExtension<FPixelArtViewExtension>();
		});
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
		Extension.Reset();
	}

private:
	TSharedPtr<FPixelArtViewExtension, ESPMode::ThreadSafe> Extension;
	FDelegateHandle PostEngineInitHandle;
};

IMPLEMENT_MODULE(FPixelArtRenderModule, PixelArtRender)
