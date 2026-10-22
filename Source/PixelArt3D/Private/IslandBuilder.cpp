#include "IslandBuilder.h"

#include "PixelArtSubsystem.h"

#include "Animation/SkeletalMeshActor.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "PixelAnimInstance.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace Tiles
{
	// Atlas layout written by Tools/gen_atlas.py (16 tiles of 16x16 texels).
	enum : int32 { Flat = 0, Grass = 1, Dirt = 2, Brick = 3, Pillar = 4, Wood = 5, Water = 6, Sand = 7, Leaves = 8, Roof = 9, Metal = 10, Cobble = 11, GrassSide = 12, DarkStone = 13, Flowers = 14, Checker = 15 };
}

AIslandBuilder::AIslandBuilder()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Cube = ConstructorHelpers::FObjectFinder<UStaticMesh>(TEXT("/Engine/BasicShapes/Cube.Cube")).Object;
	Cylinder = ConstructorHelpers::FObjectFinder<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")).Object;
}

void AIslandBuilder::BeginPlay()
{
	Super::BeginPlay();
	BuildTerrain();
	BuildStations();
}

AActor* AIslandBuilder::SpawnBlock(const FVector& Center, const FVector& SizeCm, int32 TileTop, int32 TileSide, FLinearColor Tint, bool bSprite, FName Label, UStaticMesh* Mesh, float Density)
{
	UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(this);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(Center, FRotator::ZeroRotator, P);
	A->SetMobility(EComponentMobility::Movable);
	UStaticMeshComponent* C = A->GetStaticMeshComponent();
	C->SetStaticMesh(Mesh ? Mesh : Cube.Get());
	C->SetWorldScale3D(SizeCm / 100.f);
	C->SetCastShadow(false);
	if (Sub)
	{
		UMaterialInstanceDynamic* Mid = Sub->MakeSurfaceMaterial(A, TileTop, Tint, TileTop != Tiles::Flat);
		Mid->SetScalarParameterValue(TEXT("TileSide"), TileSide);
		Mid->SetScalarParameterValue(TEXT("Density"), Density);
		C->SetMaterial(0, Mid);
		if (bSprite) Sub->RegisterSprite(A, false, false, Label);
		else Sub->RegisterTerrain(C);
	}
	return A;
}

AActor* AIslandBuilder::SpawnProp(const TCHAR* Folder, const TCHAR* Name, const FVector& At, float Yaw, FName Label, bool bCollide, float Scale)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/KayKit/%s/%s.%s"), Folder, Name, Name));
	if (!Mesh) { UE_LOG(LogTemp, Warning, TEXT("Missing KayKit prop %s/%s"), Folder, Name); return nullptr; }
	UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(this);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(At, FRotator(0, Yaw, 0), P);
	A->SetMobility(EComponentMobility::Movable);
	UStaticMeshComponent* C = A->GetStaticMeshComponent();
	C->SetStaticMesh(Mesh);
	C->SetWorldScale3D(FVector(Scale));
	C->SetCastShadow(false);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (bCollide)
	{
		// Imported without collision; a box from the bounds keeps the player out of the prop.
		const FBox B = Mesh->GetBoundingBox();
		UBoxComponent* Box = NewObject<UBoxComponent>(A);
		Box->SetupAttachment(C);
		Box->SetBoxExtent(B.GetExtent() * 0.85f);
		Box->SetRelativeLocation(B.GetCenter());
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Box->RegisterComponent();
	}
	if (Sub)
	{
		const TCHAR* Tex = FCString::Strcmp(Folder, TEXT("Halloween")) == 0 ? TEXT("halloweenbits_texture") : TEXT("knight_texture");
		Sub->ApplyMeshMaterial(C, LoadObject<UTexture>(nullptr, *FString::Printf(TEXT("/Game/KayKit/Textures/%s.%s"), Tex, Tex)));
		if (Label.IsNone()) Sub->RegisterTerrain(C);
		else Sub->RegisterSprite(A, false, false, Label);
	}
	return A;
}

AActor* AIslandBuilder::SpawnNpc(const TCHAR* Name, const TCHAR* Texture, const FVector& At, float Yaw)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/KayKit/Characters/%s.%s"), Name, Name));
	if (!Mesh) return nullptr;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Meshes face +Y; -90 turns that onto the actor's +X, the same convention as the player.
	ASkeletalMeshActor* A = GetWorld()->SpawnActor<ASkeletalMeshActor>(At, FRotator(0, Yaw - 90.f, 0), P);
	USkeletalMeshComponent* C = A->GetSkeletalMeshComponent();
	C->SetMobility(EComponentMobility::Movable);
	C->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	C->SetAnimInstanceClass(UPixelAnimInstance::StaticClass());
	C->SetSkeletalMesh(Mesh);
	C->SetCastShadow(false);
	C->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	if (UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(this))
	{
		Sub->ApplyMeshMaterial(C, LoadObject<UTexture>(nullptr, *FString::Printf(TEXT("/Game/KayKit/Textures/%s.%s"), Texture, Texture)));
		Sub->RegisterSprite(A, true, true, FName(Name));
	}
	return A;
}

void AIslandBuilder::AddGround(const TCHAR* Name, const FVector& At, float Yaw, float Scale)
{
	TObjectPtr<UInstancedStaticMeshComponent>& Ism = Ground.FindOrAdd(FName(Name));
	if (!Ism)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/KayKit/Halloween/%s.%s"), Name, Name));
		if (!Mesh) { UE_LOG(LogTemp, Warning, TEXT("Missing KayKit ground %s"), Name); return; }
		Ism = NewObject<UInstancedStaticMeshComponent>(this, FName(FString(TEXT("Ground_")) + Name));
		Ism->SetupAttachment(RootComponent);
		Ism->SetStaticMesh(Mesh);
		Ism->SetMobility(EComponentMobility::Static);
		Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ism->SetCastShadow(false);
		Ism->RegisterComponent();
		if (UPixelArtSubsystem* Sub = UPixelArtSubsystem::Get(this))
		{
			Sub->ApplyMeshMaterial(Ism, LoadObject<UTexture>(nullptr, TEXT("/Game/KayKit/Textures/halloweenbits_texture.halloweenbits_texture")));
			Sub->RegisterTerrain(Ism);
		}
	}
	Ism->AddInstance(FTransform(FRotator(0, Yaw, 0), At, FVector(Scale)), true);
}

void AIslandBuilder::LayPath(const FVector& A, const FVector& B, float StartGap, float EndGap, bool bWide)
{
	static const TCHAR* Stones[] = { TEXT("path_A"), TEXT("path_B"), TEXT("path_C"), TEXT("path_D") };
	const FVector D = (B - A).GetSafeNormal2D();
	const FVector Side(-D.Y, D.X, 0);
	const float Len = FVector::Dist2D(A, B);
	int32 N = int32(FMath::Abs(A.X + A.Y)) & 7;
	for (float T = StartGap; T < Len - EndGap; T += 70.f)
	{
		for (int32 Lane = 0; Lane < (bWide ? 2 : 1); ++Lane)
		{
			// jittered, randomly turned stones read as hand-laid cobbles
			++N;
			const float Off = (bWide ? (Lane == 0 ? -42.f : 42.f) : 0.f) + float((N * 37) % 9 - 4) * 3.f;
			const FVector At = A + D * (T + float((N * 17) % 7 - 3) * 3.f) + Side * Off;
			AddGround(Stones[(N * 7) & 3], FVector(At.X, At.Y, 2.2f), float((N * 53) % 360), 0.75f); // tile top is at +1.9 cm
		}
	}
}

void AIslandBuilder::BuildTerrain()
{
	// The map is laid from the pack's own floor tiles (2.52 m), like its sample diorama: a full grid
	// you can walk on, a lower ragged skirt of small tiles around it, and a dark backdrop below.
	constexpr float Tile = 252.f;
	constexpr int32 NX = 10, NY = 12;
	const FVector Origin(-Tile * NX * 0.5f + Tile * 0.5f, -Tile * NY * 0.5f + Tile * 0.5f, 0.f);
	auto Hash = [](int32 A, int32 B) { return (uint32(A * 73856093) ^ uint32(B * 19349663)) * 2654435761u; };

	SpawnBlock(FVector(0, 0, -420), FVector(14000, 14000, 20), Tiles::Flat, Tiles::Flat, FLinearColor(0.045f, 0.05f, 0.065f), false, NAME_None);

	const FIntPoint GraveTile(2, 11); // an open grave in the graveyard corner
	for (int32 I = 0; I < NX; ++I)
	{
		for (int32 J = 0; J < NY; ++J)
		{
			const FVector At = Origin + FVector(I * Tile, J * Tile, 0);
			const float Yaw = float(Hash(I, J) % 4) * 90.f;
			AddGround(FIntPoint(I, J) == GraveTile ? TEXT("floor_dirt_grave") : TEXT("floor_dirt"), At, Yaw);
		}
	}
	// skirt: small tiles one step lower, with gaps, around the whole grid
	for (int32 I = -1; I <= NX; ++I)
	{
		for (int32 J = -1; J <= NY; ++J)
		{
			if (I >= 0 && I < NX && J >= 0 && J < NY) continue;
			const FVector Cell = Origin + FVector(I * Tile, J * Tile, 0);
			for (int32 K = 0; K < 4; ++K)
			{
				// keep only the quarter-tiles that touch the grid, and drop some of them
				const FVector Q = Cell + FVector((K & 1) ? 63.f : -63.f, (K & 2) ? 63.f : -63.f, -14.f);
				const bool bTouches = FMath::Abs(Q.X) < Tile * NX * 0.5f + 70.f && FMath::Abs(Q.Y) < Tile * NY * 0.5f + 70.f;
				if (!bTouches || Hash(I * 4 + K, J) % 5 < 2) continue;
				AddGround(TEXT("floor_dirt_small"), Q, float(Hash(J, K) % 4) * 90.f);
			}
		}
	}

	// One box for walking on the grid; the character does not walk off ledges, so the skirt is a rim.
	UBoxComponent* Floor = NewObject<UBoxComponent>(this, TEXT("FloorCollision"));
	Floor->SetupAttachment(RootComponent);
	Floor->SetBoxExtent(FVector(Tile * NX * 0.5f, Tile * NY * 0.5f, 50.f));
	Floor->SetRelativeLocation(FVector(0, 0, 1.9f - 50.f));
	Floor->SetCollisionProfileName(TEXT("BlockAll"));
	Floor->RegisterComponent();
	UBoxComponent* Hole = NewObject<UBoxComponent>(this, TEXT("GraveCollision"));
	Hole->SetupAttachment(RootComponent);
	Hole->SetBoxExtent(FVector(110.f, 110.f, 60.f));
	Hole->SetRelativeLocation(Origin + FVector(GraveTile.X * Tile, GraveTile.Y * Tile, 60.f));
	Hole->SetCollisionProfileName(TEXT("BlockAll"));
	Hole->RegisterComponent();
}

void AIslandBuilder::BuildStations()
{
	const FLinearColor White = FLinearColor::White;
	auto Pad = [this](const FVector& At, FLinearColor C)
	{
		SpawnBlock(At + FVector(0, 0, 2), FVector(180, 180, 6), Tiles::Flat, Tiles::Flat, C, false, NAME_None, Cylinder);
	};

	// 1 -- resolution & texel density: three plates textured at 8, 16 and 32 texels per metre.
	{
		FPixelStation S; S.Index = 1; S.Pad = FVector(-750, -1050, 0); S.Focus = FVector(-950, -1050, 0);
		S.Title = TEXT("① 分辨率与 texel 密度");
		S.Body = TEXT("三块石板的贴图密度分别是 8、16、32 texel/米，屏幕是 16 像素/米。只有中间那块一个 texel 恰好占一个像素：左块被放大成色块，右块的细节互相抢像素。\n[1] 开关低分辨率渲染　[B] 最近邻 / 双线性放大　[G] 显示像素格　[Z][X] 调整像素尺寸");
		Stations.Add(S);
		Pad(S.Pad, FLinearColor(0.85f, 0.75f, 0.35f));
		const float Dens[3] = { 0.5f, 1.f, 2.f };
		for (int32 I = 0; I < 3; ++I)
		{
			SpawnBlock(FVector(-1050, -1350 + I * 300, 60), FVector(200, 200, 120), Tiles::Checker, Tiles::Brick, White, true, TEXT("DensityPlate"), nullptr, Dens[I]);
		}
		SpawnNpc(TEXT("Barbarian"), TEXT("barbarian_texture"), FVector(-820, -1330, 0), 180.f);
	}

	// 2 -- panning runway: a picket fence with thin posts and diagonal rails crawls visibly when unsnapped.
	{
		FPixelStation S; S.Index = 2; S.Pad = FVector(-750, 0, 0); S.Focus = FVector(-950, 0, 0);
		S.Title = TEXT("② 平移跑道：像素吸附");
		S.Body = TEXT("站在踏板上，镜头会慢速平移。盯住铁栅栏的细栏杆和尖刺，看右上角的闪烁率。\n[2] 关闭相机吸附：边缘像素交替明灭（像素爬行），闪烁率升高　[3] 亚像素补偿：保留吸附，同时让平移恢复平滑　[4] 物体与骨骼吸附\n[J] 相机跟随：锁定像素（人物静止、世界整格移动）/ 平滑 + 亚像素（世界平滑、人物晃半格）/ 插值滞后（两者错拍，最晕）");
		Stations.Add(S);
		Pad(S.Pad, FLinearColor(0.35f, 0.75f, 0.85f));
		// wrought-iron fence: thin bars and spikes are the worst case for crawling edges
		for (int32 I = -1; I <= 1; ++I)
		{
			SpawnProp(TEXT("Halloween"), TEXT("fence"), FVector(-1100, I * 252, 0), 90.f, TEXT("Fence"));
		}
		for (int32 I = 0; I < 4; ++I)
		{
			SpawnProp(TEXT("Halloween"), TEXT("fence_pillar"), FVector(-1100, -378 + I * 252, 0), 90.f, TEXT("FencePillar"));
		}
		SpawnNpc(TEXT("Ranger"), TEXT("ranger_texture"), FVector(-880, -330, 0), 180.f);
	}

	// 3 -- crypt wall & spear: the soldier's spear pokes into the crypt's long side when walking along it.
	{
		FPixelStation S; S.Index = 3; S.Pad = FVector(520, -800, 0); S.Focus = FVector(640, -800, 0);
		S.Title = TEXT("③ 墓室与长矛：精灵排序");
		S.Body = TEXT("贴着墓室侧墙左右走。[6] 循环排序模式：关闭时长矛插进墙里被截断；角色深度扁平化或全量排序时，士兵整体压在墙前。\n[F1] 切到「写入深度」视图对比排序前后的深度　[8] 光照改用推移后的位置：阴影随排序漂移，这正是 UE 延迟光照会出错的原因");
		Stations.Add(S);
		Pad(S.Pad, FLinearColor(0.85f, 0.45f, 0.35f));
		SpawnProp(TEXT("Halloween"), TEXT("crypt"), FVector(850, -800, 0), 0.f, TEXT("Crypt"));
		SpawnProp(TEXT("Halloween"), TEXT("lantern_standing"), FVector(600, -480, 0), 0.f, TEXT("Lantern"));
		SpawnProp(TEXT("Halloween"), TEXT("gravestone"), FVector(620, -1150, 0), 90.f, TEXT("Gravestone"));
	}

	// 4 -- cyclic overlap: three beams that pairwise cover each other on screen but never touch in 3D.
	{
		FPixelStation S; S.Index = 4; S.Pad = FVector(300, 760, 0); S.Focus = FVector(640, 690, 60);
		S.Title = TEXT("④ 三杆成环：循环遮挡");
		S.Body = TEXT("站上踏板后相机转到 45° 斜视。三根横梁在屏幕上首尾相压，任何线性顺序都至少画错一处。[7] 切换策略：最小重叠（讲座）/ 互不相交时合并层（改进 1）/ 滞回（改进 2）。\n[E] 推动红色横梁改变重叠面积　[V] 显示前后关系图（红色为被切掉的边）　[F1] 物体 ID / 排序层级视图");
		Stations.Add(S);
		Pad(S.Pad, FLinearColor(0.55f, 0.85f, 0.45f));
		CycleBeamBase = FVector(620, 650, 0);
		// Arranged so that A is above B, B is nearer than C and C is nearer than A (see Tools/cycle_layout.py).
		// Woven under the 45-degree view this exhibit switches to (Tools/cycle_layout.py 45): A covers B, C covers A,
		// B covers C, and every pair has a real separating plane, so per-pixel depth draws all three correctly.
		CycleBeams.Add(SpawnBlock(CycleBeamBase + FVector(41, 16, 40), FVector(340, 40, 30), Tiles::Flat, Tiles::Flat, FLinearColor(0.8f, 0.25f, 0.2f), true, TEXT("BeamA")));
		CycleBeams.Add(SpawnBlock(CycleBeamBase + FVector(-29, 119, 123), FVector(340, 30, 30), Tiles::Flat, Tiles::Flat, FLinearColor(0.25f, 0.45f, 0.8f), true, TEXT("BeamB")));
		CycleBeams.Add(SpawnBlock(CycleBeamBase + FVector(126, 27, 80), FVector(30, 340, 30), Tiles::Flat, Tiles::Flat, FLinearColor(0.95f, 0.75f, 0.2f), true, TEXT("BeamC")));
		// supports so the beams read as a structure
		SpawnBlock(CycleBeamBase + FVector(-110, 16, 12), FVector(24, 24, 24), Tiles::Wood, Tiles::Wood, White, true, TEXT("Support"));
		SpawnBlock(CycleBeamBase + FVector(126, -120, 32), FVector(24, 24, 64), Tiles::Wood, Tiles::Wood, White, true, TEXT("Support"));
		SpawnBlock(CycleBeamBase + FVector(-180, 119, 54), FVector(24, 24, 108), Tiles::Wood, Tiles::Wood, White, true, TEXT("Support"));
		SpawnNpc(TEXT("Rogue_Hooded"), TEXT("rogue_texture"), FVector(430, 960, 0), 200.f);
	}

	// 5 -- outlines, palette, dither: a gallery of props with many silhouettes and creases.
	{
		FPixelStation S; S.Index = 5; S.Pad = FVector(-700, 1000, 0); S.Focus = FVector(-900, 1000, 40);
		S.Title = TEXT("⑤ 描边、色阶与抖动");
		S.Body = TEXT("[9] 循环描边归属：归前方物体（讲座做法）/ 归后方 / 两侧都画，注意前后关系是否读反。[0] 开关查表清理：去掉 L 形拐角与孤立像素。\n[P] 循环明暗交界抖动：只在 toon 色阶的边界附近用 4×4 Bayer 图案过渡。屏幕空间图案会在走动的士兵身上滑动，锚在物体上的图案跟着物体走　[T] 循环 toon 色阶数（平滑时没有交界可抖）");
		Stations.Add(S);
		Pad(S.Pad, FLinearColor(0.75f, 0.5f, 0.85f));
		// many silhouettes, overlaps and creases for the outline / dither rules
		SpawnProp(TEXT("Halloween"), TEXT("coffin_decorated"), FVector(-1060, 860, 0), 90.f, TEXT("Coffin"));
		SpawnProp(TEXT("Halloween"), TEXT("gravestone"), FVector(-1100, 1040, 0), 90.f, TEXT("Gravestone"));
		SpawnProp(TEXT("Halloween"), TEXT("grave_A"), FVector(-1080, 1200, 0), 90.f, TEXT("Grave"));
		SpawnProp(TEXT("Halloween"), TEXT("shrine_candles"), FVector(-900, 1230, 0), 90.f, TEXT("Shrine"));
		SpawnProp(TEXT("Halloween"), TEXT("pumpkin_orange_jackolantern"), FVector(-930, 830, 0), 60.f, TEXT("Pumpkin"));
		SpawnProp(TEXT("Halloween"), TEXT("pumpkin_yellow_small"), FVector(-870, 900, 0), 20.f, TEXT("Pumpkin"), false);
		SpawnProp(TEXT("Halloween"), TEXT("lantern_standing"), FVector(-960, 1110, 0), 0.f, TEXT("Lantern"));
		SpawnProp(TEXT("Halloween"), TEXT("skull"), FVector(-850, 1120, 0), 70.f, TEXT("Skull"), false, 0.6f);
		SpawnNpc(TEXT("Mage"), TEXT("mage_texture"), FVector(-820, 1290, 0), 200.f);

		// graveyard enclosure: iron fence along its front and side, an arch where the path enters
		for (const float X : { -1134.f, -882.f }) SpawnProp(TEXT("Halloween"), TEXT("fence"), FVector(X, 504, 0), 0.f, TEXT("Fence"));
		for (const float X : { -1260.f, -1008.f, -756.f, -652.f }) SpawnProp(TEXT("Halloween"), TEXT("fence_pillar"), FVector(X, 504, 0), 0.f, TEXT("FencePillar"));
		SpawnProp(TEXT("Halloween"), TEXT("arch"), FVector(-504, 504, 0), 0.f, TEXT("Arch"));
		SpawnProp(TEXT("Halloween"), TEXT("fence"), FVector(-315, 504, 0), 0.f, TEXT("Fence"), true, 0.5f);
		for (const float Y : { 630.f, 882.f, 1134.f, 1386.f }) SpawnProp(TEXT("Halloween"), TEXT("fence"), FVector(-252, Y, 0), 90.f, TEXT("Fence"));
		for (const float Y : { 504.f, 756.f, 1008.f, 1260.f, 1512.f }) SpawnProp(TEXT("Halloween"), TEXT("fence_pillar"), FVector(-252, Y, 0), 0.f, TEXT("FencePillar"));
		// a row of graves along the side fence, the open grave and its coffin
		const TCHAR* Graves[] = { TEXT("grave_A"), TEXT("gravemarker_A"), TEXT("grave_B"), TEXT("grave_A_destroyed") };
		for (int32 I = 0; I < 4; ++I) SpawnProp(TEXT("Halloween"), Graves[I], FVector(-390, 760 + I * 190, 0), 90.f, TEXT("Grave"));
		SpawnProp(TEXT("Halloween"), TEXT("coffin"), FVector(-470, 1420, 0), 90.f, TEXT("Coffin"));
		SpawnProp(TEXT("Halloween"), TEXT("skull"), FVector(-760, 1440, 0), 30.f, TEXT("Skull"), false, 0.5f);
		SpawnProp(TEXT("Halloween"), TEXT("lantern_standing"), FVector(-640, 1250, 0), 0.f, TEXT("Lantern"));
	}

	// 6 -- turntable & light: facing quantization, stepped animation, toon ramp and the self-built shadow map.
	{
		FPixelStation S; S.Index = 6; S.Pad = FVector(150, -250, 0); S.Focus = FVector(150, -250, 60);
		S.Title = TEXT("⑥ 转台：动画与光照");
		S.Body = TEXT("站上转台，士兵会原地旋转。[5] 开关朝向量化与阶跃关键帧：关闭时转身和步伐连续，开启后只剩 8 个朝向、每秒 8 帧。[4] 开关骨骼吸附。\n[ 和 ] 旋转光源：明暗按 toon 色阶分档，阴影来自正交 SceneCapture 自建的深度图　[T] 循环色阶数");
		Stations.Add(S);
		Pad(S.Pad, FLinearColor(0.9f, 0.9f, 0.9f));
	}

	// Scenery: pines along the back and the corners, dead trees, small graveyard clutter.
	struct FTree { const TCHAR* Name; FVector At; };
	const FTree Trees[] = {
		{ TEXT("tree_pine_orange_large"), FVector(1120, 1300, 0) }, { TEXT("tree_pine_yellow_large"), FVector(1100, 760, 0) },
		{ TEXT("tree_pine_orange_medium"), FVector(1150, 250, 0) }, { TEXT("tree_pine_yellow_medium"), FVector(1150, -250, 0) },
		{ TEXT("tree_pine_orange_medium"), FVector(1150, -1350, 0) }, { TEXT("tree_pine_yellow_small"), FVector(900, -1400, 0) },
		{ TEXT("tree_pine_orange_small"), FVector(950, 1450, 0) }, { TEXT("tree_pine_yellow_small"), FVector(-1150, -1430, 0) },
		{ TEXT("tree_pine_orange_small"), FVector(-1150, 1440, 0) }, { TEXT("tree_pine_yellow_medium"), FVector(300, -1400, 0) },
		{ TEXT("tree_dead_large"), FVector(900, 300, 0) }, { TEXT("tree_dead_medium"), FVector(-1150, -650, 0) },
		{ TEXT("tree_dead_small"), FVector(250, 1420, 0) }, { TEXT("tree_dead_medium"), FVector(-300, -1350, 0) },
	};
	for (const FTree& T : Trees) SpawnProp(TEXT("Halloween"), T.Name, T.At, float(int32(T.At.Y) % 360), TEXT("Tree"));

	SpawnProp(TEXT("Halloween"), TEXT("shrine_candles"), FVector(1000, 1050, 0), 180.f, TEXT("Shrine"));
	SpawnProp(TEXT("Halloween"), TEXT("plaque_candles"), FVector(-420, 140, 0), 0.f, TEXT("Plaque"));
	SpawnProp(TEXT("Halloween"), TEXT("bench"), FVector(-330, -440, 0), 90.f, TEXT("Bench"));
	SpawnProp(TEXT("Halloween"), TEXT("bench"), FVector(40, 80, 0), 0.f, TEXT("Bench"));
	SpawnProp(TEXT("Halloween"), TEXT("post_lantern"), FVector(-640, -300, 0), 0.f, TEXT("Lantern"));
	SpawnProp(TEXT("Halloween"), TEXT("post_lantern"), FVector(300, -560, 0), 0.f, TEXT("Lantern"));
	SpawnProp(TEXT("Halloween"), TEXT("post_skull"), FVector(520, 1200, 0), 0.f, TEXT("Post"));
	SpawnProp(TEXT("Halloween"), TEXT("ribcage"), FVector(700, 1250, 0), 30.f, TEXT("Bones"), false);
	SpawnProp(TEXT("Halloween"), TEXT("candle_triple"), FVector(-480, 220, 0), 0.f, TEXT("Candle"), false);
	SpawnProp(TEXT("Halloween"), TEXT("pumpkin_orange_jackolantern"), FVector(-560, 380, 0), 30.f, TEXT("Pumpkin"));
	SpawnProp(TEXT("Halloween"), TEXT("pumpkin_orange_small"), FVector(-600, 300, 0), 10.f, TEXT("Pumpkin"), false);
	SpawnProp(TEXT("Halloween"), TEXT("pumpkin_yellow"), FVector(820, -350, 0), 50.f, TEXT("Pumpkin"), false);
	SpawnProp(TEXT("Halloween"), TEXT("pumpkin_yellow_small"), FVector(40, 1300, 0), 0.f, TEXT("Pumpkin"), false);
	SpawnProp(TEXT("Halloween"), TEXT("pumpkin_orange"), FVector(-1150, -200, 0), 0.f, TEXT("Pumpkin"), false);

	// Cobblestone paths: a wide avenue from the front edge through the plaza to the crypt, lanes to every pad.
	const FVector Hub(-150, -150, 0);
	LayPath(FVector(-1290, -150, 0), Hub, 0.f, 0.f, true);
	for (const FPixelStation& S : Stations)
	{
		if (S.Index == 5)
		{
			// into the graveyard through the arch
			LayPath(Hub, FVector(-504, 380, 0), 90.f, 0.f);
			LayPath(FVector(-504, 380, 0), FVector(-504, 640, 0), 0.f, 0.f);
			LayPath(FVector(-504, 640, 0), S.Pad, 0.f, S.Radius + 20.f);
		}
		else
		{
			LayPath(Hub, S.Pad, 90.f, S.Radius + 20.f, S.Index == 3);
		}
	}
}
