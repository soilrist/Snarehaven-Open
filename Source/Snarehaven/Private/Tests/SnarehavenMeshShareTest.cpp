// ROADMAP M40-01 2층 조사 — 아이템 표에서 **이름이 다른 행끼리 같은 메시를 빌려 쓰는 곳**을 찾는다.
// 루프#590 탐침이 `SM_Hat_Farmer_01`(농부 모자·동물 모자·밀짚모자 펫장식) 같은 빌림 3묶음과, 전등이 집 실내
// 기름 등불(`SM_Prop_Lantern_01`)을 빌린 것을 찾았다(Reference 「자리표시 품질로 합격 찍지 않는다」).
// 조사(`Survey`)는 겹침을 `AddInfo` 로 적기만 하고, M40-07 단정(`NoBorrowedMesh`)이 겹침 0 을 굳힌다 —
// 예외 표(`AllowedMeshShares`)에 이유와 함께 적힌 메시 말고는 다른 행끼리 같은 메시·월드 소품 빌림이 곧 실패.
//
// 보는 칸 넷: `EquipMeshRef`(손·머리·펫 꾸미기) · `EquipOutfitMeshRef`(입는 옷) · `PlacedMeshRef`(놓는 가구) ·
// `RanchDecoMeshRef`(목장 데코). 칸이 달라도 같은 메시면 겹침으로 센다(예: 손 메시와 가구 메시가 같은 것).
// 놓는 가구 메시가 월드 소품 폴더 바로 아래(`/Game/Game/Environment/Props/` 의 등불·수레 등 — 하위 폴더는 그
// 물건 전용)에 있으면 월드 소품을 빌린 것으로 따로 적는다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Data.MeshShare; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/ItemRow.h"
#include "Engine/DataTable.h"

namespace
{
	const TCHAR* MeshShareItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* WorldPropFolder = TEXT("/Game/Game/Environment/Props/");

	struct FMeshShareReport
	{
		// 메시 경로 → 그 메시를 쓰는 「행 이름(칸)」 들. 두 개 이상인 것만 남긴다.
		TMap<FString, TArray<FString>> Shared;
		// 월드 소품 폴더 바로 아래 메시를 놓는 가구로 쓰는 「행 이름 → 메시 경로」.
		TArray<FString> WorldPropBorrows;
		int32 DistinctMeshCount = 0;
		int32 FilledRefCount = 0;
	};

	// 행들을 훑어 겹침을 모은다. 바로 AddInfo 하지 않고 돌려주는 이유 — 아래 자기검사가 에셋 없이 지어낸
	// 행으로 "정말 잡는가" 를 다시 볼 수 있게 (SnarehavenItemIconTest 와 같은 짜임).
	FMeshShareReport CollectMeshShares(const TMap<FName, FItemRow>& Rows)
	{
		TMap<FString, TArray<FString>> Users;
		FMeshShareReport Report;

		auto Note = [&Users, &Report](const FName RowName, const TCHAR* Field, const FSoftObjectPath& Path)
		{
			if (Path.IsNull())
			{
				return;
			}
			++Report.FilledRefCount;
			Users.FindOrAdd(Path.GetAssetPathString()).Add(FString::Printf(TEXT("%s(%s)"), *RowName.ToString(), Field));
		};

		for (const TPair<FName, FItemRow>& RowPair : Rows)
		{
			const FItemRow& Row = RowPair.Value;
			Note(RowPair.Key, TEXT("Equip"), Row.EquipMeshRef.ToSoftObjectPath());
			Note(RowPair.Key, TEXT("Outfit"), Row.EquipOutfitMeshRef.ToSoftObjectPath());
			Note(RowPair.Key, TEXT("Placed"), Row.PlacedMeshRef.ToSoftObjectPath());
			Note(RowPair.Key, TEXT("RanchDeco"), Row.RanchDecoMeshRef.ToSoftObjectPath());

			const FSoftObjectPath Placed = Row.PlacedMeshRef.ToSoftObjectPath();
			if (!Placed.IsNull())
			{
				const FString PackagePath = Placed.GetLongPackageName();
				if (PackagePath.StartsWith(WorldPropFolder)
					&& !PackagePath.RightChop(FCString::Strlen(WorldPropFolder)).Contains(TEXT("/")))
				{
					Report.WorldPropBorrows.Add(FString::Printf(TEXT("%s → %s"), *RowPair.Key.ToString(), *PackagePath));
				}
			}
		}

		Report.DistinctMeshCount = Users.Num();
		for (TPair<FString, TArray<FString>>& Pair : Users)
		{
			// 한 행이 두 칸에 같은 메시를 쓴 것(예: 손 메시 = 놓는 메시)은 같은 물건이라 겹침이 아니다.
			TSet<FString> RowNames;
			for (const FString& User : Pair.Value)
			{
				FString RowName;
				User.Split(TEXT("("), &RowName, nullptr);
				RowNames.Add(RowName);
			}
			if (RowNames.Num() >= 2)
			{
				Pair.Value.Sort();
				Report.Shared.Add(Pair.Key, Pair.Value);
			}
		}
		Report.Shared.KeySort(TLess<FString>());
		Report.WorldPropBorrows.Sort();
		return Report;
	}

	// M40-07 예외 표 — 다른 행끼리 같은 메시를 써도 되는 것(같은 물건의 다른 판 등). 메시 경로 → 이유 한 줄.
	// 지금은 비었다(#0665 조사 「행끼리 겹친 메시 0개 · 월드 소품 빌림 0건」). 늘릴 땐 이유를 꼭 적는다.
	const TMap<FString, FString>& AllowedMeshShares()
	{
		static const TMap<FString, FString> Allowed;
		return Allowed;
	}

	// 조사 결과에서 예외 표에 없는 겹침·월드 소품 빌림을 실패 문장으로 바꾼다. 자기검사가 사본 표로 다시 부른다.
	TArray<FString> FindBorrowViolations(const FMeshShareReport& Report, const TMap<FString, FString>& Allowed)
	{
		TArray<FString> Violations;
		for (const TPair<FString, TArray<FString>>& Pair : Report.Shared)
		{
			if (!Allowed.Contains(Pair.Key))
			{
				Violations.Add(FString::Printf(TEXT("[겹침] %s ← %s"), *Pair.Key, *FString::Join(Pair.Value, TEXT(", "))));
			}
		}
		for (const FString& Borrow : Report.WorldPropBorrows)
		{
			Violations.Add(FString::Printf(TEXT("[월드 소품 빌림] %s"), *Borrow));
		}
		return Violations;
	}

	bool LoadItemRows(TMap<FName, FItemRow>& OutRows)
	{
		UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, MeshShareItemTablePath);
		if (!ItemTable)
		{
			return false;
		}
		for (const FName& RowName : ItemTable->GetRowNames())
		{
			if (const FItemRow* Row = ItemTable->FindRow<FItemRow>(RowName, TEXT("MeshShare")))
			{
				OutRows.Add(RowName, *Row);
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeshShareSurveyTest, "Snarehaven.Data.MeshShare.Survey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMeshShareSurveyTest::RunTest(const FString& Parameters)
{
	TMap<FName, FItemRow> Rows;
	if (!TestTrue(TEXT("DT_ItemTable 로드"), LoadItemRows(Rows)))
	{
		return false;
	}

	const FMeshShareReport Report = CollectMeshShares(Rows);

	// 메시 칸이 하나도 안 채워졌으면 훑기가 아무것도 안 본 것이다 (Verification.md — 빈 검사 금지).
	TestTrue(TEXT("메시 칸이 채워진 행이 있다"), Report.FilledRefCount > 0);

	for (const TPair<FString, TArray<FString>>& Pair : Report.Shared)
	{
		AddInfo(FString::Printf(TEXT("[겹침] %s ← %s"), *Pair.Key, *FString::Join(Pair.Value, TEXT(", "))));
	}
	for (const FString& Borrow : Report.WorldPropBorrows)
	{
		AddInfo(FString::Printf(TEXT("[월드 소품 빌림] %s"), *Borrow));
	}
	AddInfo(FString::Printf(TEXT("아이템 행 %d개 · 메시 칸 %d개 · 서로 다른 메시 %d개 · 행끼리 겹친 메시 %d개 · 월드 소품 빌림 %d건"),
		Rows.Num(), Report.FilledRefCount, Report.DistinctMeshCount, Report.Shared.Num(), Report.WorldPropBorrows.Num()));
	return true;
}

// M40-07 단정 — 다른 행끼리 같은 메시(예외 표 밖)·월드 소품 빌림이 하나라도 있으면 실패.
// 역검증은 아래 `NoBorrowedMeshReverse` 가 진짜 표 사본에서 한 행 메시를 다른 행 것으로 바꿔 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeshShareNoBorrowTest, "Snarehaven.Data.MeshShare.NoBorrowedMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMeshShareNoBorrowTest::RunTest(const FString& Parameters)
{
	TMap<FName, FItemRow> Rows;
	if (!TestTrue(TEXT("DT_ItemTable 로드"), LoadItemRows(Rows)))
	{
		return false;
	}

	const FMeshShareReport Report = CollectMeshShares(Rows);
	TestTrue(TEXT("메시 칸이 채워진 행이 있다"), Report.FilledRefCount > 0);

	for (const FString& Violation : FindBorrowViolations(Report, AllowedMeshShares()))
	{
		AddError(FString::Printf(TEXT("다른 행 메시를 빌려 씀 — 전용 메시를 만들거나 예외 표에 이유를 적는다: %s"), *Violation));
	}
	for (const TPair<FString, FString>& Pair : AllowedMeshShares())
	{
		AddInfo(FString::Printf(TEXT("[예외] %s — %s"), *Pair.Key, *Pair.Value));
	}
	AddInfo(FString::Printf(TEXT("단정: 아이템 행 %d개 · 서로 다른 메시 %d개 · 예외 밖 겹침·빌림 %d건 · 예외 %d개"),
		Rows.Num(), Report.DistinctMeshCount, FindBorrowViolations(Report, AllowedMeshShares()).Num(), AllowedMeshShares().Num()));
	return true;
}

// 역검증 — 진짜 표의 **사본**에서 메시가 있는 행 하나의 메시를 다른 행 메시로 바꾸면 단정이 실패 문장을 낸다.
// 표 에셋·파일은 건드리지 않는다(PROMPT ③-13 — 역검증은 사본으로).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeshShareReverseTest, "Snarehaven.Data.MeshShare.NoBorrowedMeshReverse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMeshShareReverseTest::RunTest(const FString& Parameters)
{
	TMap<FName, FItemRow> Rows;
	if (!TestTrue(TEXT("DT_ItemTable 로드"), LoadItemRows(Rows)))
	{
		return false;
	}

	// 손 메시가 있는 서로 다른 두 행을 이름 순으로 골라 둘째가 첫째 메시를 빌리게 한다.
	TArray<FName> EquipRows;
	for (const TPair<FName, FItemRow>& RowPair : Rows)
	{
		if (!RowPair.Value.EquipMeshRef.IsNull())
		{
			EquipRows.Add(RowPair.Key);
		}
	}
	EquipRows.Sort(FNameLexicalLess());
	if (!TestTrue(TEXT("손 메시가 있는 행이 둘 이상"), EquipRows.Num() >= 2))
	{
		return false;
	}

	TMap<FName, FItemRow> Copy = Rows;
	FItemRow& Borrower = Copy.FindChecked(EquipRows[1]);
	Borrower.EquipMeshRef = Copy.FindChecked(EquipRows[0]).EquipMeshRef;
	Borrower.EquipOutfitMeshRef.Reset();
	Borrower.PlacedMeshRef.Reset();
	Borrower.RanchDecoMeshRef.Reset();

	const int32 Before = FindBorrowViolations(CollectMeshShares(Rows), AllowedMeshShares()).Num();
	const TArray<FString> After = FindBorrowViolations(CollectMeshShares(Copy), AllowedMeshShares());
	TestTrue(TEXT("사본에서 빌림을 만들면 실패 문장이 늘어난다"), After.Num() > Before);
	const FString Mesh = Copy.FindChecked(EquipRows[0]).EquipMeshRef.ToSoftObjectPath().GetAssetPathString();
	TestTrue(TEXT("늘어난 실패 문장이 빌린 메시를 가리킨다"),
		After.ContainsByPredicate([&Mesh](const FString& Line) { return Line.Contains(Mesh); }));
	AddInfo(FString::Printf(TEXT("역검증: %s 가 %s 메시를 빌리게 한 사본 → 실패 문장 %d → %d"),
		*EquipRows[1].ToString(), *EquipRows[0].ToString(), Before, After.Num()));
	return true;
}

// 자기검사 — 지어낸 행으로 "겹침을 잡고, 한 행 안의 같은 메시는 겹침이 아니고, 월드 소품 하위 폴더는 빌림이
// 아니다" 를 본다. 이게 깨지면 위 조사가 0 을 내도 믿을 수 없다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeshShareSelfTest, "Snarehaven.Data.MeshShare.SelfTest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMeshShareSelfTest::RunTest(const FString& Parameters)
{
	const FString HatPath = TEXT("/Game/Test/SM_Test_Hat.SM_Test_Hat");
	const FString ChairPath = TEXT("/Game/Test/SM_Test_Chair.SM_Test_Chair");

	TMap<FName, FItemRow> Rows;
	FItemRow& HatA = Rows.Add(TEXT("Item_TestHatA"));
	HatA.EquipMeshRef = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(HatPath));
	FItemRow& HatB = Rows.Add(TEXT("Item_TestHatB"));
	HatB.EquipMeshRef = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(HatPath));
	FItemRow& Chair = Rows.Add(TEXT("Item_TestChair"));
	Chair.EquipMeshRef = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(ChairPath));
	Chair.PlacedMeshRef = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(ChairPath));
	FItemRow& Lamp = Rows.Add(TEXT("Item_TestLamp"));
	Lamp.PlacedMeshRef = TSoftObjectPtr<UStaticMesh>(
		FSoftObjectPath(TEXT("/Game/Game/Environment/Props/SM_Prop_Lantern_01.SM_Prop_Lantern_01")));
	FItemRow& Panel = Rows.Add(TEXT("Item_TestPanel"));
	Panel.PlacedMeshRef = TSoftObjectPtr<UStaticMesh>(
		FSoftObjectPath(TEXT("/Game/Game/Environment/Props/Power/SM_Prop_SolarPanel_01.SM_Prop_SolarPanel_01")));

	const FMeshShareReport Report = CollectMeshShares(Rows);
	TestEqual(TEXT("겹친 메시는 모자 하나"), Report.Shared.Num(), 1);
	TestTrue(TEXT("모자 메시를 겹침으로 잡는다"), Report.Shared.Contains(HatPath));
	TestFalse(TEXT("한 행 안에서 손·놓기가 같은 의자는 겹침이 아니다"), Report.Shared.Contains(ChairPath));
	TestEqual(TEXT("월드 소품 빌림은 등불 하나(Power 하위 폴더는 전용)"), Report.WorldPropBorrows.Num(), 1);
	TestEqual(TEXT("메시 칸 6개를 셌다"), Report.FilledRefCount, 6);

	// 단정 함수 — 예외 표 밖 모자 겹침 + 등불 빌림 = 2, 모자를 예외로 적으면 등불만 남는다.
	TestEqual(TEXT("예외 없으면 실패 문장 2"), FindBorrowViolations(Report, {}).Num(), 2);
	const TMap<FString, FString> AllowHat = { { HatPath, TEXT("시험용 — 같은 물건의 다른 판") } };
	TestEqual(TEXT("모자를 예외로 적으면 실패 문장 1"), FindBorrowViolations(Report, AllowHat).Num(), 1);
	return true;
}

#endif
