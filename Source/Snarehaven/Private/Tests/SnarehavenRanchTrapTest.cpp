// INBOX 결함 2026-10-04 12:30 (사람 보고: 거점에서 목장 뒤로 넘어가 들어가면 다시 못 나온다) 2층 검사.
// `HouseFurniture.WalkInsideAndOut` 은 마당(x ≤ 1400)만 25cm 칸 · 높이 하나 · 양방향으로 잰다 — 그래서 「올라갈 땐 디딤
// 높이로 넘고 떨어지면 못 올라오는」 한쪽 길과 오두막 뒤·섬 끝을 못 봤다. 이 시험은 저장된 `L_Ranch` 를 불러와
// 바다 벽 안 전체를 50cm 칸으로 나누고, 칸마다 설 수 있는 바닥을 **여러 층** 찾아 방향 있는 길로 잇는다:
// 옆 칸으로 갈 때 오르기는 디딤 높이(45) 이하만, 떨어지기는 높이 상관없이(캐릭터는 턱에서 걸어 떨어진다, 점프 없음).
// 시작점에서 갈 수 있는 바닥 중 **시작점으로 돌아오지 못하는 바닥 = 갇힘**, 0 이어야 통과.
// `OutpostReach` 는 같은 재기로 사냥 맵(`L_Hunt`) 거점 14곳(상인 둘레)을 잰다 — 범위 가장자리는 바깥 세계로 나가는 길로 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.World.RanchReach+Snarehaven.World.OutpostReach; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Snarehaven.h"
#include "AssetCompilingManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerStart.h"
#include "Ranch/RanchGameMode.h"
#include "Ranch/RanchInteractComponent.h"
#include "Ranch/RanchSlotActor.h"
#include "SnarehavenCharacter.h"
#include "World/SeaActor.h"
#include "World/ShopVendorActor.h"

namespace SnarehavenRanchTrapTest
{
	// 테스트 전용 값 (Verification.md — 설계 숫자가 아니다). 칸 50cm 는 INBOX 지시 그대로.
	constexpr float CellCm = 50.f;
	// 플레이어 캡슐 — `SnarehavenShopPierLevelTest.cpp` 가 실제 캡슐과 대조하는 값과 같다.
	constexpr float CapsuleRadiusCm = 34.f;
	constexpr float CapsuleHalfHeightCm = 88.f;
	constexpr float FloorClearanceCm = 6.f;
	// 바닥 찾기 빛 — 기준 높이(목장 바닥 · 거점 상인 발)에서 가장 높은 지붕 위부터 해저 아래까지. 한 칸에서 찾는 층 상한.
	constexpr float TraceAboveCm = 1500.f;
	constexpr float TraceBelowCm = 600.f;
	constexpr int32 MaxFloorsPerCell = 12;
	// 바다가 없을 때 쓰는 범위 — 목장 바닥 판(±2500).
	constexpr float FallbackHalfCm = 2600.f;
	// 새 플레이어의 열린 목장 칸 — DESIGN 3.2 「목장 슬롯 5」(서버 `slotCap` 처음 값).
	constexpr int32 FirstOpenSlots = 5;
	// 턱에서 떨어질 때 앞으로 날아가 내려앉을 수 있는 칸 수 — 바로 옆 칸(벽에 붙어 못 서는 칸) 하나를 건너뛴다.
	constexpr int32 MaxFallCells = 2;
	// 비탈 판정 — 두 칸 가운데 바닥이 두 높이의 중간에서 이만큼 안이면 이어진 면.
	constexpr float SlopeMidToleranceCm = 12.f;
	// 칸 지도를 찍는 범위 — 목장 마당·오두막 뒤(`setup_ranch_buildings.py` 배치 x -300~1100 · y ±800)를 넉넉히 덮는다.
	constexpr float PrintMinX = -1000.f, PrintMaxX = 3000.f, PrintHalfY = 2000.f;
	// 사냥 맵 거점 — `setup_outposts.py` 가 상인 둘레 1700cm 자연물을 걷고 틀 가장 먼 점이 약 1450cm. 그 밖 여유까지 잰다.
	constexpr float OutpostHalfCm = 2500.f;
	// 거점 시작 자리 — 상인 앞(바다 쪽) 거리 후보, 바닥이 있는 첫 것(`setup_outposts.py` START_SPOT 450 은 시작 거점만 뭍 검사).
	constexpr float OutpostStartAheadCm[] = { 300.f, 150.f, 450.f };
	// `islands.OUTPOSTS` 거점 수(DESIGN 1.3 「거점 14곳」).
	constexpr int32 OutpostCount = 14;
	// 역검증 우리를 세울 자리 — 첫 거점 상인 옆(틀 옆 끝 b 650 너머, 자연물 걷은 반경 1700 안).
	constexpr float OutpostPenSideCm = 1300.f;

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

	FString NameOf(const UPrimitiveComponent* Component)
	{
		// 커맨드릿 밖에선 액터 라벨이 없다 — 메시 이름과 액터 자리로 배치 스크립트 표의 줄을 찾는다.
		if (Component == nullptr)
		{
			return TEXT("?");
		}
		const UStaticMeshComponent* const Mesh = Cast<UStaticMeshComponent>(Component);
		const FVector At = Component->GetOwner() != nullptr ? Component->GetOwner()->GetActorLocation() : FVector::ZeroVector;
		return FString::Printf(TEXT("%s@(%.0f,%.0f)"),
			Mesh != nullptr && Mesh->GetStaticMesh() != nullptr ? *Mesh->GetStaticMesh()->GetName() : *GetNameSafe(Component->GetOwner()), At.X, At.Y);
	}

	struct FFloor
	{
		float Z = 0.f;
		FString Under;   // 밟고 선 것
	};

	struct FMap
	{
		FVector2D Min = FVector2D::ZeroVector;
		int32 NX = 0, NY = 0;
		TArray<TArray<FFloor>> Floors;   // 칸마다 높은 층부터
		TArray<int32> FirstNode;         // 칸 → 첫 노드 번호
		TArray<FIntPoint> NodeAt;        // 노드 → (칸, 층)
		FVector2D Center(int32 Cell) const { return Min + FVector2D((Cell % NX) * CellCm, (Cell / NX) * CellCm); }
	};

	/** 이 높이에 캡슐을 세우면 폰을 막는 것과 겹치나 — 겹치면 그 이름. */
	bool CapsuleBlocked(UWorld* World, const FVector2D& XY, float FootZ, const FCollisionQueryParams& Query, FString* OutBlocker = nullptr)
	{
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByChannel(Overlaps, FVector(XY, FootZ + CapsuleHalfHeightCm + FloorClearanceCm), FQuat::Identity,
			ECC_Pawn, FCollisionShape::MakeCapsule(CapsuleRadiusCm, CapsuleHalfHeightCm), Query);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			const UPrimitiveComponent* const Component = Overlap.GetComponent();
			if (Component != nullptr && Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
			{
				if (OutBlocker != nullptr)
				{
					*OutBlocker = NameOf(Component);
				}
				return true;
			}
		}
		return false;
	}

	/** 두 칸 사이가 끊긴 턱이 아니라 이어진 비탈인가 — 가운데 바닥이 걸을 수 있는 면이고 두 높이의 중간쯤에 있다. */
	bool IsSlope(UWorld* World, const FVector2D& Mid, float LowZ, float HighZ, float WalkableZ, const FCollisionQueryParams& Query)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(Mid, HighZ + 10.f), FVector(Mid, LowZ - 10.f), ECC_Pawn, Query) || Hit.bStartPenetrating)
		{
			return false;
		}
		return Hit.ImpactNormal.Z >= WalkableZ && FMath::Abs(Hit.ImpactPoint.Z - (LowZ + HighZ) * 0.5f) <= SlopeMidToleranceCm;
	}

	/** 한 칸 위에서 아래로 빛을 거듭 쏘아 걸을 수 있는 면(경사 한도 안 + 그 위 캡슐이 들어감)을 높은 것부터 모은다. */
	TArray<FFloor> FindFloors(UWorld* World, const FVector2D& XY, float BaseZ, float WalkableZ, const FCollisionQueryParams& Query)
	{
		TArray<FFloor> Floors;
		float From = BaseZ + TraceAboveCm;
		const float Bottom = BaseZ - TraceBelowCm;
		for (int32 Guard = 0; Guard < 80 && From > Bottom && Floors.Num() < MaxFloorsPerCell; ++Guard)
		{
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, FVector(XY, From), FVector(XY, Bottom), ECC_Pawn, Query))
			{
				break;
			}
			if (Hit.bStartPenetrating)
			{
				From -= 20.f;   // 두꺼운 몸 속에서 시작했다 — 빠져나올 때까지 조금씩 내려간다
				continue;
			}
			if (Hit.ImpactNormal.Z >= WalkableZ && !CapsuleBlocked(World, XY, Hit.ImpactPoint.Z, Query))
			{
				Floors.Add({ static_cast<float>(Hit.ImpactPoint.Z), NameOf(Hit.GetComponent()) });
			}
			From = Hit.ImpactPoint.Z - 2.f;
		}
		return Floors;
	}

	struct FResult
	{
		int32 Reached = 0;
		int32 Trapped = -1;
	};

	/**
	 * Bounds 안을 칸 지도로 재 Start 에서 갈 수 있는데 돌아오지 못하는 바닥(갇힘)을 센다. bBorderIsExit 면 가장자리 칸 바닥도
	 * 돌아갈 곳으로 친다. bPrintMap 이면 Print 범위 칸 지도를 `[Tag]` 로 찍는다(갇힘 덩어리는 늘 찍는다).
	 */
	FResult Measure(UWorld* World, const FBox2D& Bounds, const FVector& Start, float BaseZ, bool bBorderIsExit,
		const FCollisionQueryParams& Query, const TCHAR* Tag, const TCHAR* Label, bool bPrintMap, const FBox2D& Print)
	{
		const UCharacterMovementComponent* const Movement = GetDefault<ASnarehavenCharacter>()->GetCharacterMovement();
		const float MaxStep = Movement->MaxStepHeight;
		const float WalkableZ = Movement->GetWalkableFloorZ();

		FResult Result;
		FMap Map;
		Map.Min = Bounds.Min;
		Map.NX = FMath::FloorToInt((Bounds.Max.X - Bounds.Min.X) / CellCm) + 1;
		Map.NY = FMath::FloorToInt((Bounds.Max.Y - Bounds.Min.Y) / CellCm) + 1;
		const int32 CellCount = Map.NX * Map.NY;
		Map.Floors.SetNum(CellCount);
		Map.FirstNode.SetNum(CellCount);
		for (int32 Cell = 0; Cell < CellCount; ++Cell)
		{
			Map.Floors[Cell] = FindFloors(World, Map.Center(Cell), BaseZ, WalkableZ, Query);
			Map.FirstNode[Cell] = Map.NodeAt.Num();
			for (int32 F = 0; F < Map.Floors[Cell].Num(); ++F)
			{
				Map.NodeAt.Add(FIntPoint(Cell, F));
			}
		}
		const int32 NodeCount = Map.NodeAt.Num();
		// 걸을 수 있는 경사 끝(cos → tan)으로 한 칸 사이에 오를 수 있는 높이 — 디딤보다 높아도 비탈이면 걸어 오른다.
		const float MaxSlopeRise = CellCm * FMath::Tan(FMath::Acos(WalkableZ));

		// 길 — 노드 A(칸 c, 높이 za) → 옆 칸 n 의 「za + 디딤 이하에서 가장 높은 층」. 떨어지는 길이면 za 높이에서 옆 칸으로
		// 캡슐이 건너갈 수 있어야 한다(벽 위 끝에서 벽 너머로 떨어지는 것 등).
		TArray<TArray<int32>> Out, In;
		Out.SetNum(NodeCount);
		In.SetNum(NodeCount);
		for (int32 Node = 0; Node < NodeCount; ++Node)
		{
			const int32 Cell = Map.NodeAt[Node].X;
			const float Za = Map.Floors[Cell][Map.NodeAt[Node].Y].Z;
			const int32 IX = Cell % Map.NX, IY = Cell / Map.NX;
			for (const FIntPoint D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
			{
				// 턱에서 걸어 떨어질 땐 몸이 앞으로 계속 나가며 떨어진다 — 바로 옆 칸이 벽에 붙어 설 수 없으면 한 칸 더 가서 떨어진다.
				for (int32 Far = 1; Far <= MaxFallCells; ++Far)
				{
					const int32 NX = IX + D.X * Far, NY = IY + D.Y * Far;
					if (NX < 0 || NY < 0 || NX >= Map.NX || NY >= Map.NY)
					{
						break;
					}
					const int32 Next = NY * Map.NX + NX;
					const bool bAirborne = CapsuleBlocked(World, Map.Center(Next), Za, Query) == false;
					const TArray<FFloor>& Floors = Map.Floors[Next];
					int32 Landed = INDEX_NONE;
					for (int32 F = 0; F < Floors.Num() && Landed == INDEX_NONE; ++F)
					{
						const bool bClimb = Floors[F].Z <= Za + MaxStep || (Floors[F].Z <= Za + MaxSlopeRise
							&& IsSlope(World, (Map.Center(Cell) + Map.Center(Next)) * 0.5f, Za, Floors[F].Z, WalkableZ, Query));
						if (Far == 1 && bClimb && Floors[F].Z >= Za - MaxStep)
						{
							Landed = F;   // 걸어서 이어지는 칸
						}
						else if (Floors[F].Z < Za - MaxStep && bAirborne)
						{
							Landed = F;   // 떨어져 내려앉는 칸 — 그 높이로 옆 칸에 넘어갈 수 있어야 한다
						}
						else if (Floors[F].Z < Za - MaxStep)
						{
							break;        // 그 높이로는 옆 칸에 못 넘어간다 — 더 아래 층도 같은 벽에 막힌다
						}
					}
					if (Landed != INDEX_NONE)
					{
						const int32 To = Map.FirstNode[Next] + Landed;
						Out[Node].Add(To);
						In[To].Add(Node);
						break;
					}
					if (!bAirborne)
					{
						break;        // 공중으로도 못 나간다 — 더 멀리 날아가지 않는다
					}
				}
			}
		}

		auto Flood = [NodeCount](const TArray<int32>& From, const TArray<TArray<int32>>& Edges)
		{
			TArray<bool> Seen;
			Seen.SetNumZeroed(NodeCount);
			TArray<int32> Open = From;
			for (const int32 Node : From)
			{
				Seen[Node] = true;
			}
			while (!Open.IsEmpty())
			{
				const int32 Node = Open.Pop(EAllowShrinking::No);
				for (const int32 To : Edges[Node])
				{
					if (!Seen[To])
					{
						Seen[To] = true;
						Open.Add(To);
					}
				}
			}
			return Seen;
		};

		// 시작 노드 — 시작점 칸에서 시작점 발 높이 아래 가장 높은 층.
		const int32 StartCell = FMath::RoundToInt((Start.Y - Map.Min.Y) / CellCm) * Map.NX + FMath::RoundToInt((Start.X - Map.Min.X) / CellCm);
		int32 StartNode = INDEX_NONE;
		if (Map.Floors.IsValidIndex(StartCell))
		{
			for (int32 F = 0; F < Map.Floors[StartCell].Num() && StartNode == INDEX_NONE; ++F)
			{
				if (Map.Floors[StartCell][F].Z <= Start.Z)
				{
					StartNode = Map.FirstNode[StartCell] + F;
				}
			}
		}
		if (StartNode == INDEX_NONE)
		{
			return Result;
		}
		const TArray<bool> Reach = Flood({ StartNode }, Out);
		// 바깥으로 나가는 문 — 범위 가장자리 칸의 바닥은 범위 밖 세계로 이어진다고 본다(거점 둘레만 재는 사냥 맵).
		TArray<int32> Exits = { StartNode };
		for (int32 Node = 0; bBorderIsExit && Node < NodeCount; ++Node)
		{
			const int32 Cell = Map.NodeAt[Node].X;
			const int32 IX = Cell % Map.NX, IY = Cell / Map.NX;
			if (IX == 0 || IY == 0 || IX == Map.NX - 1 || IY == Map.NY - 1)
			{
				Exits.Add(Node);
			}
		}
		const TArray<bool> Back = Flood(Exits, In);

		// 갇힌 바닥을 이어진 덩어리로 묶어 자리·높이·밟은 것을 적는다.
		int32 Reached = 0, Trapped = 0;
		TArray<int32> TrapOf;
		TrapOf.Init(INDEX_NONE, NodeCount);
		TArray<FString> Clusters;
		for (int32 Node = 0; Node < NodeCount; ++Node)
		{
			Reached += Reach[Node] ? 1 : 0;
			if (!Reach[Node] || Back[Node] || TrapOf[Node] != INDEX_NONE)
			{
				continue;
			}
			TArray<int32> Open = { Node };
			TrapOf[Node] = Clusters.Num();
			FBox Box(ForceInit);
			TMap<FString, int32> Under;
			int32 Size = 0;
			while (!Open.IsEmpty())
			{
				const int32 N = Open.Pop(EAllowShrinking::No);
				const FFloor& Floor = Map.Floors[Map.NodeAt[N].X][Map.NodeAt[N].Y];
				Box += FVector(Map.Center(Map.NodeAt[N].X), Floor.Z);
				++Under.FindOrAdd(Floor.Under);
				++Size;
				for (const TArray<int32>* Edges : { &Out[N], &In[N] })
				{
					for (const int32 To : *Edges)
					{
						if (Reach[To] && !Back[To] && TrapOf[To] == INDEX_NONE)
						{
							TrapOf[To] = Clusters.Num();
							Open.Add(To);
						}
					}
				}
			}
			Trapped += Size;
			Under.ValueSort(TGreater<int32>());
			TArray<FString> UnderText;
			for (const TPair<FString, int32>& Pair : Under)
			{
				UnderText.Add(FString::Printf(TEXT("%s=%d"), *Pair.Key, Pair.Value));
			}
			Clusters.Add(FString::Printf(TEXT("칸 %d · x %.0f~%.0f · y %.0f~%.0f · z %.0f~%.0f · 밟은 것 %s"), Size,
				Box.Min.X, Box.Max.X, Box.Min.Y, Box.Max.Y, Box.Min.Z, Box.Max.Z, *FString::Join(UnderText, TEXT(", "))));
		}
		// 칸 지도 — `.` 오갈 수 있음 · `T` 갇힘 · `^` 바닥은 있지만 못 감(지붕·고원 윗면 등) · `#` 설 바닥 없음 · `S` 시작. 위 줄이 +X.
		for (float X = Print.Max.X; bPrintMap && X >= Print.Min.X; X -= CellCm)
		{
			FString Row;
			for (float Y = Print.Min.Y; Y <= Print.Max.Y; Y += CellCm)
			{
				const int32 IX = FMath::RoundToInt((X - Map.Min.X) / CellCm), IY = FMath::RoundToInt((Y - Map.Min.Y) / CellCm);
				const int32 Cell = IY * Map.NX + IX;
				TCHAR Mark = TEXT('#');
				for (int32 F = 0; Map.Floors.IsValidIndex(Cell) && F < Map.Floors[Cell].Num(); ++F)
				{
					const int32 Node = Map.FirstNode[Cell] + F;
					const TCHAR This = Node == StartNode ? TEXT('S') : !Reach[Node] ? TEXT('^') : Back[Node] ? TEXT('.') : TEXT('T');
					// 한 칸에 층이 여럿이면 눈에 띄는 것부터 — 갇힘 > 시작 > 오갈 수 있음 > 못 감
					auto Rank = [](TCHAR C) { return C == TEXT('T') ? 4 : C == TEXT('S') ? 3 : C == TEXT('.') ? 2 : C == TEXT('^') ? 1 : 0; };
					Mark = Rank(This) > Rank(Mark) ? This : Mark;
				}
				Row.AppendChar(Mark);
			}
			UE_LOG(LogSnarehaven, Display, TEXT("[%s] x%+06.0f %s"), Tag, X, *Row);
		}
		for (int32 I = 0; I < Clusters.Num(); ++I)
		{
			UE_LOG(LogSnarehaven, Display, TEXT("[%s] %s 갇힘 %d: %s"), Tag, Label, I + 1, *Clusters[I]);
		}
		Result.Reached = Reached;
		Result.Trapped = Trapped;
		UE_LOG(LogSnarehaven, Display, TEXT("[%s] %s · 범위 x %.0f~%.0f · y %.0f~%.0f · 칸 %dx%d · 바닥 %d · 시작점 %s 에서 갈 수 있음 %d · 갇힘 %d (덩어리 %d) · 디딤 %.0f · 걷는 경사 cos %.2f"),
			Tag, Label, Bounds.Min.X, Bounds.Max.X, Bounds.Min.Y, Bounds.Max.Y, Map.NX, Map.NY, NodeCount, *Start.ToCompactString(), Reached, Trapped,
			Clusters.Num(), MaxStep, WalkableZ);

		return Result;
	}

	/** 역검증 우리 — 엔진 기본 큐브(100cm, 가운데 받침)로 계단 넷(40~160)을 올라 높이 200 벽 위에서 안쪽으로 떨어지면 못 나온다.
	 *  Origin = 우리 가운데 바닥. 계단·벽 폭 100 — 캡슐 반지름 34 가 칸 가운데(상자 가운데)에서 이웃 상자에 닿지 않게. */
	TArray<AActor*> SpawnTrapPen(UWorld* World, UStaticMesh* Cube, const FVector& Origin)
	{
		const struct { FVector Center; FVector Size; } Blocks[] = {
			{ FVector(-650.f, 0.f, 20.f), FVector(100.f, 200.f, 40.f) },     // 계단 40
			{ FVector(-550.f, 0.f, 40.f), FVector(100.f, 200.f, 80.f) },     // 계단 80
			{ FVector(-450.f, 0.f, 60.f), FVector(100.f, 200.f, 120.f) },    // 계단 120
			{ FVector(-350.f, 0.f, 80.f), FVector(100.f, 200.f, 160.f) },    // 계단 160
			{ FVector(-250.f, 0.f, 100.f), FVector(100.f, 600.f, 200.f) },   // 우리 -X 벽(계단이 닿는 쪽)
			{ FVector(250.f, 0.f, 100.f), FVector(100.f, 600.f, 200.f) },    // +X 벽
			{ FVector(0.f, -250.f, 100.f), FVector(600.f, 100.f, 200.f) },   // -Y 벽
			{ FVector(0.f, 250.f, 100.f), FVector(600.f, 100.f, 200.f) },    // +Y 벽
		};
		TArray<AActor*> Spawned;
		for (const auto& Block : Blocks)
		{
			AStaticMeshActor* const Box = World->SpawnActor<AStaticMeshActor>(Origin + Block.Center, FRotator::ZeroRotator);
			Box->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Box->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Box->SetActorScale3D(Block.Size / 100.f);
			Spawned.Add(Box);
		}
		World->Tick(LEVELTICK_All, 1.f / 30.f);
		return Spawned;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchTrapTest, "Snarehaven.World.RanchReach.NoOneWayTrap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchTrapTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenRanchTrapTest;

	ULevel* Level = nullptr;
	UWorld* const World = CreateWorldWithLevel(TEXT("/Game/Game/Maps/L_Ranch"), Level);
	if (!TestNotNull(TEXT("L_Ranch 스트리밍"), Level))
	{
		DestroyTestWorld(World);
		return false;
	}
	// 스트리밍 직후엔 충돌 굽기가 덜 끝난 몸이 있다(`WalkInsideAndOut` 이 겪은 것) — 다 끝내고 몇 프레임 돌린 뒤 잰다.
	FAssetCompilingManager::Get().FinishAllCompilation();
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		World->Tick(LEVELTICK_All, 1.f / 30.f);
	}

	FVector Start = FVector::ZeroVector;
	FBox2D Bounds(FVector2D(-FallbackHalfCm), FVector2D(FallbackHalfCm));
	for (const AActor* const Actor : Level->Actors)
	{
		if (Cast<APlayerStart>(Actor) != nullptr)
		{
			Start = Actor->GetActorLocation();
		}
		else if (const ASeaActor* const Sea = Cast<ASeaActor>(Actor))
		{
			// 바다 벽(보이지 않는 막) 안이 갈 수 있는 세계 전부다.
			const FBox SeaBox = Sea->GetComponentsBoundingBox(true);
			Bounds = FBox2D(FVector2D(SeaBox.Min), FVector2D(SeaBox.Max));
		}
	}

	// 상인 좌판(막는 충돌)은 레벨 파일이 아니라 게임 모드가 세운다 — 게임과 같은 함수로 세워 같이 잰다.
	ARanchGameMode::SpawnVendors(World);
	World->Tick(LEVELTICK_All, 1.f / 30.f);

	FCollisionQueryParams Query(SCENE_QUERY_STAT(RanchTrapGrid), false);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		Query.AddIgnoredActor(*It);   // 돌아다니는 동물은 지형이 아니다
	}

	// 잠긴 확장 칸은 발판·난간이 숨고 충돌도 꺼진다(`URanchInteractComponent::ApplySlotVisibility`) — 칸 상한마다 땅 모양이
	// 달라지니 처음 칸 수부터 레벨의 칸 전부까지 상한마다 다시 잰다. 칸 지도는 처음 상태(새 플레이어)와 갇힘이 난 상태만 찍는다.
	auto MeasureRanch = [&](int32 OpenSlots, bool bPrintMap)
	{
		URanchInteractComponent::ApplySlotVisibility(World, OpenSlots);
		return Measure(World, Bounds, Start, 0.f, false, Query, TEXT("RanchTrap"), *FString::Printf(TEXT("열린 칸 %d"), OpenSlots), bPrintMap,
			FBox2D(FVector2D(PrintMinX, -PrintHalfY), FVector2D(PrintMaxX, PrintHalfY)));
	};

	int32 SlotTotal = 0;
	for (TActorIterator<ARanchSlotActor> It(World); It; ++It)
	{
		SlotTotal += It->GetSlotIndex() != INDEX_NONE ? 1 : 0;
	}
	TestTrue(*FString::Printf(TEXT("레벨 목장 칸 %d ≥ 처음 열린 칸 %d"), SlotTotal, FirstOpenSlots), SlotTotal >= FirstOpenSlots);
	for (int32 OpenSlots = FirstOpenSlots; OpenSlots <= SlotTotal; ++OpenSlots)
	{
		FResult Result = MeasureRanch(OpenSlots, false);
		if (OpenSlots == FirstOpenSlots || Result.Trapped != 0)
		{
			Result = MeasureRanch(OpenSlots, true);
		}
		TestTrue(*FString::Printf(TEXT("열린 칸 %d: 시작점에서 바닥 1000칸 넘게 갈 수 있다(바닥 찾기가 섬을 본다)"), OpenSlots), Result.Reached > 1000);
		TestEqual(*FString::Printf(TEXT("열린 칸 %d: 가 놓고 못 돌아오는 바닥(갇힘) 0"), OpenSlots), Result.Trapped, 0);
	}

	// 역검증 — 시험이 진짜 갇힘을 무는지. 열린 땅(오두막 뒤 x 1300~2250)에 계단을 올라 떨어지면 못 나오는 우리를 세운다.
	UStaticMesh* const Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (TestNotNull(TEXT("기본 큐브"), Cube))
	{
		const TArray<AActor*> Spawned = SpawnTrapPen(World, Cube, FVector(1950.f, 0.f, 0.f));
		const FResult Caught = MeasureRanch(FirstOpenSlots, true);
		TestTrue(*FString::Printf(TEXT("역검증: 계단으로 올라 떨어지는 우리 안을 갇힘으로 문다 (갇힘 %d)"), Caught.Trapped), Caught.Trapped > 0);
		for (AActor* const Box : Spawned)
		{
			Box->Destroy();
		}
	}

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenOutpostTrapTest, "Snarehaven.World.OutpostReach.NoOneWayTrap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenOutpostTrapTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenRanchTrapTest;

	ULevel* Level = nullptr;
	UWorld* const World = CreateWorldWithLevel(TEXT("/Game/Game/Maps/L_Hunt"), Level);
	if (!TestNotNull(TEXT("L_Hunt 스트리밍"), Level))
	{
		DestroyTestWorld(World);
		return false;
	}
	FAssetCompilingManager::Get().FinishAllCompilation();
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		World->Tick(LEVELTICK_All, 1.f / 30.f);
	}

	// 거점마다 상인 하나(`setup_outposts.py`) — 상인이 거점 틀의 가운데이고 바다 쪽을 본다.
	TArray<const AShopVendorActor*> Vendors;
	for (const AActor* const Actor : Level->Actors)
	{
		// 의뢰인(`AQuestGiverActor`)도 상인 갈래라 정확한 클래스만 센다.
		const AShopVendorActor* const Vendor = Cast<AShopVendorActor>(Actor);
		if (Vendor != nullptr && Vendor->GetClass() == AShopVendorActor::StaticClass())
		{
			Vendors.Add(Vendor);
		}
	}
	TestEqual(TEXT("L_Hunt 거점 상인 수"), Vendors.Num(), OutpostCount);

	FCollisionQueryParams Query(SCENE_QUERY_STAT(OutpostTrapGrid), false);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		Query.AddIgnoredActor(*It);   // 돌아다니는 동물은 지형이 아니다
	}
	// 땅 높이 — 위에서 아래로 쏜 빛이 처음 닿는 걸을 수 있는 면. 못 찾으면 false.
	const float WalkableZ = GetDefault<ASnarehavenCharacter>()->GetCharacterMovement()->GetWalkableFloorZ();
	auto GroundAt = [&](const FVector2D& XY, float AroundZ, float& OutZ)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(XY, AroundZ + TraceAboveCm), FVector(XY, AroundZ - TraceBelowCm), ECC_Pawn, Query)
			|| Hit.bStartPenetrating || Hit.ImpactNormal.Z < WalkableZ)
		{
			return false;
		}
		OutZ = Hit.ImpactPoint.Z;
		return true;
	};

	// 거점 하나를 잰다 — 상인 앞 바닥에서 시작, 둘레 ±OutpostHalfCm, 가장자리는 바깥으로 나가는 문.
	auto MeasureOutpost = [&](const AShopVendorActor* Vendor, bool bPrintMap, const FBox2D* PrintOnly = nullptr)
	{
		const FVector At = Vendor->GetActorLocation();
		const FVector2D Ahead = FVector2D(Vendor->GetActorForwardVector()).GetSafeNormal();
		const FBox2D Bounds(FVector2D(At) - FVector2D(OutpostHalfCm), FVector2D(At) + FVector2D(OutpostHalfCm));
		const FString Label = FString::Printf(TEXT("%s (%.0f,%.0f)"), *Vendor->GetName(), At.X, At.Y);
		FResult Result;
		for (const float Distance : OutpostStartAheadCm)
		{
			const FVector2D StartXY = FVector2D(At) + Ahead * Distance;
			float GroundZ = 0.f;
			if (!GroundAt(StartXY, At.Z, GroundZ))
			{
				continue;
			}
			Result = Measure(World, Bounds, FVector(StartXY, GroundZ + 10.f), GroundZ, true, Query, TEXT("OutpostTrap"), *Label, bPrintMap, PrintOnly != nullptr ? *PrintOnly : Bounds);
			if (Result.Trapped >= 0)
			{
				break;
			}
		}
		return Result;
	};

	// 칸 지도는 첫 거점 틀 둘레(상인 ±1600 — 오두막·울타리 a -1400 까지)와 갇힘이 난 거점만 찍는다.
	for (const AShopVendorActor* const Vendor : Vendors)
	{
		const FBox2D Frame(FVector2D(Vendor->GetActorLocation()) - FVector2D(1600.f), FVector2D(Vendor->GetActorLocation()) + FVector2D(1600.f));
		FResult Result = MeasureOutpost(Vendor, Vendor == Vendors[0], &Frame);
		if (Result.Trapped != 0)
		{
			Result = MeasureOutpost(Vendor, true);
		}
		const FString Name = Vendor->GetName();
		TestTrue(*FString::Printf(TEXT("%s: 상인 앞에서 바닥 500칸 넘게 갈 수 있다(시작 바닥을 찾았다)"), *Name), Result.Reached > 500);
		TestEqual(*FString::Printf(TEXT("%s: 가 놓고 못 돌아오는 바닥(갇힘) 0"), *Name), Result.Trapped, 0);
	}

	// 역검증 — 가장자리 문이 있어도 거점 안 갇힘을 무는지. 첫 거점 상인 옆(양옆 중 땅이 상인 발 높이에 가까운 쪽)에 우리를 세운다.
	UStaticMesh* const Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Vendors.Num() > 0 && TestNotNull(TEXT("기본 큐브"), Cube))
	{
		const AShopVendorActor* const Vendor = Vendors[0];
		const FVector At = Vendor->GetActorLocation();
		float BaseZ = At.Z;
		GroundAt(FVector2D(At), At.Z, BaseZ);
		FVector PenAt = FVector::ZeroVector;
		float BestGap = TNumericLimits<float>::Max();
		for (const float Side : { OutpostPenSideCm, -OutpostPenSideCm })
		{
			// 칸 가운데 = 상자 가운데가 되게 칸 격자(상인 자리 기준 50cm)에 맞춘다 — 어긋나면 계단 칸마다 캡슐이 이웃 상자에 닿는다.
			const FVector2D Offset = FVector2D(Vendor->GetActorRightVector()).GetSafeNormal() * Side;
			const FVector2D XY = FVector2D(At) + FVector2D(FMath::GridSnap(Offset.X, CellCm), FMath::GridSnap(Offset.Y, CellCm));
			float Z = 0.f;
			if (GroundAt(XY, BaseZ, Z) && FMath::Abs(Z - BaseZ) < BestGap)
			{
				BestGap = FMath::Abs(Z - BaseZ);
				PenAt = FVector(XY, Z);
			}
		}
		if (TestTrue(TEXT("역검증 우리 자리 땅"), BestGap < TNumericLimits<float>::Max()))
		{
			const TArray<AActor*> Spawned = SpawnTrapPen(World, Cube, PenAt);
			const FBox2D PenPrint(FVector2D(PenAt) - FVector2D(1000.f), FVector2D(PenAt) + FVector2D(1000.f));
			const FResult Caught = MeasureOutpost(Vendor, true, &PenPrint);
			UE_LOG(LogSnarehaven, Display, TEXT("[OutpostTrap] 역검증 우리 %s · 갇힘 %d"), *PenAt.ToCompactString(), Caught.Trapped);
			TestTrue(*FString::Printf(TEXT("역검증: 거점 옆 우리 안을 갇힘으로 문다 (갇힘 %d)"), Caught.Trapped), Caught.Trapped > 0);
			for (AActor* const Box : Spawned)
			{
				Box->Destroy();
			}
		}
	}

	DestroyTestWorld(World);
	return true;
}

// 칸 지도 모형은 높이만 보고 실제 이동 판정(턱 윗면에 발 디딤·한쪽 면 가로대)은 못 본다 — 감독 PIE 가 찾은 한쪽 길(칸 뒤 장식 울타리 줄
// `RanchDeco_Poles_*`, x=360 · 높이 36: 서→동은 넘고 동→서는 x=397 에서 멈춤)을 진짜 캐릭터 이동으로 왕복해 잰다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchRailWalkTest, "Snarehaven.World.RanchReach.SlotRailWalkBothWays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchRailWalkTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenRanchTrapTest;

	// 테스트 전용 값 — 감독 실측 줄(x=520 ↔ x=0, 4초 직진)과 같은 자리, 줄 x=360 을 지나 이만큼 더 가야 「건넜다」.
	constexpr float RailX = 360.f;
	constexpr float EastStartX = 520.f, WestStartX = 0.f;
	constexpr float CrossMarginCm = 60.f;
	constexpr float WalkSeconds = 4.f, TickSeconds = 1.f / 60.f;
	const float RowYs[] = { -300.f, 0.f, 300.f };
	// 걷기·달리기 쯤 속도 둘 — 손으로 만든 월드엔 캐릭터 Tick 이 안 돌아 엔진 기본 600 이 남는데, 600·30fps 로는 막는 줄도
	// 한 프레임에 뚫고 지나가 한쪽 길을 못 본다(이 바퀴 실험: 150~400 은 1/30~1/120 초 모두 동→서 x=397 에서 멈춤).
	const float Speeds[] = { 150.f, 400.f };

	ULevel* Level = nullptr;
	UWorld* const World = CreateWorldWithLevel(TEXT("/Game/Game/Maps/L_Ranch"), Level);
	if (!TestNotNull(TEXT("L_Ranch 스트리밍"), Level))
	{
		DestroyTestWorld(World);
		return false;
	}
	FAssetCompilingManager::Get().FinishAllCompilation();
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		World->Tick(LEVELTICK_All, 1.f / 30.f);
	}
	// 새 플레이어 칸 수(난간 줄이 서 있는 첫 줄만 열림)로 맞춘다 — 둘째 줄 난간(x=100)은 숨고 충돌이 꺼진다.
	URanchInteractComponent::ApplySlotVisibility(World, FirstOpenSlots);

	// 줄을 이루는 울타리 토막 — 줄 위(x 360±20)에 서 있는 레벨 정적 메시.
	TArray<UStaticMeshComponent*> Rails;
	for (const AActor* const Actor : Level->Actors)
	{
		const AStaticMeshActor* const MeshActor = Cast<AStaticMeshActor>(Actor);
		if (MeshActor != nullptr && FMath::Abs(MeshActor->GetActorLocation().X - RailX) < 20.f
			&& FMath::Abs(MeshActor->GetActorLocation().Y) < 600.f && MeshActor->GetStaticMeshComponent()->GetStaticMesh() != nullptr
			&& MeshActor->GetStaticMeshComponent()->GetStaticMesh()->GetName() == TEXT("SM_Building_Fence_02"))
		{
			Rails.Add(MeshActor->GetStaticMeshComponent());
		}
	}
	TestTrue(*FString::Printf(TEXT("칸 뒤 울타리 줄 토막 %d개를 찾았다"), Rails.Num()), Rails.Num() >= FirstOpenSlots);
	for (const UStaticMeshComponent* const Rail : Rails)
	{
		UE_LOG(LogSnarehaven, Display, TEXT("[RailWalk] 토막 %s · 폰 응답 %d · 충돌 %d · 프로필 %s"), *NameOf(Rail),
			static_cast<int32>(Rail->GetCollisionResponseToChannel(ECC_Pawn)), static_cast<int32>(Rail->GetCollisionEnabled()), *Rail->GetCollisionProfileName().ToString());
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// 한 줄을 걸어 본다 — 시작 자리에서 Dir(±X) 로 WalkSeconds 동안 입력, 끝 x.
	auto Walk = [&](float FromX, float Y, float Dir, float Speed)
	{
		ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(ASnarehavenCharacter::StaticClass(),
			FTransform(FVector(FromX, Y, CapsuleHalfHeightCm + 20.f)), Params);
		if (Player == nullptr)
		{
			return FromX;
		}
		Player->SpawnDefaultController();
		UCharacterMovementComponent* const Movement = Player->GetCharacterMovement();
		Movement->MaxWalkSpeed = Speed;
		Movement->SetMovementMode(MOVE_Falling);
		// 손으로 만든 월드에선 컴포넌트 틱이 안 돈다 — 이동 컴포넌트 틱을 직접 부른다(`Character.MoveAxis` 와 같은 방법).
		for (float T = 0.f; T < 0.5f; T += TickSeconds)
		{
			Movement->TickComponent(TickSeconds, LEVELTICK_All, nullptr);   // 바닥에 내려앉기
		}
		for (float T = 0.f; T < WalkSeconds; T += TickSeconds)
		{
			Player->AddMovementInput(FVector(Dir, 0.f, 0.f), 1.f);
			Movement->TickComponent(TickSeconds, LEVELTICK_All, nullptr);
		}
		const float EndX = static_cast<float>(Player->GetActorLocation().X);
		UE_LOG(LogSnarehaven, Display, TEXT("[RailWalk] 속도 %.0f · y=%.0f %.0f → %.0f (%s) · 발 높이 %.0f · 땅 위 %d"), Speed, Y, FromX, EndX, Dir < 0.f ? TEXT("동→서") : TEXT("서→동"),
			Player->GetActorLocation().Z - CapsuleHalfHeightCm, Movement->IsMovingOnGround() ? 1 : 0);
		if (AController* const Controller = Player->GetController())
		{
			Controller->Destroy();
		}
		Player->Destroy();
		return EndX;
	};

	// 몇 줄이 건넜나 — 동→서·서→동 각각.
	auto CountCrossings = [&](int32& OutWestward, int32& OutEastward)
	{
		OutWestward = OutEastward = 0;
		for (const float Speed : Speeds)
		{
			for (const float Y : RowYs)
			{
				OutWestward += Walk(EastStartX, Y, -1.f, Speed) < RailX - CrossMarginCm ? 1 : 0;
				OutEastward += Walk(WestStartX, Y, 1.f, Speed) > RailX + CrossMarginCm ? 1 : 0;
			}
		}
	};

	const int32 Tries = static_cast<int32>(UE_ARRAY_COUNT(Speeds) * UE_ARRAY_COUNT(RowYs));
	int32 Westward = 0, Eastward = 0;
	CountCrossings(Westward, Eastward);
	UE_LOG(LogSnarehaven, Display, TEXT("[RailWalk] 저장된 맵: 동→서 %d/%d · 서→동 %d/%d"), Westward, Tries, Eastward, Tries);
	TestEqual(TEXT("동→서 줄마다 울타리 줄을 건넌다(들어간 쪽에서 되돌아 나온다)"), Westward, Tries);
	TestEqual(TEXT("서→동 줄마다 울타리 줄을 건넌다"), Eastward, Tries);

	// 역검증 — 줄이 옛날처럼 몸을 막으면(저장된 맵은 그대로, 이 시험 월드 안 응답만 바꿈) 한쪽 길이 나와 이 시험이 문다.
	for (UStaticMeshComponent* const Rail : Rails)
	{
		Rail->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		// 밧줄 울타리(M99-04)를 칸 난간 크기로 줄이면 높이 37cm 라 디딤 높이로 넘는다 — 막는 높이(4배)로 세워 이 시험의 눈이 살아 있게 한다.
		Rail->SetWorldScale3D(Rail->GetComponentScale() * FVector(1.f, 1.f, 4.f));
	}
	CountCrossings(Westward, Eastward);
	UE_LOG(LogSnarehaven, Display, TEXT("[RailWalk] 역검증 막는 줄: 동→서 %d/%d · 서→동 %d/%d"), Westward, Tries, Eastward, Tries);
	TestTrue(*FString::Printf(TEXT("역검증: 막는 줄이면 동→서가 막힌다 (건넌 줄 %d)"), Westward), Westward < Tries);

	DestroyTestWorld(World);
	return true;
}

#endif
