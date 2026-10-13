#include "PixelCharacter.h"

#include "PixelAnimInstance.h"
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
	FVector Offset = FVector::ZeroVector;
	if (S.bObjectSnap && Sub->Camera.bValid)
	{
		const FVector World = GetActorTransform().TransformPosition(MeshBaseLocation);
		Offset = GetActorTransform().InverseTransformVector(Sub->SnapWorld(World) - World);
	}
	GetMesh()->SetRelativeLocationAndRotation(MeshBaseLocation + Offset, MeshRot);
}
