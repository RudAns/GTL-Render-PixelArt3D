#include "PixelArtUpscaler.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "RenderUtils.h"
#include "ScreenPass.h"
#include "SceneRendering.h"
#include "ShaderParameterStruct.h"
#include "PixelArtViewExtension.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "SceneTextures.h"
#include "SystemTextures.h"

class FPixelUpscalePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPixelUpscalePS);
	SHADER_USE_PARAMETER_STRUCT(FPixelUpscalePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, PointSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER(FVector2f, InputExtentInv)
		SHADER_PARAMETER(FVector2f, RectMin)
		SHADER_PARAMETER(FVector2f, InnerOffset)
		SHADER_PARAMETER(FVector2f, InnerSize)
		SHADER_PARAMETER(FVector2f, OutputMin)
		SHADER_PARAMETER(FVector2f, Pad)
		SHADER_PARAMETER(FVector2f, SubTexel)
		SHADER_PARAMETER(float, Scale)
		SHADER_PARAMETER(float, UseBilinear)
		SHADER_PARAMETER(float, ShowGrid)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint2>, StencilTexture)
		SHADER_PARAMETER(uint32, SteadyLayerId)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FPixelUpscalePS, "/PixelArt3D/PixelUpscale.usf", "MainPS", SF_Pixel);

namespace
{
	// Render-thread readbacks of the displayed inner window (flicker meter, frame dumps). A small ring, so
	// every rendered frame can be read back even though each copy takes a frame or two to land.
	struct FReadbackSlot
	{
		TUniquePtr<FRHIGPUTextureReadback> Readback;
		FIntRect Rect;
		EPixelFormat Format = PF_Unknown;
		uint64 Frame = 0;
		bool bPending = false;
	};
	FReadbackSlot GSlots[6];

	FColor DecodeTexel(const uint8* Row, int32 X, EPixelFormat Format)
	{
		switch (Format)
		{
		case PF_B8G8R8A8: { const uint8* P = Row + X * 4; return FColor(P[2], P[1], P[0], 255); }
		case PF_R8G8B8A8: { const uint8* P = Row + X * 4; return FColor(P[0], P[1], P[2], 255); }
		case PF_A2B10G10R10:
		{
			const uint32 V = reinterpret_cast<const uint32*>(Row)[X];
			return FColor(uint8(((V >> 0) & 1023) >> 2), uint8(((V >> 10) & 1023) >> 2), uint8(((V >> 20) & 1023) >> 2), 255);
		}
		case PF_FloatRGBA:
		{
			const FFloat16* P = reinterpret_cast<const FFloat16*>(Row) + X * 4;
			return FLinearColor(P[0].GetFloat(), P[1].GetFloat(), P[2].GetFloat(), 1.f).QuantizeRound();
		}
		case PF_FloatR11G11B10:
		{
			// Unsigned mini-floats: 5-bit exponent, 6/6/5-bit mantissa (R, G, B from the low bits up).
			auto Mini = [](uint32 Bits, int32 MantBits) -> float
			{
				const uint32 E = Bits >> MantBits, M = Bits & ((1u << MantBits) - 1);
				if (E == 0) return float(M) / float(1 << MantBits) * FMath::Pow(2.f, -14.f);
				if (E == 31) return 65504.f;
				return (1.f + float(M) / float(1 << MantBits)) * FMath::Pow(2.f, float(int32(E) - 15));
			};
			const uint32 V = reinterpret_cast<const uint32*>(Row)[X];
			return FLinearColor(Mini(V & 0x7FF, 6), Mini((V >> 11) & 0x7FF, 6), Mini(V >> 22, 5), 1.f).QuantizeRound();
		}
		default: return FColor::Black;
		}
	}

	void PollAndEnqueueReadback(FRDGBuilder& GraphBuilder, FRDGTextureRef Source, const FIntRect& Rect)
	{
		// publish finished copies oldest first
		for (;;)
		{
			FReadbackSlot* Oldest = nullptr;
			for (FReadbackSlot& S : GSlots) if (S.bPending && (!Oldest || S.Frame < Oldest->Frame)) Oldest = &S;
			if (!Oldest || !Oldest->Readback->IsReady()) break;
			int32 PitchPx = 0;
			const uint8* Data = static_cast<const uint8*>(Oldest->Readback->Lock(PitchPx));
			if (Data)
			{
				const int32 Bpp = GPixelFormats[Oldest->Format].BlockBytes;
				const FIntPoint Size = Oldest->Rect.Size();
				TArray<FColor> Out; Out.SetNumUninitialized(Size.X * Size.Y);
				for (int32 Y = 0; Y < Size.Y; ++Y)
				{
					const uint8* Row = Data + size_t(Oldest->Rect.Min.Y + Y) * PitchPx * Bpp;
					for (int32 X = 0; X < Size.X; ++X) Out[Y * Size.X + X] = DecodeTexel(Row, Oldest->Rect.Min.X + X, Oldest->Format);
				}
				Oldest->Readback->Unlock();
				PixelArtRenderPrivate::PublishTexels(MoveTemp(Out), Size, Oldest->Frame);
			}
			Oldest->bPending = false;
		}
		if (!PixelArtRenderPrivate::WantsTexelReadback()) return;
		for (FReadbackSlot& S : GSlots)
		{
			if (S.bPending) continue;
			if (!S.Readback) S.Readback = MakeUnique<FRHIGPUTextureReadback>(TEXT("PixelArt3D.TexelReadback"));
			S.Rect = Rect;
			S.Format = Source->Desc.Format;
			S.Frame = GFrameCounterRenderThread;
			AddEnqueueCopyPass(GraphBuilder, S.Readback.Get(), Source);
			S.bPending = true;
			break;
		}
	}
}

FScreenPassTexture FPixelUpscaler::AddPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FInputs& PassInputs) const
{
	FScreenPassRenderTarget Output = PassInputs.OverrideOutput;
	if (!Output.IsValid())
	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
			PassInputs.SceneColor.Texture->Desc.Extent, PassInputs.SceneColor.Texture->Desc.Format,
			FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_RenderTargetable);

		if (PassInputs.Stage == EUpscaleStage::PrimaryToSecondary)
		{
			const FIntPoint Secondary = View.GetSecondaryViewRectSize();
			QuantizeSceneBufferSize(Secondary, Desc.Extent);
			Output.ViewRect = FIntRect(FIntPoint::ZeroValue, Secondary);
		}
		else
		{
			Desc.Extent = View.UnscaledViewRect.Max;
			Output.ViewRect = View.UnscaledViewRect;
		}
		Output.Texture = GraphBuilder.CreateTexture(Desc, TEXT("PixelArt3D.Upscale"));
		Output.LoadAction = ERenderTargetLoadAction::EClear;
	}

	const FIntRect RenderRect = PassInputs.SceneColor.ViewRect;
	const FIntPoint RenderSize = RenderRect.Size();
	const FIntPoint Inner(FMath::Min(Params.Inner.X, RenderSize.X), FMath::Min(Params.Inner.Y, RenderSize.Y));
	const FIntPoint InnerOffset = (RenderSize - Inner) / 2;
	const FIntPoint OutSize = Output.ViewRect.Size();
	const int32 Scale = FMath::Max(1, FMath::Min(OutSize.X / FMath::Max(1, Inner.X), OutSize.Y / FMath::Max(1, Inner.Y)));
	const FIntPoint Pad = (OutSize - Inner * Scale) / 2;
	const FIntPoint Extent = PassInputs.SceneColor.Texture->Desc.Extent;

	FPixelUpscalePS::FParameters* P = GraphBuilder.AllocParameters<FPixelUpscalePS::FParameters>();
	P->InputTexture = PassInputs.SceneColor.Texture;
	P->PointSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	P->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	P->InputExtentInv = FVector2f(1.0f / Extent.X, 1.0f / Extent.Y);
	P->RectMin = FVector2f(RenderRect.Min);
	P->InnerOffset = FVector2f(InnerOffset);
	P->InnerSize = FVector2f(Inner);
	P->OutputMin = FVector2f(Output.ViewRect.Min);
	P->Pad = FVector2f(Pad);
	// Texture V grows downward while the camera's Up axis grows upward.
	P->SubTexel = FVector2f(Params.SubTexel.X, -Params.SubTexel.Y);
	P->Scale = float(Scale);
	P->UseBilinear = Params.bBilinear ? 1.0f : 0.0f;
	P->ShowGrid = Params.bShowGrid ? 1.0f : 0.0f;
	// Steady layer: texels whose custom stencil is the followed character are shown unshifted, everything
	// else with the camera's sub-texel shift. The background scrolls smoothly while the character, already
	// on a whole-texel offset from the snapped camera, stays exactly where its continuous position is.
	FRDGTextureSRVRef Stencil = nullptr;
	if (Params.SteadyLayerId > 0 && !Params.bBilinear)
	{
		const FSceneTextures& SceneTextures = View.GetSceneTextures();
		if (SceneTextures.CustomDepth.IsValid() && SceneTextures.CustomDepth.Stencil) Stencil = SceneTextures.CustomDepth.Stencil;
	}
	P->SteadyLayerId = Stencil ? uint32(Params.SteadyLayerId) : 0u;
	P->StencilTexture = Stencil ? Stencil : GraphBuilder.CreateSRV(FRDGTextureSRVDesc(GSystemTextures.GetZeroUIntDummy(GraphBuilder)));
	P->RenderTargets[0] = Output.GetRenderTargetBinding();

	const bool bFinal = PixelArtRenderPrivate::WantsFinalReadback();
	if (!bFinal) PollAndEnqueueReadback(GraphBuilder, PassInputs.SceneColor.Texture, FIntRect(RenderRect.Min + InnerOffset, RenderRect.Min + InnerOffset + Inner));

	TShaderMapRef<FPixelUpscalePS> PixelShader(View.ShaderMap);
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder, View.ShaderMap,
		RDG_EVENT_NAME("PixelArt3D.Upscale %dx%d x%d", Inner.X, Inner.Y, Scale),
		PixelShader, P, Output.ViewRect);
	// frame dumps of what is actually displayed (after the layer composite), for motion inspection
	if (bFinal) PollAndEnqueueReadback(GraphBuilder, Output.Texture, Output.ViewRect);

	return MoveTemp(Output);
}
