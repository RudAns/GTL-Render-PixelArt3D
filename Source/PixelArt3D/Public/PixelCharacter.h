#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "PixelCharacter.generated.h"

class UStaticMeshComponent;

/**
 * Player soldier: KayKit knight with a spear on its back. Logical position stays continuous (movement,
 * collision); only the visible mesh is snapped to the texel grid and its yaw quantized (talk p.68, p.92).
 */
UCLASS()
class PIXELART3D_API APixelCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	APixelCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> SpearShaft;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> SpearTip;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BlobShadow;

	/** Yaw the visible mesh is allowed to show (degrees between allowed facings). */
	float FacingStep = 45.f;

	/** Set by the turntable exhibit: spin in place. */
	bool bSpin = false;

private:
	FVector MeshBaseLocation = FVector(0, 0, -80);
	float MeshBaseYaw = -90.f;
};
