#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "PixelTypes.h"
#include "PixelPlayerController.generated.h"

class UPixelHUDWidget;
class AIslandBuilder;
struct FPixelStation;

/** One switch in the left panel (also reachable by hotkey). */
struct FPixelToggle
{
	FString Key;
	FString Label;
	TFunction<FString(const FPixelSettings&)> Value;
	TFunction<void(FPixelSettings&)> Act;
};

/** One technique the "spot the artifact" challenge can secretly switch off. */
struct FPixelChallengeItem
{
	FString Name;
	FString Explain;
	TFunction<void(FPixelSettings&)> Break;
};

UCLASS()
class PIXELART3D_API APixelPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	APixelPlayerController();

	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

	TArray<FPixelToggle> Toggles;
	TArray<FPixelChallengeItem> ChallengeItems;
	void ActivateToggle(int32 Index);
	void AdjustParam(int32 Param, int32 Dir);
	FString ParamLabel(int32 Param) const;
	static constexpr int32 NumParams = 7;

	float WalkTestTime = -1.f;
	bool bMotionTest = false;
	FVector WalkTestStart = FVector::ZeroVector;
	void TickWalkTest(float DeltaTime);

	const FPixelStation* ActiveStation() const;

	// challenge state
	bool bChallenge = false;
	int32 ChallengeAnswer = INDEX_NONE;
	int32 ChallengeRound = 0;
	int32 ChallengeScore = 0;
	FString ChallengeFeedback;
	void StartChallengeRound();
	void AnswerChallenge(int32 Option);
	void EndChallenge();

	bool bUiVisible = true;
	bool bParamPanel = false;

private:
	void BuildToggles();
	void HandleKeys();
	void UpdateStations(float DeltaTime);

	UPROPERTY() TObjectPtr<UPixelHUDWidget> Hud;
	UPROPERTY() TObjectPtr<AIslandBuilder> Island;
	int32 StationIndex = INDEX_NONE;
	int32 BeamPush = 0;
	FPixelSettings BeforeChallenge;
	bool bAutoPanFromStation = false;

	// Automated capture (-PixelCapture=<dir>): walks through scripted states and saves screenshots.
	void TickCapture(float DeltaTime);
	FString CaptureDir;
	int32 CaptureStep = -1;
	float CaptureTimer = 0.f;
	int32 CaptureFrames = 0;
	bool bCaptureShotPending = false;
};
