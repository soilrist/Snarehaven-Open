// ROADMAP M5-06a 2층 검사 — `DT_ItemTable` 의 장비 행이 실제로 몸에 붙일 수 있는 상태인지 (DESIGN 3.5).
// 붙이는 코드는 아직 없다(M5-06b) — 그래서 여기서 보는 것은 **데이터가 붙일 준비가 됐는가** 하나다:
// 부위가 정해져 있고, 가리키는 메시가 진짜 있고, 그 메시가 모바일 한도(DESIGN 4.2 3K) 안이고, 소켓 이름이 적혀 있다.
// 장비가 아닌 행이 장비 칸을 채우고 있지 않은지도 같이 본다 — 잘못 붙인 행은 M5-06c 에서 조용히 손에 들린다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Equipment/EquipmentComponent.h"
#include "Misc/PackageName.h"

namespace
{
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");

	// DESIGN 2·4.2 "오브젝트당 3K 이하". 설계서 값이고 이 검사가 만든 숫자가 아니다.
	constexpr int32 TriangleLimit = 3000;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEquipmentRowTest, "Snarehaven.Item.Equipment.RowsAreWearable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEquipmentRowTest::RunTest(const FString& Parameters)
{
	UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}

	int32 EquipmentRowCount = 0;
	int32 MeshPendingRowCount = 0;

	for (const TPair<FName, uint8*>& RowPair : ItemTable->GetRowMap())
	{
		const FString RowName = RowPair.Key.ToString();
		const FItemRow* Row = reinterpret_cast<const FItemRow*>(RowPair.Value);

		// 사람 꾸미기(`EItemType::Cosmetic`, `docs/specs/skin-parts.md`)는 같은 부위의 겉모습 덮개라 부위·메시·소켓을
		// 장비와 같은 규칙으로 채운다(`create_data_tables.py` `add_cosmetic_*`) — 장비 가지에서 같이 본다.
		const bool bCosmetic = Row->ItemType == EItemType::Cosmetic;
		if (Row->ItemType != EItemType::Equipment && !bCosmetic)
		{
			// 장비가 아닌 행은 장비 칸이 전부 비어 있어야 한다 — 한쪽만 채워진 행을 여기서 잡는다.
			TestTrue(FString::Printf(TEXT("[%s] 장비가 아니라 부위가 None"), *RowName),
				Row->EquipSlot == EEquipSlot::None);
			// 목장 꾸미기(DESIGN 10.9)는 펫 몸에 붙일 메시·뼈를 같은 두 칸에 둔다 — 비지 않고 둘 다 차 있어야 한다.
			const bool bPetCosmetic = Row->ItemType == EItemType::PetCosmetic;
			TestTrue(FString::Printf(TEXT("[%s] 장비가 아니라 메시 참조가 비었다(꾸미기는 찼다)"), *RowName),
				Row->EquipMeshRef.IsNull() != bPetCosmetic);
			TestTrue(FString::Printf(TEXT("[%s] 장비가 아니라 소켓 이름이 비었다(꾸미기는 찼다)"), *RowName),
				Row->EquipSocketName.IsNone() != bPetCosmetic);
			TestTrue(FString::Printf(TEXT("[%s] 장비가 아니라 옷 메시 참조가 비었다"), *RowName),
				Row->EquipOutfitMeshRef.IsNull());
			continue;
		}

		TestFalse(FString::Printf(TEXT("[%s] 붙일 부위가 정해져 있다"), *RowName),
			Row->EquipSlot == EEquipSlot::None);

		// 레퍼런스 맞춤 장비(낚싯대·손도끼·어부 모자·배낭, DESIGN 8.12·8.13)는 메시가 아직 없다 — 8.11 "메시는
		// 감독이 넘긴다, 루프는 표·백엔드부터". 그 부위(도구·머리·배낭)에서 **메시 두 칸이 다 빈 행만** 대기로 센다.
		// 상의(13.8 구명조끼 — 첫 옷 행)도 같다. 무기 행은 여전히 메시가 있어야 하고, 메시를 하나라도 적은 행은
		// 아래 검사를 그대로 받는다.
		// 사냥 총(탄창 > 0, DESIGN 8.6-c)도 무기 칸이지만 메시는 M39-06 몫이라 그때까지 대기로 센다.
		const bool bMeshPendingSlot = Row->EquipSlot == EEquipSlot::Tool || Row->EquipSlot == EEquipSlot::Head
			|| Row->EquipSlot == EEquipSlot::Backpack || Row->EquipSlot == EEquipSlot::OutfitTop
			|| Row->MagazineSize > 0;
		// 꾸미기는 겉모습만 파는 물건이라 메시 대기를 받지 않는다 — 메시 없는 꾸미기는 아래 검사에서 걸린다.
		if (!bCosmetic && bMeshPendingSlot && Row->EquipMeshRef.IsNull() && Row->EquipOutfitMeshRef.IsNull())
		{
			++MeshPendingRowCount;
			continue;
		}

		++EquipmentRowCount;

		// **부위마다 채워야 할 칸이 다르다** (M9-02) — 옷은 소켓에 매달지 않고 몸에 겹치므로
		// 소켓 이름이 아니라 스켈레탈 메시를 본다. 한 행이 양쪽을 다 채우면 어느 길로 갈지 모호해진다.
		if (UEquipmentComponent::IsOutfitSlot(Row->EquipSlot))
		{
			TestTrue(FString::Printf(TEXT("[%s] 옷 행은 손 메시 칸이 비었다"), *RowName),
				Row->EquipMeshRef.IsNull());
			if (!TestFalse(FString::Printf(TEXT("[%s] 옷 메시 참조가 적혀 있다"), *RowName),
				Row->EquipOutfitMeshRef.IsNull()))
			{
				continue;
			}
			TestNotNull(FString::Printf(TEXT("[%s] 옷 메시를 실제로 읽는다: %s"),
				*RowName, *Row->EquipOutfitMeshRef.ToString()), Row->EquipOutfitMeshRef.LoadSynchronous());
			continue;
		}

		TestFalse(FString::Printf(TEXT("[%s] 붙일 소켓 이름이 적혀 있다"), *RowName),
			Row->EquipSocketName.IsNone());
		TestTrue(FString::Printf(TEXT("[%s] 손에 드는 행은 옷 메시 칸이 비었다"), *RowName),
			Row->EquipOutfitMeshRef.IsNull());

		if (!TestFalse(FString::Printf(TEXT("[%s] 메시 참조가 적혀 있다"), *RowName), Row->EquipMeshRef.IsNull()))
		{
			continue;
		}

		// 경로만 맞는 유령 참조를 통과로 세지 않으려고 실제로 읽어 본다.
		UStaticMesh* Mesh = Row->EquipMeshRef.LoadSynchronous();
		if (!TestNotNull(FString::Printf(TEXT("[%s] 메시를 실제로 읽는다: %s"),
			*RowName, *Row->EquipMeshRef.ToString()), Mesh))
		{
			continue;
		}

		if (!TestTrue(FString::Printf(TEXT("[%s] 메시에 LOD 가 있다"), *RowName), Mesh->GetNumLODs() > 0))
		{
			continue;
		}

		// 껍데기(삼각형 0)와 한도밖을 양쪽 다 본다 — 감축이 풀리면 여기서 걸린다.
		const int32 Triangles = Mesh->GetNumTriangles(0);
		TestTrue(FString::Printf(TEXT("[%s] 메시가 비어 있지 않다 (삼각형 %d)"), *RowName, Triangles),
			Triangles > 0);
		TestTrue(FString::Printf(TEXT("[%s] 메시가 모바일 한도 안이다 (삼각형 %d 이하 %d)"),
			*RowName, Triangles, TriangleLimit), Triangles <= TriangleLimit);
	}

	// 장비 행이 0개면 위 반복이 아무것도 안 보고 조용히 통과한다 (Verification.md — 못 한 검사를 통과라 쓰지 않는다).
	TestTrue(TEXT("장비 행이 적어도 하나는 있다"), EquipmentRowCount > 0);
	AddInfo(FString::Printf(TEXT("장비 행 %d개 검사 · 메시 대기 %d개 (전체 %d행)"),
		EquipmentRowCount, MeshPendingRowCount, ItemTable->GetRowMap().Num()));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
