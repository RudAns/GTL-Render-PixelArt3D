#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "PixelArtRender.h"

namespace PixelArtRenderPrivate
{
	void StoreAppliedLayout(const FPixelArtLayout& Layout);
	bool WantsTexelReadback();
	void PublishTexels(TArray<FColor>&& Texels, FIntPoint Size);
}

/**
 * Drives r.ScreenPercentage so the game view renders at the layout's Render size, and installs
 * the pixel upscaler for that family. Only acts on the game viewport (not scene captures).
 */
class FPixelArtViewExtension : public FSceneViewExtensionBase
{
public:
	FPixelArtViewExtension(const FAutoRegister& AutoRegister);

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;

protected:
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

private:
	FPixelArtLayout FrameLayout;
	FPixelArtFrame FrameState;
	bool bFrameActive = false;
	bool bOwnsScreenPercentage = false;
};
