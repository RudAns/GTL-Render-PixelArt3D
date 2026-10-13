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
	// Render-thread readback of the displayed inner window (flicker meter).
	TUniquePtr<FRHIGPUTextureReadback> GReadback;
	FIntRect GReadbackRect;
	EPixelFormat GReadbackFormat = PF_Unknown;
	bool GReadbackPending = false;

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
		if (GReadbackPending && GReadback && GReadback->IsReady())
		{
			int32 PitchPx = 0;
			const uint8* Data = static_cast<const uint8*>(GReadback->Lock(PitchPx));
			if (Data)
			{
				const int32 Bpp = GPixelFormats[GReadbackFormat].BlockBytes;
				const FIntPoint Size = GReadbackRect.Size();
				TArray<FColor> Out; Out.SetNumUninitialized(Size.X * Size.Y);
				for (int32 Y = 0; Y < Size.Y; ++Y)
				{
					const uint8* Row = Data + size_t(GReadbackRect.Min.Y + Y) * PitchPx * Bpp;
					for (int32 X = 0; X < Size.X; ++X) Out[Y * Size.X + X] = DecodeTexel(Row, GReadbackRect.Min.X + X, GReadbackFormat);
				}
				GReadback->Unlock();
				PixelArtRenderPrivate::PublishTexels(MoveTemp(Out), Size);
			}
			GReadbackPending = false;
		}
		if (!GReadbackPending && PixelArtRenderPrivate::WantsTexelReadback())
		{
			if (!GReadback) GReadback = MakeUnique<FRHIGPUTextureReadback>(TEXT("PixelArt3D.TexelReadback"));
			GReadbackRect = Rect;
			GReadbackFormat = Source->Desc.Format;
			AddEnqueueCopyPass(GraphBuilder, GReadback.Get(), Source);
			GReadbackPending = true;
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
	P->RenderTargets[0] = Output.GetRenderTargetBinding();

	PollAndEnqueueReadback(GraphBuilder, PassInputs.SceneColor.Texture, FIntRect(RenderRect.Min + InnerOffset, RenderRect.Min + InnerOffset + Inner));

	TShaderMapRef<FPixelUpscalePS> PixelShader(View.ShaderMap);
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder, View.ShaderMap,
		RDG_EVENT_NAME("PixelArt3D.Upscale %dx%d x%d", Inner.X, Inner.Y, Scale),
		PixelShader, P, Output.ViewRect);

	return MoveTemp(Output);
}
