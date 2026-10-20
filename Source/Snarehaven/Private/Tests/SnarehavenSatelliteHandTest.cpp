// 2층 검사 — 위성 통신기는 손에 들지 않는다 (docs/reference/character_motion.md 「위성 통신기」: 레퍼런스는 좌표 HUD 만, 몸은 빈손).
// ① 표 행에 손 메시가 없다 ② 도구 칸에는 든다(좌표 HUD 줄은 행이 정한다) ③ 손도끼를 들고 있다가 바꿔 들면 손 메시가 비고 숨었던 무기가 다시 보인다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Equipment.Satellite; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "SnarehavenCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Data/ItemRow.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenSatelliteHandTest, "Snarehaven.Equipment.Satellite.HeldWithEmptyHand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenSatelliteHandTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	UEquipmentComponent* const Equipment = Player != nullptr ? Player->GetEquipment() : nullptr;
	if (TestNotNull(TEXT("장비 컴포넌트"), Equipment) && TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		// 무기(오른손) + 손도끼(같은 오른손이라 무기가 숨는다) → 위성 통신기로 바꿔 든다.
		TestTrue(TEXT("몽둥이를 든다"), Equipment->EquipItemByItemID(FName(TEXT("item.club"))));
		TestTrue(TEXT("손도끼를 든다"), Equipment->EquipItemByItemID(FName(TEXT("item.hatchet"))));
		const UStaticMeshComponent* const Weapon = Equipment->GetEquippedMesh(EEquipSlot::Weapon);
		TestTrue(TEXT("손도끼를 든 동안 무기는 숨는다"), IsValid(Weapon) && !Weapon->IsVisible());

		TestTrue(TEXT("위성 통신기를 든다"), Equipment->EquipItemByItemID(FName(TEXT("item.satellite_communicator"))));
		const FItemRow* const Row = Equipment->GetEquippedRow(EEquipSlot::Tool);
		const UStaticMeshComponent* const Tool = Equipment->GetEquippedMesh(EEquipSlot::Tool);
		const bool bHandEmpty = !IsValid(Tool) || Tool->GetStaticMesh() == nullptr;
		if (TestNotNull(TEXT("도구 칸 = 위성 통신기 행"), Row))
		{
			TestTrue(TEXT("① 표 행에 손 메시가 없다"), Row->EquipMeshRef.IsNull());
			TestTrue(TEXT("② 좌표를 띄우는 행이다"), Row->bShowsMapCoordinates);
		}
		TestTrue(TEXT("③ 손에 보이는 도구 메시가 없다(빈손)"), bHandEmpty);
		TestTrue(TEXT("③ 숨었던 무기가 다시 보인다"), IsValid(Weapon) && Weapon->IsVisible());
		AddInfo(FString::Printf(TEXT("[Satellite] 도구 칸 %s · 손 메시 %s · 무기 보임 %d"),
			*Equipment->GetEquippedItem(EEquipSlot::Tool).ToString(),
			bHandEmpty ? TEXT("없음") : *Tool->GetStaticMesh()->GetName(), IsValid(Weapon) && Weapon->IsVisible() ? 1 : 0));
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
