#include "PixelPlayerController.h"

#include "IslandBuilder.h"
#include "PixelArtSubsystem.h"
#include "PixelCameraManager.h"
#include "PixelCharacter.h"
#include "PixelHUDWidget.h"

#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "TextureResource.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
	FString OnOff(bool b) { return b ? TEXT("开") : TEXT("关"); }
	template <typename E> E Next(E V) { return E((uint8(V) + 1) % uint8(E::Count)); }
}

APixelPlayerController::APixelPlayerController()
{
	PlayerCameraManagerClass = APixelCameraManager::StaticClass();
	bShowMouseCursor = true;
	BuildToggles();
}

void APixelPlayerController::BuildToggles()
{
	auto Add = [this](const TCHAR* Key, const TCHAR* Label, TFunction<FString(const FPixelSettings&)> V, TFunction<void(FPixelSettings&)> A)
	{
		Toggles.Add({ Key, Label, MoveTemp(V), MoveTemp(A) });
	};
	Add(TEXT("1"), TEXT("低分辨率渲染 + 整数倍放大"), [](const FPixelSettings& S) { return OnOff(S.bLowRes); }, [](FPixelSettings& S) { S.bLowRes = !S.bLowRes; });
	Add(TEXT("2"), TEXT("相机吸附到像素网格"), [](const FPixelSettings& S) { return OnOff(S.bCameraSnap); }, [](FPixelSettings& S) { S.bCameraSnap = !S.bCameraSnap; });
	Add(TEXT("3"), TEXT("亚像素补偿（放大时平移）"), [](const FPixelSettings& S) { return OnOff(S.bSubTexel); }, [](FPixelSettings& S) { S.bSubTexel = !S.bSubTexel; });
	Add(TEXT("4"), TEXT("物体与骨骼吸附"), [](const FPixelSettings& S) { return OnOff(S.bObjectSnap); }, [](FPixelSettings& S) { S.bObjectSnap = !S.bObjectSnap; S.bBoneSnap = S.bObjectSnap; });
	Add(TEXT("5"), TEXT("朝向量化 + 阶跃关键帧"), [](const FPixelSettings& S) { return OnOff(S.bRotationQuantize); }, [](FPixelSettings& S) { S.bRotationQuantize = !S.bRotationQuantize; S.bSteppedAnim = S.bRotationQuantize; });
	Add(TEXT("6"), TEXT("排序模式"), [](const FPixelSettings& S) { return FString(PixelText::SortModeName(S.SortMode)); }, [](FPixelSettings& S) { S.SortMode = Next(S.SortMode); });
	Add(TEXT("7"), TEXT("切环策略"), [](const FPixelSettings& S) { return FString(PixelText::CycleModeName(S.CycleMode)); }, [](FPixelSettings& S) { S.CycleMode = Next(S.CycleMode); });
	Add(TEXT("8"), TEXT("光照计算位置"), [](const FPixelSettings& S) { return FString(S.bTrueLightPosition ? TEXT("真实位置") : TEXT("推移后（UE 默认行为）")); }, [](FPixelSettings& S) { S.bTrueLightPosition = !S.bTrueLightPosition; });
	Add(TEXT("9"), TEXT("描边归属"), [](const FPixelSettings& S) { return FString(PixelText::OutlineModeName(S.OutlineMode)); }, [](FPixelSettings& S) { S.OutlineMode = Next(S.OutlineMode); });
	Add(TEXT("0"), TEXT("描边查表清理"), [](const FPixelSettings& S) { return OnOff(S.bLutCleanup); }, [](FPixelSettings& S) { S.bLutCleanup = !S.bLutCleanup; });
	Add(TEXT("P"), TEXT("抖动"), [](const FPixelSettings& S) { return FString(PixelText::DitherModeName(S.DitherMode)); }, [](FPixelSettings& S) { S.DitherMode = Next(S.DitherMode); });
	Add(TEXT("T"), TEXT("toon 色阶数"), [](const FPixelSettings& S) { return S.ToonSteps > 0 ? FString::FromInt(S.ToonSteps) : FString(TEXT("平滑")); }, [](FPixelSettings& S) { S.ToonSteps = S.ToonSteps == 0 ? 2 : (S.ToonSteps >= 4 ? 0 : S.ToonSteps + 1); });
	Add(TEXT("H"), TEXT("自建阴影图"), [](const FPixelSettings& S) { return OnOff(S.bShadows); }, [](FPixelSettings& S) { S.bShadows = !S.bShadows; });
	Add(TEXT("B"), TEXT("放大方式"), [](const FPixelSettings& S) { return FString(S.bBilinear ? TEXT("双线性") : TEXT("最近邻")); }, [](FPixelSettings& S) { S.bBilinear = !S.bBilinear; });
	Add(TEXT("G"), TEXT("显示像素格"), [](const FPixelSettings& S) { return OnOff(S.bShowGrid); }, [](FPixelSettings& S) { S.bShowGrid = !S.bShowGrid; });
	Add(TEXT("F1"), TEXT("调试视图"), [](const FPixelSettings& S) { return FString(PixelText::ViewModeName(S.ViewMode)); }, [](FPixelSettings& S) { S.ViewMode = Next(S.ViewMode); });
	Add(TEXT("V"), TEXT("前后关系图"), [](const FPixelSettings& S) { return OnOff(S.bShowGraph); }, [](FPixelSettings& S) { S.bShowGraph = !S.bShowGraph; });
	Add(TEXT("J"), TEXT("相机跟随"), [](const FPixelSettings& S) { return FString(PixelText::FollowModeName(S.FollowMode)); }, [](FPixelSettings& S) { S.FollowMode = Next(S.FollowMode); });
	Add(TEXT("F2"), TEXT("排序体积线框"), [](const FPixelSettings& S) { return OnOff(S.bShowSortVolumes); }, [](FPixelSettings& S) { S.bShowSortVolumes = !S.bShowSortVolumes; });

	auto Ch = [this](const TCHAR* Name, const TCHAR* Explain, TFunction<void(FPixelSettings&)> Break)
	{
		ChallengeItems.Add({ Name, Explain, MoveTemp(Break) });
	};
	Ch(TEXT("低分辨率渲染"), TEXT("关掉后画面回到原生分辨率，贴图 texel 不再与屏幕像素一一对应，读起来就是普通 3D。"), [](FPixelSettings& S) { S.bLowRes = false; });
	Ch(TEXT("相机吸附"), TEXT("相机每帧挪亚像素距离，斜边附近的像素覆盖率在 50% 阈值两侧来回变化，边缘交替明灭（像素爬行）。"), [](FPixelSettings& S) { S.bCameraSnap = false; S.bAutoPan = true; });
	Ch(TEXT("物体与骨骼吸附"), TEXT("角色不在像素网格上，同一个模型在屏幕不同位置的轮廓不一样，走路时轮廓抖动。"), [](FPixelSettings& S) { S.bObjectSnap = false; S.bBoneSnap = false; });
	Ch(TEXT("朝向量化与阶跃帧"), TEXT("朝向和动作变成连续插值，读起来像「掉帧的 3D」而不是精灵图。"), [](FPixelSettings& S) { S.bRotationQuantize = false; S.bSteppedAnim = false; });
	Ch(TEXT("精灵排序"), TEXT("回到逐像素深度：长矛插进墙里被截断，物体互相穿插。"), [](FPixelSettings& S) { S.SortMode = EPixelSortMode::Off; });
	Ch(TEXT("描边"), TEXT("没有单像素外轮廓，前后物体的边界糊在一起，读不出谁在前。"), [](FPixelSettings& S) { S.OutlineMode = EPixelOutlineMode::Off; });
	Ch(TEXT("真实位置光照"), TEXT("光照改用排序推移后的位置，阴影和明暗跟着排序漂移，这就是 UE 延迟光照的问题。"), [](FPixelSettings& S) { S.bTrueLightPosition = false; });
	Ch(TEXT("toon 色阶"), TEXT("明暗变成连续渐变，画面像低分辨率的普通 3D，而不是有限色阶的像素画。"), [](FPixelSettings& S) { S.ToonSteps = 0; });
}

void APixelPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) return;

	for (TActorIterator<AIslandBuilder> It(GetWorld()); It; ++It) { Island = *It; break; }

	Hud = CreateWidget<UPixelHUDWidget>(this, UPixelHUDWidget::StaticClass());
	if (Hud) Hud->AddToViewport();

	FParse::Value(FCommandLine::Get(), TEXT("PixelCapture="), CaptureDir);
	if (FParse::Param(FCommandLine::Get(), TEXT("PixelWalkTest"))) WalkTestTime = 0.f;
	if (FParse::Param(FCommandLine::Get(), TEXT("PixelMotionTest"))) { WalkTestTime = 0.f; bMotionTest = true; }
	if (!CaptureDir.IsEmpty()) CaptureStep = 0;

	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
}

const FPixelStation* APixelPlayerController::ActiveStation() const
{
	return (Island && Island->Stations.IsValidIndex(StationIndex)) ? &Island->Stations[StationIndex] : nullptr;
}

void APixelPlayerController::ActivateToggle(int32 Index)
{
	UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(this);
	if (!Sub || !Toggles.IsValidIndex(Index) || bChallenge) return;
	Toggles[Index].Act(Sub->Settings);
}

FString APixelPlayerController::ParamLabel(int32 Param) const
{
	const UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(this);
	if (!Sub) return FString();
	const FPixelSettings& S = Sub->Settings;
	switch (Param)
	{
	case 0: return FString::Printf(TEXT("像素尺寸　%d 物理像素 / texel"), S.PixelScale);
	case 1: return FString::Printf(TEXT("每米像素　%.0f（贴图固定 16 texel/米）"), S.PixelsPerMeter);
	case 2: return FString::Printf(TEXT("俯角　%.1f°（1 米竖直边 = %.2f 像素）"), S.PitchDeg, S.PixelsPerMeter * FMath::Cos(FMath::DegreesToRadians(S.PitchDeg)));
	case 3: return FString::Printf(TEXT("深度压缩系数 K　%.3f"), S.FlattenK);
	case 4: return FString::Printf(TEXT("光源方向　%.0f°"), Sub->LightYawDeg);
	case 5: return FString::Printf(TEXT("相机朝向　%.0f°"), FMath::Fmod(S.YawDeg + 360.f, 360.f));
	case 6: return FString::Printf(TEXT("旋转档位　每次 %d°"), S.YawStepDeg);
	default: return FString();
	}
}

void APixelPlayerController::AdjustParam(int32 Param, int32 Dir)
{
	UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(this);
	if (!Sub) return;
	FPixelSettings& S = Sub->Settings;
	switch (Param)
	{
	case 0: S.PixelScale = FMath::Clamp(S.PixelScale + Dir, 2, 8); break;
	case 1:
	{
		static const float Steps[] = { 8.f, 12.f, 16.f, 20.f, 24.f, 32.f };
		int32 I = 0; while (I < 5 && Steps[I] < S.PixelsPerMeter - 0.5f) ++I;
		S.PixelsPerMeter = Steps[FMath::Clamp(I + Dir, 0, 5)];
		break;
	}
	case 2:
	{
		static const float Steps[] = { 30.f, 41.41f, 51.32f, 60.f }; // 41.41: cos=0.75 (12 px), 51.32: cos=0.625 (10 px)
		int32 I = 0; while (I < 3 && Steps[I] < S.PitchDeg - 0.5f) ++I;
		S.PitchDeg = Steps[FMath::Clamp(I + Dir, 0, 3)];
		break;
	}
	case 3: S.FlattenK = FMath::Clamp(S.FlattenK * (Dir > 0 ? 2.f : 0.5f), 0.01f, 1.f); break;
	case 4: Sub->LightYawDeg += 15.f * Dir; break;
	case 5: S.YawDeg = FMath::Fmod(S.YawDeg + Dir * S.YawStepDeg + 720.f, 360.f); break;
	case 6:
	{
		static const int32 Steps[] = { 30, 45, 90 };
		int32 I = 0; while (I < 2 && Steps[I] < S.YawStepDeg) ++I;
		S.YawStepDeg = Steps[FMath::Clamp(I + Dir, 0, 2)];
		S.YawDeg = FMath::RoundToFloat(S.YawDeg / S.YawStepDeg) * S.YawStepDeg; // back onto the new grid
		break;
	}
	}
}

void APixelPlayerController::HandleKeys()
{
	UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(this);
	if (!Sub) return;

	static const FKey ToggleKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero,
		EKeys::P, EKeys::T, EKeys::H, EKeys::B, EKeys::G, EKeys::F1, EKeys::V, EKeys::J, EKeys::F2 };
	for (int32 I = 0; I < UE_ARRAY_COUNT(ToggleKeys); ++I)
	{
		if (!WasInputKeyJustPressed(ToggleKeys[I])) continue;
		if (bChallenge && I < ChallengeItems.Num()) { AnswerChallenge(I); continue; }
		ActivateToggle(I);
	}

	if (WasInputKeyJustPressed(EKeys::Z)) AdjustParam(0, -1);
	if (WasInputKeyJustPressed(EKeys::X)) AdjustParam(0, +1);
	if (WasInputKeyJustPressed(EKeys::Comma)) AdjustParam(1, -1);
	if (WasInputKeyJustPressed(EKeys::Period)) AdjustParam(1, +1);
	if (WasInputKeyJustPressed(EKeys::LeftBracket)) AdjustParam(4, -1);
	if (WasInputKeyJustPressed(EKeys::RightBracket)) AdjustParam(4, +1);
	if (WasInputKeyJustPressed(EKeys::Q)) AdjustParam(5, -1);
	if (WasInputKeyJustPressed(EKeys::R)) AdjustParam(5, +1);
	if (WasInputKeyJustPressed(EKeys::Tab)) bParamPanel = !bParamPanel;
	if (WasInputKeyJustPressed(EKeys::F3)) bUiVisible = !bUiVisible;
	if (WasInputKeyJustPressed(EKeys::M)) Sub->bMeasureFlicker = !Sub->bMeasureFlicker;
	if (WasInputKeyJustPressed(EKeys::C)) { if (bChallenge) EndChallenge(); else StartChallengeRound(); }
	if (WasInputKeyJustPressed(EKeys::Home)) { Sub->Settings = FPixelSettings(); }

	if (WasInputKeyJustPressed(EKeys::E) && Island && StationIndex != INDEX_NONE && Island->Stations[StationIndex].Index == 4 && Island->CycleBeams.Num() > 0)
	{
		static const float Pushes[] = { 0.f, 30.f, 60.f, -30.f, -60.f };
		BeamPush = (BeamPush + 1) % 5;
		AActor* Beam = Island->CycleBeams[0];
		Beam->SetActorLocation(Island->CycleBeamBase + FVector(41, 16 + Pushes[BeamPush], 40));
	}

	// Movement in screen space: equal texels per second in every screen direction. A ground step toward
	// the camera's forward shows as only sin(pitch) = 0.66 of a texel, so moving at equal *world* speed
	// makes a diagonal advance 0.66 texel vertically per horizontal texel; the two axes then step on
	// different frames in an irregular staircase. Screen-uniform speed gives clean 1:1 pixel diagonals.
	if (APawn* P = GetPawn())
	{
		const float Yaw = Sub->Camera.bValid ? Sub->Camera.Yaw : Sub->Settings.YawDeg + Sub->Settings.YawExtraDeg;
		const FVector Fwd = FRotator(0, Yaw, 0).Vector();
		const FVector Right = FRotator(0, Yaw + 90.f, 0).Vector();
		FVector2D Screen(0, 0); // x right, y up
		if (IsInputKeyDown(EKeys::W) || IsInputKeyDown(EKeys::Up)) Screen.Y += 1;
		if (IsInputKeyDown(EKeys::S) || IsInputKeyDown(EKeys::Down)) Screen.Y -= 1;
		if (IsInputKeyDown(EKeys::D) || IsInputKeyDown(EKeys::Right)) Screen.X += 1;
		if (IsInputKeyDown(EKeys::A) || IsInputKeyDown(EKeys::Left)) Screen.X -= 1;
		if (WalkTestTime >= 0.f) { Screen.Y += 1; if (bMotionTest && !FParse::Param(FCommandLine::Get(), TEXT("PixelMotionStraight"))) Screen.X += 1; } // -PixelWalkTest / -PixelMotionTest[ -PixelMotionStraight]
		if (APixelCharacter* PC = Cast<APixelCharacter>(P)) PC->ScreenMoveDir = FIntPoint(FMath::RoundToInt(FMath::Sign(Screen.X)), FMath::RoundToInt(FMath::Sign(Screen.Y)));
		if (!Screen.IsNearlyZero())
		{
			Screen.Normalize();
			const float SinPitch = FMath::Max(0.2f, FMath::Sin(FMath::DegreesToRadians(Sub->Settings.PitchDeg)));
			static const bool bOld = FParse::Param(FCommandLine::Get(), TEXT("PixelOldMove")); // A/B: equal world speed
			const FVector World = bOld ? (Right * Screen.X + Fwd * Screen.Y).GetSafeNormal() * 1.04f : Right * Screen.X + Fwd * (Screen.Y / SinPitch); // cm per screen cm
			const float Texel = 100.f / FMath::Max(1.f, Sub->Settings.PixelsPerMeter);
			// 30 texels/s straight and 20 per axis on diagonals: one step every 2nd / 3rd frame at 60 Hz
			const bool bDiag = FMath::Abs(Screen.X) > 0.1f && FMath::Abs(Screen.Y) > 0.1f;
			const float Speed = bDiag ? ScreenSpeedTexels * (2.f / 3.f) * UE_SQRT_2 : ScreenSpeedTexels;
			if (ACharacter* Ch = Cast<ACharacter>(P)) Ch->GetCharacterMovement()->MaxWalkSpeed = Speed * Texel * World.Size();
			P->AddMovementInput(World.GetSafeNormal(), 1.f);
		}
	}
}

void APixelPlayerController::UpdateStations(float DeltaTime)
{
	UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(this);
	APawn* P = GetPawn();
	if (!Sub || !P || !Island) return;

	int32 Found = INDEX_NONE;
	for (int32 I = 0; I < Island->Stations.Num(); ++I)
	{
		if (FVector::Dist2D(P->GetActorLocation(), Island->Stations[I].Pad) < Island->Stations[I].Radius) { Found = I; break; }
	}
	if (Found != StationIndex)
	{
		// leaving a station undoes what it switched on
		if (bAutoPanFromStation) { Sub->Settings.bAutoPan = false; Sub->bMeasureFlicker = false; bAutoPanFromStation = false; }
		StationIndex = Found;
		if (const FPixelStation* S = ActiveStation())
		{
			if (S->Index == 2) { Sub->Settings.bAutoPan = true; Sub->bMeasureFlicker = true; bAutoPanFromStation = true; }
		}
	}
	const FPixelStation* S = ActiveStation();
	if (APixelCharacter* C = Cast<APixelCharacter>(P)) C->bSpin = S && S->Index == 6;
	if (APixelCameraManager* Cam = Cast<APixelCameraManager>(PlayerCameraManager))
	{
		// a fixed framing per station; a target that slides with the player makes the camera move at a
		// fraction of the player's speed, and that ratio beats against the texel grid
		if (S && S->Index != 6) Cam->FocusOverride = S->Focus; else Cam->FocusOverride.Reset();
	}
	if (S && S->Index == 6) Sub->LightYawDeg += 20.f * DeltaTime;
	// The cycle exhibit needs a diagonal view: axis-aligned boxes can only weave when F has both X and Y.
	// Offset to the nearest diagonal (45 + k*90) from whatever yaw the player picked.
	float Extra = 0.f;
	if (S && S->Index == 4)
	{
		const float Y = Sub->Settings.YawDeg;
		Extra = (FMath::RoundToFloat((Y - 45.f) / 90.f) * 90.f + 45.f) - Y;
	}
	Sub->Settings.YawExtraDeg = Extra;
}

void APixelPlayerController::StartChallengeRound()
{
	UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(this);
	if (!Sub) return;
	if (!bChallenge) { BeforeChallenge = Sub->Settings; ChallengeRound = 0; ChallengeScore = 0; }
	bChallenge = true;
	++ChallengeRound;
	Sub->Settings = FPixelSettings();
	ChallengeAnswer = FMath::RandRange(0, ChallengeItems.Num() - 1);
	ChallengeItems[ChallengeAnswer].Break(Sub->Settings);
	ChallengeFeedback = FString::Printf(TEXT("第 %d 轮：有一项技术被悄悄关掉了。四处走走，按数字键 1–%d 选出是哪一项。"), ChallengeRound, ChallengeItems.Num());
}

void APixelPlayerController::AnswerChallenge(int32 Option)
{
	if (!bChallenge || ChallengeAnswer == INDEX_NONE) return;
	const bool bRight = Option == ChallengeAnswer;
	if (bRight) ++ChallengeScore;
	ChallengeFeedback = FString::Printf(TEXT("%s 被关掉的是「%s」。%s\n得分 %d / %d　按 C 结束，或按 N 进入下一轮。"),
		bRight ? TEXT("答对了！") : TEXT("不对。"), *ChallengeItems[ChallengeAnswer].Name, *ChallengeItems[ChallengeAnswer].Explain, ChallengeScore, ChallengeRound);
	ChallengeAnswer = INDEX_NONE;
}

void APixelPlayerController::EndChallenge()
{
	if (UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(this)) Sub->Settings = BeforeChallenge;
	bChallenge = false;
	ChallengeAnswer = INDEX_NONE;
	ChallengeFeedback.Reset();
}

namespace
{
	struct FCaptureShot
	{
		const TCHAR* Name;
		int32 Station;          // 0 = spawn
		FVector Offset;         // pawn offset from the pad
		TFunction<void(FPixelSettings&)> Setup;
	};
}

void APixelPlayerController::TickCapture(float DeltaTime)
{
	static const TArray<FCaptureShot> Shots = {
		{ TEXT("00_overview"), 0, FVector::ZeroVector, [](FPixelSettings& S) {} },
		{ TEXT("01_density_raw"), 1, FVector::ZeroVector, [](FPixelSettings& S) { S.bLowRes = false; } },
		{ TEXT("02_density_pixel"), 1, FVector::ZeroVector, [](FPixelSettings& S) {} },
		{ TEXT("03_density_grid"), 1, FVector::ZeroVector, [](FPixelSettings& S) { S.bShowGrid = true; } },
		{ TEXT("04_density_bilinear"), 1, FVector::ZeroVector, [](FPixelSettings& S) { S.bBilinear = true; } },
		{ TEXT("05_runway_snap"), 2, FVector::ZeroVector, [](FPixelSettings& S) { S.bAutoPan = true; } },
		{ TEXT("06_runway_nosnap"), 2, FVector::ZeroVector, [](FPixelSettings& S) { S.bAutoPan = true; S.bCameraSnap = false; } },
		{ TEXT("07_runway_subtexel"), 2, FVector::ZeroVector, [](FPixelSettings& S) { S.bAutoPan = true; S.bSubTexel = true; } },
		{ TEXT("08_wall_nosort"), 3, FVector(70, 0, 0), [](FPixelSettings& S) { S.SortMode = EPixelSortMode::Off; } },
		{ TEXT("09_wall_flatten_chars"), 3, FVector(70, 0, 0), [](FPixelSettings& S) { S.SortMode = EPixelSortMode::CharactersOnly; } },
		{ TEXT("10_wall_sort"), 3, FVector(70, 0, 0), [](FPixelSettings& S) {} },
		{ TEXT("11_wall_pushed_depth"), 3, FVector(70, 0, 0), [](FPixelSettings& S) { S.ViewMode = EPixelViewMode::PushedDepth; } },
		{ TEXT("12_wall_true_depth"), 3, FVector(70, 0, 0), [](FPixelSettings& S) { S.ViewMode = EPixelViewMode::TrueDepth; } },
		{ TEXT("13_wall_light_pushed"), 3, FVector(70, 0, 0), [](FPixelSettings& S) { S.bTrueLightPosition = false; } },
		{ TEXT("14_cycle_minarea"), 4, FVector::ZeroVector, [](FPixelSettings& S) { S.CycleMode = EPixelCycleMode::MinArea; S.bShowGraph = true; } },
		{ TEXT("15_cycle_merge"), 4, FVector::ZeroVector, [](FPixelSettings& S) { S.bShowGraph = true; } },
		{ TEXT("16_cycle_levels"), 4, FVector::ZeroVector, [](FPixelSettings& S) { S.ViewMode = EPixelViewMode::SortLevel; S.bShowSortVolumes = true; } },
		{ TEXT("17_outline_front"), 5, FVector::ZeroVector, [](FPixelSettings& S) {} },
		{ TEXT("18_outline_back"), 5, FVector::ZeroVector, [](FPixelSettings& S) { S.OutlineMode = EPixelOutlineMode::Back; } },
		{ TEXT("19_outline_both"), 5, FVector::ZeroVector, [](FPixelSettings& S) { S.OutlineMode = EPixelOutlineMode::Both; } },
		{ TEXT("20_outline_nolut"), 5, FVector::ZeroVector, [](FPixelSettings& S) { S.bLutCleanup = false; } },
		{ TEXT("21_dither_screen"), 5, FVector::ZeroVector, [](FPixelSettings& S) { S.DitherMode = EPixelDitherMode::Screen; } },
		{ TEXT("22_dither_object"), 5, FVector::ZeroVector, [](FPixelSettings& S) { S.DitherMode = EPixelDitherMode::Object; } },
		{ TEXT("23_turntable"), 6, FVector::ZeroVector, [](FPixelSettings& S) {} },
		{ TEXT("24_turntable_smooth"), 6, FVector::ZeroVector, [](FPixelSettings& S) { S.bRotationQuantize = false; S.bSteppedAnim = false; S.ToonSteps = 0; } },
		{ TEXT("25_light_only"), 6, FVector::ZeroVector, [](FPixelSettings& S) { S.ViewMode = EPixelViewMode::LightOnly; } },
		{ TEXT("26_ids"), 0, FVector::ZeroVector, [](FPixelSettings& S) { S.ViewMode = EPixelViewMode::ObjectId; } },
		{ TEXT("27_yaw45"), 0, FVector::ZeroVector, [](FPixelSettings& S) { S.YawDeg = 45.f; } },
		{ TEXT("28_yaw30_step"), 0, FVector::ZeroVector, [](FPixelSettings& S) { S.YawStepDeg = 30; S.YawDeg = 330.f; } },
		{ TEXT("zz_end"), 0, FVector::ZeroVector, [](FPixelSettings& S) {} },
	};

	UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(this);
	if (!Sub || CaptureStep < 0) return;
	if (CaptureStep >= Shots.Num())
	{
		UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
		CaptureStep = -1;
		return;
	}
#if WITH_EDITOR
	// Wait for shader compilation: slow frames would otherwise race through the script.
	if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0) { CaptureTimer = 0.f; CaptureFrames = 0; return; }
#endif
	CaptureTimer += FMath::Min(DeltaTime, 0.05f);
	++CaptureFrames;
	FString Only;
	if (FParse::Value(FCommandLine::Get(), TEXT("PixelCaptureOnly="), Only, false) && CaptureFrames == 1)
	{
		// skip shots whose name does not start with any listed prefix
		TArray<FString> Prefixes; Only.ParseIntoArray(Prefixes, TEXT(","));
		while (CaptureStep < Shots.Num() && !Prefixes.ContainsByPredicate([&](const FString& P) { return FString(Shots[CaptureStep].Name).StartsWith(P); })) ++CaptureStep;
		if (CaptureStep >= Shots.Num()) return;
	}
	const FCaptureShot& Shot = Shots[CaptureStep];
	if (CaptureFrames == 1)
	{
		Sub->Settings = FPixelSettings();
		Shot.Setup(Sub->Settings);
		Sub->bMeasureFlicker = Shot.Station == 2;
		if (APawn* P = GetPawn())
		{
			FVector At = FVector(0, -300, 100);
			if (Island) { for (const FPixelStation& St : Island->Stations) { if (St.Index == Shot.Station) At = St.Pad + FVector(0, 0, 100); } }
			P->SetActorLocation(At + Shot.Offset, false, nullptr, ETeleportType::TeleportPhysics);
			P->SetActorRotation(FRotator(0, Shot.Station == 3 ? 90.f : 0.f, 0)); // walk along the wall
		}
	}
	const float Need = Shot.Station == 2 ? 4.0f : 1.6f; // flicker meter needs ~12 samples
	if (CaptureTimer > Need && CaptureFrames > 40 && !bCaptureShotPending)
	{
		FScreenshotRequest::RequestScreenshot(CaptureDir / FString(Shot.Name) + TEXT(".png"), true, false);
		bCaptureShotPending = true;
		UE_LOG(LogTemp, Display, TEXT("Capture %s: flicker %.3f%%, sort %d pairs %d cycles %d cuts %d merged"), Shot.Name, Sub->FlickerPercent,
			Sub->GetSortStats().Pairs, Sub->GetSortStats().Cycles, Sub->GetSortStats().Cuts, Sub->GetSortStats().Merged);
		if (FString(Shot.Name).StartsWith(TEXT("23")))
		{
			// dump the shadow depth map for inspection
			if (UTextureRenderTarget2D* RT = Sub->GetShadowTarget())
			{
				TArray<FLinearColor> Px;
				if (RT->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Px))
				{
					float Lo = 1e9f, Hi = -1e9f;
					for (const FLinearColor& C : Px) { if (C.R < 1e5f) { Lo = FMath::Min(Lo, C.R); Hi = FMath::Max(Hi, C.R); } }
					TArray<FColor> Out; Out.SetNum(Px.Num());
					for (int32 I = 0; I < Px.Num(); ++I)
					{
						const float V = Px[I].R >= 1e5f ? 1.f : (Px[I].R - Lo) / FMath::Max(1.f, Hi - Lo);
						const uint8 G = uint8(FMath::Clamp(V, 0.f, 1.f) * 255.f);
						Out[I] = Px[I].R >= 1e5f ? FColor(255, 0, 255) : FColor(G, G, G);
					}
					TArray64<uint8> Png;
					FImageUtils::PNGCompressImageArray(RT->SizeX, RT->SizeY, TArrayView64<const FColor>(Out.GetData(), Out.Num()), Png);
					FFileHelper::SaveArrayToFile(Png, *(CaptureDir / TEXT("shadowmap.png")));
					UE_LOG(LogTemp, Display, TEXT("Shadow depth range %.1f .. %.1f"), Lo, Hi);
				}
			}
		}
	}
	if (bCaptureShotPending && CaptureFrames > 50 && CaptureTimer > Need + 0.3f)
	{
		CaptureTimer = 0.f; CaptureFrames = 0; bCaptureShotPending = false; ++CaptureStep;
	}
}

void APixelPlayerController::TickWalkTest(float DeltaTime)
{
	if (WalkTestTime < 0.f || !GetPawn()) return;
	UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(this);
	if (WalkTestTime == 0.f)
	{
		float SX = 0.f, SY = 0.f; // -PixelStartX= / -PixelStartY=: start somewhere else (e.g. on plain tiles)
		if (FParse::Value(FCommandLine::Get(), TEXT("PixelStartX="), SX) && FParse::Value(FCommandLine::Get(), TEXT("PixelStartY="), SY))
		{
			GetPawn()->SetActorLocation(FVector(SX, SY, 120.f), false, nullptr, ETeleportType::TeleportPhysics);
		}
		WalkTestStart = GetPawn()->GetActorLocation();
		if (bMotionTest && Sub)
		{
			// -PixelFollow=N picks the follow mode under test
			int32 Mode = 0;
			if (FParse::Value(FCommandLine::Get(), TEXT("PixelFollow="), Mode)) Sub->Settings.FollowMode = EPixelFollow(FMath::Clamp(Mode, 0, 3));
		}
	}
	WalkTestTime += DeltaTime;
	if (Sub) Sub->bMotionProbe = bMotionTest && WalkTestTime > 0.6f && WalkTestTime <= 3.0f; // skip the acceleration
	if (bMotionTest && WalkTestTime > 3.0f && Sub)
	{
		// Player wobble: distance of each on-screen sample from its 9-frame centred mean (should be ~0).
		// World judder: per-frame scroll velocity minus its 9-frame mean (0 = perfectly even scroll).
		const TArray<FVector4d>& M = Sub->MotionSamples;
		double Wobble = 0, WobbleMax = 0, Judder = 0, Speed = 0; int32 N = 0, StepFrames = 0, BothFrames = 0;
		for (int32 I = 1; I < M.Num(); ++I)
		{
			const bool bX = FMath::Abs(M[I].Z - M[I - 1].Z) > 0.5, bY = FMath::Abs(M[I].W - M[I - 1].W) > 0.5;
			StepFrames += (bX || bY) ? 1 : 0; BothFrames += (bX && bY) ? 1 : 0;
		}
		for (int32 I = 5; I + 5 < M.Num(); ++I)
		{
			FVector2d Mean(0, 0), VMean(0, 0);
			for (int32 K = -4; K <= 4; ++K) { Mean += FVector2d(M[I + K].X, M[I + K].Y); VMean += FVector2d(M[I + K].Z - M[I + K - 1].Z, M[I + K].W - M[I + K - 1].W); }
			Mean /= 9.0; VMean /= 9.0;
			const double W = (FVector2d(M[I].X, M[I].Y) - Mean).Size();
			const FVector2d V(M[I].Z - M[I - 1].Z, M[I].W - M[I - 1].W);
			Wobble += W; WobbleMax = FMath::Max(WobbleMax, W); Judder += (V - VMean).Size(); Speed += V.Size(); ++N;
		}
		if (N > 0)
		{
			UE_LOG(LogTemp, Display, TEXT("MotionTest[%s]: %d frames, player wobble mean %.3f max %.3f texel, world scroll %.3f texel/frame, judder %.3f texel/frame, diagonal steps on both axes %d / %d"),
				PixelText::FollowModeName(Sub->Settings.FollowMode), N, Wobble / N, WobbleMax, Speed / N, Judder / N, BothFrames, StepFrames);
		}
		Sub->bMotionProbe = false;
		WalkTestTime = -1.f;
		if (CaptureDir.IsEmpty()) UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
		return;
	}
	if (!bMotionTest && WalkTestTime > 2.f)
	{
		const FVector End = GetPawn()->GetActorLocation();
		UE_LOG(LogTemp, Display, TEXT("WalkTest: from %s to %s, moved %.1f cm in 2 s"), *WalkTestStart.ToString(), *End.ToString(), FVector::Dist2D(WalkTestStart, End));
		WalkTestTime = -1.f;
		if (CaptureDir.IsEmpty()) UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
	}
}

void APixelPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	TickWalkTest(DeltaTime);
	TickCapture(DeltaTime);
	HandleKeys();
	if (bChallenge && ChallengeAnswer == INDEX_NONE && WasInputKeyJustPressed(EKeys::N)) StartChallengeRound();
	UpdateStations(DeltaTime);
}
