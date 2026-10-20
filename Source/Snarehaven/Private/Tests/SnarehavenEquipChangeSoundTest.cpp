// ROADMAP M47-03 2층 검사 — 손 칸·옷 부위가 **플레이어가 바꾼 길에서만** 소리 요청을 한 번 내는지.
// 규칙 함수(`SnareSound::EquipCue`·`OutfitCue`)는 `Snarehaven.World.EquipSound` 가 본다 — 여기서는 `UEquipmentComponent`
// 의 세 길(`EquipItem`·`UnequipSlot`·`SetCosmetic`)이 그 함수를 부르는 때만 본다: 바꿈 1 → 1 · 같은 행 → 0 ·
// 무음 길(`FQuietChanges` — 시작 입힘·복원) → 0 · 표의 간격(0.25초) 안 연타 → 첫 한 번.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Equipment.ChangeSound; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "SnarehavenCharacter.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"

namespace
{
	const FName ClubRow(TEXT("Item_Club"));
	const FName HatRow(TEXT("Item_MiningHat"));

	UWorld* CreateChangeSoundWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	/** 몸을 올린 플레이어의 장비 컴포넌트 — 몸이 없으면 붙일 소켓이 없어 아무것도 안 든다. */
	UEquipmentComponent* SpawnEquipment(UWorld* World)
	{
		ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
		return (Player != nullptr && Player->ApplyPlayerBody()) ? Player->GetEquipment() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEquipChangeSoundTest, "Snarehaven.Equipment.ChangeSound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEquipChangeSoundTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateChangeSoundWorld();
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	// ① 빈손 → 몽둥이 = 꺼내기 1 (둔기). 같은 행 다시 들기 = 0. 간격 안 넣기 = 첫 한 번만이라 0.
	UEquipmentComponent* const A = SpawnEquipment(World);
	if (!TestNotNull(TEXT("플레이어 A 장비"), A))
	{
		return false;
	}
	TestEqual(TEXT("처음엔 소리 0"), A->GetChangeCueCount(), 0);
	TestTrue(TEXT("A 가 몽둥이를 들었다"), A->EquipItem(ClubRow));
	TestEqual(TEXT("꺼내기 1"), A->GetChangeCueCount(), 1);
	TestEqual(TEXT("몽둥이 = 둔기 꺼내기"), A->GetLastChangeCue(), FName(TEXT("EquipBlunt")));
	TestTrue(TEXT("같은 몽둥이를 다시 듦"), A->EquipItem(ClubRow));
	TestEqual(TEXT("같은 행 다시 들기 = 소리 0"), A->GetChangeCueCount(), 1);
	TestTrue(TEXT("A 가 몽둥이를 내렸다"), A->UnequipSlot(EEquipSlot::Weapon));
	TestEqual(TEXT("간격(0.25초) 안 넣기 = 첫 한 번만"), A->GetChangeCueCount(), 1);
	// 손 칸 간격은 옷 칸과 따로 — 같은 때 모자를 써도 부스럭은 난다.
	TestTrue(TEXT("A 가 채굴 모자를 썼다"), A->EquipItem(HatRow));
	TestEqual(TEXT("모자 = 옷 소리 1"), A->GetChangeCueCount(), 2);
	TestEqual(TEXT("모자 = 옷 부스럭"), A->GetLastChangeCue(), FName(TEXT("Outfit")));

	// ② 무음 길 — 시작 입힘·복원은 `FQuietChanges` 안에서. 그 뒤 플레이어가 내리면 넣기 1.
	UEquipmentComponent* const B = SpawnEquipment(World);
	if (!TestNotNull(TEXT("플레이어 B 장비"), B))
	{
		return false;
	}
	{
		const UEquipmentComponent::FQuietChanges Quiet(B);
		TestTrue(TEXT("B 시작 입힘 몽둥이"), B->EquipItem(ClubRow));
		TestTrue(TEXT("B 시작 입힘 모자"), B->EquipItem(HatRow));
		TestTrue(TEXT("B 복원 모자 벗김"), B->UnequipSlot(EEquipSlot::Head));
	}
	TestEqual(TEXT("무음 길 = 소리 0"), B->GetChangeCueCount(), 0);
	TestTrue(TEXT("B 가 몽둥이를 내렸다"), B->UnequipSlot(EEquipSlot::Weapon));
	TestEqual(TEXT("무음 길이 끝난 뒤 넣기 1"), B->GetChangeCueCount(), 1);
	TestEqual(TEXT("빈손 = 넣기"), B->GetLastChangeCue(), FName(TEXT("Holster")));
	TestFalse(TEXT("빈 손을 또 내리면 바뀐 것 없음"), B->UnequipSlot(EEquipSlot::Weapon));
	TestEqual(TEXT("빈 손 또 내리기 = 소리 0"), B->GetChangeCueCount(), 1);

	// ③ 총 — 탄 칸이 있는 손 행을 들면 총 꺼내기. 메시가 반입된 첫 총 행으로 본다.
	const UDataTable* const Items = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (!TestNotNull(TEXT("DT_ItemTable"), Items))
	{
		return false;
	}
	UEquipmentComponent* const C = SpawnEquipment(World);
	if (!TestNotNull(TEXT("플레이어 C 장비"), C))
	{
		return false;
	}
	FName GunRow;
	for (const TPair<FName, uint8*>& Pair : Items->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->ItemType == EItemType::Equipment && !Row->AmmoItemID.IsNone() && C->EquipItem(Pair.Key))
		{
			GunRow = Pair.Key;
			break;
		}
	}
	if (TestFalse(TEXT("들 수 있는 총 행이 있다"), GunRow.IsNone()))
	{
		TestEqual(FString::Printf(TEXT("%s 꺼내기 1"), *GunRow.ToString()), C->GetChangeCueCount(), 1);
		TestEqual(TEXT("총 = 철컥"), C->GetLastChangeCue(), FName(TEXT("EquipGun")));
	}
	return true;
}

#endif
