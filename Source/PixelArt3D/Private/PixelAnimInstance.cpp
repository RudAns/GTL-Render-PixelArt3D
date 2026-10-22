#include "PixelAnimInstance.h"

#include "PixelArtSubsystem.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimNodeBase.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"

namespace
{
	// Joints snapped individually; every other bone inherits its parent's (snapped) motion.
	// KayKit Rig_Medium (shared by every adventurer).
	const TCHAR* SnapBones[] = {
		TEXT("hips"), TEXT("spine"), TEXT("chest"), TEXT("head"),
		TEXT("upperarm.l"), TEXT("lowerarm.l"), TEXT("hand.l"),
		TEXT("upperarm.r"), TEXT("lowerarm.r"), TEXT("hand.r"),
		TEXT("upperleg.l"), TEXT("lowerleg.l"), TEXT("foot.l"),
		TEXT("upperleg.r"), TEXT("lowerleg.r"), TEXT("foot.r"),
	};
	// Snapped from the unsnapped chain so corrections do not accumulate down the leg (talk p.69).
	const TCHAR* NoInheritBones[] = { TEXT("foot.l"), TEXT("foot.r") };
}

void UPixelAnimInstance::NativeInitializeAnimation()
{
	IdleSeq = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/KayKit/Anims/Rig_Medium_General_Idle_A.Rig_Medium_General_Idle_A"));
	WalkSeq = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/KayKit/Anims/Rig_Medium_MovementBasic_Walking_A.Rig_Medium_MovementBasic_Walking_A"));
	IdleTime = FMath::FRand() * 2.f; // bystanders should not breathe in sync
}

void UPixelAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	const APawn* Pawn = TryGetPawnOwner();
	const float Speed = Pawn ? Pawn->GetVelocity().Size2D() : 0.f;
	const float Target = FMath::Clamp(Speed / 120.f, 0.f, 1.f);
	WalkAlpha = FMath::FInterpTo(WalkAlpha, Target, DeltaSeconds, 10.f);
	IdleTime += DeltaSeconds;
	WalkTime += DeltaSeconds * FMath::Clamp(Speed / 165.f, 0.6f, 1.6f);
}

void FPixelAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	UPixelAnimInstance* Inst = CastChecked<UPixelAnimInstance>(InAnimInstance);
	Idle = Inst->IdleSeq;
	Walk = Inst->WalkSeq;
	IdleTime = Inst->IdleTime;
	WalkTime = Inst->WalkTime;
	WalkAlpha = Inst->WalkAlpha;
	if (const USkeletalMeshComponent* Comp = Inst->GetSkelMeshComponent())
	{
		ComponentToWorld = Comp->GetComponentTransform();
	}
	if (const UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(Inst))
	{
		bStepped = Sub->Settings.bSteppedAnim;
		bBoneSnap = Sub->Settings.bBoneSnap && Sub->Settings.bObjectSnap;
		bChibi = Sub->Settings.bChibi;
		CamRight = Sub->Camera.Right;
		CamUp = Sub->Camera.Up;
		Texel = Sub->Camera.Texel;
	}
}

bool FPixelAnimProxy::Evaluate(FPoseContext& Output)
{
	if (!Idle || !Walk)
	{
		Output.ResetToRefPose();
		return true;
	}

	// Stepped keys sample a fixed, even number of poses per loop (8 for the 1.07 s walk), like a sprite
	// sheet: every cycle shows the same frames. Sampling at a fixed rate instead drifts against a loop
	// that is not a whole number of frames long, so the hip bob (half a texel) rounds to a 1-texel hop
	// on different frames each cycle and reads as a random jump.
	auto Sample = [this](const UAnimSequence* Seq, float T)
	{
		const float Len = FMath::Max(Seq->GetPlayLength(), 0.01f);
		const float Phase = FMath::Fmod(T, Len) / Len;
		if (!bStepped) return Phase * Len;
		const int32 N = FMath::Max(2, 2 * FMath::RoundToInt(Len * StepFps * 0.5f));
		return FMath::FloorToFloat(Phase * N) / N * Len;
	};
	// In stepped mode the blend weight is binary too: an in-between weight is just interpolation again.
	const float Alpha = bStepped ? (WalkAlpha > 0.5f ? 1.f : 0.f) : WalkAlpha;

	{
		FAnimationPoseData IdleData(Output);
		Idle->GetAnimationPose(IdleData, FAnimExtractContext(double(Sample(Idle, IdleTime)), false, FDeltaTimeRecord(), true));
	}
	if (Alpha > 0.f)
	{
		FPoseContext WalkPose(Output);
		FAnimationPoseData WalkData(WalkPose);
		Walk->GetAnimationPose(WalkData, FAnimExtractContext(double(Sample(Walk, WalkTime)), false, FDeltaTimeRecord(), true));
		FAnimationPoseData OutData(Output);
		FAnimationRuntime::BlendTwoPosesTogetherInPlace(OutData, WalkData, 1.f - Alpha);
	}

	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	auto Compact = [&Bones](const TCHAR* Name) -> FCompactPoseBoneIndex
	{
		const int32 PoseIdx = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return PoseIdx == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(PoseIdx));
	};

	if (bChibi)
	{
		const FCompactPoseBoneIndex Head = Compact(TEXT("head"));
		static bool bLogged = false;
		if (!bLogged) { bLogged = true; UE_LOG(LogTemp, Display, TEXT("PixelAnim: head bone %s, %d bones"), Head.IsValid() ? TEXT("found") : TEXT("missing"), Output.Pose.GetNumBones()); }
		if (Head.IsValid()) Output.Pose[Head].SetScale3D(FVector(1.7));
	}

	if (!bBoneSnap) ShownOffset.Reset();
	if (bBoneSnap && Texel > 0.0)
	{
		TSet<int32> Snap, NoInherit;
		for (const TCHAR* N : SnapBones) { const FCompactPoseBoneIndex I = Compact(N); if (I.IsValid()) Snap.Add(I.GetInt()); }
		for (const TCHAR* N : NoInheritBones) { const FCompactPoseBoneIndex I = Compact(N); if (I.IsValid()) NoInherit.Add(I.GetInt()); }

		const int32 Num = Output.Pose.GetNumBones();
		TArray<FTransform> CS, CS0;
		CS.SetNum(Num); CS0.SetNum(Num);
		// Each joint sits on a whole-texel offset from the (snapped) root, with hysteresis: it only moves
		// to a new texel once it is 0.75 texel away from the one it shows. Plain rounding turns the walk's
		// 0.4-texel hip bob into a full-texel hop whenever the bob crosses a rounding boundary.
		const FVector RootW = ComponentToWorld.GetLocation();
		const FVector2d RootT(FMath::RoundToDouble(FVector::DotProduct(RootW, CamRight) / Texel), FMath::RoundToDouble(FVector::DotProduct(RootW, CamUp) / Texel));
		auto SnapW = [this, &RootT](const FVector& W, int32 Bone)
		{
			const double X = FVector::DotProduct(W, CamRight) / Texel;
			const double Y = FVector::DotProduct(W, CamUp) / Texel;
			const FVector2d Rel(X - RootT.X, Y - RootT.Y);
			FVector2d& Shown = ShownOffset.FindOrAdd(Bone, FVector2d(FMath::RoundToDouble(Rel.X), FMath::RoundToDouble(Rel.Y)));
			if (FMath::Abs(Rel.X - Shown.X) >= 0.75) Shown.X = FMath::RoundToDouble(Rel.X);
			if (FMath::Abs(Rel.Y - Shown.Y) >= 0.75) Shown.Y = FMath::RoundToDouble(Rel.Y);
			return W + CamRight * ((RootT.X + Shown.X - X) * Texel) + CamUp * ((RootT.Y + Shown.Y - Y) * Texel);
		};
		for (const FCompactPoseBoneIndex B : Output.Pose.ForEachBoneIndex())
		{
			const FCompactPoseBoneIndex P = Bones.GetParentBoneIndex(B);
			const FTransform& L = Output.Pose[B];
			CS0[B.GetInt()] = P.IsValid() ? L * CS0[P.GetInt()] : L;
			CS[B.GetInt()] = P.IsValid() ? L * CS[P.GetInt()] : L;
			if (!Snap.Contains(B.GetInt())) continue;

			const FVector From = NoInherit.Contains(B.GetInt()) ? CS0[B.GetInt()].GetLocation() : CS[B.GetInt()].GetLocation();
			const FVector Cs = ComponentToWorld.InverseTransformPosition(SnapW(ComponentToWorld.TransformPosition(From), B.GetInt()));
			CS[B.GetInt()].SetLocation(Cs);
			Output.Pose[B].SetTranslation(P.IsValid() ? CS[P.GetInt()].InverseTransformPosition(Cs) : Cs);
		}
	}
	return true;
}
