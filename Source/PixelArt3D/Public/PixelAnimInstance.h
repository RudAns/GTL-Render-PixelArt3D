#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "PixelAnimInstance.generated.h"

class UAnimSequence;

/**
 * Evaluates idle/walk directly in C++ (no anim graph), then applies the sprite rules from the talk:
 *  - stepped keys: sample time held at a low frame rate (p.93)
 *  - joint translation snapped to the screen texel grid in model space (p.69); feet do not inherit
 *    the parent's correction so they stay planted
 *  - optional chibi head scale (style)
 */
USTRUCT()
struct FPixelAnimProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FPixelAnimProxy() = default;
	FPixelAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	UAnimSequence* Idle = nullptr;
	UAnimSequence* Walk = nullptr;
	float IdleTime = 0.f;
	float WalkTime = 0.f;
	float WalkAlpha = 0.f;
	bool bStepped = true;
	float StepFps = 8.f;
	bool bBoneSnap = true;
	bool bChibi = true;
	FTransform ComponentToWorld = FTransform::Identity;
	FVector CamRight = FVector::RightVector;
	FVector CamUp = FVector::UpVector;
	double Texel = 6.25;
	TMap<int32, FVector2d> ShownOffset; // per joint: texel offset from the root currently shown (hysteresis)
};

UCLASS(Transient)
class PIXELART3D_API UPixelAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UPROPERTY() TObjectPtr<UAnimSequence> IdleSeq;
	UPROPERTY() TObjectPtr<UAnimSequence> WalkSeq;

	float IdleTime = 0.f;
	float WalkTime = 0.f;
	float WalkAlpha = 0.f;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FPixelAnimProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete static_cast<FPixelAnimProxy*>(InProxy); }

	friend struct FPixelAnimProxy;
};
