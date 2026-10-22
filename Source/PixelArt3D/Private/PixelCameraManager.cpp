#include "PixelCameraManager.h"

#include "PixelArtSubsystem.h"
#include "PixelArtRender.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UnrealClient.h"

APixelCameraManager::APixelCameraManager()
{
	bAlwaysApplyModifiers = false;
}

void APixelCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(this);
	if (!Sub)
	{
		Super::UpdateViewTarget(OutVT, DeltaTime);
		return;
	}
	const FPixelSettings& S = Sub->Settings;

	// --- framing (the follow itself is resolved below, once the texel grid is known) -------------
	const APawn* Pawn = Cast<APawn>(OutVT.Target);
	const FVector PawnPoint = Pawn ? Pawn->GetActorLocation() + FVector(0, 0, 40) : FVector::ZeroVector;
	// The visible character is its snapped mesh, not the actor: frame that, or the two quantisations beat.
	FVector SnappedPoint = PawnPoint;
	if (const ACharacter* Char = Cast<ACharacter>(Pawn)) SnappedPoint = Char->GetMesh()->GetComponentLocation() + FVector(0, 0, 120);

	if (S.bAutoPan)
	{
		PanTime += DeltaTime;
		// slow drift: ~0.2-0.6 texels per frame, the regime where unsnapped edges crawl
		PanOffset = FVector(FMath::Sin(PanTime * 0.25f) * 220.f, FMath::Sin(PanTime * 0.17f + 1.f) * 140.f, 0.f);
	}
	else
	{
		PanOffset = FMath::VInterpTo(PanOffset, FVector::ZeroVector, DeltaTime, 2.f);
	}

	// Rotation eases over ~0.2 s; the texel grid is screen-aligned, so snapping holds at any yaw.
	const float TargetYaw = S.YawDeg + S.YawExtraDeg;
	if (!bHasYaw) { ShownYaw = TargetYaw; bHasYaw = true; }
	const float DYaw = FMath::FindDeltaAngleDegrees(ShownYaw, TargetYaw);
	ShownYaw = FMath::Abs(DYaw) < 0.01f ? TargetYaw : ShownYaw + FMath::Clamp(DYaw, -270.f * DeltaTime, 270.f * DeltaTime);
	const FRotator Rot(-S.PitchDeg, ShownYaw, 0.f);
	const FRotationMatrix M(Rot);
	const FVector F = M.GetScaledAxis(EAxis::X);
	const FVector R = M.GetScaledAxis(EAxis::Y);
	const FVector U = M.GetScaledAxis(EAxis::Z);

	// --- resolution --------------------------------------------------------------------------
	FIntPoint ViewportSize(1920, 1080);
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport) ViewportSize = GEngine->GameViewport->Viewport->GetSizeXY();
	const FPixelArtLayout Layout = PixelArtRender::ComputeLayout(ViewportSize, S.PixelScale);
	const double Texel = 100.0 / FMath::Max(1.f, S.PixelsPerMeter);
	const double WidthTexels = S.bLowRes ? Layout.Render.X : double(ViewportSize.X) / S.PixelScale;

	FVector2D Parity = FVector2D::ZeroVector;
	if (S.bLowRes && Layout.bValid)
	{
		// An odd render dimension puts the frustum edge half a texel off the origin-anchored grid.
		Parity = FVector2D((Layout.Render.X & 1) ? 0.5 : 0.0, (Layout.Render.Y & 1) ? 0.5 : 0.0);
	}
	auto SnapToCameraGrid = [&](const FVector& P)
	{
		// round onto the object grid first, then add the parity half texel: rounding (n - 0.5) would be a
		// tie that float noise flips from frame to frame
		const double X = FVector::DotProduct(P, R) / Texel;
		const double Y = FVector::DotProduct(P, U) / Texel;
		return P + R * ((FMath::RoundToDouble(X) + Parity.X - X) * Texel) + U * ((FMath::RoundToDouble(Y) + Parity.Y - Y) * Texel);
	};

	// --- follow ----------------------------------------------------------------------------------
	const FVector Wanted = FocusOverride.IsSet() ? FocusOverride.GetValue() : FVector::ZeroVector;
	switch (S.FollowMode)
	{
	case EPixelFollow::Locked:
	{
		// Ride the player's snapped position, already on the camera grid: the player never moves on screen
		// while walking and the world steps exactly when the player does. Only station framing eases.
		const FVector Anchor = (S.bCameraSnap && S.bObjectSnap) ? SnapToCameraGrid(SnappedPoint) : PawnPoint;
		const FVector WantOffset = FocusOverride.IsSet() ? Wanted - Anchor : FVector::ZeroVector;
		FollowOffset = FMath::VInterpTo(FollowOffset, WantOffset, DeltaTime, 4.0f);
		if (!FocusOverride.IsSet() && FollowOffset.SizeSquared() < 0.25) FollowOffset = FVector::ZeroVector;
		Focus = Anchor + FollowOffset;
		bHasFocus = true;
		break;
	}
	case EPixelFollow::SmoothSub:
	{
		const FVector WantOffset = FocusOverride.IsSet() ? Wanted - PawnPoint : FVector::ZeroVector;
		FollowOffset = FMath::VInterpTo(FollowOffset, WantOffset, DeltaTime, 4.0f);
		Focus = PawnPoint + FollowOffset;
		bHasFocus = true;
		break;
	}
	default:
	{
		const FVector Target = FocusOverride.IsSet() ? Wanted : PawnPoint;
		if (!bHasFocus) { Focus = Target; bHasFocus = true; }
		Focus = FMath::VInterpTo(Focus, Target, DeltaTime, 4.0f);
		FollowOffset = Focus - PawnPoint;
		break;
	}
	}

	// --- snapping ----------------------------------------------------------------------------
	FVector Pos = Focus + PanOffset - F * 6000.0;
	FVector2D Remainder = FVector2D::ZeroVector;
	if (S.bCameraSnap)
	{
		const double X = FVector::DotProduct(Pos, R) / Texel - Parity.X;
		const double Y = FVector::DotProduct(Pos, U) / Texel - Parity.Y;
		const double SX = FMath::RoundToDouble(X), SY = FMath::RoundToDouble(Y);
		Remainder = FVector2D(X - SX, Y - SY);
		Pos += R * ((SX - X) * Texel) + U * ((SY - Y) * Texel);
	}

	FPixelArtFrame Frame = PixelArtRender::GetFrame();
	const bool bShift = (S.bSubTexel || S.FollowMode == EPixelFollow::SmoothSub) && S.bCameraSnap;
	Frame.SubTexel = bShift ? FVector2f(Remainder) : FVector2f::ZeroVector;
	{
		// What the eye sees, in texels: the player's position on screen and the world's scroll offset.
		const FVector2D Shift = bShift ? Remainder : FVector2D::ZeroVector;
		const FVector D = SnappedPoint - Pos;
		Sub->MotionSamples.Add(FVector4d(FVector::DotProduct(D, R) / Texel - Shift.X, FVector::DotProduct(D, U) / Texel - Shift.Y,
			FVector::DotProduct(Pos, R) / Texel + Shift.X, FVector::DotProduct(Pos, U) / Texel + Shift.Y));
		if (!Sub->bMotionProbe && Sub->MotionSamples.Num() > 48) Sub->MotionSamples.RemoveAt(0, Sub->MotionSamples.Num() - 48);
	}
	PixelArtRender::SetFrame(Frame);

	// --- view ------------------------------------------------------------------------------------
	FMinimalViewInfo& POV = OutVT.POV;
	POV.Location = Pos;
	POV.Rotation = Rot;
	POV.ProjectionMode = ECameraProjectionMode::Orthographic;
	POV.OrthoWidth = float(WidthTexels * Texel);
	POV.bAutoCalculateOrthoPlanes = false;
	POV.bUpdateOrthoPlanes = false;
	POV.OrthoNearClipPlane = 10.f;
	POV.OrthoFarClipPlane = 20000.f;
	POV.AspectRatioAxisConstraint = EAspectRatioAxisConstraint::AspectRatio_MaintainXFOV;
	POV.bConstrainAspectRatio = false;

	FPostProcessSettings& PP = POV.PostProcessSettings;
	PP = FPostProcessSettings();
	PP.bOverride_AutoExposureMethod = true; PP.AutoExposureMethod = AEM_Manual;
	PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true; PP.AutoExposureApplyPhysicalCameraExposure = false;
	PP.bOverride_AutoExposureBias = true; PP.AutoExposureBias = 0.f;
	PP.bOverride_BloomIntensity = true; PP.BloomIntensity = 0.f;
	PP.bOverride_VignetteIntensity = true; PP.VignetteIntensity = 0.f;
	PP.bOverride_SceneFringeIntensity = true; PP.SceneFringeIntensity = 0.f;
	PP.bOverride_MotionBlurAmount = true; PP.MotionBlurAmount = 0.f;
	if (!PostMid)
	{
		if (UMaterialInterface* Post = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/PixelArt/M_PixelPost.M_PixelPost")))
		{
			PostMid = UMaterialInstanceDynamic::Create(Post, this);
		}
	}
	if (PostMid) PP.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, PostMid));
	POV.PostProcessBlendWeight = 1.f;

	FPixelCameraState& C = Sub->Camera;
	C.Position = Pos; C.Forward = F; C.Right = R; C.Up = U; C.Yaw = ShownYaw;
	C.Texel = Texel; C.Remainder = Remainder; C.Layout = Layout; C.Parity = Parity; C.bValid = true;
}
