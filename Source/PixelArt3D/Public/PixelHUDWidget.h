#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PixelHUDWidget.generated.h"

class UTextBlock;
class UVerticalBox;
class UHorizontalBox;
class UBorder;
class UButton;
class UPanelWidget;
class APixelPlayerController;

/** Click target for one panel row; forwards to the controller. */
UCLASS()
class UPixelRowHandler : public UObject
{
	GENERATED_BODY()
public:
	int32 Kind = 0;   // 0 toggle, 1 param minus, 2 param plus, 3 challenge option, 4 panel tab
	int32 Index = 0;
	TWeakObjectPtr<APixelPlayerController> Owner;
	UFUNCTION() void OnClicked();
};

/**
 * Whole on-screen UI, built in code and styled like a game HUD: a tabbed pipeline panel (switches /
 * parameters) on the left, a readout card on the right, an RPG-style dialogue box for the station you
 * stand on, and a key-cap hint bar along the bottom.
 */
UCLASS()
class PIXELART3D_API UPixelHUDWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UTextBlock* MakeText(int32 Size, FLinearColor Color, bool bBold = false);
	UBorder* MakePanel(FLinearColor Fill, FLinearColor Outline, float Radius, FMargin Padding);
	UBorder* MakeKeyCap(const FString& Key, int32 Size = 12);
	UButton* MakeButton(UPixelRowHandler*& OutHandler, int32 Kind, int32 Index);
	UHorizontalBox* MakeHintBar();

	UPROPERTY() TArray<TObjectPtr<UPixelRowHandler>> Handlers;

	// pipeline panel
	UPROPERTY() TObjectPtr<UBorder> LeftPanel;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> TabTexts;
	UPROPERTY() TArray<TObjectPtr<UBorder>> TabBacks;
	UPROPERTY() TObjectPtr<UVerticalBox> ToggleBox;
	UPROPERTY() TObjectPtr<UVerticalBox> ParamBox;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> ToggleLabels;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> ToggleValues;
	UPROPERTY() TArray<TObjectPtr<UBorder>> TogglePills;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> ParamNames;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> ParamValues;

	// readout
	UPROPERTY() TObjectPtr<UBorder> RightPanel;
	UPROPERTY() TObjectPtr<UTextBlock> ResValue;
	UPROPERTY() TObjectPtr<UTextBlock> ResNote;
	UPROPERTY() TObjectPtr<UTextBlock> FlickerValue;
	UPROPERTY() TObjectPtr<UTextBlock> SortValue;
	UPROPERTY() TObjectPtr<UTextBlock> SnapValue;
	UPROPERTY() TObjectPtr<UTextBlock> WobbleValue;
	UPROPERTY() TObjectPtr<UTextBlock> ViewValue;

	// station dialogue
	UPROPERTY() TObjectPtr<UBorder> DialoguePanel;
	UPROPERTY() TObjectPtr<UBorder> StationBadge;
	UPROPERTY() TObjectPtr<UTextBlock> StationNumber;
	UPROPERTY() TObjectPtr<UTextBlock> StationTitle;
	UPROPERTY() TObjectPtr<UTextBlock> StationBody;

	// top: title + challenge
	UPROPERTY() TObjectPtr<UBorder> TitleCard;
	UPROPERTY() TObjectPtr<UTextBlock> TitleSub;
	UPROPERTY() TObjectPtr<UBorder> ChallengePanel;
	UPROPERTY() TObjectPtr<UTextBlock> ChallengeText;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> ChallengeTexts;

	UPROPERTY() TObjectPtr<UHorizontalBox> HintBar;
};
