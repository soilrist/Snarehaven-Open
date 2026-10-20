// ROADMAP M0-05 1층 검사 — Content/Game/Data 의 데이터 테이블이 성한지 (docs/checks/unreal.md "데이터 무결성").
// 테이블 목록을 손으로 적지 않고 에셋 레지스트리로 훑기 때문에, M1-01 에서 DT_AnimalTable 이 생기면
// 아무 것도 안 고쳐도 그 테이블이 이 검사에 들어온다. 지금은 테이블이 0개라 "검사할 것 없음" 으로 끝난다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Animation/AnimBank.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
	// 데이터 테이블이 사는 곳 (FolderStructure.md · engines/unreal.md "폴더 구조").
	const TCHAR* DataRootPath = TEXT("/Game/Game/Data");

	// 행 한 줄에서 찾은 문제. 바로 AddError 하지 않고 모아서 돌려주는 이유 —
	// 아래 CheckCatchesBrokenReference 가 "이 검사가 정말 잡는가" 를 다시 검사할 수 있게.
	struct FRowIssue
	{
		bool bIsError = false;
		FString Message;
	};

	// 행 한 줄의 에셋 참조(TSoftObjectPtr)를 본다.
	void CollectRowIssues(const FString& TableName, const FString& RowName, const UScriptStruct* RowStruct,
		const uint8* RowData, TArray<FRowIssue>& OutIssues)
	{
		for (TFieldIterator<FSoftObjectProperty> PropertyIt(RowStruct); PropertyIt; ++PropertyIt)
		{
			const FSoftObjectProperty* SoftObjectProperty = *PropertyIt;
			const FSoftObjectPtr Reference = SoftObjectProperty->GetPropertyValue_InContainer(RowData);
			const FString PropertyName = SoftObjectProperty->GetName();

			if (Reference.IsNull())
			{
				// 빈 참조는 "아직 안 붙인 에셋" 일 수 있어 경고로만 센다 (DataDriven.md 자리표시와 같은 취급).
				OutIssues.Add({ false, FString::Printf(TEXT("[%s:%s] %s 참조가 비었다"),
					*TableName, *RowName, *PropertyName) });
				continue;
			}

			// 경로는 적혀 있는데 그 에셋이 없는 것 — 이름을 바꾸거나 지웠을 때 생기는 진짜 깨진 참조다.
			const FString PackageName = Reference.ToSoftObjectPath().GetLongPackageName();
			if (!FPackageName::DoesPackageExist(PackageName))
			{
				OutIssues.Add({ true, FString::Printf(TEXT("[%s:%s] %s 가 없는 에셋을 가리킨다: %s"),
					*TableName, *RowName, *PropertyName, *Reference.ToString()) });
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDataTableIntegrityTest, "Snarehaven.Data.Tables.AreSound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDataTableIntegrityTest::RunTest(const FString& Parameters)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
		TEXT("AssetRegistry")).Get();

	// 커맨드릿에서는 레지스트리가 아직 안 훑은 상태라 동기 스캔이 필요하다 (안 하면 테이블 0개로 조용히 통과).
	AssetRegistry.ScanPathsSynchronous({ FString(DataRootPath) }, /*bForceRescan=*/true);

	TArray<FAssetData> DataTableAssets;
	AssetRegistry.GetAssetsByClass(UDataTable::StaticClass()->GetClassPathName(), DataTableAssets,
		/*bSearchSubClasses=*/true);
	DataTableAssets.RemoveAll([](const FAssetData& AssetData)
	{
		return !AssetData.PackageName.ToString().StartsWith(DataRootPath);
	});

	if (DataTableAssets.Num() == 0)
	{
		// 아직 테이블이 없는 건 오류가 아니다 (M1-01 전). 다만 "검사가 아무것도 안 봤다" 를 로그로 남겨
		// 나중에 이 검사가 조용히 비어 있는 걸 통과로 착각하지 않게 한다 (Verification.md).
		AddInfo(FString::Printf(TEXT("%s 아래에 데이터 테이블이 아직 없다 — 검사할 것 없음"), DataRootPath));
		return true;
	}

	for (const FAssetData& AssetData : DataTableAssets)
	{
		const FString TableName = AssetData.AssetName.ToString();
		UDataTable* DataTable = Cast<UDataTable>(AssetData.GetAsset());
		if (!IsValid(DataTable))
		{
			AddError(FString::Printf(TEXT("[%s] 데이터 테이블을 열 수 없다"), *TableName));
			continue;
		}

		const UScriptStruct* RowStruct = DataTable->GetRowStruct();
		if (!IsValid(RowStruct))
		{
			// 행 구조체가 없으면 행을 하나도 읽을 수 없다 — 아래 검사가 전부 무의미해지므로 여기서 끊는다.
			AddError(FString::Printf(TEXT("[%s] 행 구조체(RowStruct)가 비었다"), *TableName));
			continue;
		}

		if (DataTable->GetRowMap().Num() == 0)
		{
			AddError(FString::Printf(TEXT("[%s] 행이 하나도 없다"), *TableName));
			continue;
		}

		for (const TPair<FName, uint8*>& RowPair : DataTable->GetRowMap())
		{
			const FString RowName = RowPair.Key.ToString();
			if (RowName.TrimStartAndEnd().IsEmpty())
			{
				AddError(FString::Printf(TEXT("[%s] 행 이름이 비었다"), *TableName));
				continue;
			}

			TArray<FRowIssue> RowIssues;
			CollectRowIssues(TableName, RowName, RowStruct, RowPair.Value, RowIssues);
			for (const FRowIssue& Issue : RowIssues)
			{
				Issue.bIsError ? AddError(Issue.Message) : AddWarning(Issue.Message);
			}
		}

		AddInfo(FString::Printf(TEXT("[%s] 행 %d개 검사"), *TableName, DataTable->GetRowMap().Num()));
	}

	return true;
}


// 위 검사가 "테이블이 없어서" 조용히 통과하는 동안에도 검사 로직이 살아 있는지 본다 (M1-01 전까지 특히 중요).
// 에셋을 만들지 않고 행 구조체 인스턴스를 손으로 만들어 검사 함수에 직접 먹인다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDataTableCheckSelfTest,
	"Snarehaven.Data.Tables.CheckCatchesBrokenReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDataTableCheckSelfTest::RunTest(const FString& Parameters)
{
	// 소프트 참조 한 칸짜리 엔진 구조체를 행 구조체 대역으로 쓴다 (테스트 전용 — Verification.md 6).
	const UScriptStruct* RowStruct = FSoftAnimBankItem::StaticStruct();

	FSoftAnimBankItem EmptyRow;
	TArray<FRowIssue> EmptyRowIssues;
	CollectRowIssues(TEXT("DT_Test"), TEXT("EmptyRef"), RowStruct, reinterpret_cast<const uint8*>(&EmptyRow),
		EmptyRowIssues);
	TestEqual(TEXT("빈 참조는 문제 1건"), EmptyRowIssues.Num(), 1);
	if (EmptyRowIssues.Num() == 1)
	{
		TestFalse(TEXT("빈 참조는 경고(오류 아님)"), EmptyRowIssues[0].bIsError);
	}

	FSoftAnimBankItem BrokenRow;
	BrokenRow.BankAsset = TSoftObjectPtr<UAnimBank>(
		FSoftObjectPath(TEXT("/Game/Game/Data/NoSuchAsset.NoSuchAsset")));
	TArray<FRowIssue> BrokenRowIssues;
	CollectRowIssues(TEXT("DT_Test"), TEXT("BrokenRef"), RowStruct, reinterpret_cast<const uint8*>(&BrokenRow),
		BrokenRowIssues);
	TestEqual(TEXT("없는 에셋 참조는 문제 1건"), BrokenRowIssues.Num(), 1);
	if (BrokenRowIssues.Num() == 1)
	{
		TestTrue(TEXT("없는 에셋 참조는 오류"), BrokenRowIssues[0].bIsError);
	}

	// 멀쩡한 참조까지 오류로 만들면 검사가 쓸모없어진다 — 실제로 있는 패키지로 반대쪽도 본다.
	FSoftAnimBankItem SoundRow;
	SoundRow.BankAsset = TSoftObjectPtr<UAnimBank>(FSoftObjectPath(TEXT("/Game/Game/Maps/L_Boot.L_Boot")));
	TArray<FRowIssue> SoundRowIssues;
	CollectRowIssues(TEXT("DT_Test"), TEXT("SoundRef"), RowStruct, reinterpret_cast<const uint8*>(&SoundRow),
		SoundRowIssues);
	TestEqual(TEXT("있는 에셋 참조는 문제 없음"), SoundRowIssues.Num(), 0);

	return true;
}

// M79 — 아이템 표시 이름(`DisplayName`)은 비어 있지 않고 서로 겹치지 않는다.
// 비었거나 겹치면 배낭·도감·상점에서 두 칸이 같은 이름(또는 빈 글씨)으로 보이는데 다른 시험이 못 잡는다.
// 위와 같은 짜임: 수집기가 이슈를 돌려주고, 자기검사가 손으로 지은 표로 수집기를 먼저 본다.
namespace
{
	TArray<FString> CollectDisplayNameIssues(const TMap<FName, FItemRow>& Rows)
	{
		TArray<FString> Issues;
		TMap<FString, FName> FirstOwner;
		for (const TPair<FName, FItemRow>& RowPair : Rows)
		{
			const FString Name = RowPair.Value.DisplayName.ToString().TrimStartAndEnd();
			if (Name.IsEmpty())
			{
				Issues.Add(FString::Printf(TEXT("[%s] DisplayName 이 비었다"), *RowPair.Key.ToString()));
				continue;
			}
			if (const FName* Owner = FirstOwner.Find(Name))
			{
				Issues.Add(FString::Printf(TEXT("[%s] DisplayName \"%s\" 이 [%s] 와 겹친다"),
					*RowPair.Key.ToString(), *Name, *Owner->ToString()));
			}
			else
			{
				FirstOwner.Add(Name, RowPair.Key);
			}
		}
		return Issues;
	}

	FItemRow MakeNamedRow(const TCHAR* Name)
	{
		FItemRow Row;
		Row.DisplayName = FText::FromString(Name);
		return Row;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenItemDisplayNamesTest, "Snarehaven.Data.Items.DisplayNamesUnique",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenItemDisplayNamesTest::RunTest(const FString& Parameters)
{
	UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (!TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}

	TMap<FName, FItemRow> Rows;
	for (const FName& RowName : ItemTable->GetRowNames())
	{
		if (const FItemRow* Row = ItemTable->FindRow<FItemRow>(RowName, TEXT("ItemDisplayNamesTest")))
		{
			Rows.Add(RowName, *Row);
		}
	}
	if (!TestTrue(TEXT("아이템 행이 하나 이상 있다"), Rows.Num() > 0))
	{
		return false;
	}

	const TArray<FString> Issues = CollectDisplayNameIssues(Rows);
	for (const FString& Issue : Issues)
	{
		AddError(Issue);
	}
	AddInfo(FString::Printf(TEXT("아이템 행 %d개 · 표시 이름 문제 %d건"), Rows.Num(), Issues.Num()));
	return true;
}

// M79-02 역검증 — 손으로 지은 표(검사 전용 값)로 수집기가 정말 잡는지.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenItemDisplayNamesSelfTest,
	"Snarehaven.Data.Items.DisplayNameCheckCatchesBlankAndDuplicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenItemDisplayNamesSelfTest::RunTest(const FString& Parameters)
{
	TMap<FName, FItemRow> Sound;
	Sound.Add(FName(TEXT("A")), MakeNamedRow(TEXT("가")));
	Sound.Add(FName(TEXT("B")), MakeNamedRow(TEXT("나")));
	TestEqual(TEXT("멀쩡한 표는 문제 없음"), CollectDisplayNameIssues(Sound).Num(), 0);

	TMap<FName, FItemRow> Blank = Sound;
	Blank.Add(FName(TEXT("C")), MakeNamedRow(TEXT("  ")));
	TestEqual(TEXT("공백뿐인 이름은 문제 1건"), CollectDisplayNameIssues(Blank).Num(), 1);

	TMap<FName, FItemRow> Dup = Sound;
	Dup.Add(FName(TEXT("C")), MakeNamedRow(TEXT("가")));
	TestEqual(TEXT("겹친 쌍은 문제 1건"), CollectDisplayNameIssues(Dup).Num(), 1);
	return true;
}

// M80 — 아이템 행 이름은 ItemID 에서 규칙으로 나온다: `item.pig_meat` → `Item_PigMeat`, `gacha.pet_bell` → `Item_GachaPetBell`.
// 행 이름을 코드가 문자열로 찾는 곳이 있어 어긋난 행은 조용히 못 찾는다. 규칙은 표 스크립트와 같다.
namespace
{
	FString ExpectedItemRowName(const FString& ItemID)
	{
		FString Prefix, Rest;
		if (!ItemID.Split(TEXT("."), &Prefix, &Rest))
		{
			return FString();
		}
		const FString Joined = (Prefix == TEXT("item")) ? Rest : (Prefix + TEXT("_") + Rest);
		TArray<FString> Parts;
		Joined.ParseIntoArray(Parts, TEXT("_"));
		FString Out = TEXT("Item_");
		for (FString& Part : Parts)
		{
			if (Part.Len() > 0)
			{
				Out += Part.Left(1).ToUpper() + Part.Mid(1);
			}
		}
		return Out;
	}

	TArray<FString> CollectRowNameIssues(const TMap<FName, FItemRow>& Rows)
	{
		TArray<FString> Issues;
		for (const TPair<FName, FItemRow>& RowPair : Rows)
		{
			const FString Expected = ExpectedItemRowName(RowPair.Value.ItemID.ToString());
			if (Expected.IsEmpty() || Expected != RowPair.Key.ToString())
			{
				Issues.Add(FString::Printf(TEXT("[%s] ItemID \"%s\" 의 행 이름은 \"%s\" 여야 한다"),
					*RowPair.Key.ToString(), *RowPair.Value.ItemID.ToString(), *Expected));
			}
		}
		return Issues;
	}

	FItemRow MakeIdRow(const TCHAR* ItemID)
	{
		FItemRow Row;
		Row.ItemID = FName(ItemID);
		return Row;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenItemRowNamesTest, "Snarehaven.Data.Items.RowNameMatchesItemID",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenItemRowNamesTest::RunTest(const FString& Parameters)
{
	UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (!TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}

	TMap<FName, FItemRow> Rows;
	for (const FName& RowName : ItemTable->GetRowNames())
	{
		if (const FItemRow* Row = ItemTable->FindRow<FItemRow>(RowName, TEXT("ItemRowNamesTest")))
		{
			Rows.Add(RowName, *Row);
		}
	}
	if (!TestTrue(TEXT("아이템 행이 하나 이상 있다"), Rows.Num() > 0))
	{
		return false;
	}

	const TArray<FString> Issues = CollectRowNameIssues(Rows);
	for (const FString& Issue : Issues)
	{
		AddError(Issue);
	}
	AddInfo(FString::Printf(TEXT("아이템 행 %d개 · 행 이름 문제 %d건"), Rows.Num(), Issues.Num()));
	return true;
}

// M80-02 역검증 — 손으로 지은 표로 수집기가 어긋난 행 이름을 정말 잡는지.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenItemRowNamesSelfTest,
	"Snarehaven.Data.Items.RowNameCheckCatchesMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenItemRowNamesSelfTest::RunTest(const FString& Parameters)
{
	TMap<FName, FItemRow> Sound;
	Sound.Add(FName(TEXT("Item_PigMeat")), MakeIdRow(TEXT("item.pig_meat")));
	Sound.Add(FName(TEXT("Item_GachaPetBell")), MakeIdRow(TEXT("gacha.pet_bell")));
	TestEqual(TEXT("멀쩡한 표는 문제 없음"), CollectRowNameIssues(Sound).Num(), 0);

	TMap<FName, FItemRow> Wrong = Sound;
	Wrong.Add(FName(TEXT("Item_PigMeet")), MakeIdRow(TEXT("item.pig_meat")));
	TestEqual(TEXT("어긋난 행 이름은 문제 1건"), CollectRowNameIssues(Wrong).Num(), 1);

	TMap<FName, FItemRow> NoDot = Sound;
	NoDot.Add(FName(TEXT("Item_X")), MakeIdRow(TEXT("nodot")));
	TestEqual(TEXT("점 없는 ItemID 는 문제 1건"), CollectRowNameIssues(NoDot).Num(), 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
