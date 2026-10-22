#include "PixelArtSubsystem.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/CapsuleComponent.h"
#include "Components/MeshComponent.h"
#include "Components/ShapeComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "UnrealClient.h"
#include "DrawDebugHelpers.h"

namespace
{
	const TCHAR* MpcPath = TEXT("/Game/PixelArt/MPC_Pixel.MPC_Pixel");
	const TCHAR* SurfacePath = TEXT("/Game/PixelArt/M_PixelSurface.M_PixelSurface");
	const TCHAR* AtlasPath = TEXT("/Game/PixelArt/T_Atlas.T_Atlas");

	// CPD layout shared with M_PixelSurface (Tools/build_assets.py).
	enum ECpd : int32
	{
		CpdSlot = 0,    // target depth of the pivot along the view (absolute dot(P, F))
		CpdPivot = 1,   // true depth of the pivot (or of the merge group's middle)
		CpdKeep = 2,    // fraction of the object's own depth that survives (K, or 1 for merged groups)
		CpdEnable = 3,  // 0: true depth, 1: relocate
		CpdId = 4,
		CpdPivotX = 5, CpdPivotY = 6, CpdPivotZ = 7,
		CpdLevel = 8,
	};

	uint64 EdgeKey(int32 Back, int32 Front) { return (uint64(uint32(Back)) << 32) | uint32(Front); }

	/** Tarjan SCC over node graph. Returns component index per node. */
	TArray<int32> Tarjan(int32 N, const TArray<TArray<int32>>& Adj)
	{
		TArray<int32> Index, Low, Comp, Stack;
		TArray<bool> OnStack;
		Index.Init(-1, N); Low.Init(0, N); Comp.Init(-1, N); OnStack.Init(false, N);
		int32 Counter = 0, CompCount = 0;

		TFunction<void(int32)> Strong = [&](int32 V)
		{
			Index[V] = Low[V] = Counter++;
			Stack.Push(V); OnStack[V] = true;
			for (int32 W : Adj[V])
			{
				if (Index[W] < 0) { Strong(W); Low[V] = FMath::Min(Low[V], Low[W]); }
				else if (OnStack[W]) { Low[V] = FMath::Min(Low[V], Index[W]); }
			}
			if (Low[V] == Index[V])
			{
				int32 W;
				do { W = Stack.Pop(); OnStack[W] = false; Comp[W] = CompCount; } while (W != V);
				++CompCount;
			}
		};
		for (int32 V = 0; V < N; ++V) { if (Index[V] < 0) Strong(V); }
		return Comp;
	}
}

namespace PixelText
{
	const TCHAR* SortModeName(EPixelSortMode M)
	{
		switch (M) { case EPixelSortMode::Off: return TEXT("关（逐像素深度）"); case EPixelSortMode::CharactersOnly: return TEXT("仅角色深度扁平化"); default: return TEXT("全量精灵排序"); }
	}
	const TCHAR* CycleModeName(EPixelCycleMode M)
	{
		switch (M) { case EPixelCycleMode::MinArea: return TEXT("切最小重叠（讲座）"); case EPixelCycleMode::Merge: return TEXT("不相交则合并层"); default: return TEXT("最小重叠 + 滞回"); }
	}
	const TCHAR* OutlineModeName(EPixelOutlineMode M)
	{
		switch (M) { case EPixelOutlineMode::Off: return TEXT("关"); case EPixelOutlineMode::Front: return TEXT("归前方物体"); case EPixelOutlineMode::Back: return TEXT("归后方物体"); default: return TEXT("两侧都画"); }
	}
	const TCHAR* FollowModeName(EPixelFollow M)
	{
		switch (M) { case EPixelFollow::Locked: return TEXT("锁定像素"); case EPixelFollow::SmoothSub: return TEXT("平滑 + 亚像素"); default: return TEXT("插值滞后（旧）"); }
	}
	const TCHAR* DitherModeName(EPixelDitherMode M)
	{
		switch (M) { case EPixelDitherMode::Off: return TEXT("关"); case EPixelDitherMode::Screen: return TEXT("明暗交界抖动 · 屏幕空间"); default: return TEXT("明暗交界抖动 · 物体空间"); }
	}
	const TCHAR* ViewModeName(EPixelViewMode M)
	{
		switch (M)
		{
		case EPixelViewMode::Final: return TEXT("成品");
		case EPixelViewMode::ObjectId: return TEXT("物体 ID");
		case EPixelViewMode::SortLevel: return TEXT("排序层级");
		case EPixelViewMode::TrueDepth: return TEXT("真实深度");
		case EPixelViewMode::PushedDepth: return TEXT("写入深度（排序后）");
		default: return TEXT("仅明暗");
		}
	}
}

UPixelArtSubsystem* UPixelArtSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UPixelArtSubsystem>() : nullptr;
}

bool UPixelArtSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPixelArtSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPixelArtSubsystem, STATGROUP_Tickables);
}

void UPixelArtSubsystem::Initialize(FSubsystemCollectionBase& Collection_)
{
	Super::Initialize(Collection_);
	Collection = LoadObject<UMaterialParameterCollection>(nullptr, MpcPath);
	SurfaceMaterial = LoadObject<UMaterialInterface>(nullptr, SurfacePath);
	Atlas = LoadObject<UTexture>(nullptr, AtlasPath);

	ShadowTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("RT_ShadowDepth"));
	ShadowTarget->RenderTargetFormat = RTF_R32f;
	ShadowTarget->ClearColor = FLinearColor(1e6f, 0, 0, 0);
	ShadowTarget->bAutoGenerateMips = false;
	ShadowTarget->Filter = TF_Nearest;
	// 2.3 cm shadow texels: finer than the 6.25 cm screen texel, so a moving shadow re-quantises once (on screen), not twice
	ShadowTarget->InitAutoFormat(2048, 2048);
	ShadowTarget->UpdateResourceImmediate(true);
}

void UPixelArtSubsystem::Deinitialize()
{
	Sprites.Reset();
	Super::Deinitialize();
}

void UPixelArtSubsystem::RegisterSprite(AActor* Actor, bool bDynamic, bool bCharacter, FName Label)
{
	if (!Actor) return;
	FPixelSprite S;
	S.Actor = Actor;
	S.bDynamic = bDynamic;
	S.bCharacter = bCharacter;
	S.Label = Label;
	S.Id = NextId < 250 ? NextId++ : 250;
	TArray<UPrimitiveComponent*> Prims;
	Actor->GetComponents<UPrimitiveComponent>(Prims);
	for (UPrimitiveComponent* P : Prims)
	{
		if (P->IsA<UShapeComponent>()) continue; // collision only
		P->SetRenderCustomDepth(true);
		P->SetCustomDepthStencilValue(S.Id);
		P->SetCustomPrimitiveDataFloat(CpdId, S.Id);
		S.Prims.Add(P);
	}
	Sprites.Add(S);
}

void UPixelArtSubsystem::RegisterTerrain(UPrimitiveComponent* Prim)
{
	if (!Prim) return;
	Prim->SetRenderCustomDepth(true);
	Prim->SetCustomDepthStencilValue(1);
	Prim->SetCustomPrimitiveDataFloat(CpdId, 1);
	Prim->SetCustomPrimitiveDataFloat(CpdEnable, 0);
	Terrain.Add(Prim);
}

FVector UPixelArtSubsystem::SnapWorld(const FVector& World) const
{
	const double T = Camera.Texel;
	const double X = FVector::DotProduct(World, Camera.Right) / T;
	const double Y = FVector::DotProduct(World, Camera.Up) / T;
	return World + Camera.Right * ((FMath::RoundToDouble(X) - X) * T) + Camera.Up * ((FMath::RoundToDouble(Y) - Y) * T);
}

UMaterialInstanceDynamic* UPixelArtSubsystem::MakeSurfaceMaterial(UObject* Outer, int32 Tile, FLinearColor Tint, bool bUseTexture)
{
	if (!SurfaceMaterial) return nullptr;
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(SurfaceMaterial, Outer);
	Mid->SetScalarParameterValue(TEXT("Tile"), Tile);
	Mid->SetScalarParameterValue(TEXT("UseTexture"), bUseTexture ? 1.f : 0.f);
	Mid->SetVectorParameterValue(TEXT("Tint"), Tint);
	if (Atlas) Mid->SetTextureParameterValue(TEXT("Atlas"), Atlas);
	if (ShadowTarget) Mid->SetTextureParameterValue(TEXT("ShadowDepth"), ShadowTarget);
	Mids.Add(Mid);
	return Mid;
}

UMaterialInstanceDynamic* UPixelArtSubsystem::MakeMeshMaterial(UObject* Outer, UTexture* Texture)
{
	UMaterialInstanceDynamic* Mid = MakeSurfaceMaterial(Outer, 0, FLinearColor::White, false);
	if (!Mid) return nullptr;
	Mid->SetScalarParameterValue(TEXT("UseTexture"), Texture ? 2.f : 0.f);
	if (Texture) Mid->SetTextureParameterValue(TEXT("MeshTex"), Texture);
	return Mid;
}

void UPixelArtSubsystem::ApplyMeshMaterial(UMeshComponent* Mesh, UTexture* Texture)
{
	if (!Mesh) return;
	UMaterialInstanceDynamic* Mid = MakeMeshMaterial(Mesh, Texture);
	for (int32 I = 0; I < Mesh->GetNumMaterials(); ++I) Mesh->SetMaterial(I, Mid);
}

bool UPixelArtSubsystem::InFront(const FBox& A, const FBox& B) const
{
	// The talk (p.81): find the axis that separates the boxes and compare it with the view direction.
	// "Minimum" is undefined in the slides; we take the separation that is largest along the view.
	const FVector F = Camera.Forward;
	int32 Best = -1; double BestGap = -1.0;
	for (int32 K = 0; K < 3; ++K)
	{
		const double Fk = F[K];
		if (FMath::Abs(Fk) < 1e-6) continue;
		if (A.Max[K] <= B.Min[K])
		{
			const double Gap = (B.Min[K] - A.Max[K]) * FMath::Abs(Fk) + 1e-3;
			if (Gap > BestGap) { BestGap = Gap; Best = Fk > 0 ? 1 : 0; } // A on the negative side: nearer when F points +K
		}
		if (B.Max[K] <= A.Min[K])
		{
			const double Gap = (A.Min[K] - B.Max[K]) * FMath::Abs(Fk) + 1e-3;
			if (Gap > BestGap) { BestGap = Gap; Best = Fk < 0 ? 1 : 0; }
		}
	}
	if (Best >= 0) return Best == 1;
	return FVector::DotProduct(A.GetCenter(), F) < FVector::DotProduct(B.GetCenter(), F);
}

void UPixelArtSubsystem::UpdateSort()
{
	const double T0 = FPlatformTime::Seconds();
	Edges.Reset();
	SortStats = FPixelSortStats();

	Sprites.RemoveAll([](const FPixelSprite& S) { return !S.Actor.IsValid(); });
	const int32 N = Sprites.Num();
	SortStats.Sprites = N;
	if (!Camera.bValid || N == 0) return;

	const double T = Camera.Texel;
	for (FPixelSprite& S : Sprites)
	{
		S.Box = FBox(ForceInit);
		for (const TWeakObjectPtr<UPrimitiveComponent>& P : S.Prims)
		{
			if (P.IsValid() && P->IsVisible()) S.Box += P->Bounds.GetBox();
		}
		if (!S.Box.IsValid) S.Box = FBox(S.Actor->GetActorLocation() - FVector(10), S.Actor->GetActorLocation() + FVector(10));
		if (const ACharacter* C = Cast<ACharacter>(S.Actor.Get()))
		{
			S.Pivot = C->GetActorLocation() - FVector(0, 0, C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		}
		else
		{
			S.Pivot = S.Box.GetCenter();
		}
		S.Rect = FBox2D(ForceInit);
		for (int32 I = 0; I < 8; ++I)
		{
			const FVector C(I & 1 ? S.Box.Max.X : S.Box.Min.X, I & 2 ? S.Box.Max.Y : S.Box.Min.Y, I & 4 ? S.Box.Max.Z : S.Box.Min.Z);
			S.Rect += FVector2D(FVector::DotProduct(C, Camera.Right) / T, FVector::DotProduct(C, Camera.Up) / T);
		}
		S.Node = INDEX_NONE;
		S.Level = 0;
	}

	if (Settings.SortMode != EPixelSortMode::Full)
	{
		SortStats.Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
		return;
	}

	// 1. screen-overlapping pairs -> directed edges (back -> front), weighted by overlap area
	for (int32 I = 0; I < N; ++I)
	{
		for (int32 J = I + 1; J < N; ++J)
		{
			const FBox2D& A = Sprites[I].Rect; const FBox2D& B = Sprites[J].Rect;
			const double W = FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X);
			const double H = FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y);
			if (W <= 0 || H <= 0) continue;
			FPixelSortEdge E;
			const bool bIFront = InFront(Sprites[I].Box, Sprites[J].Box);
			E.Back = bIFront ? J : I; E.Front = bIFront ? I : J; E.Area = float(W * H);
			Edges.Add(E);
		}
	}
	SortStats.Pairs = Edges.Num();

	// 2. condense: every sprite starts as its own node; merge groups collapse nodes
	TArray<int32> NodeOf; NodeOf.SetNum(N);
	for (int32 I = 0; I < N; ++I) NodeOf[I] = I;
	auto Find = [&](int32 X) { while (NodeOf[X] != X) X = NodeOf[X] = NodeOf[NodeOf[X]]; return X; };

	TSet<uint64> CutsThisFrame;
	for (int32 Iter = 0; Iter < 64; ++Iter)
	{
		TArray<TArray<int32>> Adj; Adj.SetNum(N);
		for (const FPixelSortEdge& E : Edges)
		{
			if (E.bCut) continue;
			const int32 A = Find(E.Back), B = Find(E.Front);
			if (A != B) Adj[A].AddUnique(B);
		}
		const TArray<int32> Comp = Tarjan(N, Adj);
		TMap<int32, TArray<int32>> Members; // component -> root nodes
		for (int32 V = 0; V < N; ++V) { if (Find(V) == V) Members.FindOrAdd(Comp[V]).Add(V); }

		bool bChanged = false;
		for (const TPair<int32, TArray<int32>>& KV : Members)
		{
			if (KV.Value.Num() < 2) continue;
			++SortStats.Cycles;
			TSet<int32> NodeSet(KV.Value);

			if (Settings.CycleMode == EPixelCycleMode::Merge)
			{
				// Improvement 1: if no two sprites of the cycle intersect in 3D, per-pixel depth already
				// draws them correctly — keep their true depth inside one layer instead of cutting.
				TArray<int32> SpriteIdx;
				for (int32 S = 0; S < N; ++S) { if (NodeSet.Contains(Find(S))) SpriteIdx.Add(S); }
				bool bIntersect = false;
				for (int32 A = 0; A < SpriteIdx.Num() && !bIntersect; ++A)
				{
					for (int32 B = A + 1; B < SpriteIdx.Num(); ++B)
					{
						if (Sprites[SpriteIdx[A]].Box.ExpandBy(-2.0).Intersect(Sprites[SpriteIdx[B]].Box.ExpandBy(-2.0))) { bIntersect = true; break; }
					}
				}
				if (!bIntersect)
				{
					const int32 Root = KV.Value[0];
					for (int32 V : KV.Value) NodeOf[V] = Root;
					for (FPixelSortEdge& E : Edges) { if (Find(E.Back) == Root && Find(E.Front) == Root) E.bMerged = true; }
					SortStats.Merged += SpriteIdx.Num();
					bChanged = true;
					continue;
				}
			}

			// Cut the lightest edge inside the component; prefer edges touching a dynamic sprite so the
			// static environment's order stays locked (talk p.84).
			int32 BestEdge = INDEX_NONE; float BestCost = TNumericLimits<float>::Max();
			for (int32 EI = 0; EI < Edges.Num(); ++EI)
			{
				const FPixelSortEdge& E = Edges[EI];
				if (E.bCut || E.bMerged) continue;
				if (!NodeSet.Contains(Find(E.Back)) || !NodeSet.Contains(Find(E.Front)) || Find(E.Back) == Find(E.Front)) continue;
				const bool bDyn = Sprites[E.Back].bDynamic || Sprites[E.Front].bDynamic;
				const float Cost = E.Area * (bDyn ? 1.f : 4.f);
				if (Cost < BestCost) { BestCost = Cost; BestEdge = EI; }
			}
			if (Settings.CycleMode == EPixelCycleMode::Hysteresis && BestEdge != INDEX_NONE)
			{
				// Improvement 2: keep last frame's cut while it costs at most 1.2x the current best.
				for (int32 EI = 0; EI < Edges.Num(); ++EI)
				{
					const FPixelSortEdge& E = Edges[EI];
					if (E.bCut || E.bMerged || !LastCuts.Contains(EdgeKey(E.Back, E.Front))) continue;
					if (!NodeSet.Contains(Find(E.Back)) || !NodeSet.Contains(Find(E.Front))) continue;
					const bool bDyn = Sprites[E.Back].bDynamic || Sprites[E.Front].bDynamic;
					if (E.Area * (bDyn ? 1.f : 4.f) <= BestCost * 1.2f) { BestEdge = EI; break; }
				}
			}
			if (BestEdge != INDEX_NONE)
			{
				Edges[BestEdge].bCut = true;
				CutsThisFrame.Add(EdgeKey(Edges[BestEdge].Back, Edges[BestEdge].Front));
				++SortStats.Cuts;
				bChanged = true;
			}
		}
		if (!bChanged) break;
	}
	LastCuts = MoveTemp(CutsThisFrame);

	// 3. longest-path levels over the condensed DAG (Kahn)
	TArray<TArray<int32>> Adj; Adj.SetNum(N);
	TArray<int32> InDeg; InDeg.Init(0, N);
	for (const FPixelSortEdge& E : Edges)
	{
		if (E.bCut || E.bMerged) continue;
		const int32 A = Find(E.Back), B = Find(E.Front);
		if (A == B || Adj[A].Contains(B)) continue;
		Adj[A].Add(B); ++InDeg[B];
	}
	TArray<int32> Level; Level.Init(0, N);
	TArray<int32> Queue;
	for (int32 V = 0; V < N; ++V) { if (Find(V) == V && InDeg[V] == 0) Queue.Add(V); }
	for (int32 Q = 0; Q < Queue.Num(); ++Q)
	{
		const int32 U = Queue[Q];
		for (int32 V : Adj[U]) { Level[V] = FMath::Max(Level[V], Level[U] + 1); if (--InDeg[V] == 0) Queue.Add(V); }
	}
	int32 MaxLevel = 0;
	for (int32 I = 0; I < N; ++I)
	{
		Sprites[I].Node = Find(I);
		Sprites[I].Level = Level[Sprites[I].Node];
		MaxLevel = FMath::Max(MaxLevel, Sprites[I].Level);
	}
	SortStats.Levels = MaxLevel + 1;
	SortStats.Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
}

void UPixelArtSubsystem::ApplySortToPrims()
{
	const FVector F = Camera.Forward;
	const double CamDepth = FVector::DotProduct(Camera.Position, F);
	const float K = FMath::Clamp(Settings.FlattenK, 0.005f, 1.f);

	// Slot depths: level 0 sits farthest, each level gets enough thickness for its widest member.
	// Merge groups keep their true internal depth, so their slot is as thick as the group.
	TArray<double> GroupMin, GroupMax; GroupMin.Init(DBL_MAX, Sprites.Num()); GroupMax.Init(-DBL_MAX, Sprites.Num());
	int32 MaxLevel = 0;
	for (const FPixelSprite& S : Sprites)
	{
		if (S.Node == INDEX_NONE) continue;
		for (int32 I = 0; I < 8; ++I)
		{
			const FVector C(I & 1 ? S.Box.Max.X : S.Box.Min.X, I & 2 ? S.Box.Max.Y : S.Box.Min.Y, I & 4 ? S.Box.Max.Z : S.Box.Min.Z);
			const double D = FVector::DotProduct(C, F);
			GroupMin[S.Node] = FMath::Min(GroupMin[S.Node], D); GroupMax[S.Node] = FMath::Max(GroupMax[S.Node], D);
		}
		MaxLevel = FMath::Max(MaxLevel, S.Level);
	}
	TArray<int32> GroupSize; GroupSize.Init(0, Sprites.Num());
	for (const FPixelSprite& S : Sprites) { if (S.Node != INDEX_NONE) ++GroupSize[S.Node]; }
	TArray<double> Thick; Thick.Init(30.0, MaxLevel + 1);
	for (const FPixelSprite& S : Sprites)
	{
		if (S.Node != INDEX_NONE && GroupSize[S.Node] > 1)
		{
			Thick[S.Level] = FMath::Max(Thick[S.Level], GroupMax[S.Node] - GroupMin[S.Node] + 30.0);
		}
	}
	TArray<double> Slot; Slot.SetNum(MaxLevel + 1);
	double Cursor = CamDepth + 3200.0;
	for (int32 L = 0; L <= MaxLevel; ++L) { Slot[L] = Cursor - Thick[L] * 0.5; Cursor -= Thick[L]; }

	for (FPixelSprite& S : Sprites)
	{
		const double PivotDepth = FVector::DotProduct(S.Pivot, F);
		float Enable = 0.f, SlotDepth = PivotDepth, PivotUsed = PivotDepth, Keep = 1.f;
		if (Settings.SortMode == EPixelSortMode::CharactersOnly && S.bCharacter)
		{
			Enable = 1.f; SlotDepth = PivotDepth; Keep = K;
		}
		else if (Settings.SortMode == EPixelSortMode::Full && S.Node != INDEX_NONE)
		{
			Enable = 1.f;
			SlotDepth = Slot[S.Level];
			if (GroupSize[S.Node] > 1) { PivotUsed = 0.5 * (GroupMin[S.Node] + GroupMax[S.Node]); Keep = 1.f; }
			else { Keep = K; }
		}
		for (const TWeakObjectPtr<UPrimitiveComponent>& P : S.Prims)
		{
			if (!P.IsValid()) continue;
			P->SetCustomPrimitiveDataFloat(CpdSlot, SlotDepth);
			P->SetCustomPrimitiveDataFloat(CpdPivot, PivotUsed);
			P->SetCustomPrimitiveDataFloat(CpdKeep, Keep);
			P->SetCustomPrimitiveDataFloat(CpdEnable, Enable);
			P->SetCustomPrimitiveDataFloat(CpdPivotX, S.Pivot.X);
			P->SetCustomPrimitiveDataFloat(CpdPivotY, S.Pivot.Y);
			P->SetCustomPrimitiveDataFloat(CpdPivotZ, S.Pivot.Z);
			P->SetCustomPrimitiveDataFloat(CpdLevel, Settings.SortMode == EPixelSortMode::Full ? S.Level : 0);
		}
	}
}

void UPixelArtSubsystem::UpdateShadowCapture()
{
	UWorld* World = GetWorld();
	if (!World) return;
	if (!ShadowCapture)
	{
		FActorSpawnParameters P; P.ObjectFlags |= RF_Transient;
		ShadowCapture = World->SpawnActor<ASceneCapture2D>(P);
		USceneCaptureComponent2D* C = ShadowCapture->GetCaptureComponent2D();
		C->ProjectionType = ECameraProjectionMode::Orthographic;
		C->OrthoWidth = ShadowExtent;
		C->CaptureSource = SCS_SceneDepth;
		C->TextureTarget = ShadowTarget;
		C->bCaptureEveryFrame = false;
		C->bCaptureOnMovement = false;
		C->bAutoCalculateOrthoPlanes = false;
		C->bAlwaysPersistRenderingState = true;
		C->ShowFlags.SetPostProcessing(false);
		C->ShowFlags.SetAtmosphere(false);
		C->ShowFlags.SetFog(false);
	}
	const FRotator Rot(LightPitchDeg, LightYawDeg, 0.f);
	ShadowForward = Rot.Vector();
	ShadowRight = FRotationMatrix(Rot).GetScaledAxis(EAxis::Y);
	ShadowUp = FRotationMatrix(Rot).GetScaledAxis(EAxis::Z);
	ShadowOrigin = FVector(0, 0, 100) - ShadowForward * 8000.0;
	ShadowCapture->SetActorLocationAndRotation(ShadowOrigin, Rot);
	// Capture explicitly, after the light basis for this frame is final: a deferred every-frame capture
	// can lag one frame behind the material parameters and shift every shadow lookup while the light moves.
	if (Settings.bShadows) ShadowCapture->GetCaptureComponent2D()->CaptureScene();
}

void UPixelArtSubsystem::UpdateMaterialParameters()
{
	UWorld* World = GetWorld();
	if (!Collection || !World) return;
	UMaterialParameterCollectionInstance* Inst = World->GetParameterCollectionInstance(Collection);
	if (!Inst) return;
	auto V = [](const FVector& X, float W = 0.f) { return FLinearColor(float(X.X), float(X.Y), float(X.Z), W); };
	Inst->SetVectorParameterValue(TEXT("CamForward"), V(Camera.Forward));
	Inst->SetVectorParameterValue(TEXT("CamRight"), V(Camera.Right));
	Inst->SetVectorParameterValue(TEXT("CamUp"), V(Camera.Up));
	Inst->SetVectorParameterValue(TEXT("CamPos"), V(Camera.Position, float(Camera.Texel)));
	Inst->SetVectorParameterValue(TEXT("LightDir"), V(ShadowForward));
	Inst->SetVectorParameterValue(TEXT("LightRight"), V(ShadowRight));
	Inst->SetVectorParameterValue(TEXT("LightUp"), V(ShadowUp));
	Inst->SetVectorParameterValue(TEXT("LightOrigin"), V(ShadowOrigin, ShadowExtent));
	Inst->SetVectorParameterValue(TEXT("Misc0"), FLinearColor(Settings.ToonSteps, float(uint8(Settings.ViewMode)), Settings.bTrueLightPosition ? 1.f : 0.f, Settings.bShadows ? 1.f : 0.f));
	Inst->SetVectorParameterValue(TEXT("Misc1"), FLinearColor(float(uint8(Settings.DitherMode)), float(Camera.Texel), 6.f, World->GetTimeSeconds()));
	Inst->SetVectorParameterValue(TEXT("Misc2"), FLinearColor(float(uint8(Settings.OutlineMode)), Settings.bLutCleanup ? 1.f : 0.f, FVector::DotProduct(Camera.Position, Camera.Forward), 0.f));
}

void UPixelArtSubsystem::MeasureFlicker()
{
	// Reads the displayed low-res texels back from the GPU (before upscaling) and compares each new frame
	// with the previous one after the best integer shift, exactly like the measurement in the report (ch.03).
	PixelArtRender::SetTexelReadback(bMeasureFlicker && Settings.bLowRes);
	if (!bMeasureFlicker || !Settings.bLowRes) { FlickerPercent = -1.f; PrevFrame.Reset(); FlickerHistory.Reset(); return; }
	TArray<FColor> Cur; FIntPoint Inner;
	if (!PixelArtRender::FetchTexels(Cur, Inner)) return;
	if (PrevFrame.Num() == Cur.Num() && PrevSize == Inner)
	{
		double Best = 1.0;
		for (int32 DY = -2; DY <= 2; ++DY)
		{
			for (int32 DX = -2; DX <= 2; ++DX)
			{
				int64 Diff = 0, Tot = 0;
				for (int32 Y = 3; Y < Inner.Y - 3; ++Y)
				{
					for (int32 X = 3; X < Inner.X - 3; ++X)
					{
						const FColor& A = Cur[Y * Inner.X + X];
						const FColor& B = PrevFrame[(Y + DY) * Inner.X + (X + DX)];
						++Tot;
						if (FMath::Abs(A.R - B.R) + FMath::Abs(A.G - B.G) + FMath::Abs(A.B - B.B) > 24) ++Diff;
					}
				}
				Best = FMath::Min(Best, Tot ? double(Diff) / Tot : 1.0);
			}
		}
		FlickerHistory.Add(float(Best * 100.0));
		if (FlickerHistory.Num() > 20) FlickerHistory.RemoveAt(0);
		float Sum = 0; for (float F : FlickerHistory) Sum += F;
		FlickerPercent = Sum / FlickerHistory.Num();
	}
	PrevFrame = MoveTemp(Cur);
	PrevSize = Inner;
}

void UPixelArtSubsystem::Tick(float DeltaTime)
{
	FPixelArtFrame Frame = PixelArtRender::GetFrame();
	Frame.bEnabled = Settings.bLowRes;
	Frame.Scale = Settings.PixelScale;
	Frame.Filter = Settings.bBilinear ? EPixelArtUpscale::Bilinear : EPixelArtUpscale::Nearest;
	Frame.bShowGrid = Settings.bShowGrid;
	PixelArtRender::SetFrame(Frame); // SubTexel is owned by the camera manager

	UpdateShadowCapture();
	UpdateSort();
	ApplySortToPrims();
	UpdateMaterialParameters();
	MeasureFlicker();

	if (Settings.bShowSortVolumes)
	{
		for (const FPixelSprite& S : Sprites)
		{
			const FColor C = S.bDynamic ? FColor(255, 160, 40) : FColor(80, 200, 220);
			DrawDebugBox(GetWorld(), S.Box.GetCenter(), S.Box.GetExtent(), C, false, -1.f, SDPG_Foreground, 1.5f);
		}
	}
}
