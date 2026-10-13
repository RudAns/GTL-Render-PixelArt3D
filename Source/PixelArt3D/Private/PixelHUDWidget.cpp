#include "PixelHUDWidget.h"

#include "IslandBuilder.h"
#include "PixelArtSubsystem.h"
#include "PixelPlayerController.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

namespace
{
	// Night-blue panels with a brass rim, parchment key caps: reads as a game HUD over a bright scene.
	const FLinearColor PanelFill(0.012f, 0.016f, 0.030f, 0.90f);
	const FLinearColor PanelRim(0.62f, 0.42f, 0.16f, 0.85f);
	const FLinearColor RowFill(0.030f, 0.038f, 0.060f, 0.95f);
	const FLinearColor RowHover(0.060f, 0.075f, 0.110f, 1.f);
	const FLinearColor Ink(0.93f, 0.92f, 0.88f);
	const FLinearColor Dim(0.55f, 0.57f, 0.62f);
	const FLinearColor Gold(1.00f, 0.66f, 0.22f);
	const FLinearColor CapFill(0.86f, 0.80f, 0.68f);
	const FLinearColor CapInk(0.035f, 0.030f, 0.025f);
	const FLinearColor OnFill(0.13f, 0.52f, 0.20f);
	const FLinearColor OffFill(0.10f, 0.11f, 0.13f);
	const FLinearColor ValueFill(0.45f, 0.27f, 0.06f);

	FLinearColor StationColor(int32 Index)
	{
		static const FLinearColor C[] = { FLinearColor(0.6f, 0.6f, 0.6f), FLinearColor(0.85f, 0.75f, 0.35f), FLinearColor(0.35f, 0.75f, 0.85f),
			FLinearColor(0.85f, 0.45f, 0.35f), FLinearColor(0.55f, 0.85f, 0.45f), FLinearColor(0.75f, 0.5f, 0.85f), FLinearColor(0.9f, 0.9f, 0.9f) };
		return C[FMath::Clamp(Index, 0, 6)];
	}

	FSlateBrush Rounded(FLinearColor Fill, float Radius, FLinearColor Outline = FLinearColor::Transparent, float Width = 0.f)
	{
		FSlateRoundedBoxBrush B(Fill, Radius);
		B.OutlineSettings = FSlateBrushOutlineSettings(FVector4(Radius, Radius, Radius, Radius), FSlateColor(Outline), Width);
		return B;
	}

	/** "名称　值" (full-width space) -> name, value */
	void SplitLabel(const FString& In, FString& Name, FString& Value)
	{
		if (!In.Split(TEXT("　"), &Name, &Value)) { Name = In; Value.Reset(); }
	}
}

void UPixelRowHandler::OnClicked()
{
	APixelPlayerController* PC = Owner.Get();
	if (!PC) return;
	switch (Kind)
	{
	case 0: PC->ActivateToggle(Index); break;
	case 1: PC->AdjustParam(Index, -1); break;
	case 2: PC->AdjustParam(Index, +1); break;
	case 3: PC->AnswerChallenge(Index); break;
	case 4: PC->bParamPanel = Index == 1; break;
	}
}

UTextBlock* UPixelHUDWidget::MakeText(int32 Size, FLinearColor Color, bool bBold)
{
	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
	T->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size));
	T->SetColorAndOpacity(FSlateColor(Color));
	T->SetShadowOffset(FVector2D(1.f, 1.f));
	T->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	return T;
}

UBorder* UPixelHUDWidget::MakePanel(FLinearColor Fill, FLinearColor Outline, float Radius, FMargin InPadding)
{
	UBorder* B = WidgetTree->ConstructWidget<UBorder>();
	B->SetBrush(Rounded(Fill, Radius, Outline, Outline.A > 0.f ? 1.5f : 0.f));
	B->SetPadding(InPadding);
	return B;
}

UBorder* UPixelHUDWidget::MakeKeyCap(const FString& Key, int32 Size)
{
	UBorder* Cap = MakePanel(CapFill, FLinearColor(0.35f, 0.28f, 0.18f, 1.f), 4.f, FMargin(7, 2, 7, 3));
	UTextBlock* T = MakeText(Size, CapInk, true);
	T->SetShadowColorAndOpacity(FLinearColor::Transparent);
	T->SetText(FText::FromString(Key));
	T->SetJustification(ETextJustify::Center);
	Cap->SetContent(T);
	Cap->SetHorizontalAlignment(HAlign_Center);
	Cap->SetVerticalAlignment(VAlign_Center);
	return Cap;
}

UButton* UPixelHUDWidget::MakeButton(UPixelRowHandler*& OutHandler, int32 Kind, int32 Index)
{
	UButton* B = WidgetTree->ConstructWidget<UButton>();
	FButtonStyle Style;
	Style.SetNormal(Rounded(RowFill, 5.f));
	Style.SetHovered(Rounded(RowHover, 5.f, PanelRim, 1.f));
	Style.SetPressed(Rounded(RowHover, 5.f, Gold, 1.f));
	Style.SetNormalPadding(FMargin(0)); Style.SetPressedPadding(FMargin(0));
	B->SetStyle(Style);
	OutHandler = NewObject<UPixelRowHandler>(this);
	OutHandler->Kind = Kind; OutHandler->Index = Index; OutHandler->Owner = Cast<APixelPlayerController>(GetOwningPlayer());
	B->OnClicked.AddDynamic(OutHandler, &UPixelRowHandler::OnClicked);
	Handlers.Add(OutHandler);
	return B;
}

UHorizontalBox* UPixelHUDWidget::MakeHintBar()
{
	UHorizontalBox* Bar = WidgetTree->ConstructWidget<UHorizontalBox>();
	struct FHint { TArray<FString> Keys; const TCHAR* Label; };
	const FHint Hints[] = {
		{ { TEXT("W"), TEXT("A"), TEXT("S"), TEXT("D") }, TEXT("移动") },
		{ { TEXT("Q"), TEXT("R") }, TEXT("旋转视角") },
		{ { TEXT("Tab") }, TEXT("开关 / 参数") },
		{ { TEXT("F1") }, TEXT("调试视图") },
		{ { TEXT("M") }, TEXT("闪烁率") },
		{ { TEXT("C") }, TEXT("找穿帮") },
		{ { TEXT("Home") }, TEXT("复位") },
		{ { TEXT("F3") }, TEXT("隐藏界面") },
	};
	for (const FHint& H : Hints)
	{
		for (const FString& K : H.Keys)
		{
			Bar->AddChildToHorizontalBox(MakeKeyCap(K))->SetPadding(FMargin(0, 0, 3, 0));
		}
		UTextBlock* L = MakeText(13, Ink);
		L->SetText(FText::FromString(H.Label));
		UHorizontalBoxSlot* S = Bar->AddChildToHorizontalBox(L);
		S->SetPadding(FMargin(4, 0, 18, 0));
		S->SetVerticalAlignment(VAlign_Center);
	}
	return Bar;
}

TSharedRef<SWidget> UPixelHUDWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Root;
		APixelPlayerController* PC = Cast<APixelPlayerController>(GetOwningPlayer());

		auto Place = [Root](UWidget* W, FAnchors Anchors, FVector2D Align, FVector2D Pos)
		{
			UCanvasPanelSlot* S = Root->AddChildToCanvas(W);
			S->SetAnchors(Anchors); S->SetAlignment(Align); S->SetPosition(Pos); S->SetAutoSize(true);
		};

		// --- top left: title card ------------------------------------------------------------
		TitleCard = MakePanel(PanelFill, PanelRim, 8.f, FMargin(14, 8, 16, 9));
		{
			UVerticalBox* V = WidgetTree->ConstructWidget<UVerticalBox>();
			TitleCard->SetContent(V);
			UTextBlock* T = MakeText(18, Gold, true);
			T->SetText(FText::FromString(TEXT("像素工坊")));
			V->AddChildToVerticalBox(T);
			TitleSub = MakeText(12, Dim);
			TitleSub->SetText(FText::FromString(TEXT("3D 像素画渲染 · 交互拆解")));
			V->AddChildToVerticalBox(TitleSub);
		}
		Place(TitleCard, FAnchors(0, 0), FVector2D(0, 0), FVector2D(18, 16));

		// --- left: tabbed pipeline panel -----------------------------------------------------
		LeftPanel = MakePanel(PanelFill, PanelRim, 8.f, FMargin(10, 10, 10, 10));
		UVerticalBox* Left = WidgetTree->ConstructWidget<UVerticalBox>();
		LeftPanel->SetContent(Left);
		{
			UHorizontalBox* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>();
			Left->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0, 0, 0, 8));
			const TCHAR* TabNames[] = { TEXT("管线开关"), TEXT("参数") };
			for (int32 I = 0; I < 2; ++I)
			{
				UPixelRowHandler* H = nullptr;
				UButton* B = MakeButton(H, 4, I);
				UBorder* Back = MakePanel(FLinearColor::Transparent, FLinearColor::Transparent, 5.f, FMargin(12, 4));
				UTextBlock* T = MakeText(14, Ink, true);
				T->SetText(FText::FromString(TabNames[I]));
				Back->SetContent(T);
				B->AddChild(Back);
				TabTexts.Add(T); TabBacks.Add(Back);
				Tabs->AddChildToHorizontalBox(B)->SetPadding(FMargin(0, 0, 6, 0));
			}
			UBorder* TabKey = MakeKeyCap(TEXT("Tab"), 11);
			UHorizontalBoxSlot* KS = Tabs->AddChildToHorizontalBox(TabKey);
			KS->SetVerticalAlignment(VAlign_Center);
			KS->SetPadding(FMargin(4, 0, 0, 0));
		}

		ToggleBox = WidgetTree->ConstructWidget<UVerticalBox>();
		Left->AddChildToVerticalBox(ToggleBox);
		if (PC)
		{
			for (int32 I = 0; I < PC->Toggles.Num(); ++I)
			{
				UPixelRowHandler* H = nullptr;
				UButton* B = MakeButton(H, 0, I);
				UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
				B->AddChild(Row);
				if (UButtonSlot* BS = Cast<UButtonSlot>(B->GetContentSlot())) { BS->SetPadding(FMargin(5, 3)); BS->SetHorizontalAlignment(HAlign_Fill); }

				USizeBox* CapBox = WidgetTree->ConstructWidget<USizeBox>();
				CapBox->SetMinDesiredWidth(34.f);
				CapBox->AddChild(MakeKeyCap(PC->Toggles[I].Key, 11));
				Row->AddChildToHorizontalBox(CapBox)->SetVerticalAlignment(VAlign_Center);

				UTextBlock* L = MakeText(13, Ink);
				L->SetText(FText::FromString(PC->Toggles[I].Label));
				UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(L);
				LS->SetPadding(FMargin(8, 0, 12, 0)); LS->SetVerticalAlignment(VAlign_Center);
				LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

				UBorder* Pill = MakePanel(OffFill, FLinearColor::Transparent, 9.f, FMargin(8, 1, 8, 2));
				UTextBlock* V = MakeText(12, Ink, true);
				Pill->SetContent(V);
				UHorizontalBoxSlot* PS = Row->AddChildToHorizontalBox(Pill);
				PS->SetVerticalAlignment(VAlign_Center); PS->SetHorizontalAlignment(HAlign_Right);

				ToggleLabels.Add(L); ToggleValues.Add(V); TogglePills.Add(Pill);
				ToggleBox->AddChildToVerticalBox(B)->SetPadding(FMargin(0, 1));
			}
		}

		ParamBox = WidgetTree->ConstructWidget<UVerticalBox>();
		Left->AddChildToVerticalBox(ParamBox);
		{
			const TCHAR* ParamKeys[] = { TEXT("Z X"), TEXT(", ."), TEXT(""), TEXT(""), TEXT("[ ]"), TEXT("Q R"), TEXT("") };
			for (int32 I = 0; I < APixelPlayerController::NumParams; ++I)
			{
				UBorder* RowBack = MakePanel(RowFill, FLinearColor::Transparent, 5.f, FMargin(8, 4, 4, 4));
				UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
				RowBack->SetContent(Row);
				UVerticalBox* Names = WidgetTree->ConstructWidget<UVerticalBox>();
				UTextBlock* N = MakeText(12, Dim);
				UTextBlock* V = MakeText(14, Ink, true);
				Names->AddChildToVerticalBox(N);
				Names->AddChildToVerticalBox(V);
				UHorizontalBoxSlot* NS = Row->AddChildToHorizontalBox(Names);
				NS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); NS->SetPadding(FMargin(0, 0, 10, 0));
				if (ParamKeys[I][0])
				{
					UTextBlock* K = MakeText(11, Dim);
					K->SetText(FText::FromString(ParamKeys[I]));
					UHorizontalBoxSlot* KS = Row->AddChildToHorizontalBox(K);
					KS->SetVerticalAlignment(VAlign_Center); KS->SetPadding(FMargin(0, 0, 8, 0));
				}
				for (int32 Side = 0; Side < 2; ++Side)
				{
					UPixelRowHandler* H = nullptr;
					UButton* B = MakeButton(H, Side == 0 ? 1 : 2, I);
					B->SetStyle([&] { FButtonStyle St = B->GetStyle(); St.SetNormal(Rounded(FLinearColor(0.08f, 0.10f, 0.15f, 1.f), 5.f, PanelRim, 1.f)); return St; }());
					UTextBlock* Sign = MakeText(15, Gold, true);
					Sign->SetText(FText::FromString(Side == 0 ? TEXT("−") : TEXT("+")));
					B->AddChild(Sign);
					if (UButtonSlot* BS = Cast<UButtonSlot>(B->GetContentSlot())) BS->SetPadding(FMargin(10, 2));
					UHorizontalBoxSlot* BS2 = Row->AddChildToHorizontalBox(B);
					BS2->SetVerticalAlignment(VAlign_Center); BS2->SetPadding(FMargin(2, 0));
				}
				ParamNames.Add(N); ParamValues.Add(V);
				ParamBox->AddChildToVerticalBox(RowBack)->SetPadding(FMargin(0, 2));
			}
		}
		{
			USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
			Width->SetWidthOverride(340.f);
			Width->AddChild(LeftPanel);
			Place(Width, FAnchors(0, 0), FVector2D(0, 0), FVector2D(18, 84));
		}

		// --- right: readout card ---------------------------------------------------------------
		RightPanel = MakePanel(PanelFill, PanelRim, 8.f, FMargin(14, 10, 14, 12));
		{
			UVerticalBox* V = WidgetTree->ConstructWidget<UVerticalBox>();
			RightPanel->SetContent(V);
			auto Stat = [this, V](const TCHAR* Caption, int32 Size) -> UTextBlock*
			{
				UTextBlock* C = MakeText(11, Dim);
				C->SetText(FText::FromString(Caption));
				V->AddChildToVerticalBox(C)->SetPadding(FMargin(0, 6, 0, 0));
				UTextBlock* T = MakeText(Size, Ink, true);
				V->AddChildToVerticalBox(T);
				return T;
			};
			ResValue = Stat(TEXT("内部分辨率"), 20);
			ResNote = MakeText(11, Dim);
			V->AddChildToVerticalBox(ResNote);
			FlickerValue = Stat(TEXT("帧间闪烁率"), 20);
			SnapValue = Stat(TEXT("相机吸附余量"), 14);
			SortValue = Stat(TEXT("精灵排序"), 14);
			ViewValue = MakeText(13, Gold, true);
			V->AddChildToVerticalBox(ViewValue)->SetPadding(FMargin(0, 8, 0, 0));
		}
		{
			USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
			Width->SetWidthOverride(300.f);
			Width->AddChild(RightPanel);
			Place(Width, FAnchors(1, 0), FVector2D(1, 0), FVector2D(-18, 16));
		}

		// --- bottom: station dialogue -------------------------------------------------------
		DialoguePanel = MakePanel(PanelFill, PanelRim, 10.f, FMargin(16, 12, 18, 14));
		{
			UHorizontalBox* H = WidgetTree->ConstructWidget<UHorizontalBox>();
			DialoguePanel->SetContent(H);
			StationBadge = MakePanel(StationColor(0), FLinearColor(0, 0, 0, 0.6f), 22.f, FMargin(0));
			StationNumber = MakeText(20, FLinearColor(0.02f, 0.02f, 0.03f), true);
			StationNumber->SetShadowColorAndOpacity(FLinearColor::Transparent);
			StationNumber->SetJustification(ETextJustify::Center);
			StationBadge->SetContent(StationNumber);
			StationBadge->SetHorizontalAlignment(HAlign_Center); StationBadge->SetVerticalAlignment(VAlign_Center);
			USizeBox* BadgeBox = WidgetTree->ConstructWidget<USizeBox>();
			BadgeBox->SetWidthOverride(44.f); BadgeBox->SetHeightOverride(44.f);
			BadgeBox->AddChild(StationBadge);
			UHorizontalBoxSlot* BS = H->AddChildToHorizontalBox(BadgeBox);
			BS->SetVerticalAlignment(VAlign_Top); BS->SetPadding(FMargin(0, 2, 14, 0));

			UVerticalBox* V = WidgetTree->ConstructWidget<UVerticalBox>();
			H->AddChildToHorizontalBox(V);
			StationTitle = MakeText(17, Gold, true);
			StationBody = MakeText(14, Ink);
			StationBody->SetAutoWrapText(false);
			StationBody->SetWrapTextAt(820.f);
			StationBody->SetLineHeightPercentage(1.15f);
			V->AddChildToVerticalBox(StationTitle)->SetPadding(FMargin(0, 0, 0, 6));
			V->AddChildToVerticalBox(StationBody);
		}
		Place(DialoguePanel, FAnchors(0.5f, 1), FVector2D(0.5f, 1), FVector2D(0, -62));

		// --- bottom: key hints ---------------------------------------------------------------
		UBorder* HintBack = MakePanel(FLinearColor(0.f, 0.f, 0.f, 0.55f), FLinearColor::Transparent, 8.f, FMargin(14, 6, 0, 6));
		HintBar = MakeHintBar();
		HintBack->SetContent(HintBar);
		Place(HintBack, FAnchors(0.5f, 1), FVector2D(0.5f, 1), FVector2D(0, -12));

		// --- top centre: challenge ---------------------------------------------------------------
		ChallengePanel = MakePanel(PanelFill, Gold, 10.f, FMargin(16, 12, 16, 12));
		{
			UVerticalBox* Ch = WidgetTree->ConstructWidget<UVerticalBox>();
			ChallengePanel->SetContent(Ch);
			UTextBlock* Head = MakeText(17, Gold, true);
			Head->SetText(FText::FromString(TEXT("找穿帮：哪一项技术被关掉了？")));
			Ch->AddChildToVerticalBox(Head)->SetPadding(FMargin(0, 0, 0, 6));
			ChallengeText = MakeText(14, Ink);
			ChallengeText->SetWrapTextAt(600.f);
			Ch->AddChildToVerticalBox(ChallengeText)->SetPadding(FMargin(0, 0, 0, 8));
			if (PC)
			{
				for (int32 I = 0; I < PC->ChallengeItems.Num(); ++I)
				{
					UPixelRowHandler* H = nullptr;
					UButton* B = MakeButton(H, 3, I);
					UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
					B->AddChild(Row);
					if (UButtonSlot* BS = Cast<UButtonSlot>(B->GetContentSlot())) { BS->SetPadding(FMargin(6, 3)); BS->SetHorizontalAlignment(HAlign_Left); }
					Row->AddChildToHorizontalBox(MakeKeyCap(FString::FromInt((I + 1) % 10), 11))->SetVerticalAlignment(VAlign_Center);
					UTextBlock* T = MakeText(13, Ink);
					T->SetText(FText::FromString(PC->ChallengeItems[I].Name));
					UHorizontalBoxSlot* TS = Row->AddChildToHorizontalBox(T);
					TS->SetPadding(FMargin(8, 0, 0, 0)); TS->SetVerticalAlignment(VAlign_Center);
					ChallengeTexts.Add(T);
					Ch->AddChildToVerticalBox(B)->SetPadding(FMargin(0, 1));
				}
			}
		}
		Place(ChallengePanel, FAnchors(0.5f, 0), FVector2D(0.5f, 0), FVector2D(0, 16));
	}
	return Super::RebuildWidget();
}

void UPixelHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	APixelPlayerController* PC = Cast<APixelPlayerController>(GetOwningPlayer());
	UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(PC);
	if (!PC || !Sub) return;
	const FPixelSettings& S = Sub->Settings;

	const ESlateVisibility Vis = PC->bUiVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	const ESlateVisibility Click = PC->bUiVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	TitleCard->SetVisibility(Vis);
	LeftPanel->SetVisibility(Click);
	RightPanel->SetVisibility(Vis);
	HintBar->GetParent()->SetVisibility(Vis);

	// tabs
	for (int32 I = 0; I < TabBacks.Num(); ++I)
	{
		const bool bActive = (I == 1) == PC->bParamPanel;
		TabBacks[I]->SetBrush(Rounded(bActive ? ValueFill : FLinearColor::Transparent, 5.f));
		TabTexts[I]->SetColorAndOpacity(FSlateColor(bActive ? Ink : Dim));
	}
	ToggleBox->SetVisibility(PC->bParamPanel ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	ParamBox->SetVisibility(PC->bParamPanel ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	for (int32 I = 0; I < ToggleValues.Num() && I < PC->Toggles.Num(); ++I)
	{
		const FPixelToggle& T = PC->Toggles[I];
		const bool bHidden = PC->bChallenge && I < 10;
		const FString Value = bHidden ? FString(TEXT("？")) : T.Value(S);
		ToggleValues[I]->SetText(FText::FromString(Value));
		const FLinearColor Fill = bHidden ? OffFill : Value == TEXT("开") ? OnFill : Value == TEXT("关") ? OffFill : ValueFill;
		TogglePills[I]->SetBrush(Rounded(Fill, 9.f));
		ToggleValues[I]->SetColorAndOpacity(FSlateColor(Value == TEXT("关") ? Dim : Ink));
	}
	for (int32 I = 0; I < ParamNames.Num(); ++I)
	{
		FString Name, Value;
		SplitLabel(PC->ParamLabel(I), Name, Value);
		ParamNames[I]->SetText(FText::FromString(Name));
		ParamValues[I]->SetText(FText::FromString(Value));
	}

	// readout
	const FPixelArtLayout L = PixelArtRender::GetAppliedLayout();
	const FPixelSortStats& St = Sub->GetSortStats();
	if (S.bLowRes && L.bValid)
	{
		ResValue->SetText(FText::FromString(FString::Printf(TEXT("%d × %d"), L.Inner.X, L.Inner.Y)));
		ResNote->SetText(FText::FromString(FString::Printf(TEXT("×%d %s放大 · 1 texel = %.2f cm"), L.Scale, S.bBilinear ? TEXT("双线性") : TEXT("最近邻"), Sub->Camera.Texel)));
	}
	else
	{
		ResValue->SetText(FText::FromString(TEXT("原生")));
		ResNote->SetText(FText::FromString(TEXT("低分辨率渲染已关闭")));
	}
	if (Sub->FlickerPercent >= 0.f)
	{
		FlickerValue->SetText(FText::FromString(FString::Printf(TEXT("%.2f %%"), Sub->FlickerPercent)));
		FlickerValue->SetColorAndOpacity(FSlateColor(Sub->FlickerPercent < 0.2f ? FLinearColor(0.45f, 0.95f, 0.5f) : FLinearColor(1.f, 0.4f, 0.3f)));
	}
	else
	{
		FlickerValue->SetText(FText::FromString(TEXT("按 M 测量")));
		FlickerValue->SetColorAndOpacity(FSlateColor(Dim));
	}
	SnapValue->SetText(FText::FromString(S.bCameraSnap
		? FString::Printf(TEXT("(%+.2f, %+.2f) texel%s"), Sub->Camera.Remainder.X, Sub->Camera.Remainder.Y, S.bSubTexel ? TEXT(" · 已补偿") : TEXT(""))
		: FString(TEXT("关"))));
	SortValue->SetText(FText::FromString(FString::Printf(TEXT("%d 个精灵 · %d 层 · %.2f ms\n重叠 %d · 环 %d · 切边 %d · 合并 %d"), St.Sprites, St.Levels, St.Ms, St.Pairs, St.Cycles, St.Cuts, St.Merged)));
	ViewValue->SetVisibility(S.ViewMode != EPixelViewMode::Final ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	ViewValue->SetText(FText::FromString(FString::Printf(TEXT("调试视图：%s"), PixelText::ViewModeName(S.ViewMode))));

	// dialogue
	const FPixelStation* Station = PC->ActiveStation();
	if (PC->bChallenge || !PC->bUiVisible)
	{
		DialoguePanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		DialoguePanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		const int32 Index = Station ? Station->Index : 0;
		StationBadge->SetBrush(Rounded(StationColor(Index), 22.f, FLinearColor(0, 0, 0, 0.6f), 2.f));
		StationNumber->SetText(FText::FromString(Station ? FString::FromInt(Index) : FString(TEXT("?"))));
		if (Station)
		{
			// titles carry a circled number already; the badge shows it, so drop it from the text
			FString Title = Station->Title;
			if (Title.Len() > 2 && Title[1] == TEXT(' ')) Title.RightChopInline(2);
			StationTitle->SetText(FText::FromString(Title));
			StationBody->SetText(FText::FromString(Station->Body));
		}
		else
		{
			StationTitle->SetText(FText::FromString(TEXT("欢迎来到像素工坊")));
			StationBody->SetText(FText::FromString(TEXT("岛上 6 个彩色踏板各拆解一项技术，沿石板路走上去就会出现说明。\n① 分辨率与 texel 密度　② 平移跑道　③ 墓室与长矛　④ 三杆成环　⑤ 描边与抖动　⑥ 转台\n想考考自己？按 C 开始「找穿帮」挑战。")));
		}
	}

	ChallengePanel->SetVisibility(PC->bChallenge ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (PC->bChallenge) ChallengeText->SetText(FText::FromString(PC->ChallengeFeedback));
}
