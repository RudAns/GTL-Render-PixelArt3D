#include "PixelCharacter.h"

#include "PixelAnimInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PixelArtSubsystem.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"

APixelCharacter::APixelCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	GetCapsuleComponent()->InitCapsuleSize(30.f, 80.f);
	bUseControllerRotationYaw = false;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->RotationRate = FRotator(0, 540, 0);
	Move->MaxWalkSpeed = 260.f;
	Move->MaxAcceleration = 1400.f;
	Move->BrakingDecelerationWalking = 2000.f;
	Move->bCanWalkOffLedges = false; // the map is a raised diorama; stay on the tiles

	USkeletalMeshComponent* SkMesh = GetMesh();
	SkMesh->SetRelativeLocationAndRotation(MeshBaseLocation, FRotator(0, MeshBaseYaw, 0));
	SkMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	SkMesh->SetAnimInstanceClass(UPixelAnimInstance::StaticClass());
	SkMesh->SetTickGroup(TG_PostPhysics);
	SkMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	// Spear slung across the back: long enough to poke into a wall the soldier walks along.
	SpearShaft = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SpearShaft"));
	SpearShaft->SetupAttachment(SkMesh);
	SpearShaft->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpearTip = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SpearTip"));
	SpearTip->SetupAttachment(SpearShaft);
	SpearTip->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	BlobShadow = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BlobShadow"));
	BlobShadow->SetupAttachment(GetCapsuleComponent());
	BlobShadow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BlobShadow->SetRelativeLocation(FVector(0, 0, -79.f));
	BlobShadow->SetRelativeScale3D(FVector(0.55f, 0.45f, 0.01f));
	BlobShadow->SetVisibility(false);
}

void APixelCharacter::BeginPlay()
{
	Super::BeginPlay();

	USkeletalMeshComponent* SkMesh = GetMesh();
	if (USkeletalMesh* Knight = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/KayKit/Characters/Knight.Knight")))
	{
		SkMesh->SetSkeletalMesh(Knight);
	}
	SkMesh->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);

	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	SpearShaft->SetStaticMesh(Cyl);
	SpearTip->SetStaticMesh(Cone);
	// Cylinder is 100 cm tall along Z: a 2.8 m shaft slung across the back, along the body's left-right
	// axis (mesh X) and tilted up 25 degrees, so walking along a wall pushes one end into it.
	SpearShaft->SetRelativeScale3D(FVector(0.09f, 0.09f, 2.8f));
	SpearShaft->SetRelativeLocationAndRotation(FVector(0.f, -22.f, 78.f), FRotator(-65.f, 0.f, 0.f));
	// Hang it on the chest bone so it bobs and snaps with the body instead of floating on the root.
	const FName Chest(TEXT("chest"));
	if (SkMesh->DoesSocketExist(Chest))
	{
		const FTransform OnRoot = SpearShaft->GetRelativeTransform();
		const FTransform ChestCS = SkMesh->GetSocketTransform(Chest, RTS_Component);
		SpearShaft->AttachToComponent(SkMesh, FAttachmentTransformRules::KeepRelativeTransform, Chest);
		SpearShaft->SetRelativeTransform(OnRoot.GetRelativeTransform(ChestCS));
	}
	SpearTip->SetRelativeScale3D(FVector(2.0f, 2.0f, 0.1f));
	SpearTip->SetRelativeLocation(FVector(0, 0, 53.f));

	if (UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(this))
	{
		Sub->ApplyMeshMaterial(SkMesh, LoadObject<UTexture>(nullptr, TEXT("/Game/KayKit/Textures/knight_texture.knight_texture")));
		SpearShaft->SetMaterial(0, Sub->MakeSurfaceMaterial(this, 0, FLinearColor(0.55f, 0.36f, 0.18f), false));
		SpearTip->SetMaterial(0, Sub->MakeSurfaceMaterial(this, 0, FLinearColor(0.82f, 0.84f, 0.88f), false));
		Sub->RegisterSprite(this, true, true, TEXT("Soldier"));
	}
}

void APixelCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bSpin)
	{
		AddActorWorldRotation(FRotator(0, 70.f * DeltaSeconds, 0));
	}

	const UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(this);
	if (!Sub) return;
	const FPixelSettings& S = Sub->Settings;

	// Facing: the actor turns smoothly; the visible mesh only shows quantized yaws, like a sprite sheet.
	const float ActorYaw = GetActorRotation().Yaw;
	const float ShownYaw = S.bRotationQuantize ? FMath::RoundToFloat(ActorYaw / FacingStep) * FacingStep : ActorYaw;
	const FRotator MeshRot(0, MeshBaseYaw + (ShownYaw - ActorYaw), 0);

	// Position: snap the mesh root's world position onto the texel grid, keep the actor continuous.
	// On a screen diagonal, snap along the diagonal itself (both axes step on the same frame) instead of
	// rounding x and y separately, which steps them on alternating frames and zig-zags.
	FVector Offset = FVector::ZeroVector;
	if (S.bObjectSnap && Sub->Camera.bValid)
	{
		const FVector World = GetActorTransform().TransformPosition(MeshBaseLocation);
		const FVector R = Sub->Camera.Right, U = Sub->Camera.Up;
		const double T = Sub->Camera.Texel;
		const FVector2d Pt(FVector::DotProduct(World, R) / T, FVector::DotProduct(World, U) / T);
		const FVector2d V(FVector::DotProduct(GetVelocity(), R), FVector::DotProduct(GetVelocity(), U));
		// Hysteresis (step once 0.9 texel behind): at 30 texels/s and 60 fps the position then steps on every
		// 2nd frame exactly, instead of drifting into an occasional 3-frame gap that reads as a stutter.
		if (!bHasShown || FMath::Abs(Pt.X - ShownTexel.X) > 1.5 || FMath::Abs(Pt.Y - ShownTexel.Y) > 1.5)
		{
			ShownTexel = FVector2d(FMath::RoundToDouble(Pt.X), FMath::RoundToDouble(Pt.Y)); bHasShown = true;
		}
		if (FMath::Abs(Pt.X - ShownTexel.X) >= 0.9) ShownTexel.X = FMath::RoundToDouble(Pt.X);
		if (FMath::Abs(Pt.Y - ShownTexel.Y) >= 0.9) ShownTexel.Y = FMath::RoundToDouble(Pt.Y);
		FVector2d Snapped = ShownTexel;
		static const bool bOld = FParse::Param(FCommandLine::Get(), TEXT("PixelOldMove"));
		// Diagonal from the held keys, not the velocity: the velocity is still turning while the character
		// accelerates, and every frame spent rounding x and y separately is a frame of left-then-up.
		const bool bDiagonal = !bOld && ScreenMoveDir.X != 0 && ScreenMoveDir.Y != 0;
		if (bDiagonal)
		{
			const FIntPoint Dir = ScreenMoveDir;
			auto Along = [&](const FVector2d& A) { return ((Pt.X - A.X) * Dir.X + (Pt.Y - A.Y) * Dir.Y) * 0.5; };
			auto Across = [&](const FVector2d& A) { return ((Pt.X - A.X) * Dir.Y - (Pt.Y - A.Y) * Dir.X) * 0.5; };
			if (!bDiagonalAnchored || Dir != DiagonalDir || FMath::Abs(Across(DiagonalAnchor)) > 0.6)
			{
				DiagonalAnchor = Snapped; DiagonalDir = Dir; bDiagonalAnchored = true; DiagonalK = 0.0;
			}
			const double Al = Along(DiagonalAnchor);
			if (FMath::Abs(Al - DiagonalK) >= 0.9) DiagonalK = FMath::RoundToDouble(Al);
			Snapped = DiagonalAnchor + FVector2d(DiagonalK * Dir.X, DiagonalK * Dir.Y);
			ShownTexel = Snapped;
		}
		else
		{
			bDiagonalAnchored = false;
		}
		const FVector Target = World + R * ((Snapped.X - Pt.X) * T) + U * ((Snapped.Y - Pt.Y) * T);
		Offset = GetActorTransform().InverseTransformVector(Target - World);
	}
	GetMesh()->SetRelativeLocationAndRotation(MeshBaseLocation + Offset, MeshRot);
}
