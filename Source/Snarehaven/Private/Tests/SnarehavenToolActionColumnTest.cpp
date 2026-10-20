// ROADMAP M16-01 2층 검사 — 도구 행이 몸 동작·소리 칸을 갖고 있고 표 값이 지금 동작과 같은가.
// 코드가 도구 이름(`pickaxe`)을 가르지 않고 이 칸만 읽는다는 것은 소스 grep 으로 따로 본다(STATUS 인용).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Item.ToolAction; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/ItemAction.h"
#include "Data/ItemRow.h"
#include "Data/ItemRowLookup.h"
#include "Engine/DataTable.h"

namespace
{
	struct FToolExpect
	{
		const TCHAR* ItemID;
		EItemAction Action;
		const TCHAR* Sound;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenToolActionColumnTest, "Snarehaven.Item.ToolAction.RowsCarryColumns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenToolActionColumnTest::RunTest(const FString& Parameters)
{
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
	if (!TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}

	// 도구마다 다른 동작·소리가 합격선이다 — 손도끼는 내려치기+쿵, 마체테는 옆 베기+풀 베는 소리(M16-04).
	const FToolExpect Expects[] = {
		{TEXT("item.hatchet"), EItemAction::Smash, TEXT("Chop")},
		{TEXT("item.chainsaw"), EItemAction::Chop, TEXT("Chainsaw_Rev")},
		{TEXT("item.machete"), EItemAction::Chop, TEXT("Machete")},
		{TEXT("item.pickaxe_basic"), EItemAction::Mine, TEXT("Mine")},
		{TEXT("item.pickaxe_advanced"), EItemAction::Mine, TEXT("Mine")},
		{TEXT("item.pickaxe_electric"), EItemAction::Mine, TEXT("Mine")},
		{TEXT("item.gather_basket"), EItemAction::Gather, TEXT("Pickup")},
	};

	// 손도끼와 마체테는 동작도 소리도 서로 달라야 한다(합격선).
	const FItemRow* const Hatchet = SnareItemRows::FindByItemID(ItemTable, FName(TEXT("item.hatchet")));
	const FItemRow* const Machete = SnareItemRows::FindByItemID(ItemTable, FName(TEXT("item.machete")));
	if (Hatchet != nullptr && Machete != nullptr)
	{
		TestTrue(TEXT("손도끼·마체테 동작이 다르다"), Hatchet->UseAction != Machete->UseAction);
		TestTrue(TEXT("손도끼·마체테 소리가 다르다"), Hatchet->UseSoundCue != Machete->UseSoundCue);
	}

	// 곡괭이 소리는 손도끼와 달라야 한다(M16-05 합격선).
	const FItemRow* const Pick = SnareItemRows::FindByItemID(ItemTable, FName(TEXT("item.pickaxe_basic")));
	if (Hatchet != nullptr && Pick != nullptr)
	{
		TestTrue(TEXT("곡괭이·손도끼 소리가 다르다"), Hatchet->UseSoundCue != Pick->UseSoundCue);
	}

	// 낚싯대 4종은 던지기·입질·감기 소리 칸을 모두 갖고, 소리 조합이 서로 달라야 한다(M16-06 합격선).
	const TCHAR* const RodIds[] = {TEXT("item.rod_basic"), TEXT("item.rod_telescopic"), TEXT("item.rod_bait"), TEXT("item.rod_lucky")};
	TSet<FString> RodCombos;
	for (const TCHAR* RodId : RodIds)
	{
		const FItemRow* const Rod = SnareItemRows::FindByItemID(ItemTable, FName(RodId));
		if (!TestNotNull(FString::Printf(TEXT("[%s] 행이 있다"), RodId), Rod))
		{
			continue;
		}
		TestFalse(FString::Printf(TEXT("[%s] 던지기 소리 칸"), RodId), Rod->UseSoundCue.IsNone());
		TestFalse(FString::Printf(TEXT("[%s] 입질 소리 칸"), RodId), Rod->BiteSoundCue.IsNone());
		TestFalse(FString::Printf(TEXT("[%s] 감기 소리 칸"), RodId), Rod->ReelSoundCue.IsNone());
		RodCombos.Add(FString::Printf(TEXT("%s|%s|%s"), *Rod->UseSoundCue.ToString(), *Rod->BiteSoundCue.ToString(), *Rod->ReelSoundCue.ToString()));
	}
	TestEqual(TEXT("낚싯대 4종의 소리 조합이 서로 다르다"), RodCombos.Num(), 4);

	for (const FToolExpect& Expect : Expects)
	{
		const FItemRow* const Row = SnareItemRows::FindByItemID(ItemTable, FName(Expect.ItemID));
		if (!TestNotNull(FString::Printf(TEXT("[%s] 행이 있다"), Expect.ItemID), Row))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("[%s] UseAction 이 표 값(%s)이다"), Expect.ItemID, *UEnum::GetValueAsString(Expect.Action)),
			Row->UseAction == Expect.Action);
		TestEqual(FString::Printf(TEXT("[%s] UseSoundCue"), Expect.ItemID), Row->UseSoundCue, FName(Expect.Sound));
		// 든 동안 되풀이 소리는 전기톱(M16-03)·전기 곡괭이(M16-05)만 갖는다 — 나머지는 조각이 올 때마다 여기 기대를 바꾼다.
		FName ExpectLoop = NAME_None;
		if (FName(Expect.ItemID) == FName(TEXT("item.chainsaw")))
		{
			ExpectLoop = FName(TEXT("Chainsaw_Idle"));
		}
		else if (FName(Expect.ItemID) == FName(TEXT("item.pickaxe_electric")))
		{
			ExpectLoop = FName(TEXT("ElectricHum"));
		}
		TestEqual(FString::Printf(TEXT("[%s] HoldLoopSoundCue"), Expect.ItemID), Row->HoldLoopSoundCue, ExpectLoop);
	}

	// 몽둥이는 맞을 때 `Club` 소리, 맨손(무기 없음)은 칸이 비어 기본 `Hit` 이다(M16-07).
	if (const FItemRow* const Club = SnareItemRows::FindByItemID(ItemTable, FName(TEXT("item.club"))))
	{
		TestEqual(TEXT("[item.club] 타격 소리 칸"), Club->UseSoundCue, FName(TEXT("Club")));
	}
	else
	{
		AddError(TEXT("[item.club] 행이 있다"));
	}

	// 쓰는 동작이 없는 도구는 None 이다(쌍안경·위성 통신기).
	for (const TCHAR* ItemID : {TEXT("item.binoculars"), TEXT("item.satellite_communicator")})
	{
		const FItemRow* const Row = SnareItemRows::FindByItemID(ItemTable, FName(ItemID));
		if (TestNotNull(FString::Printf(TEXT("[%s] 행이 있다"), ItemID), Row))
		{
			TestTrue(FString::Printf(TEXT("[%s] 쓰는 동작 없음"), ItemID), Row->UseAction == EItemAction::None);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
