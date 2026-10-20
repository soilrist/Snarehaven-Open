// ROADMAP M2-02 2층 검사 — 목장 생산 테이블(`DT_RanchProductTable`)이 성한지:
// 행이 가리키는 동물·아이템이 다른 테이블에 실제로 있고, 숫자 칸에 자리표시(-1)가 남아 있지 않은지.
// 아직 이 표를 읽는 런타임 코드는 없다(배치·수확은 M2-04) — 그래서 이 검사는 **표 자체**만 본다.
// 비어 있는 `ProducedItemID` 는 오류가 아니라 **아직 사람이 정하지 않은 칸**이다 (DataDriven.md 3,
// INBOX 질문 2026-09-19). 다만 "비었다" 를 로그에 남겨 아무도 모르게 굳지 않게 한다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/AnimalRow.h"
#include "Data/ItemRow.h"
#include "Data/RanchProductRow.h"
#include "Engine/DataTable.h"

namespace
{
	// 실제 데이터 테이블. 기대값(주기·상한·아이템)은 전부 여기서 읽는다 — 코드에 적지 않는다.
	const TCHAR* RanchTablePath = TEXT("/Game/Game/Data/Ranch/DT_RanchProductTable.DT_RanchProductTable");
	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");

	// ID 로 다른 테이블의 행을 찾는다. 이름 문자열 비교가 아니라 행에 적힌 ID 칸끼리 맞춘다.
	template <typename TRow>
	const TRow* FindRowByID(const UDataTable* Table, FName TRow::* IDField, const FName& ID)
	{
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const TRow* Row = reinterpret_cast<const TRow*>(Pair.Value);
			if (Row != nullptr && Row->*IDField == ID)
			{
				return Row;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchProductTableTest, "Snarehaven.Ranch.Table.RowsPointAtRealRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchProductTableTest::RunTest(const FString& Parameters)
{
	UDataTable* RanchTable = LoadObject<UDataTable>(nullptr, RanchTablePath);
	UDataTable* AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_RanchProductTable 로드"), RanchTable)
		|| !TestNotNull(TEXT("DT_AnimalTable 로드"), AnimalTable)
		|| !TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}

	// 몇 종인지 코드에 적지 않는다 — 행이 늘면 그만큼 돈다 (DataDriven.md 1).
	const TArray<FName> RowNames = RanchTable->GetRowNames();
	if (!TestTrue(TEXT("목장 생산 행이 하나 이상 있다"), RowNames.Num() > 0))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("DT_RanchProductTable 행 %d개를 본다"), RowNames.Num()));

	for (const FName& RowName : RowNames)
	{
		const FRanchProductRow* Row =
			RanchTable->FindRow<FRanchProductRow>(RowName, TEXT("RanchProductTableTest"));
		if (!TestNotNull(*FString::Printf(TEXT("%s 행"), *RowName.ToString()), Row))
		{
			continue;
		}

		// 생산 주체가 진짜 있는 동물인지. 없으면 백엔드가 못 알아듣는 슬롯이 된다 (DESIGN 4.5 목장 슬롯 = AnimalID).
		const FAnimalRow* AnimalRow = FindRowByID<FAnimalRow>(AnimalTable, &FAnimalRow::AnimalID, Row->AnimalID);
		TestNotNull(*FString::Printf(TEXT("%s 의 AnimalID(%s)가 DT_AnimalTable 에 있다"),
			*RowName.ToString(), *Row->AnimalID.ToString()), AnimalRow);

		// 숫자는 DESIGN 3.2 표에서 온 값이어야 한다. 자리표시(-1)가 남아 있으면 미정이라는 뜻이다.
		TestTrue(*FString::Printf(TEXT("%s 의 CycleMinutes 가 미정(-1)이 아니다"), *RowName.ToString()),
			Row->CycleMinutes > 0);
		TestTrue(*FString::Printf(TEXT("%s 의 MaxStack 이 미정(-1)이 아니다"), *RowName.ToString()),
			Row->MaxStack > 0);

		if (Row->ProducedItemID.IsNone())
		{
			// 오류가 아니다 — 사람이 아직 안 정한 칸이다. 조용히 지나가지 않게 로그로 남긴다.
			AddInfo(FString::Printf(TEXT("[%s] 주기 %d분 · 미수확 상한 %d개 · 생산 아이템 **미정**(INBOX 질문)"),
				*Row->AnimalID.ToString(), Row->CycleMinutes, Row->MaxStack));
			continue;
		}

		const FItemRow* ProducedRow = FindRowByID<FItemRow>(ItemTable, &FItemRow::ItemID, Row->ProducedItemID);
		AddInfo(FString::Printf(TEXT("[%s] 주기 %d분 · 미수확 상한 %d개 · 생산 아이템 %s"),
			*Row->AnimalID.ToString(), Row->CycleMinutes, Row->MaxStack, *Row->ProducedItemID.ToString()));
		TestNotNull(*FString::Printf(TEXT("%s 가 주는 아이템이 DT_ItemTable 에 있다"), *RowName.ToString()),
			ProducedRow);
	}

	return true;
}

// 위 검사는 **있는 행**만 훑는다 — 종이 늘었는데 생산 행을 안 만들면 조용히 통과한다.
// 실제로 루프#75 가 소·양을 넣었을 때 목장 생산 행이 돼지 하나뿐인 채로 열다섯 바퀴가 지났다.
// 그래서 반대 방향으로 한 번 더 본다: `DT_AnimalTable` 의 모든 종이 생산 행을 갖는가.
// 종을 하나 더 넣는 사람은 여기서 걸려 생산물·주기를 사람에게 물어보게 된다 (DataDriven.md 3).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchEverySpeciesProducesTest,
	"Snarehaven.Ranch.Table.EverySpeciesHasProduct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchEverySpeciesProducesTest::RunTest(const FString& Parameters)
{
	UDataTable* RanchTable = LoadObject<UDataTable>(nullptr, RanchTablePath);
	UDataTable* AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("DT_RanchProductTable 로드"), RanchTable)
		|| !TestNotNull(TEXT("DT_AnimalTable 로드"), AnimalTable))
	{
		return false;
	}

	for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
	{
		const FAnimalRow* AnimalRow = reinterpret_cast<const FAnimalRow*>(Pair.Value);
		if (AnimalRow == nullptr)
		{
			continue;
		}

		const FRanchProductRow* RanchRow =
			FindRowByID<FRanchProductRow>(RanchTable, &FRanchProductRow::AnimalID, AnimalRow->AnimalID);
		TestNotNull(*FString::Printf(TEXT("%s 종의 목장 생산 행이 DT_RanchProductTable 에 있다"),
			*AnimalRow->AnimalID.ToString()), RanchRow);

		if (RanchRow != nullptr)
		{
			// 생산 아이템이 비어 있으면 목장에 놓아도 아무것도 안 나온다 — 위 검사는 이걸 통과시킨다
			// (아직 사람이 안 정한 칸이라서). 종이 다 갖춰진 지금은 비어 있는 것이 결함이다.
			TestFalse(*FString::Printf(TEXT("%s 종의 생산 아이템이 비어 있지 않다"),
				*AnimalRow->AnimalID.ToString()), RanchRow->ProducedItemID.IsNone());
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
