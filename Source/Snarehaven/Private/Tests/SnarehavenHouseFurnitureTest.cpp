// M18-03 2층 검사 — 저장된 `L_Ranch` 를 **실제로 불러와** 식탁 있는 집(집 A) 안을 잰다 (DESIGN 8.11 집 실내 표):
// (a) 집 안 가구 메시가 종류 7가지 이상(탁자·의자·보관함 + 화덕·상자 더미·등불·화분)이고, 전부 폰을 막는 충돌이 있다,
// (b) 캡슐(34·88)을 문 밖에서 탁자 앞까지 쓸어 막힘이 없다 (가구가 문→탁자 길을 막지 않는다).
// 가구는 `tools/unreal/setup_ranch_buildings.py` 가 세운다. 맵은 `SnarehavenShopPierLevelTest.cpp` 처럼 검사 월드에 스트리밍한다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.World.HouseFurniture; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Snarehaven.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerStart.h"
#include "World/SnareHouseRoof.h"
#include "World/ZonePortalActor.h"

namespace SnarehavenHouseFurnitureTest
{
	// 테스트 전용 값 (Verification.md — 설계 숫자가 아니다). 종류 수는 ROADMAP M18-03 합격선(기존 3 + 4가지 이상).
	constexpr int32 MinFurnitureKinds = 7;
	// 플레이어 캡슐 — `SnarehavenShopPierLevelTest.cpp` 가 실제 캡슐과 대조하는 값과 같다.
	constexpr float CapsuleRadiusCm = 34.f;
	constexpr float CapsuleHalfHeightCm = 88.f;
	// 캡슐 바닥을 마루(+2) 위로 띄우는 여유, 문 밖 출발점·탁자 앞 멈춤 여유.
	constexpr float FloorClearanceCm = 6.f;
	constexpr float OutsideDoorCm = 80.f;
	constexpr float TableGapCm = 10.f;
	const TCHAR* HouseMeshName = TEXT("SM_Building_Cottage_01");
	const TCHAR* RoofMeshName = TEXT("SM_Building_CottageRoof_01");
	const TCHAR* TableMeshName = TEXT("SM_Furn_TableWood_01");

	UWorld* CreateWorldWithLevel(const TCHAR* LevelPackage, ULevel*& OutLevel)
	{
		UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		bool bSuccess = false;
		const ULevelStreamingDynamic* const Streaming = ULevelStreamingDynamic::LoadLevelInstance(
			World, LevelPackage, FVector::ZeroVector, FRotator::ZeroRotator, bSuccess);
		World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
		OutLevel = bSuccess && Streaming != nullptr ? Streaming->GetLoadedLevel() : nullptr;
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	FString MeshNameOf(const AStaticMeshActor* Actor)
	{
		const UStaticMeshComponent* const Component = Actor->GetStaticMeshComponent();
		const UStaticMesh* const Mesh = Component != nullptr ? Component->GetStaticMesh() : nullptr;
		return Mesh != nullptr ? Mesh->GetName() : FString();
	}

	/** 레벨의 집 하나(몸통·덮개)와 그 안 가구들. 「안」은 게임이 덮개를 숨기는 판정(`SnareHouseRoof::IsUnderRoof`) 그대로다. */
	struct FHouse
	{
		const AStaticMeshActor* Body = nullptr;
		FBox Bounds = FBox(ForceInit);
		FBox RoofBounds = FBox(ForceInit);
		TArray<const AStaticMeshActor*> Furniture;
		const AStaticMeshActor* Table = nullptr;
	};

	TArray<FHouse> CollectHouses(const ULevel* Level)
	{
		TArray<FHouse> Houses;
		TArray<const AStaticMeshActor*> Pieces;
		TArray<FBox> Roofs;
		for (const AActor* const Actor : Level->Actors)
		{
			const AStaticMeshActor* const MeshActor = Cast<AStaticMeshActor>(Actor);
			if (MeshActor == nullptr)
			{
				continue;
			}
			const FString MeshName = MeshNameOf(MeshActor);
			if (MeshName == HouseMeshName)
			{
				FHouse& House = Houses.AddDefaulted_GetRef();
				House.Body = MeshActor;
				House.Bounds = MeshActor->GetComponentsBoundingBox();
			}
			else if (MeshName == RoofMeshName)
			{
				Roofs.Add(MeshActor->GetComponentsBoundingBox());
			}
			else if (!MeshName.IsEmpty())
			{
				Pieces.Add(MeshActor);
			}
		}
		for (FHouse& House : Houses)
		{
			for (const FBox& Roof : Roofs)
			{
				if (SnareHouseRoof::IsUnderRoof(Roof, House.Body->GetActorLocation() + FVector(0.f, 0.f, CapsuleHalfHeightCm)))
				{
					House.RoofBounds = Roof;
				}
			}
			for (const AStaticMeshActor* const Piece : Pieces)
			{
				// 가구 피벗은 바닥 — 발 높이 판정에 맞춰 캡슐 반높이만큼 올려 묻는다.
				if (SnareHouseRoof::IsUnderRoof(House.RoofBounds, Piece->GetActorLocation() + FVector(0.f, 0.f, CapsuleHalfHeightCm)))
				{
					House.Furniture.Add(Piece);
					if (MeshNameOf(Piece) == TableMeshName)
					{
						House.Table = Piece;
					}
				}
			}
		}
		return Houses;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHouseFurnitureTest, "Snarehaven.World.HouseFurniture.FilledAndDoorClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHouseFurnitureTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenHouseFurnitureTest;

	ULevel* Level = nullptr;
	UWorld* const World = CreateWorldWithLevel(TEXT("/Game/Game/Maps/L_Ranch"), Level);
	if (!TestNotNull(TEXT("L_Ranch 스트리밍"), Level))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TArray<FHouse> Houses = CollectHouses(Level);
	const FHouse* const DiningHouse = Houses.FindByPredicate([](const FHouse& House) { return House.Table != nullptr; });
	if (!TestNotNull(TEXT("탁자 있는 집"), DiningHouse))
	{
		DestroyTestWorld(World);
		return false;
	}

	// (a) 가구 종류 수 · 충돌
	TSet<FString> Kinds;
	for (const AStaticMeshActor* const Piece : DiningHouse->Furniture)
	{
		const FString MeshName = MeshNameOf(Piece);
		Kinds.Add(MeshName);
		const UStaticMeshComponent* const Component = Piece->GetStaticMeshComponent();
		TestTrue(*FString::Printf(TEXT("%s 이 폰을 막는다"), *MeshName),
			Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision
			&& Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);
	}
	TArray<FString> KindNames = Kinds.Array();
	KindNames.Sort();
	TestTrue(*FString::Printf(TEXT("가구 종류 %d ≥ %d"), Kinds.Num(), MinFurnitureKinds), Kinds.Num() >= MinFurnitureKinds);

	// (b) 문 밖 → 탁자 앞. 문은 집 앞면(-X) 한가운데 — 탁자도 같은 y 에 있다.
	const FBox TableBounds = DiningHouse->Table->GetComponentsBoundingBox();
	const float LaneY = DiningHouse->Table->GetActorLocation().Y;
	const float CapsuleZ = DiningHouse->Body->GetActorLocation().Z + CapsuleHalfHeightCm + FloorClearanceCm;
	const FVector From(DiningHouse->Bounds.Min.X - OutsideDoorCm, LaneY, CapsuleZ);
	const FVector To(TableBounds.Min.X - CapsuleRadiusCm - TableGapCm, LaneY, CapsuleZ);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(HouseFurnitureSweep), false);
	FHitResult Hit;
	const bool bBlocked = World->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(CapsuleRadiusCm, CapsuleHalfHeightCm), Query);
	const FString Blocker = bBlocked && Hit.GetComponent() != nullptr
		? FString::Printf(TEXT("%s.%s"), *GetNameSafe(Hit.GetActor()), *Hit.GetComponent()->GetName()) : FString(TEXT("없음"));
	TestFalse(*FString::Printf(TEXT("문 밖→탁자 앞 막힘 없음 (막은 것: %s)"), *Blocker), bBlocked);

	UE_LOG(LogSnarehaven, Display, TEXT("[HouseFurniture] 집 %d채 · 식탁 집 가구 %d개 종류 %d (%s) · 문 밖 %s → 탁자 앞 %s 막힘 %s (%s)"),
		Houses.Num(), DiningHouse->Furniture.Num(), Kinds.Num(), *FString::Join(KindNames, TEXT(", ")),
		*From.ToCompactString(), *To.ToCompactString(), bBlocked ? TEXT("있음") : TEXT("없음"), *Blocker);

	DestroyTestWorld(World);
	return true;
}

// M19 폰 결함(사람 보고: 거점 이동 → 집에 들어가면 울타리 때문에 못 나온다 · 집 안에 못 가는 곳이 있다).
// `L_Ranch` 마당을 25cm 칸으로 나눠 칸마다 캡슐(34·88)이 설 수 있는지 재고, `PlayerStart` 에서 4방향으로 이어
// 갈 수 있는 칸을 센다. 집마다 (a) 안쪽 칸 중 막힌 칸 수와 막은 구성요소 · (b) 문 밖 → 집 안 → 울타리 밖·포탈이
// 한 덩어리로 이어지는지 본다. 덮개 숨김·실내 페이드는 `SetHiddenInGame` 이라 충돌을 안 바꾼다 — 그대로 잰다.
// 에디터 전용 API(`FAssetCompilingManager`)를 쓴다.
#if WITH_EDITOR
#include "AssetCompilingManager.h"

namespace SnarehavenHouseWalkTest
{
	using namespace SnarehavenHouseFurnitureTest;

	// 테스트 전용 값 (Verification.md). 칸 25cm · 마당 범위는 `setup_ranch_buildings.py` 배치(울타리 x -300·y ±800, 집 x ~1080)를 덮는다.
	constexpr float CellCm = 25.f;
	// y 는 사냥터 포탈(y -2800)까지 덮는다.
	constexpr float MinX = -700.f, MaxX = 1400.f, MinY = -3000.f, MaxY = 1400.f;
	// 캐릭터 기본 `MaxStepHeight` 45 — 이보다 높은 바닥은 올라설 수 없다. 바닥 찾기 빛은 디딤 높이 바로 위에서 쏜다:
	// 240 에서 쏘던 때는 문틀 윗막대(z 208~222) 윗면에 맞아 집 B 문턱 칸(x675)을 "높음" 으로 막았다(캡슐은 그 밑으로 지나간다).
	// 더 높은 것은 그 위에 세우는 캡슐 겹침이 잡는다.
	constexpr float MaxStepCm = 45.f;
	constexpr float TraceTopCm = MaxStepCm + 15.f;
	// 벽 안쪽 반폭(벽 15cm 를 뺀 185·255, `make_house_meshes.py`) — 캡슐 가운데가 설 수 있는 안쪽은 반지름만큼 더 좁다.
	constexpr float InnerHalfXCm = 185.f, InnerHalfYCm = 255.f;
	// 울타리 밖 = 앞 울타리 줄(x -300) 너머.
	constexpr float OutsideFenceX = -400.f;
	// 합격선 — 집 안쪽 칸(캡슐 가운데가 설 수 있는 띠) 중 시작점에서 가야 하는 몫. 칸은 캡슐 반지름만큼 부푼 가구에 막히므로
	// 가구 바닥 넓이보다 훨씬 많이 막힌다(10-02 실측 집 A 92/216 · 집 B 128/216 갈 수 있음). 핵심은 "갇힌 칸 0" 이다.
	constexpr float MinReachedInteriorShare = 0.4f;

	struct FGrid
	{
		int32 NX = 0, NY = 0;
		TArray<bool> Free;
		TArray<FString> Blocker;
		TArray<bool> Reached;
		int32 Index(int32 IX, int32 IY) const { return IY * NX + IX; }
		FVector2D Center(int32 IX, int32 IY) const { return FVector2D(MinX + IX * CellCm, MinY + IY * CellCm); }
		bool CellOf(const FVector& P, int32& IX, int32& IY) const
		{
			IX = FMath::RoundToInt((P.X - MinX) / CellCm);
			IY = FMath::RoundToInt((P.Y - MinY) / CellCm);
			return IX >= 0 && IY >= 0 && IX < NX && IY < NY;
		}
	};

	FString NameOf(const UPrimitiveComponent* Component)
	{
		// 커맨드릿 밖에선 액터 라벨이 없다 — 메시 이름과 액터 자리로 `setup_ranch_buildings.py` 표의 줄을 찾는다.
		const UStaticMeshComponent* const Mesh = Cast<UStaticMeshComponent>(Component);
		const FVector At = Component->GetOwner() != nullptr ? Component->GetOwner()->GetActorLocation() : FVector::ZeroVector;
		return FString::Printf(TEXT("%s@(%.0f,%.0f)"),
			Mesh != nullptr && Mesh->GetStaticMesh() != nullptr ? *Mesh->GetStaticMesh()->GetName() : *GetNameSafe(Component->GetOwner()), At.X, At.Y);
	}

	/** 한 칸에 캡슐이 설 수 있나 — 바닥이 디딤 높이 이하이고, 그 위에 세운 캡슐이 막는 것과 안 겹치면 선다. */
	bool Stand(UWorld* World, const FVector2D& XY, const FCollisionQueryParams& Query, FString& OutBlocker)
	{
		FHitResult Ground;
		float GroundZ = 0.f;
		if (World->LineTraceSingleByChannel(Ground, FVector(XY, TraceTopCm), FVector(XY, -100.f), ECC_Pawn, Query))
		{
			GroundZ = Ground.ImpactPoint.Z;
			if (GroundZ > MaxStepCm)
			{
				OutBlocker = NameOf(Ground.GetComponent()) + TEXT("(높음)");
				return false;
			}
		}
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByChannel(Overlaps, FVector(XY, GroundZ + CapsuleHalfHeightCm + FloorClearanceCm), FQuat::Identity,
			ECC_Pawn, FCollisionShape::MakeCapsule(CapsuleRadiusCm, CapsuleHalfHeightCm), Query);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			const UPrimitiveComponent* const Component = Overlap.GetComponent();
			if (Component != nullptr && Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
			{
				OutBlocker = NameOf(Component);
				return false;
			}
		}
		return true;
	}

	FGrid Measure(UWorld* World, const FVector& Start)
	{
		FGrid Grid;
		Grid.NX = FMath::FloorToInt((MaxX - MinX) / CellCm) + 1;
		Grid.NY = FMath::FloorToInt((MaxY - MinY) / CellCm) + 1;
		Grid.Free.SetNumZeroed(Grid.NX * Grid.NY);
		Grid.Blocker.SetNum(Grid.NX * Grid.NY);
		Grid.Reached.SetNumZeroed(Grid.NX * Grid.NY);
		// 돌아다니는 폰(목장 동물 등)은 길을 막는 지형이 아니다 — 무작위 자리라 빼지 않으면 실행마다 칸 결과가 달라진다.
		FCollisionQueryParams Query(SCENE_QUERY_STAT(HouseWalkGrid), false);
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			Query.AddIgnoredActor(*It);
		}
		for (int32 IY = 0; IY < Grid.NY; ++IY)
		{
			for (int32 IX = 0; IX < Grid.NX; ++IX)
			{
				const int32 I = Grid.Index(IX, IY);
				Grid.Free[I] = Stand(World, Grid.Center(IX, IY), Query, Grid.Blocker[I]);
			}
		}
		int32 SX = 0, SY = 0;
		if (!Grid.CellOf(Start, SX, SY) || !Grid.Free[Grid.Index(SX, SY)])
		{
			return Grid;
		}
		TArray<FIntPoint> Open = { FIntPoint(SX, SY) };
		Grid.Reached[Grid.Index(SX, SY)] = true;
		while (!Open.IsEmpty())
		{
			const FIntPoint P = Open.Pop(EAllowShrinking::No);
			for (const FIntPoint D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
			{
				const FIntPoint N = P + D;
				if (N.X < 0 || N.Y < 0 || N.X >= Grid.NX || N.Y >= Grid.NY)
				{
					continue;
				}
				const int32 I = Grid.Index(N.X, N.Y);
				if (Grid.Free[I] && !Grid.Reached[I])
				{
					Grid.Reached[I] = true;
					Open.Add(N);
				}
			}
		}
		return Grid;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHouseWalkTest, "Snarehaven.World.HouseFurniture.WalkInsideAndOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHouseWalkTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenHouseWalkTest;

	ULevel* Level = nullptr;
	UWorld* const World = CreateWorldWithLevel(TEXT("/Game/Game/Maps/L_Ranch"), Level);
	if (!TestNotNull(TEXT("L_Ranch 스트리밍"), Level))
	{
		DestroyTestWorld(World);
		return false;
	}
	FVector Start = FVector::ZeroVector;
	FVector Portal = FVector::ZeroVector;
	for (const AActor* const Actor : Level->Actors)
	{
		if (Cast<APlayerStart>(Actor) != nullptr)
		{
			Start = Actor->GetActorLocation();
		}
		else if (Cast<AZonePortalActor>(Actor) != nullptr)
		{
			Portal = Actor->GetActorLocation();
		}
	}
	// 스트리밍 직후엔 메시 컴파일(충돌 굽기)이 덜 끝난 몸이 충돌 없이 들어가 칸 결과가 실행마다 달랐다
	// (상자 더미·탁자가 어떤 실행에선 안 막았다) — 컴파일을 다 끝내고 몇 프레임 돌려 자리 잡힌 뒤 잰다.
	FAssetCompilingManager::Get().FinishAllCompilation();
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		World->Tick(LEVELTICK_All, 1.f / 30.f);
	}
	const FGrid Grid = Measure(World, Start);
	const FGrid Again = Measure(World, Start);
	TestTrue(TEXT("두 번 잰 칸 결과가 같다"), Grid.Free == Again.Free);
	int32 SX = 0, SY = 0;
	TestTrue(TEXT("시작점 칸에 설 수 있다"), Grid.CellOf(Start, SX, SY) && Grid.Reached[Grid.Index(SX, SY)]);

	// 칸 지도 — `.` 갈 수 있음 · `o` 설 수 있지만 못 감(갇힌 곳) · `#` 막힘 · `S` 시작. 위 줄이 +X(화면 위).
	for (int32 IX = Grid.NX - 1; IX >= 0; --IX)
	{
		FString Row;
		for (int32 IY = 0; IY < Grid.NY; ++IY)
		{
			const int32 I = Grid.Index(IX, IY);
			Row.AppendChar(IX == SX && IY == SY ? TEXT('S') : Grid.Reached[I] ? TEXT('.') : Grid.Free[I] ? TEXT('o') : TEXT('#'));
		}
		UE_LOG(LogSnarehaven, Display, TEXT("[HouseWalk] x%+05.0f %s"), MinX + IX * CellCm, *Row);
	}

	// 마당 막은 것 목록 — 이름마다 막은 칸 수와 칸 범위(x·y). 지도의 `#` 덩어리가 무엇인지 찾는 데 쓴다.
	{
		TMap<FString, FBox2D> Extent;
		TMap<FString, int32> Count;
		for (int32 IY = 0; IY < Grid.NY; ++IY)
		{
			for (int32 IX = 0; IX < Grid.NX; ++IX)
			{
				const int32 I = Grid.Index(IX, IY);
				const FVector2D C = Grid.Center(IX, IY);
				if (!Grid.Free[I] && C.X > -400.f && FMath::Abs(C.Y) < 900.f)
				{
					Extent.FindOrAdd(Grid.Blocker[I], FBox2D(ForceInit)) += C;
					++Count.FindOrAdd(Grid.Blocker[I]);
				}
			}
		}
		for (const TPair<FString, FBox2D>& Pair : Extent)
		{
			UE_LOG(LogSnarehaven, Display, TEXT("[HouseWalk] 마당 막은 것 %s 칸 %d · x %.0f~%.0f · y %.0f~%.0f"), *Pair.Key, Count[Pair.Key],
				Pair.Value.Min.X, Pair.Value.Max.X, Pair.Value.Min.Y, Pair.Value.Max.Y);
		}
	}

	bool bOutsideFence = false, bPortal = false;
	for (int32 IY = 0; IY < Grid.NY; ++IY)
	{
		for (int32 IX = 0; IX < Grid.NX; ++IX)
		{
			const FVector2D C = Grid.Center(IX, IY);
			bOutsideFence |= Grid.Reached[Grid.Index(IX, IY)] && C.X < OutsideFenceX;
			bPortal |= Grid.Reached[Grid.Index(IX, IY)] && FVector2D::Distance(C, FVector2D(Portal)) < 150.f;
		}
	}
	TestTrue(TEXT("시작점에서 울타리 밖까지 이어진다"), bOutsideFence);
	// 포탈 도달은 기록만 한다 — 부두(`SM_Prop_FerryPier_01`)가 디딤 높이보다 높아 칸 길은 포탈 300cm 앞에서 끝나고,
	// 버튼 사거리 판정은 `Snarehaven.Zone.Travel` 몫이다. 이 시험은 집·마당 길을 본다.

	for (const FHouse& House : CollectHouses(Level))
	{
		const FVector C = House.Body->GetActorLocation();
		const FString HouseName = FString::Printf(TEXT("집(%.0f,%.0f)"), C.X, C.Y);
		int32 Inner = 0, Blocked = 0, Stranded = 0, Reached = 0;
		TMap<FString, int32> Blockers;
		for (int32 IY = 0; IY < Grid.NY; ++IY)
		{
			for (int32 IX = 0; IX < Grid.NX; ++IX)
			{
				const FVector2D P = Grid.Center(IX, IY);
				if (FMath::Abs(P.X - C.X) > InnerHalfXCm - CapsuleRadiusCm || FMath::Abs(P.Y - C.Y) > InnerHalfYCm - CapsuleRadiusCm)
				{
					continue;
				}
				const int32 I = Grid.Index(IX, IY);
				++Inner;
				Reached += Grid.Reached[I] ? 1 : 0;
				Stranded += Grid.Free[I] && !Grid.Reached[I] ? 1 : 0;
				if (!Grid.Free[I])
				{
					++Blocked;
					++Blockers.FindOrAdd(Grid.Blocker[I]);
				}
			}
		}
		// 문 앞 길 — 문(앞면 -X 한가운데)에서 바깥으로 300cm 까지 칸마다 무엇이 막는지.
		TArray<FString> Lane;
		for (float X = House.Bounds.Min.X + 100.f; X >= House.Bounds.Min.X - 300.f; X -= CellCm)
		{
			int32 IX = 0, IY = 0;
			if (Grid.CellOf(FVector(X, C.Y, 0.f), IX, IY))
			{
				const int32 I = Grid.Index(IX, IY);
				Lane.Add(FString::Printf(TEXT("x%.0f:%s"), X, Grid.Reached[I] ? TEXT("갈 수 있음") : Grid.Free[I] ? TEXT("갇힘") : *Grid.Blocker[I]));
			}
		}
		UE_LOG(LogSnarehaven, Display, TEXT("[HouseWalk] %s 문 앞 길 %s"), *FString::Printf(TEXT("집(%.0f,%.0f)"), C.X, C.Y),
			*FString::Join(Lane, TEXT(" | ")));
		Blockers.ValueSort(TGreater<int32>());
		TArray<FString> BlockerText;
		for (const TPair<FString, int32>& Pair : Blockers)
		{
			BlockerText.Add(FString::Printf(TEXT("%s=%d"), *Pair.Key, Pair.Value));
		}
		UE_LOG(LogSnarehaven, Display, TEXT("[HouseWalk] %s 안쪽 칸 %d · 막힘 %d · 갈 수 있음 %d · 갇힘 %d · 막은 것: %s"),
			*HouseName, Inner, Blocked, Reached, Stranded, *FString::Join(BlockerText, TEXT(", ")));
		TestTrue(*FString::Printf(TEXT("%s 안에 시작점에서 들어간다"), *HouseName), Reached > 0);
		TestEqual(*FString::Printf(TEXT("%s 안 빈 칸이 전부 이어진다(갇힌 칸 0)"), *HouseName), Stranded, 0);
		TestTrue(*FString::Printf(TEXT("%s 갈 수 있는 칸 %d/%d ≥ %.0f%%"), *HouseName, Reached, Inner, MinReachedInteriorShare * 100.f),
			Reached >= Inner * MinReachedInteriorShare);
	}
	UE_LOG(LogSnarehaven, Display, TEXT("[HouseWalk] 시작점 %s · 울타리 밖 %s · 포탈 %s %s"), *Start.ToCompactString(),
		bOutsideFence ? TEXT("이어짐") : TEXT("끊김"), *Portal.ToCompactString(), bPortal ? TEXT("이어짐") : TEXT("끊김"));
	// 포탈 쪽 길 — 시작점 x 줄을 따라 포탈 y 까지, 상태가 바뀌는 칸만.
	int32 PX = 0, PY = 0;
	if (Grid.CellOf(Portal, PX, PY))
	{
		TArray<FString> PortalLane;
		FString Last;
		for (int32 IY = SY; IY != PY + (PY < SY ? -1 : 1); IY += PY < SY ? -1 : 1)
		{
			const int32 I = Grid.Index(SX, IY);
			const FString State = Grid.Reached[I] ? TEXT("갈 수 있음") : Grid.Free[I] ? TEXT("갇힘") : Grid.Blocker[I];
			if (State != Last)
			{
				PortalLane.Add(FString::Printf(TEXT("y%.0f:%s"), MinY + IY * CellCm, *State));
				Last = State;
			}
		}
		UE_LOG(LogSnarehaven, Display, TEXT("[HouseWalk] 포탈 쪽 길 %s"), *FString::Join(PortalLane, TEXT(" | ")));
	}

	DestroyTestWorld(World);
	return true;
}
#endif // WITH_EDITOR

#endif
