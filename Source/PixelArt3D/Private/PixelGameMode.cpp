#include "PixelGameMode.h"

#include "IslandBuilder.h"
#include "PixelArtSubsystem.h"
#include "PixelCharacter.h"
#include "PixelPlayerController.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

APixelGameMode::APixelGameMode()
{
	DefaultPawnClass = APixelCharacter::StaticClass();
	PlayerControllerClass = APixelPlayerController::StaticClass();
	HUDClass = APixelDebugHUD::StaticClass();
}

APawn* APixelGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	FTransform T = SpawnTransform;
	T.SetLocation(FVector(-150.f, -150.f, 120.f)); // open grass in the middle of the island, capsule above the ground
	T.SetRotation(FQuat::Identity);
	return Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayer, T);
}

void APixelGameMode::StartPlay()
{
	// The island must exist (and register its sprites) before the player controller looks for it.
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GetWorld()->SpawnActor<AIslandBuilder>(FVector::ZeroVector, FRotator::ZeroRotator, P);
	Super::StartPlay();
}

void APixelDebugHUD::DrawHUD()
{
	Super::DrawHUD();
	const UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(this);
	if (!Sub || !Canvas) return;

	// Station markers: a number on the near rim of each pad, so it never covers whoever stands on it.
	const FVector Toward = -FVector(Sub->Camera.Forward.X, Sub->Camera.Forward.Y, 0).GetSafeNormal();
	for (TActorIterator<AIslandBuilder> It(GetWorld()); It; ++It)
	{
		for (const FPixelStation& S : It->Stations)
		{
			const FVector P = Project(S.Pad + Toward * (S.Radius + 20.f), false);
			if (P.Z <= 0.f) continue;
			const FString Label = FString::Printf(TEXT("%d"), S.Index);
			DrawRect(FLinearColor(0.03f, 0.04f, 0.05f, 0.75f), P.X - 13, P.Y - 13, 26, 26);
			DrawText(Label, FLinearColor(1.f, 0.75f, 0.35f), P.X - 6, P.Y - 11, GEngine->GetMediumFont(), 1.3f);
		}
		break;
	}

	if (!Sub->Settings.bShowGraph) return;
	const TArray<FPixelSprite>& Sprites = Sub->GetSprites();
	for (const FPixelSortEdge& E : Sub->GetEdges())
	{
		if (!Sprites.IsValidIndex(E.Back) || !Sprites.IsValidIndex(E.Front)) continue;
		const FVector A = Project(Sprites[E.Front].Box.GetCenter(), false);
		const FVector B = Project(Sprites[E.Back].Box.GetCenter(), false);
		FLinearColor C = E.bCut ? FLinearColor(1.f, 0.2f, 0.2f) : (E.bMerged ? FLinearColor(0.3f, 1.f, 0.4f) : FLinearColor(0.9f, 0.9f, 0.95f, 0.8f));
		DrawLine(A.X, A.Y, B.X, B.Y, C, E.bCut ? 3.f : 1.5f);
		// arrow head at the back end: the front object covers the back one
		const FVector2D Dir = FVector2D(B.X - A.X, B.Y - A.Y).GetSafeNormal();
		const FVector2D N(-Dir.Y, Dir.X);
		const FVector2D Tip(B.X, B.Y);
		DrawLine(Tip.X, Tip.Y, Tip.X - Dir.X * 10 + N.X * 5, Tip.Y - Dir.Y * 10 + N.Y * 5, C, 1.5f);
		DrawLine(Tip.X, Tip.Y, Tip.X - Dir.X * 10 - N.X * 5, Tip.Y - Dir.Y * 10 - N.Y * 5, C, 1.5f);
		if (E.bCut) DrawText(FString::Printf(TEXT("%.0f"), E.Area), C, (A.X + B.X) * 0.5f, (A.Y + B.Y) * 0.5f, GEngine->GetSmallFont());
	}
	for (const FPixelSprite& S : Sprites)
	{
		const FVector P = Project(S.Box.GetCenter(), false);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), P.X - 3, P.Y - 3, 6, 6);
		if (Sub->Settings.SortMode == EPixelSortMode::Full) DrawText(FString::FromInt(S.Level), FLinearColor::White, P.X + 4, P.Y - 6, GEngine->GetSmallFont());
	}
}
