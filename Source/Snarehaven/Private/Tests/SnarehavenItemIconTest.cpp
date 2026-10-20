// ROADMAP M5-08 2층 검사 — 아이템 행의 `IconRef` 가 **그 행의 ID 에 맞는 아이콘**을 가리키는지.
// 채우는 곳은 `tools/unreal/create_data_tables.py` 의 `fill_item_icons()` 이고, 들이는 곳은
// `tools/unreal/import_item_icons.py` 다. 둘 다 `item.fox_meat` → `T_Icon_FoxMeat` 라는 **같은 한 규칙**을
// 쓰는데, 규칙을 세 자리(임포터·표 채우기·검사)에 적은 이상 셋이 갈릴 수 있다. 이 검사가 그 갈림을 잡는다.
//
// 보는 것 넷: ① 아이콘 에셋이 있는데 행의 칸이 빈 것(표를 다시 안 채웠다) ② 칸이 ID 와 **다른** 아이콘을
// 가리키는 것(짝이 어긋났다 — 무결성 검사는 "경로가 살아 있나" 만 봐서 이걸 못 잡는다) ③ 칸이 없는 에셋을
// 가리키는 것 ④ 아이콘은 있는데 **짝이 되는 행이 없는** 것(행 이름이 아이콘과 갈렸다 — 루프#176 이 찾은
// `item.feather` ↔ `T_Icon_PheasantFeather` 가 이 모양이었다, 감독 2026-09-24 12:40). **아이콘이 아직 없는 행의 빈 칸은 주문서(`docs/specs/icon-sheet-order.md`) 21행만 허용한다**
// (M78 — `IconsAwaitingSheetIDs`). 목록 밖 빈 칸은 실패, 목록 안인데 아이콘이 붙었으면 「목록에서 지워라」 로 실패.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Data.Items; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Misc/PackageName.h"

namespace
{
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* IconFolder = TEXT("/Game/Game/UI/Icons/Items");

	// `item.fox_meat` → `T_Icon_FoxMeat`. 파이썬 두 자리와 같은 규칙이다 (`AssetNaming.md` — ID 에서
	// 이름을, 이름에서 ID 를 기계적으로 만들 수 있어야 한다).
	FString IconAssetNameForItemID(const FName ItemID)
	{
		FString Suffix = ItemID.ToString();
		int32 DotIndex = INDEX_NONE;
		if (Suffix.FindChar(TEXT('.'), DotIndex))
		{
			Suffix.MidInline(DotIndex + 1);
		}

		// 행 ID 와 아이콘 이름이 갈린 것 — `create_data_tables.py` 의 `ICON_NAME_EXCEPTIONS` 와 같은 표 (M13-23).
		if (Suffix == TEXT("hatchet"))
		{
			return TEXT("T_Icon_HandAxe");
		}

		TArray<FString> Parts;
		Suffix.ParseIntoArray(Parts, TEXT("_"), /*InCullEmpty=*/true);

		FString Name = TEXT("T_Icon_");
		for (const FString& Part : Parts)
		{
			Name += Part.Left(1).ToUpper() + Part.RightChop(1);
		}
		return Name;
	}

	FString IconPackagePathForItemID(const FName ItemID)
	{
		return FString::Printf(TEXT("%s/%s"), IconFolder, *IconAssetNameForItemID(ItemID));
	}

	FString IconObjectPathForItemID(const FName ItemID)
	{
		const FString AssetName = IconAssetNameForItemID(ItemID);
		return FString::Printf(TEXT("%s/%s.%s"), IconFolder, *AssetName, *AssetName);
	}

	// 행들을 훑어 문제를 모은다. 바로 AddError 하지 않고 돌려주는 이유 — 아래 자기검사가 "이 검사가
	// 정말 잡는가" 를 에셋 없이 다시 볼 수 있게 (SnarehavenZoneTableTest 와 같은 짜임).
	// AssetExists 를 인자로 받는 것도 같은 이유다: 자기검사가 디스크를 안 건드리고 에셋을 흉내낸다.
	TArray<FString> CollectIconIssues(const TMap<FName, FItemRow>& Rows,
		TFunctionRef<bool(const FString&)> AssetExists, const TSet<FName>& AwaitingSheet,
		int32& OutFilledCount, int32& OutEmptyCount)
	{
		TArray<FString> Issues;
		OutFilledCount = 0;
		OutEmptyCount = 0;

		for (const TPair<FName, FItemRow>& RowPair : Rows)
		{
			const FString RowName = RowPair.Key.ToString();
			const FItemRow& Row = RowPair.Value;

			if (Row.ItemID.IsNone())
			{
				// ID 가 없으면 어느 아이콘이 맞는지 셀 수조차 없다 — 무결성 검사가 아니라 여기서도 짚는다.
				Issues.Add(FString::Printf(TEXT("[%s] ItemID 가 비었다 — 아이콘 짝을 잴 수 없다"), *RowName));
				continue;
			}

			const FString ExpectedObjectPath = IconObjectPathForItemID(Row.ItemID);
			const bool bIconAssetExists = AssetExists(IconPackagePathForItemID(Row.ItemID));

			if (Row.IconRef.IsNull())
			{
				++OutEmptyCount;
				// M78 — 빈 칸은 주문서(`docs/specs/icon-sheet-order.md`)에 적힌 행만 허용한다.
				// 새 아이템을 넣고 아이콘을 잊으면 여기서 걸린다.
				if (!bIconAssetExists && !AwaitingSheet.Contains(Row.ItemID))
				{
					Issues.Add(FString::Printf(
						TEXT("[%s] 아이콘이 없는데 주문서 목록(IconsAwaitingSheet)에도 없다 — 아이콘을 붙이거나 주문서에 올려라"),
						*RowName));
				}
				if (bIconAssetExists)
				{
					Issues.Add(FString::Printf(
						TEXT("[%s] 아이콘 에셋 %s 가 있는데 IconRef 가 비었다 (create_data_tables.py 를 다시 돌려라)"),
						*RowName, *ExpectedObjectPath));
				}
				continue;
			}

			++OutFilledCount;

			if (AwaitingSheet.Contains(Row.ItemID))
			{
				Issues.Add(FString::Printf(
					TEXT("[%s] 아이콘이 이미 붙었다 — IconsAwaitingSheet 목록에서 지워라"), *RowName));
			}

			const FString ActualObjectPath = Row.IconRef.ToString();
			if (ActualObjectPath != ExpectedObjectPath)
			{
				Issues.Add(FString::Printf(TEXT("[%s] IconRef 가 ID(%s)와 안 맞는다: %s (맞는 것 %s)"),
					*RowName, *Row.ItemID.ToString(), *ActualObjectPath, *ExpectedObjectPath));
			}
			else if (!bIconAssetExists)
			{
				Issues.Add(FString::Printf(TEXT("[%s] IconRef 가 없는 에셋을 가리킨다: %s"),
					*RowName, *ActualObjectPath));
			}
		}

		return Issues;
	}

	// M78 — 아이콘이 아직 없어도 되는 행: 감독 시트 반입을 기다리는 주문서 3행 (`docs/specs/icon-sheet-order.md`).
	// 시트가 들어와 아이콘이 붙으면 여기서 지운다 (안 지우면 위 수집기가 실패로 알려 준다).
	const TCHAR* const IconsAwaitingSheetIDs[] = {
		// 시트 1·2·탄약 18칸은 10-11 감독 반입, 가챠 꾸미기 셋은 10-15 감독 아이콘(`b09f7291`) — 지금 기다리는 행 0.
		// 안개뿔 펫(10-16 #1185, 이야기 종)은 아이콘 주문 대기 — 아이콘이 붙으면 여기서 지운다.
		// 빈 배열은 C++ 에서 못 쓰니 빈 글자 하나를 둔다(`MakeAwaitingSheet` 가 건너뛴다).
		TEXT("item.pet_fogbuck"),
		TEXT("")
	};

	TSet<FName> MakeAwaitingSheet()
	{
		TSet<FName> Result;
		for (const TCHAR* const ID : IconsAwaitingSheetIDs)
		{
			if (ID[0] != TEXT('\0'))
			{
				Result.Add(FName(ID));
			}
		}
		return Result;
	}

	// ④ 에서 뺄 아이콘 — 아이콘이 먼저 들어왔고 행이 아직 짝이 안 된 것. 행이 서면 여기서 지운다.
	// 채집 바구니는 M9-04 로 행이 섰고, 손도끼는 M13-23 의 이름 예외(`item.hatchet` → `T_Icon_HandAxe`)로 짝이 섰다.
	const TCHAR* const IconsAwaitingRows[] = {
		TEXT("T_Icon_FishingRod")
	};

	// ④ 아이콘 폴더에서 출발해 행 쪽을 본다 — 위 수집기는 행에서 출발해서 행이 없는 아이콘을 못 본다.
	TArray<FString> CollectOrphanIcons(const TMap<FName, FItemRow>& Rows, const TArray<FString>& IconAssetNames)
	{
		TSet<FString> Claimed;
		for (const TCHAR* const Awaiting : IconsAwaitingRows)
		{
			Claimed.Add(Awaiting);
		}
		for (const TPair<FName, FItemRow>& RowPair : Rows)
		{
			Claimed.Add(IconAssetNameForItemID(RowPair.Value.ItemID));
		}

		TArray<FString> Issues;
		for (const FString& IconName : IconAssetNames)
		{
			if (!Claimed.Contains(IconName))
			{
				Issues.Add(FString::Printf(TEXT("[%s] 아이콘은 있는데 짝이 되는 아이템 행이 없다 (행 ID 가 아이콘 이름과 갈렸다)"),
					*IconName));
			}
		}
		return Issues;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenItemIconTest, "Snarehaven.Data.Items.IconMatchesItemID",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenItemIconTest::RunTest(const FString& Parameters)
{
	UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}

	TMap<FName, FItemRow> Rows;
	for (const FName& RowName : ItemTable->GetRowNames())
	{
		if (const FItemRow* Row = ItemTable->FindRow<FItemRow>(RowName, TEXT("ItemIconTest")))
		{
			Rows.Add(RowName, *Row);
		}
	}

	if (!TestTrue(TEXT("아이템 행이 하나 이상 있다"), Rows.Num() > 0))
	{
		return false;
	}

	int32 FilledCount = 0;
	int32 EmptyCount = 0;
	TArray<FString> Issues = CollectIconIssues(Rows, [](const FString& PackagePath)
	{
		return FPackageName::DoesPackageExist(PackagePath);
	}, MakeAwaitingSheet(), FilledCount, EmptyCount);

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.ScanPathsSynchronous({FString(IconFolder)}, /*bForceRescan=*/false);
	TArray<FAssetData> IconAssets;
	Registry.GetAssetsByPath(FName(IconFolder), IconAssets, /*bRecursive=*/false);
	TArray<FString> IconAssetNames;
	for (const FAssetData& Asset : IconAssets)
	{
		IconAssetNames.Add(Asset.AssetName.ToString());
	}
	TestTrue(TEXT("아이콘 폴더에서 에셋을 읽었다"), IconAssetNames.Num() > 0);
	Issues.Append(CollectOrphanIcons(Rows, IconAssetNames));

	for (const FString& Issue : Issues)
	{
		AddError(Issue);
	}

	// 한 행도 안 채워졌으면 위 훑기가 아무것도 안 본 것이다 (Verification.md — 빈 검사 금지).
	TestTrue(TEXT("아이콘이 붙은 행이 하나 이상 있다"), FilledCount > 0);
	AddInfo(FString::Printf(TEXT("빈 칸 행 %d개 (주문서 허용 %d개)"), EmptyCount, MakeAwaitingSheet().Num()));
	// M31-05 — 빈 칸 행 이름을 다 적고, 빈 칸 수가 주문서 수와 같아야 한다 (목록에 남은 낡은 ID 도 잡는다).
	for (const TPair<FName, FItemRow>& RowPair : Rows)
	{
		if (RowPair.Value.IconRef.IsNull())
		{
			AddInfo(FString::Printf(TEXT("[빈 칸] %s (%s)"), *RowPair.Key.ToString(), *RowPair.Value.ItemID.ToString()));
		}
	}
	TestEqual(TEXT("빈 칸 행 수 = 주문서 목록 수"), EmptyCount, MakeAwaitingSheet().Num());
	AddInfo(FString::Printf(TEXT("아이템 행 %d개 중 아이콘이 붙은 행 %d개 · 아이콘 %d개 · 어긋난 짝 %d건"),
		Rows.Num(), FilledCount, IconAssetNames.Num(), Issues.Num()));

	// M13-23 손도끼 — 행 ID 는 `item.hatchet` 그대로, 아이콘은 이름 예외로 `T_Icon_HandAxe`.
	const FItemRow* const Hatchet = Rows.Find(FName(TEXT("Item_Hatchet")));
	if (TestNotNull(TEXT("Item_Hatchet 행"), Hatchet))
	{
		AddInfo(FString::Printf(TEXT("[Item_Hatchet] IconRef %s"), *Hatchet->IconRef.ToString()));
		TestEqual(TEXT("Item_Hatchet IconRef"), Hatchet->IconRef.ToString(),
			FString(TEXT("/Game/Game/UI/Icons/Items/T_Icon_HandAxe.T_Icon_HandAxe")));
	}

	return true;
}


// 위 검사가 "표가 멀쩡해서" 통과할 때도 검사 로직이 살아 있는지 본다 (역검증).
// 에셋을 건드리지 않고 행을 손으로 지어 먹인다 — 검사 전용 값이라 실제 표의 이름에 기대지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenItemIconCheckSelfTest,
	"Snarehaven.Data.Items.IconCheckCatchesMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenItemIconCheckSelfTest::RunTest(const FString& Parameters)
{
	// 이름 규칙부터 — 세 자리가 갈리면 여기가 먼저 깨진다.
	TestEqual(TEXT("item.fox_meat → T_Icon_FoxMeat"),
		IconAssetNameForItemID(FName(TEXT("item.fox_meat"))), FString(TEXT("T_Icon_FoxMeat")));
	TestEqual(TEXT("점 없는 ID 도 같은 규칙"),
		IconAssetNameForItemID(FName(TEXT("club"))), FString(TEXT("T_Icon_Club")));
	TestEqual(TEXT("예외 item.hatchet → T_Icon_HandAxe"),
		IconAssetNameForItemID(FName(TEXT("item.hatchet"))), FString(TEXT("T_Icon_HandAxe")));

	// 검사용 가짜 아이콘 하나. 이 경로만 "있는 에셋" 으로 쳐 준다.
	const FName TestItemID(TEXT("item.test_thing"));
	const FString ExistingPackage = IconPackagePathForItemID(TestItemID);
	auto AssetExists = [&ExistingPackage](const FString& PackagePath) { return PackagePath == ExistingPackage; };

	FItemRow SoundRow;
	SoundRow.ItemID = TestItemID;
	SoundRow.IconRef = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(IconObjectPathForItemID(TestItemID)));

	int32 FilledCount = 0;
	int32 EmptyCount = 0;
	const TSet<FName> NoneAwaiting;
	TMap<FName, FItemRow> SoundRows;
	SoundRows.Add(FName(TEXT("Item_TestThing")), SoundRow);
	// 멀쩡한 짝까지 물면 검사가 쓸모없어진다 — 반대쪽부터 본다.
	TestEqual(TEXT("ID 에 맞는 아이콘은 문제 없음"),
		CollectIconIssues(SoundRows, AssetExists, NoneAwaiting, FilledCount, EmptyCount).Num(), 0);
	TestEqual(TEXT("채워진 행을 셌다"), FilledCount, 1);

	// ① 아이콘이 아직 없는 행의 빈 칸은 문제가 아니다 (자리표시 돼지·펫 행이 이 모양이다).
	TMap<FName, FItemRow> NoIconRows;
	FItemRow NoIconRow;
	NoIconRow.ItemID = FName(TEXT("item.test_no_icon"));
	NoIconRows.Add(FName(TEXT("Item_TestNoIcon")), NoIconRow);
	const TSet<FName> NoIconAwaiting = {FName(TEXT("item.test_no_icon"))};
	TestEqual(TEXT("주문서에 올라 있는 빈 칸은 문제 없음"),
		CollectIconIssues(NoIconRows, AssetExists, NoIconAwaiting, FilledCount, EmptyCount).Num(), 0);
	TestEqual(TEXT("빈 칸은 안 센다"), FilledCount, 0);
	TestEqual(TEXT("빈 칸 수를 셌다"), EmptyCount, 1);
	// M78 — 주문서 밖의 빈 칸은 문제 1건 (새 아이템에 아이콘을 잊은 경우).
	TestEqual(TEXT("목록 밖 빈 칸은 문제 1건"),
		CollectIconIssues(NoIconRows, AssetExists, NoneAwaiting, FilledCount, EmptyCount).Num(), 1);
	// M78 — 목록 안인데 아이콘이 이미 붙었으면 문제 1건 (시트가 들어왔는데 목록을 안 지운 경우).
	const TSet<FName> SoundAwaiting = {TestItemID};
	TestEqual(TEXT("아이콘이 붙은 행이 목록에 있으면 문제 1건"),
		CollectIconIssues(SoundRows, AssetExists, SoundAwaiting, FilledCount, EmptyCount).Num(), 1);

	// ② 아이콘은 있는데 칸이 빈 것 — 표를 다시 안 채운 경우다.
	TMap<FName, FItemRow> ForgottenRows;
	FItemRow ForgottenRow;
	ForgottenRow.ItemID = TestItemID;
	ForgottenRows.Add(FName(TEXT("Item_TestThing")), ForgottenRow);
	TestEqual(TEXT("에셋이 있는데 칸이 비면 문제 1건"),
		CollectIconIssues(ForgottenRows, AssetExists, NoneAwaiting, FilledCount, EmptyCount).Num(), 1);

	// ③ 짝이 어긋난 것 — 경로는 살아 있어서 무결성 검사는 못 잡는다.
	TMap<FName, FItemRow> MismatchRows;
	FItemRow MismatchRow = SoundRow;
	MismatchRow.IconRef = TSoftObjectPtr<UTexture2D>(
		FSoftObjectPath(IconObjectPathForItemID(FName(TEXT("item.other_thing")))));
	MismatchRows.Add(FName(TEXT("Item_TestThing")), MismatchRow);
	TestEqual(TEXT("ID 와 다른 아이콘을 가리키면 문제 1건"),
		CollectIconIssues(MismatchRows, AssetExists, NoneAwaiting, FilledCount, EmptyCount).Num(), 1);

	// ④ 이름은 맞는데 에셋이 없는 것 — 아이콘을 지웠거나 이름을 바꿨을 때.
	TMap<FName, FItemRow> MissingAssetRows;
	FItemRow MissingAssetRow;
	MissingAssetRow.ItemID = FName(TEXT("item.test_gone"));
	MissingAssetRow.IconRef = TSoftObjectPtr<UTexture2D>(
		FSoftObjectPath(IconObjectPathForItemID(MissingAssetRow.ItemID)));
	MissingAssetRows.Add(FName(TEXT("Item_TestGone")), MissingAssetRow);
	TestEqual(TEXT("없는 에셋을 가리키면 문제 1건"),
		CollectIconIssues(MissingAssetRows, AssetExists, NoneAwaiting, FilledCount, EmptyCount).Num(), 1);

	// ⑤ ID 가 빈 행.
	TMap<FName, FItemRow> NoIDRows;
	NoIDRows.Add(FName(TEXT("Item_TestNoID")), FItemRow());
	TestEqual(TEXT("ItemID 가 비면 문제 1건"),
		CollectIconIssues(NoIDRows, AssetExists, NoneAwaiting, FilledCount, EmptyCount).Num(), 1);

	// ⑥ 아이콘은 있는데 짝이 되는 행이 없는 것 — 행이 선 아이콘·행을 기다리는 도구 아이콘은 문제 없다.
	const TArray<FString> IconNames = {
		IconAssetNameForItemID(TestItemID), TEXT("T_Icon_TestOrphan"), TEXT("T_Icon_FishingRod")
	};
	TestEqual(TEXT("행 없는 아이콘은 문제 1건"), CollectOrphanIcons(SoundRows, IconNames).Num(), 1);
	TestEqual(TEXT("행이 다 있으면 문제 없음"),
		CollectOrphanIcons(SoundRows, {IconAssetNameForItemID(TestItemID)}).Num(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
