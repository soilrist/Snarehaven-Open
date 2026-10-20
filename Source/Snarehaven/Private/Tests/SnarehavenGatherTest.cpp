// ROADMAP M1-06 2층 검사 — 채집이 DESIGN 3.3 대로 되는지:
// 사거리 안 가장 가까운 오브젝트를 터치 즉시 획득하고, 집은 오브젝트는 사라지고 **리스폰하지 않으며**,
// 획득은 **지급 요청 대기열에만** 쌓이는지(보유는 백엔드 — CLAUDE.md 8).
// 마지막 검사는 실제 `DT_GatherableTable` 행을 읽어 무엇을 몇 개 주는지 행에서만 가져온다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Combat/MeleeAttackComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Data/GatherableRow.h"
#include "Data/ItemAction.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Equipment/EquipmentComponent.h"
#include "Gathering/GatherComponent.h"
#include "Gathering/GatherableActor.h"
#include "Inventory/InventoryComponent.h"
#include "SnarehavenCharacter.h"
#include "Player/SnareHaptics.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	const FName TestGatherableID = TEXT("gatherable.test_bush");
	const FName TestItemID = TEXT("item.test_berry");
	constexpr int32 TestGrantCount = 2;
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestRangeCm = 300.f;
	constexpr float TestIntervalSeconds = 1.f;
	constexpr float TestNearDistanceCm = 100.f;
	constexpr float TestFarButInRangeDistanceCm = 250.f;
	constexpr float TestOutOfRangeDistanceCm = 1000.f;

	// 실제 데이터 테이블 (M1-06). 주는 아이템·개수는 여기서만 온다 — 기대값을 코드에 적지 않는다.
	const TCHAR* GatherableTablePath = TEXT("/Game/Game/Data/Gatherables/DT_GatherableTable.DT_GatherableTable");
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");

	const FItemRow* FindItem(const UDataTable* ItemTable, FName ItemID)
	{
		for (const TPair<FName, uint8*>& ItemPair : ItemTable->GetRowMap())
		{
			const FItemRow* ItemRow = reinterpret_cast<const FItemRow*>(ItemPair.Value);
			if (ItemRow != nullptr && ItemRow->ItemID == ItemID)
			{
				return ItemRow;
			}
		}
		return nullptr;
	}

	/** 채집물이 줄 수 있는 아이템 전부 — 후보가 있으면 후보, 없으면 고정 아이템 하나. */
	TArray<FName> GrantCandidates(const FGatherableRow& Row)
	{
		TArray<FName> IDs;
		for (const FGatherGrantOption& Option : Row.GrantOptions)
		{
			IDs.Add(Option.ItemID);
		}
		if (IDs.IsEmpty())
		{
			IDs.Add(Row.GrantItemID);
		}
		return IDs;
	}

	UWorld* CreateTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	// 테스트 전용 아이템·개수로 채운 채집물. 실제 테이블을 읽지 않는다.
	AGatherableActor* SpawnTestGatherable(UWorld* World, float DistanceCm,
		FName ItemID = TestItemID, int32 Count = TestGrantCount)
	{
		AGatherableActor* Gatherable =
			World->SpawnActor<AGatherableActor>(FVector(DistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		if (Gatherable == nullptr)
		{
			return nullptr;
		}

		FGatherableRow Row;
		Row.GatherableID = TestGatherableID;
		Row.GrantItemID = ItemID;
		Row.GrantCount = Count;
		Gatherable->InitializeFromRow(Row);
		return Gatherable;
	}

	ASnarehavenCharacter* SpawnTestPlayer(UWorld* World)
	{
		ASnarehavenCharacter* Player =
			World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Player == nullptr)
		{
			return nullptr;
		}

		// 채집 사거리는 근접 공격 컴포넌트에서 빌려 오므로, 테스트 전용 사거리도 거기에 넣는다.
		Player->GetMeleeAttack()->SetAttackSettings(TestAttackDamage, TestRangeCm, TestIntervalSeconds);
		return Player;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGatherNoTargetTest, "Snarehaven.Gather.Action.NoTargetDoesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGatherNoTargetTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AGatherableActor* FarBush = SpawnTestGatherable(World, TestOutOfRangeDistanceCm);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("사거리 밖 채집물"), FarBush))
	{
		DestroyTestWorld(World);
		return false;
	}

	UGatherComponent* Gather = Player->GetGather();
	if (!TestNotNull(TEXT("채집 컴포넌트가 붙어 있다"), Gather))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 사거리는 근접 공격 컴포넌트에서 빌려 온다 — 두 군데로 갈리면 안 되니 같은 값인지 본다.
	TestEqual(TEXT("채집 사거리는 자동 상호작용 사거리와 같다"),
		Gather->GetGatherRangeCm(), Player->GetMeleeAttack()->GetAttackRangeCm());

	TestNull(TEXT("사거리 밖이면 채집 대상이 없다"), Gather->RefreshTarget());
	TestFalse(TEXT("대상이 없으면 채집 버튼이 안 켜진다"), Gather->CanGather());
	TestFalse(TEXT("대상이 없으면 채집도 안 된다"), Gather->TryGatherNearest());
	TestFalse(TEXT("사거리 밖 채집물은 그대로 남아 있다"), FarBush->IsGathered());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGatherNearestTest, "Snarehaven.Gather.Action.PicksNearest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGatherNearestTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AGatherableActor* NearBush = SpawnTestGatherable(World, TestNearDistanceCm);
	AGatherableActor* FarBush = SpawnTestGatherable(World, TestFarButInRangeDistanceCm);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player)
		|| !TestNotNull(TEXT("가까운 채집물"), NearBush) || !TestNotNull(TEXT("먼 채집물"), FarBush))
	{
		DestroyTestWorld(World);
		return false;
	}

	UGatherComponent* Gather = Player->GetGather();
	TestTrue(TEXT("둘 중 가까운 쪽이 대상이다"), Gather->RefreshTarget() == NearBush);
	TestTrue(TEXT("채집 버튼이 켜진다"), Gather->CanGather());
	TestTrue(TEXT("가까운 쪽을 집는다"), Gather->TryGatherNearest());

	// 집은 뒤에는 남은 쪽으로 하이라이트가 넘어간다 (사거리 안에 아직 하나 있으므로).
	TestTrue(TEXT("남은 채집물이 다음 대상이 된다"), Gather->GetGatherTarget() == FarBush);
	TestFalse(TEXT("남은 채집물은 아직 안 집혔다"), FarBush->IsGathered());

	DestroyTestWorld(World);
	return true;
}

// 진짜 캐릭터 위치는 캡슐 중심(땅 위)이고 채집물·나무는 뿌리가 땅이다 — 높이 차가 사거리를 먹으면 안 된다
// (폰 채집 0회 결함, 루프#410: 수평 122cm·높이 90cm 나무가 3차원 151.5cm 로 사거리 150cm 밖이었다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGatherGroundTargetTest, "Snarehaven.Gather.Action.GroundTargetAtCapsuleHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGatherGroundTargetTest::RunTest(const FString& Parameters)
{
	// 시험 전용 높이 — 수평은 사거리 안(250 < 300)인데 3차원으론 밖(√(250²+200²) ≈ 320 > 300)이 되게 고른다.
	constexpr float TestCapsuleHeightCm = 200.f;

	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AGatherableActor* GroundBush = SpawnTestGatherable(World, TestFarButInRangeDistanceCm);
	AGatherableActor* FarBush = SpawnTestGatherable(World, -TestOutOfRangeDistanceCm);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player)
		|| !TestNotNull(TEXT("땅 위 채집물"), GroundBush) || !TestNotNull(TEXT("사거리 밖 채집물"), FarBush))
	{
		DestroyTestWorld(World);
		return false;
	}
	Player->SetActorLocation(FVector(0.f, 0.f, TestCapsuleHeightCm), false, nullptr, ETeleportType::TeleportPhysics);

	UGatherComponent* Gather = Player->GetGather();
	const float Distance3D = FVector::Dist(Player->GetActorLocation(), GroundBush->GetActorLocation());
	AddInfo(FString::Printf(TEXT("[GatherGround] 수평 %.1f · 3차원 %.1f · 사거리 %.1f"),
		FVector::Dist2D(Player->GetActorLocation(), GroundBush->GetActorLocation()), Distance3D, Gather->GetGatherRangeCm()));
	TestTrue(TEXT("3차원 거리는 사거리 밖이다(시험 전제)"), Distance3D > Gather->GetGatherRangeCm());
	TestTrue(TEXT("수평 사거리 안의 땅 채집물이 대상이다"), Gather->RefreshTarget() == GroundBush);
	TestTrue(TEXT("눌러서 집힌다"), Gather->TryGatherNearest());
	TestTrue(TEXT("집혔다"), GroundBush->IsGathered());
	TestTrue(TEXT("수평 사거리 밖은 대상이 아니다"), Gather->RefreshTarget() == nullptr);
	TestFalse(TEXT("사거리 밖은 안 집힌다"), FarBush->IsGathered());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGatherNoRespawnTest, "Snarehaven.Gather.Action.GatheredStaysGone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGatherNoRespawnTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AGatherableActor* Bush = SpawnTestGatherable(World, TestNearDistanceCm);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("채집물 스폰"), Bush))
	{
		DestroyTestWorld(World);
		return false;
	}

	UGatherComponent* Gather = Player->GetGather();
	TestTrue(TEXT("한 번은 집힌다"), Gather->TryGatherNearest());
	TestTrue(TEXT("집은 오브젝트는 채집 표시가 남는다"), Bush->IsGathered());
	TestFalse(TEXT("집은 오브젝트는 사라진다 (DESIGN 3.3 — 리스폰 없음)"), IsValid(Bush));

	// 같은 자리에서 다시 눌러도 아무것도 없다 — MVP 는 리스폰이 없으므로 계속 비어 있어야 한다.
	TestNull(TEXT("다시 찾아도 대상이 없다"), Gather->RefreshTarget());
	TestFalse(TEXT("두 번째 채집은 실패한다"), Gather->TryGatherNearest());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGatherPendingGrantTest, "Snarehaven.Gather.Item.QueuesPendingGrant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGatherPendingGrantTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AGatherableActor* Bush = SpawnTestGatherable(World, TestNearDistanceCm);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("채집물 스폰"), Bush))
	{
		DestroyTestWorld(World);
		return false;
	}

	UInventoryComponent* Inventory = Player->GetInventory();
	TestTrue(TEXT("채집 성공"), Player->GetGather()->TryGatherNearest());

	// CLAUDE.md 8 · DESIGN 4.5 — 채집 재료도 보유 원본은 백엔드다. 클라이언트는 요청만 쌓는다.
	TestEqual(TEXT("행 개수만큼 지급 요청 대기열에 쌓였다"),
		Inventory->GetPendingCount(TestItemID), TestGrantCount);
	TestEqual(TEXT("보유 수량은 아직 0 (백엔드 응답 전)"), Inventory->GetItemCount(TestItemID), 0);
	TestEqual(TEXT("보유 칸도 그대로 비어 있다"), Inventory->GetSlots().Num(), 0);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGatherUnsetStaysOffTest, "Snarehaven.Gather.Item.UnsetNumbersStayOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGatherUnsetStaysOffTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	// 개수가 아직 미정(-1)인 행 — 안 채운 행을 대신한다 (Verification.md 7).
	AGatherableActor* UnsetBush = SpawnTestGatherable(World, TestNearDistanceCm, TestItemID, -1);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("미정 채집물 스폰"), UnsetBush))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 컴포넌트가 남기는 경고(지급 미정)는 그대로 로그에 두고 AddExpectedError 로 잡지 않는다 —
	// 기존 검사들(Drop.UnsetNumbersStayOff)과 같은 취급이고, 경고는 검사를 실패시키지 않는다.
	UGatherComponent* Gather = Player->GetGather();
	TestFalse(TEXT("미정 채집물은 채집 후보가 아니다"), UnsetBush->CanGather());
	TestNull(TEXT("미정 채집물은 하이라이트도 안 된다"), Gather->RefreshTarget());
	TestFalse(TEXT("미정 채집물은 안 집힌다"), Gather->TryGatherNearest());
	TestTrue(TEXT("미정 채집물은 사라지지도 않는다"), IsValid(UnsetBush));
	TestEqual(TEXT("지급 요청은 하나도 쌓이지 않았다"), Player->GetInventory()->GetPendingGrants().Num(), 0);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGatherTableTest, "Snarehaven.Gather.Table.RowsGrantTableItems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGatherTableTest::RunTest(const FString& Parameters)
{
	UDataTable* GatherableTable = LoadObject<UDataTable>(nullptr, GatherableTablePath);
	UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_GatherableTable 로드"), GatherableTable)
		|| !TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}

	// 몇 종인지도 코드에 적지 않는다 — 행이 늘면 그만큼 돈다 (DataDriven.md 1).
	TArray<FName> RowNames = GatherableTable->GetRowNames();
	if (!TestTrue(TEXT("채집물 행이 하나 이상 있다"), RowNames.Num() > 0))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("DT_GatherableTable 행 %d개를 실제로 집어 본다"), RowNames.Num()));

	for (const FName& RowName : RowNames)
	{
		const FGatherableRow* Row =
			GatherableTable->FindRow<FGatherableRow>(RowName, TEXT("GatherTableTest"));
		if (!TestNotNull(*FString::Printf(TEXT("%s 행"), *RowName.ToString()), Row))
		{
			continue;
		}

		UWorld* World = CreateTestWorld();

		// 여기서는 사거리도 갈아 끼우지 않는다 — DESIGN 3.1 확정값과 테이블 값으로 실제와 같은 길을 탄다.
		ASnarehavenCharacter* Player =
			World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		AGatherableActor* Gatherable =
			World->SpawnActor<AGatherableActor>(FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("채집물 스폰"), Gatherable))
		{
			DestroyTestWorld(World);
			continue;
		}

		Gatherable->SetGatherableData(GatherableTable, RowName);
		if (!TestTrue(*FString::Printf(TEXT("%s 행으로 초기화"), *RowName.ToString()),
			Gatherable->InitializeFromTable()))
		{
			DestroyTestWorld(World);
			continue;
		}

		// 도구가 필요한 행(나무 — DESIGN 8.10)은 맨손 거절만 본다. 도구·굴림·재생은 Snarehaven.Base.Tree 가 맡는다.
		if (!Row->RequiredToolItemID.IsNone())
		{
			TestFalse(*FString::Printf(TEXT("%s 맨손 채집은 거절"), *RowName.ToString()),
				Player->GetGather()->TryGatherNearest());
			TestEqual(TEXT("거절이면 지급 요청도 없다"), Player->GetInventory()->GetPendingGrants().Num(), 0);
		}
		else
		{
			if (!TestTrue(*FString::Printf(TEXT("%s 채집 성공"), *RowName.ToString()),
				Player->GetGather()->TryGatherNearest()))
			{
				DestroyTestWorld(World);
				continue;
			}

			// 기대값을 코드에 적지 않는다 — 행에 적힌 아이템·개수를 그대로 비교한다 (DataDriven.md 3).
			// 채집 더미(13.4)는 굴린 아이템이 후보 중 하나다 — 굴린 결과를 채집 뒤에 읽는다.
			const FName Granted = Gatherable->GetGrantItemID();
			const int32 PendingCount = Player->GetInventory()->GetPendingCount(Granted);
			AddInfo(FString::Printf(TEXT("[%s] 채집 → %s %d개 지급 요청 (행 %d개 · 보유 %d)"),
				*Row->GatherableID.ToString(), *Granted.ToString(), PendingCount, Row->GrantCount,
				Player->GetInventory()->GetItemCount(Granted)));
			TestTrue(TEXT("준 아이템은 행의 후보 중 하나다"), GrantCandidates(*Row).Contains(Granted));
			TestEqual(TEXT("행에 적힌 개수만큼 지급 요청이 쌓였다"), PendingCount, Row->GrantCount);
			TestEqual(TEXT("보유 수량은 여전히 0 (백엔드 몫)"),
				Player->GetInventory()->GetItemCount(Granted), 0);
		}

		// 줄 수 있는 아이템 ID 가 아이템 테이블에 실제로 있는 행인지도 본다 — 없으면 백엔드가 못 알아듣는다.
		// 재료가 기본이고, 날로 먹는 채집물(13.4 광대버섯)만 음식이다.
		for (const FName& Candidate : GrantCandidates(*Row))
		{
			const FItemRow* GrantedItemRow = FindItem(ItemTable, Candidate);
			if (TestNotNull(*FString::Printf(TEXT("%s 가 주는 %s 가 DT_ItemTable 에 있다"),
				*RowName.ToString(), *Candidate.ToString()), GrantedItemRow))
			{
				TestTrue(*FString::Printf(TEXT("%s 는 재료나 음식이다"), *Candidate.ToString()),
					GrantedItemRow->ItemType == EItemType::Material || GrantedItemRow->ItemType == EItemType::Food);
			}
		}

		DestroyTestWorld(World);
	}

	return true;
}

// ROADMAP M13-02: 채집 더미가 가중치대로 하나를 고르는지 — 굴림 값을 직접 넣어 경계를 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGatherWeightedPickTest, "Snarehaven.Gather.Piles.WeightedPick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGatherWeightedPickTest::RunTest(const FString& Parameters)
{
	TArray<FGatherGrantOption> Options;
	Options.Add({ FName(TEXT("test_a")), 3 });
	Options.Add({ FName(TEXT("test_zero")), 0 });
	Options.Add({ FName(TEXT("test_b")), 1 });

	TestEqual(TEXT("굴림 0 → 첫 후보"), AGatherableActor::PickWeightedItem(Options, 0), FName(TEXT("test_a")));
	TestEqual(TEXT("굴림 2 → 첫 후보 끝"), AGatherableActor::PickWeightedItem(Options, 2), FName(TEXT("test_a")));
	TestEqual(TEXT("굴림 3 → 가중치 0 은 건너뛰고 셋째"), AGatherableActor::PickWeightedItem(Options, 3), FName(TEXT("test_b")));
	TestEqual(TEXT("굴림이 합을 넘으면 없음"), AGatherableActor::PickWeightedItem(Options, 4), FName(NAME_None));
	return true;
}

// ROADMAP M13-02: DESIGN 13.4 표의 더미·아이템·가중치·개수·다시 나기·광대버섯 −20 이 테이블과 같은지.
// 숫자를 코드에 적지 않는다 — 설계서를 읽어 테이블과 맞춘다 (DataDriven.md 3).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGatherPilesDesignTest, "Snarehaven.Gather.Piles.MatchDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGatherPilesDesignTest::RunTest(const FString& Parameters)
{
	UDataTable* GatherableTable = LoadObject<UDataTable>(nullptr, GatherableTablePath);
	UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	FString Doc;
	if (!TestNotNull(TEXT("DT_GatherableTable 로드"), GatherableTable)
		|| !TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable)
		|| !TestTrue(TEXT("DESIGN.md 읽기"),
			FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}

	const int32 Start = Doc.Find(TEXT("### 13.4"));
	if (!TestTrue(TEXT("DESIGN 13.4 절이 있다"), Start != INDEX_NONE))
	{
		return false;
	}
	const int32 End = Doc.Find(TEXT("\n### "), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + 1);
	const FString Section = End == INDEX_NONE ? Doc.Mid(Start) : Doc.Mid(Start, End - Start);

	int32 DesignCount = -1;
	float DesignRespawn = -1.f;
	int32 DesignPoison = -1;
	TArray<FString> Lines;
	Section.ParseIntoArrayLines(Lines);

	for (const FString& Line : Lines)
	{
		FRegexMatcher Once(FRegexPattern(TEXT("^\\| 한 번에 \\| \\*\\*([0-9]+)\\*\\*개 · 다시 나기 \\*\\*([0-9]+)초\\*\\*")), Line);
		if (Once.FindNext())
		{
			DesignCount = FCString::Atoi(*Once.GetCaptureGroup(1));
			DesignRespawn = FCString::Atof(*Once.GetCaptureGroup(2));
		}
		FRegexMatcher Poison(FRegexPattern(TEXT("^\\| 광대버섯 먹기 \\| 체력 \\*\\*[−-]([0-9]+)\\*\\*")), Line);
		if (Poison.FindNext())
		{
			DesignPoison = FCString::Atoi(*Poison.GetCaptureGroup(1));
		}
	}
	if (!TestTrue(TEXT("13.4 한 번에·다시 나기·광대버섯 값을 읽었다"), DesignCount > 0 && DesignRespawn > 0.f && DesignPoison > 0))
	{
		return false;
	}

	int32 PilesChecked = 0;
	for (const FString& Line : Lines)
	{
		FRegexMatcher PileMatch(FRegexPattern(TEXT("^\\| [^|]*`(gatherable\\.[a-z_]+)`")), Line);
		if (!PileMatch.FindNext())
		{
			continue;
		}
		const FName PileID(*PileMatch.GetCaptureGroup(1));
		const int32 CellStart = Line.Find(TEXT("|"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
		const FString ValueCell = Line.Mid(CellStart);

		TArray<FName> DesignItems;
		FRegexMatcher ItemMatch(FRegexPattern(TEXT("`(item\\.[a-z_]+)`")), ValueCell);
		while (ItemMatch.FindNext())
		{
			DesignItems.Add(FName(*ItemMatch.GetCaptureGroup(1)));
		}
		TArray<int32> DesignWeights;
		FRegexMatcher WeightMatch(FRegexPattern(TEXT("\\*\\*([0-9]+(?: : [0-9]+)+)\\*\\*")), ValueCell);
		if (WeightMatch.FindNext())
		{
			TArray<FString> Parts;
			WeightMatch.GetCaptureGroup(1).ParseIntoArray(Parts, TEXT(" : "));
			for (const FString& Part : Parts)
			{
				DesignWeights.Add(FCString::Atoi(*Part));
			}
		}
		else
		{
			DesignWeights.Init(1, DesignItems.Num()); // "균등" 또는 한 가지
		}

		const FGatherableRow* Row = nullptr;
		for (const TPair<FName, uint8*>& Pair : GatherableTable->GetRowMap())
		{
			const FGatherableRow* Candidate = reinterpret_cast<const FGatherableRow*>(Pair.Value);
			if (Candidate != nullptr && Candidate->GatherableID == PileID)
			{
				Row = Candidate;
			}
		}
		if (!TestNotNull(*FString::Printf(TEXT("%s 행이 테이블에 있다"), *PileID.ToString()), Row)
			|| !TestEqual(*FString::Printf(TEXT("%s 가중치 칸 수"), *PileID.ToString()), DesignWeights.Num(), DesignItems.Num()))
		{
			continue;
		}
		++PilesChecked;

		// 후보가 하나면 GrantItemID 한 칸으로 둬도 같다.
		const TArray<FName> TableItems = GrantCandidates(*Row);
		TestEqual(*FString::Printf(TEXT("%s 아이템 목록"), *PileID.ToString()),
			FString::JoinBy(TableItems, TEXT(","), [](const FName& N) { return N.ToString(); }),
			FString::JoinBy(DesignItems, TEXT(","), [](const FName& N) { return N.ToString(); }));
		if (Row->GrantOptions.Num() > 0)
		{
			for (int32 i = 0; i < FMath::Min(Row->GrantOptions.Num(), DesignWeights.Num()); ++i)
			{
				TestEqual(*FString::Printf(TEXT("%s %s 가중치"), *PileID.ToString(), *Row->GrantOptions[i].ItemID.ToString()),
					Row->GrantOptions[i].Weight, DesignWeights[i]);
			}
		}
		for (const FName& Item : DesignItems)
		{
			TestNotNull(*FString::Printf(TEXT("%s 가 DT_ItemTable 에 있다"), *Item.ToString()), FindItem(ItemTable, Item));
		}
		TestEqual(*FString::Printf(TEXT("%s 한 번에"), *PileID.ToString()), Row->GrantCount, DesignCount);
		TestTrue(*FString::Printf(TEXT("%s 개수 굴림 없음"), *PileID.ToString()), Row->GrantCountMax <= Row->GrantCount);
		TestEqual(*FString::Printf(TEXT("%s 다시 나기"), *PileID.ToString()), Row->RespawnSeconds, DesignRespawn);
		TestTrue(*FString::Printf(TEXT("%s 맨손"), *PileID.ToString()), Row->RequiredToolItemID.IsNone());
		AddInfo(FString::Printf(TEXT("[%s] %s · 가중치 %d칸 · %d개 · %.0f초 — 13.4 와 대조"),
			*PileID.ToString(), *FString::JoinBy(TableItems, TEXT("/"), [](const FName& N) { return N.ToString(); }),
			DesignWeights.Num(), Row->GrantCount, Row->RespawnSeconds));
	}
	TestTrue(TEXT("13.4 더미 5종 이상을 대조했다"), PilesChecked >= 5);

	const FItemRow* FlyAmanita = FindItem(ItemTable, FName(TEXT("item.fly_amanita")));
	if (TestNotNull(TEXT("광대버섯 아이템"), FlyAmanita))
	{
		TestTrue(TEXT("광대버섯은 먹을 수 있다(음식)"), FlyAmanita->ItemType == EItemType::Food);
		TestEqual(TEXT("광대버섯 먹기 = 13.4 체력 −"), FlyAmanita->HealAmount, -DesignPoison);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGatherToolUsesTest, "Snarehaven.Gather.Tools.BonusToolCountsUse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// DESIGN 13.5 — 보너스 도구(전기톱·마체테)를 도구 칸에 들고 캔 번만 대기열 줄의 `ToolUses` 가 는다.
// +1 자체는 서버(ClaimLoot)가 가방을 보고 정한다 — 여기선 「도구로 얻은 번」이 제대로 세어지는지만 본다.
bool FSnarehavenGatherToolUsesTest::RunTest(const FString& Parameters)
{
	const FName Chainsaw = TEXT("item.chainsaw");
	const FName Hatchet = TEXT("item.hatchet");
	const FName Machete = TEXT("item.machete");
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("장비"), Player->GetEquipment()))
	{
		DestroyTestWorld(World);
		return false;
	}
	// 도구 행만 테스트 값으로 바꾼 채집물 (아이템·개수는 테스트 전용 — Verification.md 6).
	auto Gather = [&](FName Required, FName Bonus, FName Held) -> bool
	{
		AGatherableActor* const Target = World->SpawnActor<AGatherableActor>(FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		FGatherableRow Row;
		Row.GatherableID = TestGatherableID;
		Row.GrantItemID = TestItemID;
		Row.GrantCount = TestGrantCount;
		Row.RequiredToolItemID = Required;
		Row.BonusToolItemID = Bonus;
		if (Target == nullptr || !Target->InitializeFromRow(Row))
		{
			return false;
		}
		// 도구 행은 메시가 아직 없어(8.11 — 메시 대기) `EquipItem` 이 못 건다. 호스트 목록을 옮기는 길로 칸만 채운다.
		TArray<FEquippedSlot> Slots;
		if (!Held.IsNone())
		{
			FEquippedSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.Slot = EEquipSlot::Tool;
			Slot.ItemRowName = Player->GetEquipment()->FindEquipmentRowByItemID(Held);
			TestFalse(*FString::Printf(TEXT("%s 는 표의 장비 행이다"), *Held.ToString()), Slot.ItemRowName.IsNone());
		}
		Player->GetEquipment()->ApplyEquippedSlots(Slots);
		const bool bGathered = Player->GetGather()->TryGatherNearest();
		if (IsValid(Target))
		{
			Target->Destroy();
		}
		return bGathered;
	};
	auto ToolUses = [&]() -> int32
	{
		const FInventorySlot* const Line = Player->GetInventory()->GetPendingGrants().FindByPredicate(
			[](const FInventorySlot& Slot) { return Slot.ItemID == TestItemID; });
		return Line != nullptr ? Line->ToolUses : -1;
	};

	TestTrue(TEXT("전기톱으로 나무를 벤다(손도끼 대신)"), Gather(Hatchet, Chainsaw, Chainsaw));
	TestEqual(TEXT("전기톱 → 도구로 얻은 번 1"), ToolUses(), 1);
	TestTrue(TEXT("손도끼로 벤다"), Gather(Hatchet, Chainsaw, Hatchet));
	TestEqual(TEXT("손도끼는 번을 안 센다"), ToolUses(), 1);
	TestFalse(TEXT("마체테로는 나무를 못 벤다"), Gather(Hatchet, Chainsaw, Machete));
	TestTrue(TEXT("마체테로 덤불을 딴다"), Gather(NAME_None, Machete, Machete));
	TestEqual(TEXT("마체테 → 번 +1"), ToolUses(), 2);
	TestTrue(TEXT("맨손으로 덤불을 딴다"), Gather(NAME_None, Machete, NAME_None));
	TestTrue(TEXT("보너스 없는 덤불을 마체테로 딴다"), Gather(NAME_None, NAME_None, Machete));
	TestEqual(TEXT("맨손·보너스 없는 채집물은 번을 안 센다"), ToolUses(), 2);
	TestEqual(TEXT("개수는 다섯 번 다 쌓였다"), Player->GetInventory()->GetPendingCount(TestItemID), TestGrantCount * 5);
	AddInfo(FString::Printf(TEXT("[Gather] 도구 번 %d · 대기 %d개"), ToolUses(), Player->GetInventory()->GetPendingCount(TestItemID)));

	// 여러 번 쳐야 쓰러지는 나무 — 손도끼는 한 번에 안 넘어가고 전동 톱은 한 번에 넘어간다 (사용자 2026-10-16 「전기톱은 금방 베야」).
	auto FellsInOneHit = [&](FName Held) -> bool
	{
		AGatherableActor* const Tree = World->SpawnActor<AGatherableActor>(FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		FGatherableRow Row;
		Row.GatherableID = TestGatherableID;
		Row.GrantItemID = TestItemID;
		Row.GrantCount = TestGrantCount;
		Row.HitsToFell = 3;
		Row.RequiredToolItemID = Hatchet;
		Row.BonusToolItemID = Chainsaw;
		if (Tree == nullptr || !Tree->InitializeFromRow(Row))
		{
			return false;
		}
		TArray<FEquippedSlot> Slots;
		FEquippedSlot& Slot = Slots.AddDefaulted_GetRef();
		Slot.Slot = EEquipSlot::Tool;
		Slot.ItemRowName = Player->GetEquipment()->FindEquipmentRowByItemID(Held);
		Player->GetEquipment()->ApplyEquippedSlots(Slots);
		const int32 Before = Player->GetInventory()->GetPendingCount(TestItemID);
		TestTrue(*FString::Printf(TEXT("%s 로 한 번 친다"), *Held.ToString()), Player->GetGather()->TryGatherNearest());
		const bool bFelled = Player->GetInventory()->GetPendingCount(TestItemID) > Before;
		if (IsValid(Tree))
		{
			Tree->Destroy();
		}
		return bFelled;
	};
	TestFalse(TEXT("세 번 나무는 손도끼 한 번으로 안 쓰러진다"), FellsInOneHit(Hatchet));
	TestTrue(TEXT("세 번 나무도 전동 톱은 한 번에 쓰러뜨린다"), FellsInOneHit(Chainsaw));

	// DESIGN 8.10 덤 수지 — 벤 번마다 굴린 만큼 대기열에 따로 쌓이고, 전기톱 번은 본 지급 줄에만 센다.
	const FName Resin = TEXT("item.resin");
	int32 ExpectedResin = 0;
	for (int32 Try = 0; Try < 10; ++Try)
	{
		AGatherableActor* const Tree = World->SpawnActor<AGatherableActor>(FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		FGatherableRow Row;
		Row.GatherableID = TestGatherableID;
		Row.GrantItemID = TestItemID;
		Row.GrantCount = TestGrantCount;
		Row.ExtraItemID = Resin;
		Row.ExtraCountMax = 2;
		if (!TestTrue(TEXT("덤 나무가 켜진다"), Tree != nullptr && Tree->InitializeFromRow(Row)))
		{
			break;
		}
		ExpectedResin += Tree->GetExtraCount();
		TestTrue(TEXT("덤 나무를 벤다"), Player->GetGather()->TryGatherNearest());
		if (IsValid(Tree))
		{
			Tree->Destroy();
		}
	}
	TestTrue(TEXT("10번 중 수지가 한 번은 나온다"), ExpectedResin > 0);
	TestEqual(TEXT("굴린 수지가 그대로 대기열에"), Player->GetInventory()->GetPendingCount(Resin), ExpectedResin);
	const FInventorySlot* const ResinLine = Player->GetInventory()->GetPendingGrants().FindByPredicate(
		[&Resin](const FInventorySlot& Slot) { return Slot.ItemID == Resin; });
	TestTrue(TEXT("수지 줄은 도구 번을 안 센다"), ResinLine == nullptr || ResinLine->ToolUses == 0);

	DestroyTestWorld(World);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGatherBonusToolActionTest, "Snarehaven.Gather.Tools.BonusToolPlaysToolAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M28-09a 실측 빈 칸 — 마체테를 들고 마체테 덤 덤불을 따면 DESIGN 3.3 도구 채집 줄대로 그 도구 행의
// 몸 동작(`UseAction`)·소리(`UseSoundCue`)를 튼다. 덤 도구가 아닌 덤불이나 맨손이면 그대로 줍기(`Gather`·`Pickup`).
// 기대값은 표 행에서 읽는다 — 코드에 클립·소리 이름을 적지 않는다.
bool FSnarehavenGatherBonusToolActionTest::RunTest(const FString& Parameters)
{
	const FName Machete = TEXT("item.machete");
	const FName Pickup = TEXT("Pickup");
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	const FItemRow* const MacheteRow = ItemTable != nullptr ? FindItem(ItemTable, Machete) : nullptr;
	if (!TestNotNull(TEXT("마체테 표 행"), MacheteRow))
	{
		return false;
	}
	TestTrue(TEXT("마체테 행은 줍기와 다른 동작·소리"), MacheteRow->UseAction != EItemAction::Gather
		&& MacheteRow->UseAction != EItemAction::None && !MacheteRow->UseSoundCue.IsNone() && MacheteRow->UseSoundCue != Pickup);

	// 쓴 도구 고르기 — 필요한 도구가 먼저, 없으면 든 것이 덤 도구일 때만.
	TestEqual(TEXT("덤불 + 마체테 = 마체테"), UGatherComponent::GetActingToolItemID(NAME_None, Machete, Machete), Machete);
	TestTrue(TEXT("덤불 + 맨손 = 맨손"), UGatherComponent::GetActingToolItemID(NAME_None, Machete, NAME_None).IsNone());
	TestTrue(TEXT("덤 없는 덤불 + 마체테 = 맨손"), UGatherComponent::GetActingToolItemID(NAME_None, NAME_None, Machete).IsNone());
	TestEqual(TEXT("나무 + 전기톱 = 필요한 도구"), UGatherComponent::GetActingToolItemID(TEXT("item.hatchet"), TEXT("item.chainsaw"),
		TEXT("item.chainsaw")), FName(TEXT("item.hatchet")));

	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("장비"), Player->GetEquipment())
		|| !TestTrue(TEXT("사람 몸을 올렸다(몸 클립을 읽으려고)"), Player->ApplyPlayerBody()))
	{
		DestroyTestWorld(World);
		return false;
	}
	struct FResult
	{
		bool bGathered = false;
		EItemAction Action = EItemAction::None;
		FName Cue;
		int32 Actions = 0;
		FString Clip;
	};
	auto Gather = [&](FName Bonus, FName Held) -> FResult
	{
		FResult Result;
		AGatherableActor* const Target = World->SpawnActor<AGatherableActor>(FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		FGatherableRow Row;
		Row.GatherableID = TestGatherableID;
		Row.GrantItemID = TestItemID;
		Row.GrantCount = TestGrantCount;
		Row.BonusToolItemID = Bonus;
		if (Target == nullptr || !Target->InitializeFromRow(Row))
		{
			return Result;
		}
		TArray<FEquippedSlot> Slots;
		if (!Held.IsNone())
		{
			FEquippedSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.Slot = EEquipSlot::Tool;
			Slot.ItemRowName = Player->GetEquipment()->FindEquipmentRowByItemID(Held);
		}
		Player->GetEquipment()->ApplyEquippedSlots(Slots);
		// 덤 알림은 같은 프레임이면 동작을 다시 안 튼다(`HandleGathered`) — 채집마다 다른 프레임으로.
		++GFrameCounter;
		const int32 ActionsBefore = Player->GetItemActionCount();
		Result.bGathered = Player->GetGather()->TryGatherNearest();
		Result.Action = Player->GetLastItemAction();
		Result.Cue = Player->GetLastRelayedSoundCue();
		Result.Actions = Player->GetItemActionCount() - ActionsBefore;
		Player->UpdateBodyAnimation();
		Result.Clip = GetNameSafe(Player->GetCurrentBodyAnim());
		if (IsValid(Target))
		{
			Target->Destroy();
		}
		return Result;
	};

	const FResult WithMachete = Gather(Machete, Machete);
	TestTrue(TEXT("[마체테·덤 덤불] 딴다"), WithMachete.bGathered);
	TestEqual(TEXT("[마체테·덤 덤불] 몸 동작 1번"), WithMachete.Actions, 1);
	TestEqual(TEXT("[마체테·덤 덤불] 몸 동작 = 마체테 행"), WithMachete.Action, MacheteRow->UseAction);
	TestEqual(TEXT("[마체테·덤 덤불] 소리 = 마체테 행"), WithMachete.Cue, MacheteRow->UseSoundCue);
	// 모션표 「마체테」 줄 — 동작 이름만이 아니라 몸에 실제로 걸린 클립이 마체테 행 쓰기 클립이다.
	TestEqual(TEXT("[마체테·덤 덤불] 몸 클립 = 마체테 행 쓰기 클립"), WithMachete.Clip, GetNameSafe(MacheteRow->UseAnim.LoadSynchronous()));

	const FResult BareHand = Gather(Machete, NAME_None);
	TestTrue(TEXT("[맨손·덤 덤불] 딴다"), BareHand.bGathered);
	TestEqual(TEXT("[맨손·덤 덤불] 몸 동작 = 줍기"), BareHand.Action, EItemAction::Gather);
	TestEqual(TEXT("[맨손·덤 덤불] 소리 = 줍기"), BareHand.Cue, Pickup);

	const FResult NoBonus = Gather(NAME_None, Machete);
	TestTrue(TEXT("[마체테·덤 없는 덤불] 딴다"), NoBonus.bGathered);
	TestEqual(TEXT("[마체테·덤 없는 덤불] 몸 동작 = 줍기"), NoBonus.Action, EItemAction::Gather);
	TestEqual(TEXT("[마체테·덤 없는 덤불] 소리 = 줍기"), NoBonus.Cue, Pickup);

	AddInfo(FString::Printf(TEXT("[Gather] 마체테 덤불 클립 %s / 맨손 클립 %s"), *WithMachete.Clip, *BareHand.Clip));
	AddInfo(FString::Printf(TEXT("[Gather] 마체테 덤불 동작 %d · 소리 %s / 맨손 %d · %s / 덤 없음 %d · %s"),
		static_cast<int32>(WithMachete.Action), *WithMachete.Cue.ToString(), static_cast<int32>(BareHand.Action),
		*BareHand.Cue.ToString(), static_cast<int32>(NoBonus.Action), *NoBonus.Cue.ToString()));

	DestroyTestWorld(World);
	return true;
}

// M63-03 — **도구로 친 사람 본인 기기만 손이 톡 떤다** (DESIGN 2.2 「타격 진동」). 마체테 덤불(도구질) → 1,
// 맨손 줍기 → 0, 남(조종기 없음) → 0, 끔 → 0. 실제 길(`TryGatherNearest`)로 딴다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGatherStrikeHapticTest, "Snarehaven.Gather.Tools.StrikeHaptic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGatherStrikeHapticTest::RunTest(const FString& Parameters)
{
	const FName Machete = TEXT("item.machete");
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("아이템 표"), ItemTable))
	{
		return false;
	}
	const bool bWasEnabled = SnareHaptics::IsEnabled();
	SnareHaptics::SetEnabled(true);
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	APlayerController* const Controller = Player != nullptr ? World->SpawnActor<APlayerController>() : nullptr;
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("조종기"), Controller))
	{
		SnareHaptics::SetEnabled(bWasEnabled);
		DestroyTestWorld(World);
		return false;
	}
	// 마체테 덤불 하나를 따고 늘어난 진동 횟수를 돌려준다. 같은 프레임 덤 알림 거름을 피하려 프레임을 올린다.
	auto GatherAndCount = [&](FName Held) -> int32
	{
		AGatherableActor* const Target = World->SpawnActor<AGatherableActor>(FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		FGatherableRow Row;
		Row.GatherableID = TestGatherableID;
		Row.GrantItemID = TestItemID;
		Row.GrantCount = TestGrantCount;
		Row.BonusToolItemID = Machete;
		if (Target == nullptr || !Target->InitializeFromRow(Row))
		{
			AddError(TEXT("덤불을 못 세웠다"));
			return -1;
		}
		TArray<FEquippedSlot> Slots;
		if (!Held.IsNone())
		{
			FEquippedSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.Slot = EEquipSlot::Tool;
			Slot.ItemRowName = Player->GetEquipment()->FindEquipmentRowByItemID(Held);
		}
		Player->GetEquipment()->ApplyEquippedSlots(Slots);
		++GFrameCounter;
		const int32 Before = SnareHaptics::GetPlayCount();
		TestTrue(TEXT("딴다"), Player->GetGather()->TryGatherNearest());
		const int32 Count = SnareHaptics::GetPlayCount() - Before;
		if (IsValid(Target))
		{
			Target->Destroy();
		}
		return Count;
	};

	// ① 남 — 조종기 없는 몸은 0
	TestEqual(TEXT("남이 도구질 → 0"), GatherAndCount(Machete), 0);

	// ② 내 기기 — `ULocalPlayer` 붙은 조종기만 "이 기기"
	Controller->Player = NewObject<ULocalPlayer>(GEngine, ULocalPlayer::StaticClass());
	Controller->Possess(Player);
	TestTrue(TEXT("내 몸은 내 기기 조종"), Player->IsLocallyControlled());
	TestEqual(TEXT("맨손 줍기 → 0"), GatherAndCount(NAME_None), 0);
	TestEqual(TEXT("내가 도구질 → 1"), GatherAndCount(Machete), 1);

	// ③ 끔 — 간격이 지나도 설정이 먼저다
	SnareHaptics::SetEnabled(false);
	TestEqual(TEXT("끔 → 0"), GatherAndCount(Machete), 0);

	AddInfo(FString::Printf(TEXT("[GatherStrikeHaptic] 도구질 1 · 맨손 0 · 남 0 · 끔 0 (누적 %d)"), SnareHaptics::GetPlayCount()));
	SnareHaptics::SetEnabled(bWasEnabled);
	DestroyTestWorld(World);
	return true;
}

// M13-15: 줄기 칸(`TrunkCollisionCm`)이 있는 채집물은 플레이어를 막아야 한다 — 몸 메시는 충돌이 없으니 숨은 줄기 원기둥이 막는다.
// 지금 표에 줄기 행은 없다(09-25 수지 나무 더미를 빼고 수지는 벤 나무 덤 — DESIGN 8.10) — 버섯 행에 줄기를 달아 칸 자체를 본다.
// 잡는 것: 줄기가 폰을 안 막는 것 · 벤 동안에도 막는 것 · 다시 자라도 안 막는 것 · 줄기 없는 더미에 줄기가 생기는 것.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGatherResinTrunkTest, "Snarehaven.Gather.Trunk.Blocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGatherResinTrunkTest::RunTest(const FString& Parameters)
{
	UDataTable* const Table = LoadObject<UDataTable>(nullptr, GatherableTablePath);
	if (!TestNotNull(TEXT("DT_GatherableTable 로드"), Table))
	{
		return false;
	}
	const FGatherableRow* const Mushroom = Table->FindRow<FGatherableRow>(TEXT("Gatherable_MushroomPile"), TEXT("TrunkTest"));
	if (!TestNotNull(TEXT("버섯 더미 행"), Mushroom))
	{
		return false;
	}
	TestTrue(TEXT("버섯 더미는 줄기가 없다"), Mushroom->TrunkCollisionCm.IsZero());
	FGatherableRow TrunkRow = *Mushroom;
	TrunkRow.TrunkCollisionCm = FVector2D(40.f, 200.f);   // 존 소품 나무 `TRUNK_*` 와 같은 값
	const FGatherableRow* const Trunked = &TrunkRow;

	UWorld* const World = CreateTestWorld();
	AGatherableActor* const Tree = World->SpawnActor<AGatherableActor>(FVector(500.f, 0.f, 0.f), FRotator::ZeroRotator);
	AGatherableActor* const Pile = World->SpawnActor<AGatherableActor>(FVector(-500.f, 0.f, 0.f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("줄기 더미 스폰"), Tree) || !TestNotNull(TEXT("버섯 더미 스폰"), Pile))
	{
		DestroyTestWorld(World);
		return false;
	}
	// 맵 배치처럼 몸을 키워도 줄기는 행의 cm 그대로다.
	Tree->SetActorScale3D(FVector(3.f));
	TestTrue(TEXT("줄기 더미가 켜진다"), Tree->InitializeFromRow(*Trunked));
	Pile->InitializeFromRow(*Mushroom);

	const UStaticMeshComponent* const Trunk = Tree->GetTrunk();
	if (!TestNotNull(TEXT("줄기 컴포넌트"), Trunk))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("줄기가 막는다"), Trunk->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
	TestEqual(TEXT("폰(플레이어)을 막는다"), Trunk->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
	TestFalse(TEXT("줄기는 안 보인다"), Trunk->IsVisible());
	TestTrue(TEXT("줄기 굵기 = 행 지름"), FMath::IsNearlyEqual(Trunk->GetComponentScale().X * 100.f, Trunked->TrunkCollisionCm.X, 0.1f));
	TestTrue(TEXT("줄기 높이 = 행 높이"), FMath::IsNearlyEqual(Trunk->GetComponentScale().Z * 100.f, Trunked->TrunkCollisionCm.Y, 0.1f));
	TestTrue(TEXT("줄기는 땅에서 선다(가운데 = 높이 절반)"),
		FMath::IsNearlyEqual(Trunk->GetComponentLocation().Z - Tree->GetActorLocation().Z, Trunked->TrunkCollisionCm.Y * 0.5f, 0.1f));
	TestEqual(TEXT("버섯 더미 줄기는 꺼져 있다"), Pile->GetTrunk()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);

	TestTrue(TEXT("캔다"), Tree->Gather());
	TestEqual(TEXT("캔 동안(다시 자라기 전)은 안 막는다"), Trunk->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	Tree->Respawn();
	TestEqual(TEXT("다시 자라면 다시 막는다"), Trunk->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);

	DestroyTestWorld(World);
	return true;
}

// M13-20: 채집 더미가 탑다운 화면에서 한눈에 보인다 — 실제 표 행으로 세운 몸의 월드 높이 ≥ DESIGN 3.3 더미 크기 표 값.
// 잡는 것: 표 칸이 빠지는 것 · 액터가 칸을 안 쓰고 메시 원래 크기(버섯 25cm)로 서는 것 · 맵 배율이 표를 이기는 것.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGatherPileSizeTest, "Snarehaven.Gather.Piles.BodySize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGatherPileSizeTest::RunTest(const FString& Parameters)
{
	FString Doc;
	UDataTable* const Table = LoadObject<UDataTable>(nullptr, GatherableTablePath);
	if (!TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md"))))
		|| !TestNotNull(TEXT("DT_GatherableTable 로드"), Table))
	{
		return false;
	}
	// `| 몸 높이 `Gatherable_X`·`Gatherable_Y` | 110cm ...` — 한 줄에 행 이름이 여럿일 수 있다.
	TMap<FName, float> Expected;
	TArray<FString> Lines;
	Doc.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		FRegexMatcher Value(FRegexPattern(TEXT(R"(^\| 몸 높이 [^|]+\| (\d+)cm)")), Line);
		if (!Value.FindNext())
		{
			continue;
		}
		for (FRegexMatcher Name(FRegexPattern(TEXT(R"(`(Gatherable_[A-Za-z]+)`)")), Line); Name.FindNext();)
		{
			Expected.Add(FName(*Name.GetCaptureGroup(1)), FCString::Atof(*Value.GetCaptureGroup(1)));
		}
	}
	TestEqual(TEXT("설계서 3.3 표의 더미 13행"), Expected.Num(), 13); // 09-25 수지 나무 더미를 뺐다(수지는 벤 나무 덤 — 8.10)

	UWorld* const World = CreateTestWorld();
	TSet<FString> Meshes;
	for (const TPair<FName, float>& Pair : Expected)
	{
		const FGatherableRow* const Row = Table->FindRow<FGatherableRow>(Pair.Key, TEXT("PileSizeTest"));
		AGatherableActor* const Pile = Row != nullptr ? World->SpawnActor<AGatherableActor>() : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s 행·액터"), *Pair.Key.ToString()), Pile))
		{
			continue;
		}
		Pile->SetActorScale3D(FVector(1.8f)); // 맵 배치의 옛 배율 — 표가 이긴다
		Pile->InitializeFromRow(*Row);
		const UStaticMeshComponent* const Body = Cast<UStaticMeshComponent>(Pile->GetRootComponent());
		const FVector Size = IsValid(Body) ? Body->Bounds.GetBox().GetSize() : FVector::ZeroVector;
		Meshes.Add(Row->MeshRef.ToString());
		TestTrue(*FString::Printf(TEXT("%s 월드 높이 %.1f ≥ 표 %.0fcm"), *Pair.Key.ToString(), Size.Z, Pair.Value),
			Size.Z >= Pair.Value - 0.5f);
		AddInfo(FString::Printf(TEXT("[PileSize] %s 높이 %.1f · 폭 %.1f×%.1f cm (표 %.0f)"),
			*Pair.Key.ToString(), Size.Z, Size.X, Size.Y, Pair.Value));
	}
	TestEqual(TEXT("메시 13종"), Meshes.Num(), 13); // 09-25 덤불 여섯(설원 주황·평원 검보라·분홍빛 빨강·가을섬 빨강·파랑·작은 섬 자홍) + 사탕무·밀 + 늪 버섯·늪꽃·수련이 따로
	DestroyTestWorld(World);
	return true;
}

// M22 숲 나무: 맵의 나무 대부분은 폴리지(ISM 인스턴스)다. 호스트가 사람 곁 인스턴스에만 채집물을 세우고,
// 베면 그 인스턴스가 숨었다가(배율 0) 다시 자랄 시각 뒤 원래대로 돌아오는지, 멀어지면 채집물이 걷히는지 본다.
// 잡는 것: 인스턴스 나무를 못 찾는 것 · 같은 나무에 두 번 세우는 것 · 숨김/복귀가 안 되는 것 · 액터가 쌓이는 것.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGatherInstancedTreeTest, "Snarehaven.Gather.Trees.InstancedForest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGatherInstancedTreeTest::RunTest(const FString& Parameters)
{
	UDataTable* const Table = LoadObject<UDataTable>(nullptr, GatherableTablePath);
	if (!TestNotNull(TEXT("DT_GatherableTable 로드"), Table))
	{
		return false;
	}
	// 실제 표에서 장식 접두어로 벨 수 있는 나무 행 하나 — 접두어·도구·다시 자람 초는 행에서만 온다.
	const FGatherableRow* TreeRow = nullptr;
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FGatherableRow* const Row = reinterpret_cast<const FGatherableRow*>(Pair.Value);
		if (Row != nullptr && !Row->DecorMeshPrefix.IsEmpty() && Row->RespawnSeconds > 0.f)
		{
			TreeRow = Row;
			break;
		}
	}
	if (!TestNotNull(TEXT("장식 접두어 나무 행이 있다"), TreeRow))
	{
		return false;
	}

	UWorld* const World = CreateTestWorld();
	ASnarehavenCharacter* const Player = SpawnTestPlayer(World);
	AActor* const Forest = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("숲 액터 스폰"), Forest))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 폴리지와 같은 인스턴스 컴포넌트 — 메시 이름만 행 접두어로 시작하면 된다(맵·좌표에 묶지 않는다).
	UStaticMesh* const TreeMesh = NewObject<UStaticMesh>(GetTransientPackage(), MakeUniqueObjectName(
		GetTransientPackage(), UStaticMesh::StaticClass(), FName(*(TreeRow->DecorMeshPrefix + TEXT("InstanceTest")))));
	UInstancedStaticMeshComponent* const Instances = NewObject<UInstancedStaticMeshComponent>(Forest);
	Forest->SetRootComponent(Instances);
	Instances->SetStaticMesh(TreeMesh);
	Instances->RegisterComponent();
	const FTransform NearTree(FRotator(0.f, 30.f, 0.f), FVector(TestNearDistanceCm, 0.f, 0.f), FVector(1.5f));
	Instances->AddInstance(NearTree, true);
	Instances->AddInstance(FTransform(FVector(TestOutOfRangeDistanceCm * 100.f, 0.f, 0.f)), true);

	const float RangeCm = Player->GetGather()->GetGatherRangeCm();
	TestEqual(TEXT("곁의 인스턴스 나무 하나만 선다(먼 나무는 안 선다)"),
		AGatherableActor::SpawnAtInstancedTreesNear(World, Player->GetActorLocation(), RangeCm), 1);
	TestEqual(TEXT("다시 불러도 같은 나무에 또 세우지 않는다"),
		AGatherableActor::SpawnAtInstancedTreesNear(World, Player->GetActorLocation(), RangeCm), 0);

	AGatherableActor* Tree = nullptr;
	for (TActorIterator<AGatherableActor> It(World); It; ++It)
	{
		Tree = *It;
	}
	if (!TestNotNull(TEXT("인스턴스 나무 채집물"), Tree))
	{
		DestroyTestWorld(World);
		return false;
	}
	// 검사 월드는 게임모드가 없어 스폰된 액터가 BeginPlay 를 못 받는다 — 게임에서 스폰 직후 도는 길을 손으로 태운다.
	Tree->DispatchBeginPlay();
	TestTrue(TEXT("복제된다(누가 베면 모두에게 사라진다)"), Tree->GetIsReplicated());
	TestEqual(TEXT("그 인스턴스를 몸으로 삼았다"), Tree->GetAdoptedInstances(), Instances);
	TestEqual(TEXT("가까운 인스턴스(0번)다"), Tree->GetAdoptedInstanceIndex(), 0);

	// 행의 도구를 든다 (도구 행은 메시 대기라 `EquipItem` 대신 칸만 채운다 — 위 ToolUses 검사와 같은 길).
	TArray<FEquippedSlot> Slots;
	FEquippedSlot& Slot = Slots.AddDefaulted_GetRef();
	Slot.Slot = EEquipSlot::Tool;
	Slot.ItemRowName = Player->GetEquipment()->FindEquipmentRowByItemID(TreeRow->RequiredToolItemID);
	Player->GetEquipment()->ApplyEquippedSlots(Slots);

	// 타이머만 흘린다. `FTimerManager::Tick` 은 한 프레임에 한 번만 일하고,
	// 그 프레임 전에 건 타이머는 첫 Tick 에선 깨우기만 한다 — 그래서 프레임을 넘기며 두 번 흘린다.
	const auto AdvanceTimers = [World](float Seconds)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(KINDA_SMALL_NUMBER);
		++GFrameCounter;
		World->GetTimerManager().Tick(Seconds);
	};

	// 나무는 행의 타격 수만큼 쳐야 쓰러진다(DESIGN 8.10) — 마지막 번에 지급이 쌓이고 넘어가는 연출 뒤 숨는다.
	const FName Granted = Tree->GetGrantItemID();
	const int32 Hits = FMath::Max(1, TreeRow->HitsToFell);
	for (int32 Hit = 1; Hit <= Hits; ++Hit)
	{
		TestTrue(TEXT("손도끼로 인스턴스 나무를 친다"), Player->GetGather()->TryGatherNearest());
	}
	TestTrue(TEXT("지급은 대기열에만 쌓인다(보유는 백엔드)"), Player->GetInventory()->GetPendingCount(Granted) > 0);
	AdvanceTimers(2.f);
	FTransform Shown;
	Instances->GetInstanceTransform(0, Shown, true);
	TestTrue(TEXT("벤 인스턴스는 넘어간 뒤 숨는다(배율 0)"), Shown.GetScale3D().IsNearlyZero());
	TestFalse(TEXT("벤 동안은 다시 못 벤다"), Tree->CanGather());
	TestEqual(TEXT("인스턴스는 지우지 않는다(인덱스가 밀리지 않게)"), Instances->GetInstanceCount(), 2);

	AdvanceTimers(TreeRow->RespawnSeconds + 1.f);
	Instances->GetInstanceTransform(0, Shown, true);
	TestTrue(TEXT("다시 자라면 원래 크기·자리로 돌아온다"), Shown.Equals(NearTree, 0.01f));
	TestTrue(TEXT("다시 자라면 또 벤다"), IsValid(Tree) && Tree->CanGather());

	// 사람이 멀어지면 안 베인 채집물은 걷힌다 — 숲을 걸어 다녀도 액터가 쌓이지 않는다. 인스턴스(나무)는 그대로다.
	Player->SetActorLocation(FVector(0.f, TestOutOfRangeDistanceCm * 100.f, 0.f));
	AdvanceTimers(60.f);
	TestFalse(TEXT("곁에 사람이 없으면 채집물이 걷힌다"), IsValid(Tree));
	Instances->GetInstanceTransform(0, Shown, true);
	TestTrue(TEXT("걷혀도 나무는 보인다"), Shown.Equals(NearTree, 0.01f));

	DestroyTestWorld(World);
	return true;
}

// DESIGN 8.10 「나무는 여러 번 쳐야 쓰러진다」 (INBOX 보정 10-06 02:45) — 타격 수는 테스트 전용 값이다.
// 잡는 것: 첫 번에 지급이 쌓이는 것 · 마지막 번 전에 나무가 숨는 것 · 쓰러질 때 서 있는 채로 사라지는 것 ·
// 다시 자란 뒤 친 번 수가 남는 것 · 타격 수 칸이 없는(0) 채집물이 한 번에 안 거둬지는 것.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGatherHitsToFellTest, "Snarehaven.Gather.Trees.HitsToFell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGatherHitsToFellTest::RunTest(const FString& Parameters)
{
	constexpr int32 TestHitsToFell = 3;
	UWorld* const World = CreateTestWorld();
	ASnarehavenCharacter* const Player = SpawnTestPlayer(World);
	AGatherableActor* const Tree = World->SpawnActor<AGatherableActor>(FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
	FGatherableRow Row;
	Row.GatherableID = TestGatherableID;
	Row.GrantItemID = TestItemID;
	Row.GrantCount = TestGrantCount;
	Row.RespawnSeconds = 10.f;
	Row.HitsToFell = TestHitsToFell;
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestTrue(TEXT("나무가 켜진다"), Tree != nullptr && Tree->InitializeFromRow(Row)))
	{
		DestroyTestWorld(World);
		return false;
	}
	const auto AdvanceTimers = [World](float Seconds)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(KINDA_SMALL_NUMBER);
		++GFrameCounter;
		World->GetTimerManager().Tick(Seconds);
	};
	UInventoryComponent* const Inventory = Player->GetInventory();
	const FQuat Upright = Tree->GetActorQuat();

	for (int32 Hit = 1; Hit < TestHitsToFell; ++Hit)
	{
		TestTrue(*FString::Printf(TEXT("%d번째 — 친다"), Hit), Player->GetGather()->TryGatherNearest());
		TestEqual(*FString::Printf(TEXT("%d번째 — 친 번 수"), Hit), Tree->GetHitsTaken(), Hit);
		TestEqual(*FString::Printf(TEXT("%d번째 — 지급 없음"), Hit), Inventory->GetPendingCount(TestItemID), 0);
		TestTrue(*FString::Printf(TEXT("%d번째 — 아직 서 있다"), Hit), Tree->CanGather() && Tree->GetBody()->IsVisible());
	}
	TestTrue(TEXT("흔들림 중엔 기울어 있다"), !Tree->GetActorQuat().Equals(Upright, KINDA_SMALL_NUMBER));
	AdvanceTimers(1.f);
	TestTrue(TEXT("흔들림이 끝나면 똑바로 선다"), Tree->GetActorQuat().Equals(Upright, KINDA_SMALL_NUMBER));

	TestTrue(TEXT("마지막 번 — 벤다"), Player->GetGather()->TryGatherNearest());
	TestEqual(TEXT("마지막 번에 지급이 한 번 쌓인다"), Inventory->GetPendingCount(TestItemID), TestGrantCount);
	TestTrue(TEXT("벤 뒤엔 못 친다"), !Tree->CanGather());
	TestTrue(TEXT("넘어가는 중이다"), Tree->IsFalling());
	TestTrue(TEXT("넘어가는 동안은 아직 보인다"), Tree->GetBody()->IsVisible());
	AdvanceTimers(2.f);
	TestFalse(TEXT("다 넘어가면 연출이 끝난다"), Tree->IsFalling());
	TestFalse(TEXT("다 넘어가면 숨는다"), Tree->GetBody()->IsVisible());
	TestTrue(TEXT("숨은 몸은 똑바로 돌려 둔다(다시 자랄 때 선 채로)"), Tree->GetActorQuat().Equals(Upright, KINDA_SMALL_NUMBER));
	AddInfo(FString::Printf(TEXT("[HitsToFell] %d번에 쓰러짐 · 대기 %d개"), TestHitsToFell, Inventory->GetPendingCount(TestItemID)));

	Tree->Respawn();
	TestEqual(TEXT("다시 자라면 친 번 수 0"), Tree->GetHitsTaken(), 0);
	TestTrue(TEXT("다시 자라면 첫 번은 지급 없음"), Player->GetGather()->TryGatherNearest()
		&& Inventory->GetPendingCount(TestItemID) == TestGrantCount);

	// 타격 수 칸이 0 인 채집물(덤불·더미)은 예전처럼 한 번에 거둔다.
	AGatherableActor* const Pile = SpawnTestGatherable(World, TestNearDistanceCm * 0.5f);
	TestTrue(TEXT("더미는 한 번에 거둔다"), Pile != nullptr && Player->GetGather()->TryGatherNearest());
	TestEqual(TEXT("더미 지급이 쌓인다"), Inventory->GetPendingCount(TestItemID), TestGrantCount * 2);

	DestroyTestWorld(World);
	return true;
}


// INBOX 결함 10-06 02:20: 맨손으로 열매 덤불을 따면 덤불이 통째로 사라졌다 — 레퍼런스는 덤불은 서 있고 열매만 없어진다.
// 잡는 것: 베리 덤불 행에 칸이 빠지는 것 · 거둔 뒤 몸이 숨는 것 · 누른 순간(손이 닿기 전)에 열매가 없어지는 것 ·
// 다시 나도 열매가 안 돌아오는 것 · 줍는 더미(꽃)까지 서 있게 되는 것 · 리스폰 없는 베리 덤불이 파괴되는 것.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGatherBerryBushStaysTest, "Snarehaven.Gather.Piles.BerryBushKeepsBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGatherBerryBushStaysTest::RunTest(const FString& Parameters)
{
	UDataTable* const Table = LoadObject<UDataTable>(nullptr, GatherableTablePath);
	if (!TestNotNull(TEXT("DT_GatherableTable 로드"), Table))
	{
		return false;
	}

	// 몸 메시가 베리 덤불인 행은 전부 몸을 남기고 늦춰 바뀐다. 다른 행은 지금처럼 사라진다.
	int32 BerryRows = 0;
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FGatherableRow* const Row = reinterpret_cast<const FGatherableRow*>(Pair.Value);
		const bool bBerryMesh = Row->MeshRef.ToSoftObjectPath().GetAssetName().StartsWith(TEXT("SM_Nature_BerryBush_"));
		BerryRows += bBerryMesh ? 1 : 0;
		TestEqual(*FString::Printf(TEXT("[%s] 몸 남김 = 베리 덤불 메시"), *Pair.Key.ToString()), Row->bKeepBodyWhenGathered, bBerryMesh);
		TestEqual(*FString::Printf(TEXT("[%s] 늦춤은 베리 덤불만"), *Pair.Key.ToString()), Row->GatheredVisualDelaySeconds > 0.f, bBerryMesh);
	}
	TestEqual(TEXT("베리 덤불 행 7개 (DESIGN 13.4 「열매 덤불 거둔 뒤」)"), BerryRows, 7);

	const FGatherableRow* const Raspberry = Table->FindRow<FGatherableRow>(TEXT("Gatherable_RaspberryBush"), TEXT("BerryBushTest"));
	const FGatherableRow* const Flower = Table->FindRow<FGatherableRow>(TEXT("Gatherable_FlowerPatch"), TEXT("BerryBushTest"));
	const FGatherableRow* const OldBerry = Table->FindRow<FGatherableRow>(TEXT("Gatherable_BerryBush"), TEXT("BerryBushTest"));
	if (!TestNotNull(TEXT("라즈베리 덤불 행"), Raspberry) || !TestNotNull(TEXT("꽃 더미 행"), Flower)
		|| !TestNotNull(TEXT("베리 덤불 행"), OldBerry))
	{
		return false;
	}
	// 늦춤은 시험 값으로 — 표 숫자가 바뀌어도 안 깨지게.
	constexpr float TestDelaySeconds = 0.5f;
	FGatherableRow BushRow = *Raspberry;
	BushRow.GatheredVisualDelaySeconds = TestDelaySeconds;

	UWorld* const World = CreateTestWorld();
	AGatherableActor* const Bush = World->SpawnActor<AGatherableActor>(FVector(500.f, 0.f, 0.f), FRotator::ZeroRotator);
	AGatherableActor* const Patch = World->SpawnActor<AGatherableActor>(FVector(-500.f, 0.f, 0.f), FRotator::ZeroRotator);
	AGatherableActor* const Lasting = World->SpawnActor<AGatherableActor>(FVector(0.f, 500.f, 0.f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("덤불 스폰"), Bush) || !TestNotNull(TEXT("꽃 스폰"), Patch) || !TestNotNull(TEXT("베리 덤불 스폰"), Lasting))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestTrue(TEXT("덤불이 켜진다"), Bush->InitializeFromRow(BushRow));
	TestTrue(TEXT("꽃이 켜진다"), Patch->InitializeFromRow(*Flower));
	TestTrue(TEXT("베리 덤불이 켜진다"), Lasting->InitializeFromRow(*OldBerry));

	UStaticMeshComponent* const Body = Bush->GetBody();
	if (!TestNotNull(TEXT("덤불 몸"), Body) || !TestTrue(TEXT("덤불 메시에 잎·열매 두 칸"), Body->GetNumMaterials() >= 2))
	{
		DestroyTestWorld(World);
		return false;
	}
	UMaterialInterface* const Leaves = Body->GetMaterial(0);
	UMaterialInterface* const Fruit = Body->GetMaterial(1);
	TestNotEqual(TEXT("처음엔 열매 칸이 잎과 다르다"), Fruit, Leaves);

	// 타이머는 한 프레임에 한 번만 일한다 — 아래 숲 나무 시험과 같이 프레임을 넘기며 흘린다.
	const auto AdvanceTimers = [World](float Seconds)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(KINDA_SMALL_NUMBER);
		++GFrameCounter;
		World->GetTimerManager().Tick(Seconds);
	};
	TestTrue(TEXT("딴다"), Bush->Gather());
	TestTrue(TEXT("거둔 표시"), Bush->IsGathered());
	TestFalse(TEXT("거둔 뒤엔 다시 못 딴다"), Bush->CanGather());
	TestEqual(TEXT("누른 순간엔 열매 그대로(손이 아직 안 닿음)"), Body->GetMaterial(1), Fruit);
	AdvanceTimers(TestDelaySeconds * 0.8f);
	TestEqual(TEXT("늦춤 전엔 열매 그대로"), Body->GetMaterial(1), Fruit);
	AdvanceTimers(TestDelaySeconds * 0.4f);
	TestTrue(TEXT("덤불은 서 있다"), Body->IsVisible());
	TestEqual(TEXT("손이 닿은 뒤 열매 칸 = 잎"), Body->GetMaterial(1), Leaves);
	TestEqual(TEXT("잎 칸은 그대로"), Body->GetMaterial(0), Leaves);

	Bush->Respawn();
	TestTrue(TEXT("다시 나면 또 딸 수 있다"), Bush->CanGather());
	TestEqual(TEXT("다시 나면 열매가 돌아온다"), Body->GetMaterial(1), Fruit);
	TestTrue(TEXT("다시 난 덤불도 서 있다"), Body->IsVisible());

	TestTrue(TEXT("꽃을 줍는다"), Patch->Gather());
	TestFalse(TEXT("꽃 더미는 사라진다(원본 줍기)"), Patch->GetBody()->IsVisible());

	TestTrue(TEXT("리스폰 없는 베리 덤불도 딴다"), Lasting->Gather());
	TestTrue(TEXT("리스폰 없는 베리 덤불도 파괴되지 않는다"), IsValid(Lasting));
	AdvanceTimers(OldBerry->GatheredVisualDelaySeconds + 0.1f);
	TestTrue(TEXT("리스폰 없는 베리 덤불도 서 있다"), IsValid(Lasting) && Lasting->GetBody()->IsVisible());

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
