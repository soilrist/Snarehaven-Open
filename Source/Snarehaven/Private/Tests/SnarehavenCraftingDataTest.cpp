// DESIGN 8.10~8.14 2층 검사 — 제작·요리·상점·판매가·서식지 데이터가 설계서와 같은가.
// 아직 이 표들을 읽는 런타임 코드는 없다(제작 화면·상점·직접 낚시는 다음 조각) — 그래서 **표 자체**만 본다.
// 보는 것: ① 레시피의 재료·결과·설계도 ID 가 아이템 표에 있는가 + 8.10 표의 결과가 다 레시피로 섰는가
// ② 상점 품목·가격이 8.14 "사는 목록" 과 같은가 ③ 생선구이 회복량이 8.4 "레퍼런스 조리 회복량" 열과 같은가
// ④ 물고기 판매가가 8.14 "낚시 시간 ÷ 15" 규칙대로인가 ⑤ 어종 서식지가 8.13 과 같은가.
//
// **숫자는 설계서에서 읽는다** — 여기 적으면 사본이 셋이 되어 "한쪽만 고치기" 를 못 막는다 (낚시 표 대조와 같은 짜임).
// 이름공간을 따로 둔 이유: 다른 검사 파일의 익명 이름공간(`ItemTablePath` 등)과 유니티 빌드에서 부딪히지 않게.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/AnyOf.h"
#include "Data/FishingRow.h"
#include "Data/GatherableRow.h"
#include "Data/ItemRow.h"
#include "Data/RecipeRow.h"
#include "Data/ShopRow.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Inventory/ChestLootRules.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "World/WaterRules.h"

namespace SnarehavenCraftingDataTest
{
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* FishingTablePath = TEXT("/Game/Game/Data/Fishing/DT_FishingTable.DT_FishingTable");
	const TCHAR* RecipeTablePath = TEXT("/Game/Game/Data/Crafting/DT_RecipeTable.DT_RecipeTable");
	const TCHAR* ShopTablePath = TEXT("/Game/Game/Data/Shop/DT_ShopTable.DT_ShopTable");
	const TCHAR* GatherableTablePath = TEXT("/Game/Game/Data/Gatherables/DT_GatherableTable.DT_GatherableTable");

	/** `### 8.x …` 부터 다음 제목 전까지만 잘라 낸다. */
	FString ExtractSection(const FString& Doc, const FString& HeadingPrefix)
	{
		const int32 Start = Doc.Find(HeadingPrefix, ESearchCase::CaseSensitive);
		if (Start == INDEX_NONE)
		{
			return FString();
		}
		const int32 BodyStart = Start + HeadingPrefix.Len();
		const int32 NextHeading = Doc.Find(TEXT("\n#"), ESearchCase::CaseSensitive, ESearchDir::FromStart, BodyStart);
		return NextHeading == INDEX_NONE ? Doc.Mid(BodyStart) : Doc.Mid(BodyStart, NextHeading - BodyStart);
	}

	/** 정규식의 첫 매치에서 캡처 그룹 하나를 꺼낸다. 없으면 빈 문자열. */
	FString FirstCapture(const FString& Text, const TCHAR* Pattern, int32 Group = 1)
	{
		FRegexMatcher Matcher(FRegexPattern(Pattern), Text);
		return Matcher.FindNext() ? Matcher.GetCaptureGroup(Group) : FString();
	}

	bool LoadDesign(FAutomationTestBase& Test, FString& OutDoc)
	{
		const FString DocPath = FPaths::ProjectDir() / TEXT("docs/DESIGN.md");
		return Test.TestTrue(*FString::Printf(TEXT("설계서를 읽는다 (%s)"), *DocPath),
			FFileHelper::LoadFileToString(OutDoc, *DocPath));
	}

	template <typename RowType>
	TArray<RowType> LoadRows(FAutomationTestBase& Test, const TCHAR* Path)
	{
		TArray<RowType> Rows;
		const UDataTable* Table = LoadObject<UDataTable>(nullptr, Path);
		if (Test.TestNotNull(*FString::Printf(TEXT("표 로드 %s"), Path), Table))
		{
			for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
			{
				Rows.Add(*reinterpret_cast<const RowType*>(Pair.Value));
			}
		}
		return Rows;
	}

	FRecipeIngredient MakeIngredient(FName ItemID, ECraftTag Tag, int32 Count)
	{
		FRecipeIngredient Ingredient;
		Ingredient.ItemID = ItemID;
		Ingredient.Tag = Tag;
		Ingredient.Count = Count;
		return Ingredient;
	}

	TMap<FName, FItemRow> IndexItems(const TArray<FItemRow>& Items)
	{
		TMap<FName, FItemRow> ByID;
		for (const FItemRow& Item : Items)
		{
			ByID.Add(Item.ItemID, Item);
		}
		return ByID;
	}

	// 레시피 행의 참조를 본다. 바로 AddError 하지 않고 돌려주는 이유 — 아래 자기검사가 가짜 행으로 다시 볼 수 있게.
	TArray<FString> CollectRecipeIssues(const TArray<FRecipeRow>& Recipes, const TMap<FName, FItemRow>& Items)
	{
		TArray<FString> Issues;
		TSet<ECraftTag> UsedTags;
		for (const TPair<FName, FItemRow>& Pair : Items)
		{
			UsedTags.Add(Pair.Value.CraftTag);
		}

		for (const FRecipeRow& Recipe : Recipes)
		{
			const FString Name = Recipe.RecipeID.ToString();
			if (!Items.Contains(Recipe.ResultItemID))
			{
				Issues.Add(FString::Printf(TEXT("[%s] 결과 %s 가 아이템 표에 없다"), *Name, *Recipe.ResultItemID.ToString()));
			}
			if (Recipe.ResultCount <= 0)
			{
				Issues.Add(FString::Printf(TEXT("[%s] 결과 개수가 0 이하"), *Name));
			}
			if (!Recipe.RequiredBlueprintID.IsNone() && !Items.Contains(Recipe.RequiredBlueprintID))
			{
				Issues.Add(FString::Printf(TEXT("[%s] 설계도 %s 가 아이템 표에 없다"), *Name, *Recipe.RequiredBlueprintID.ToString()));
			}
			if (Recipe.Ingredients.Num() == 0)
			{
				Issues.Add(FString::Printf(TEXT("[%s] 재료가 없다"), *Name));
			}
			for (const FRecipeIngredient& Ingredient : Recipe.Ingredients)
			{
				const bool bHasID = !Ingredient.ItemID.IsNone();
				const bool bHasTag = Ingredient.Tag != ECraftTag::None;
				if (bHasID == bHasTag)
				{
					Issues.Add(FString::Printf(TEXT("[%s] 재료 칸은 ID 와 태그 중 하나만 채운다"), *Name));
				}
				else if (bHasID && !Items.Contains(Ingredient.ItemID))
				{
					Issues.Add(FString::Printf(TEXT("[%s] 재료 %s 가 아이템 표에 없다"), *Name, *Ingredient.ItemID.ToString()));
				}
				else if (bHasTag && !UsedTags.Contains(Ingredient.Tag))
				{
					Issues.Add(FString::Printf(TEXT("[%s] 재료 태그 %s 를 가진 아이템이 없다"),
						*Name, *UEnum::GetValueAsString(Ingredient.Tag)));
				}
				if (Ingredient.Count <= 0)
				{
					Issues.Add(FString::Printf(TEXT("[%s] 재료 개수가 0 이하"), *Name));
				}
			}
		}
		return Issues;
	}

	/** DESIGN 13.6 재료 표 한 칸. 한 줄에 여럿(소금 · 흑후추 · 딜)이면 " · " 로 나눈 하나씩. */
	struct FIngredient136
	{
		FString Name;
		FName ItemID;
		FString Source;
		FString Raw;
	};

	TArray<FIngredient136> ReadIngredients136(const FString& Doc)
	{
		TArray<FIngredient136> Out;
		TArray<FString> Lines;
		ExtractSection(Doc, TEXT("### 13.6")).ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			TArray<FString> Cells;
			Line.ParseIntoArray(Cells, TEXT("|"), false);
			if (Cells.Num() != 8 || !Cells[3].TrimStartAndEnd().StartsWith(TEXT("`item.")))
			{
				continue; // 6칸 재료 표만 — 버프 표는 7칸이다
			}
			TArray<FString> Names;
			TArray<FString> Raws;
			Cells[1].TrimStartAndEnd().Replace(TEXT("(있음)"), TEXT("")).ParseIntoArray(Names, TEXT(" · "));
			Cells[5].TrimStartAndEnd().ParseIntoArray(Raws, TEXT(" · "));
			FRegexMatcher IdMatcher(FRegexPattern(TEXT("`(item\\.[a-z_]+)`")), Cells[3]);
			for (int32 Index = 0; IdMatcher.FindNext(); ++Index)
			{
				Out.Add({ Names.IsValidIndex(Index) ? Names[Index] : FString(), FName(*IdMatcher.GetCaptureGroup(1)),
					Cells[4].TrimStartAndEnd(), Raws.IsValidIndex(Index) ? Raws[Index] : FString() });
			}
		}
		return Out;
	}

	/** DESIGN 13.5 모자 표 한 줄. ShopPrice 는 「잡화점 **N**」 이면 N, 상자 등이면 0. */
	struct FHat135
	{
		FString Name;
		FName ItemID;
		FString Effect;
		int32 ShopPrice = 0;
	};

	TArray<FHat135> ReadHats135(const FString& Doc)
	{
		TArray<FHat135> Out;
		TArray<FString> Lines;
		ExtractSection(Doc, TEXT("### 13.5")).ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			TArray<FString> Cells;
			Line.ParseIntoArray(Cells, TEXT("|"), false);
			const FString Id = Cells.Num() == 8 ? FirstCapture(Cells[3], TEXT("^\\s*`(item\\.[a-z_]+_hat)`\\s*$")) : FString();
			if (!Id.IsEmpty())
			{
				Out.Add({ Cells[1].TrimStartAndEnd().Replace(TEXT("(바꿈)"), TEXT("")), FName(*Id), Cells[4].TrimStartAndEnd(),
					FCString::Atoi(*FirstCapture(Cells[5], TEXT("^\\s*잡화점 \\*\\*([0-9]+)\\*\\*\\s*$"))) });
			}
		}
		return Out;
	}

	/** DESIGN 13.5 도구 표 한 줄(모자가 아닌 `item.*` 행, 값 칸 `**N**`). */
	TArray<FHat135> ReadTools135(const FString& Doc)
	{
		TArray<FHat135> Out;
		TArray<FString> Lines;
		ExtractSection(Doc, TEXT("### 13.5")).ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			TArray<FString> Cells;
			Line.ParseIntoArray(Cells, TEXT("|"), false);
			const FString Id = Cells.Num() == 8 ? FirstCapture(Cells[3], TEXT("^\\s*`(item\\.[a-z_]+)`\\s*$")) : FString();
			const FString Price = Cells.Num() == 8 ? FirstCapture(Cells[5], TEXT("^\\s*\\*\\*([0-9]+)\\*\\*\\s*$")) : FString();
			if (!Id.IsEmpty() && !Id.EndsWith(TEXT("_hat")) && !Price.IsEmpty())
			{
				Out.Add({ Cells[1].TrimStartAndEnd(), FName(*Id), Cells[4].TrimStartAndEnd(), FCString::Atoi(*Price) });
			}
		}
		return Out;
	}

	/** 날로 먹기 칸 → 체력. "+10 (원본)" 10 · "**−40** (…)" −40 · "—" 0(못 먹는다). */
	int32 RawHeal(const FString& Cell)
	{
		FRegexMatcher Matcher(FRegexPattern(TEXT("^(?:\\*\\*)?([+−-])([0-9]+)")), Cell.TrimStartAndEnd());
		return Matcher.FindNext() ? (Matcher.GetCaptureGroup(1) == TEXT("+") ? 1 : -1) * FCString::Atoi(*Matcher.GetCaptureGroup(2)) : 0;
	}

	/**
	 * DESIGN 8.11 「집 2층·전력」 표 한 줄(항목 칸 = RowLabel)의 값 칸에서 "이름 N" 들을 ID → N 으로 읽는다(M20-02).
	 * 값 칸을 CellPattern 의 캡처 1 로 좁힌 뒤 " · " 로 나누고, 숫자로 끝나지 않는 조각(「회로판 = 재료 합 + 1」)은 건너뛴다.
	 * 항목 칸 앞 「🟡 」 는 있어도 없어도 같은 줄이다 — 감독이 확인을 닫으며 떼어 낸다(그때 읽기 0 이 된 일, #488~#512).
	 */
	TMap<FName, int32> ReadPower811(const FString& Doc, const TCHAR* RowLabel, const TCHAR* CellPattern)
	{
		static const TMap<FString, FName> Ids = {
			{TEXT("키트"), FName(TEXT("item.solar_kit"))}, {TEXT("햇빛 집열판"), FName(TEXT("item.solar_kit"))},
			{TEXT("배터리"), FName(TEXT("item.battery"))}, {TEXT("전구"), FName(TEXT("item.light_bulb"))},
			{TEXT("폴리머"), FName(TEXT("item.polymer"))}, {TEXT("전등"), FName(TEXT("item.lamp_electric"))}};
		TMap<FName, int32> Out;
		TArray<FString> Lines;
		ExtractSection(Doc, TEXT("### 8.11")).ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			if (!Line.StartsWith(FString::Printf(TEXT("| %s |"), RowLabel))
				&& !Line.StartsWith(FString::Printf(TEXT("| 🟡 %s |"), RowLabel)))
			{
				continue;
			}
			TArray<FString> Parts;
			FirstCapture(Line, CellPattern).ParseIntoArray(Parts, TEXT(" · "));
			for (const FString& Part : Parts)
			{
				FRegexMatcher M(FRegexPattern(TEXT("^(.+?) ([0-9]+)$")), Part.TrimStartAndEnd());
				if (M.FindNext() && Ids.Contains(M.GetCaptureGroup(1)))
				{
					Out.Add(Ids[M.GetCaptureGroup(1)], FCString::Atoi(*M.GetCaptureGroup(2)));
				}
			}
		}
		return Out;
	}
}

using namespace SnarehavenCraftingDataTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRecipeTableTest, "Snarehaven.Crafting.Recipe.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRecipeTableTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!LoadDesign(*this, Doc))
	{
		return false;
	}
	const TMap<FName, FItemRow> Items = IndexItems(LoadRows<FItemRow>(*this, ItemTablePath));
	const TArray<FRecipeRow> Recipes = LoadRows<FRecipeRow>(*this, RecipeTablePath);
	if (!TestTrue(TEXT("레시피 행이 있다"), Recipes.Num() > 0))
	{
		return false;
	}

	for (const FString& Issue : CollectRecipeIssues(Recipes, Items))
	{
		AddError(Issue);
	}

	// 8.10 레시피 표의 결과 ID 가 전부 레시피로 섰는가. `item.seed_<작물>` 같은 틀 줄은 아래 생선구이 검사가 따로 본다.
	TSet<FName> Results;
	for (const FRecipeRow& Recipe : Recipes)
	{
		Results.Add(Recipe.ResultItemID);
	}
	TArray<FString> Lines;
	ExtractSection(Doc, TEXT("### 8.10")).ParseIntoArrayLines(Lines);
	const FRegexPattern RowPattern(TEXT("^\\|[^|]+\\|\\s*`(item\\.[a-z0-9_]+)`\\s*\\|"));
	int32 DesignCount = 0;
	for (const FString& Line : Lines)
	{
		FRegexMatcher Matcher(RowPattern, Line);
		if (Matcher.FindNext())
		{
			++DesignCount;
			const FString ResultID = Matcher.GetCaptureGroup(1);
			TestTrue(*FString::Printf(TEXT("DESIGN 8.10 의 %s 레시피가 표에 있다"), *ResultID),
				Results.Contains(FName(*ResultID)));
		}
	}
	TestTrue(TEXT("DESIGN 8.10 에서 레시피 줄을 읽었다"), DesignCount > 0);
	AddInfo(FString::Printf(TEXT("레시피 %d행 · 설계서 고정 ID 줄 %d개"), Recipes.Num(), DesignCount));
	return true;
}

// 위 검사가 "표가 멀쩡해서" 통과할 때도 살아 있는지 — 가짜 행으로 본다 (역검증).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRecipeCheckSelfTest, "Snarehaven.Crafting.Recipe.CheckCatchesBrokenRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRecipeCheckSelfTest::RunTest(const FString& Parameters)
{
	FItemRow Wood;
	Wood.ItemID = TEXT("item.test_wood");
	Wood.CraftTag = ECraftTag::Wood;
	TMap<FName, FItemRow> Items;
	Items.Add(Wood.ItemID, Wood);

	FRecipeRow Good;
	Good.RecipeID = TEXT("recipe.test_good");
	Good.ResultItemID = Wood.ItemID;
	Good.Ingredients.Add(MakeIngredient(Wood.ItemID, ECraftTag::None, 2));
	TestEqual(TEXT("멀쩡한 행은 문제 0건"), CollectRecipeIssues({ Good }, Items).Num(), 0);

	FRecipeRow MissingItem = Good;
	MissingItem.Ingredients[0].ItemID = TEXT("item.test_missing");
	TestEqual(TEXT("없는 재료를 잡는다"), CollectRecipeIssues({ MissingItem }, Items).Num(), 1);

	FRecipeRow BothFilled = Good;
	BothFilled.Ingredients[0].Tag = ECraftTag::Wood;
	TestEqual(TEXT("ID·태그를 둘 다 채운 칸을 잡는다"), CollectRecipeIssues({ BothFilled }, Items).Num(), 1);

	FRecipeRow UnknownTag = Good;
	UnknownTag.Ingredients[0] = MakeIngredient(NAME_None, ECraftTag::Fish, 1);
	TestEqual(TEXT("아무 아이템도 안 가진 태그를 잡는다"), CollectRecipeIssues({ UnknownTag }, Items).Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShopTableTest, "Snarehaven.Shop.Table.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShopTableTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!LoadDesign(*this, Doc))
	{
		return false;
	}
	const TMap<FName, FItemRow> Items = IndexItems(LoadRows<FItemRow>(*this, ItemTablePath));
	const TArray<FShopRow> Shop = LoadRows<FShopRow>(*this, ShopTablePath);

	// 8.14 "| 사는 목록 | 기본 낚싯대 3 · 텔레스코픽 릴 10 · … |" — 이름과 가격을 짝지어 읽는다.
	const FString ListCell = FirstCapture(ExtractSection(Doc, TEXT("### 8.14")), TEXT("\\|\\s*사는 목록\\s*\\|([^|]+)\\|"));
	TArray<FString> Entries;
	ListCell.ParseIntoArray(Entries, TEXT("·"));
	if (!TestTrue(TEXT("DESIGN 8.14 사는 목록을 읽었다"), Entries.Num() > 0))
	{
		return false;
	}

	// 13.3 「씨앗 파는 곳」 — 새 작물 씨앗 N종, 값 = 그 작물 재배 분 단의 값("15분 1 · 30분 2 · …").
	// 씨앗 ID 와 재배 분은 13.3 작물 표의 씨앗·재배 열에서 읽는다.
	const FString Section133 = ExtractSection(Doc, TEXT("### 13.3"));
	const FString SeedCell = FirstCapture(Section133, TEXT("\\|\\s*씨앗 파는 곳\\s*\\|([^|]+)\\|"));
	const int32 SeedKinds = FCString::Atoi(*FirstCapture(SeedCell, TEXT("([0-9]+)종")));
	TMap<FName, int32> SeedPrices;
	TArray<FString> Lines133;
	Section133.ParseIntoArrayLines(Lines133);
	const FRegexPattern SeedRowPattern(TEXT("\\|\\s*`(item\\.seed_[a-z0-9_]+)`\\s*\\|\\s*([0-9]+)분"));
	for (const FString& Line : Lines133)
	{
		FRegexMatcher Matcher(SeedRowPattern, Line);
		if (Matcher.FindNext())
		{
			const FString PricePattern = Matcher.GetCaptureGroup(2) + TEXT("분 ([0-9]+)");
			SeedPrices.Add(FName(*Matcher.GetCaptureGroup(1)), FCString::Atoi(*FirstCapture(SeedCell, *PricePattern)));
		}
	}
	TestTrue(TEXT("DESIGN 13.3 씨앗 줄 수 = 「씨앗 파는 곳」 종 수"), SeedKinds > 0 && SeedPrices.Num() == SeedKinds);
	// 13.6 잡화점 재료(소금·흑후추·딜·빈 병) — 가격은 아래 CookingIngredients 검사가 본다.
	int32 Shop136 = 0;
	for (const FIngredient136& Row : ReadIngredients136(Doc))
	{
		Shop136 += Row.Source.Contains(TEXT("잡화점")) ? 1 : 0;
	}
	// 13.6 버프 소모품 표(7칸)의 잡화점 줄(핫팩·탄산·에너지바) — 가격은 Snarehaven.Buff.Table 검사가 본다.
	TArray<FString> BuffLines;
	ExtractSection(Doc, TEXT("### 13.6")).ParseIntoArrayLines(BuffLines);
	for (const FString& Line : BuffLines)
	{
		TArray<FString> Cells;
		Line.ParseIntoArray(Cells, TEXT("|"), false);
		Shop136 += Cells.Num() == 9 && Cells[3].Contains(TEXT("`item.")) && Cells[4].Contains(TEXT("잡화점")) ? 1 : 0;
	}
	// 13.5 잡화점 모자 — 가격은 아래 Hats 검사가 본다.
	int32 Shop135 = 0;
	for (const FHat135& Hat : ReadHats135(Doc))
	{
		Shop135 += Hat.ShopPrice > 0 ? 1 : 0;
	}
	Shop135 += ReadTools135(Doc).Num();   // 13.5 도구 — 전부 잡화점, 가격은 아래 Tools 검사가 본다
	// 8.16 곡괭이 — 「곡괭이 얻기」 줄의 "기본 = 잡화점 **N 코인**" · "전기 = 잡화점 **N 코인**".
	TMap<FName, int32> Pickaxes816;
	TArray<FString> MiningLines;
	ExtractSection(Doc, TEXT("### 8.16")).ParseIntoArrayLines(MiningLines);
	for (const FString& Line : MiningLines)
	{
		if (Line.StartsWith(TEXT("| 곡괭이 얻기 |")))
		{
			const FRegexPattern PickPattern(TEXT("(기본|전기) = 잡화점 \\*\\*([0-9]+) 코인"));
			for (FRegexMatcher M(PickPattern, Line); M.FindNext();)
			{
				Pickaxes816.Add(FName(M.GetCaptureGroup(1) == TEXT("기본") ? TEXT("item.pickaxe_basic") : TEXT("item.pickaxe_electric")),
					FCString::Atoi(*M.GetCaptureGroup(2)));
			}
		}
	}
	TestEqual(TEXT("8.16 잡화점 곡괭이 2"), Pickaxes816.Num(), 2);
	for (const TPair<FName, int32>& Pick : Pickaxes816)
	{
		const FShopRow* Row = Shop.FindByPredicate([&](const FShopRow& ShopRow) { return ShopRow.ItemID == Pick.Key; });
		if (TestNotNull(*FString::Printf(TEXT("곡괭이 %s 가 상점 표에 있다"), *Pick.Key.ToString()), Row))
		{
			TestEqual(*FString::Printf(TEXT("곡괭이 %s 가격이 8.16 과 같다"), *Pick.Key.ToString()), Row->Price, Pick.Value);
		}
	}
	// 13.8 페인트 — 「아이템」 줄의 ID 전부가 잡화점, 값은 「값」 줄 "잡화점 각 **N**".
	TArray<FName> Paints138;
	int32 PaintPrice = -1;
	TArray<FName> Outfits138;
	int32 OutfitPrice = -1;
	TArray<FString> Lines138;
	ExtractSection(Doc, TEXT("### 13.8")).ParseIntoArrayLines(Lines138);
	for (const FString& Line : Lines138)
	{
		if (Line.StartsWith(TEXT("| 아이템 |")) && Line.Contains(TEXT("item.paint_")))
		{
			const FRegexPattern PaintPattern(TEXT("`(item\\.paint_[a-z]+)`"));
			for (FRegexMatcher M(PaintPattern, Line); M.FindNext();) { Paints138.Add(FName(*M.GetCaptureGroup(1))); }
		}
		if (Line.StartsWith(TEXT("| 값 |")) && Line.Contains(TEXT("잡화점 각")))
		{
			PaintPrice = FCString::Atoi(*FirstCapture(Line, TEXT("잡화점 각 \\*\\*([0-9]+)\\*\\*"), 1));
		}
		// 13.8 「셔츠·바지」 — 「🟡 아이템」 줄의 옷 ID 전부가 잡화점, 값은 「🟡 얻는 곳」 줄 "잡화점 **각 N 코인**".
		if (Line.StartsWith(TEXT("| 🟡 아이템 |")) && Line.Contains(TEXT("OutfitBottom")))
		{
			const FRegexPattern OutfitPattern(TEXT("`(item\\.[a-z_]+)`"));
			for (FRegexMatcher M(OutfitPattern, Line); M.FindNext();) { Outfits138.Add(FName(*M.GetCaptureGroup(1))); }
		}
		if (Line.StartsWith(TEXT("| 🟡 얻는 곳 |")) && Line.Contains(TEXT("잡화점 **각")))
		{
			OutfitPrice = FCString::Atoi(*FirstCapture(Line, TEXT("잡화점 \\*\\*각 ([0-9]+) 코인"), 1));
		}
	}
	TestEqual(TEXT("13.8 셔츠·바지 2"), Outfits138.Num(), 2);
	for (const FName& Outfit : Outfits138)
	{
		const FShopRow* Row = Shop.FindByPredicate([&](const FShopRow& ShopRow) { return ShopRow.ItemID == Outfit; });
		if (TestNotNull(*FString::Printf(TEXT("옷 %s 가 상점 표에 있다"), *Outfit.ToString()), Row))
		{
			TestEqual(*FString::Printf(TEXT("옷 %s 가격이 13.8 과 같다"), *Outfit.ToString()), Row->Price, OutfitPrice);
		}
	}
	TestEqual(TEXT("13.8 페인트 7"), Paints138.Num(), 7);
	for (const FName& Paint : Paints138)
	{
		const FShopRow* Row = Shop.FindByPredicate([&](const FShopRow& ShopRow) { return ShopRow.ItemID == Paint; });
		if (TestNotNull(*FString::Printf(TEXT("페인트 %s 가 상점 표에 있다"), *Paint.ToString()), Row))
		{
			TestEqual(*FString::Printf(TEXT("페인트 %s 가격이 13.8 과 같다"), *Paint.ToString()), Row->Price, PaintPrice);
		}
	}
	// 8.11 「집 2층·전력」 — 「태양광 키트·배터리·전구 얻기」 줄 "잡화점 **키트 30 · 배터리 15 · 전구 5** 코인" (M20-02).
	const TMap<FName, int32> Shop811 = ReadPower811(Doc, TEXT("햇빛 집열판·배터리·전구 얻기"), TEXT("잡화점 \\*\\*(.+?)\\*\\* 코인"));
	TestEqual(TEXT("8.11 잡화점 전자 재료 3"), Shop811.Num(), 3);
	for (const TPair<FName, int32>& Electric : Shop811)
	{
		const FShopRow* Row = Shop.FindByPredicate([&](const FShopRow& ShopRow) { return ShopRow.ItemID == Electric.Key; });
		if (TestNotNull(*FString::Printf(TEXT("%s 가 상점 표에 있다"), *Electric.Key.ToString()), Row))
		{
			TestEqual(*FString::Printf(TEXT("%s 가격이 8.11 과 같다"), *Electric.Key.ToString()), Row->Price, Electric.Value);
		}
	}
	// 10.8 「열쇠 코인 값」 — 잡화점 상자 열쇠 한 줄 (상점 개편 B).
	const int32 KeyPrice108 = FCString::Atoi(*FirstCapture(Doc, TEXT("\\| 열쇠 코인 값 \\| \\*\\*([0-9]+)\\*\\* 코인")));
	TestTrue(TEXT("10.8 열쇠 코인 값을 읽었다"), KeyPrice108 > 0);
	const FShopRow* const KeyRow = Shop.FindByPredicate([](const FShopRow& ShopRow) { return ShopRow.ItemID == FName(TEXT("item.crate_key")); });
	if (TestNotNull(TEXT("상자 열쇠가 상점 표에 있다"), KeyRow))
	{
		TestEqual(TEXT("상자 열쇠 가격이 10.8 과 같다"), KeyRow->Price, KeyPrice108);
	}
	// 8.6-c 사냥 총 — 「권총 잡화점 값」·「권총탄·탄 … 묶음」 줄 (M39-01). 값·발 수는 Snarehaven.Combat.Gun.Rows 가 본다.
	int32 Shop86c = 0;
	TArray<FString> GunLines;
	ExtractSection(Doc, TEXT("### 8.6-c")).ParseIntoArrayLines(GunLines);
	for (const FString& Line : GunLines)
	{
		Shop86c += (Line.StartsWith(TEXT("| 권총 잡화점 값 |")) || Line.StartsWith(TEXT("| 산탄총 잡화점 값 |")) || Line.StartsWith(TEXT("| 석궁 잡화점 값 |")) || Line.StartsWith(TEXT("| 연발 권총 잡화점 값 |")) || Line.StartsWith(TEXT("| 기관단총 잡화점 값 |")) || Line.StartsWith(TEXT("| 점사 소총 잡화점 값 |")) || Line.StartsWith(TEXT("| 반자동 소총 잡화점 값 |")) || Line.StartsWith(TEXT("| 연사 산탄총 잡화점 값 |")) || Line.StartsWith(TEXT("| 짧은 산탄총 잡화점 값 |")) || Line.StartsWith(TEXT("| 기관총 잡화점 값 |")) || Line.StartsWith(TEXT("| 바주카 `item.bazooka`")) || Line.StartsWith(TEXT("| 바주카 탄 `item.rocket`")) || ((Line.StartsWith(TEXT("| 권총탄 ")) || Line.StartsWith(TEXT("| 탄 `item."))) && Line.Contains(TEXT("묶음 |")))) ? 1 : 0;
	}
	TestEqual(TEXT("8.6-c 잡화점 총·탄약 16(바주카·로켓 2줄 포함)"), Shop86c, 16);
	TestEqual(TEXT("상점 행 수 = 8.14 품목 수 + 13.3 씨앗 수 + 13.6 잡화점 재료 수 + 13.5 잡화점 모자·도구 수 + 13.8 페인트·옷 수 + 8.16 곡괭이 수 + 8.11 전자 재료 수 + 10.8 열쇠 + 8.6-c 총·탄약"), Shop.Num(),
		Entries.Num() + SeedPrices.Num() + Shop136 + Shop135 + Paints138.Num() + Outfits138.Num() + Pickaxes816.Num() + Shop811.Num() + 1 + Shop86c);
	for (const TPair<FName, int32>& Seed : SeedPrices)
	{
		const FShopRow* Row = Shop.FindByPredicate([&](const FShopRow& ShopRow) { return ShopRow.ItemID == Seed.Key; });
		if (TestNotNull(*FString::Printf(TEXT("씨앗 %s 가 상점 표에 있다"), *Seed.Key.ToString()), Row))
		{
			TestEqual(*FString::Printf(TEXT("씨앗 %s 가격이 13.3 과 같다"), *Seed.Key.ToString()), Row->Price, Seed.Value);
		}
	}

	for (const FString& Entry : Entries)
	{
		const FString Name = FirstCapture(Entry.TrimStartAndEnd(), TEXT("^(.+?)\\s+([0-9]+)$"), 1);
		const int32 Price = FCString::Atoi(*FirstCapture(Entry.TrimStartAndEnd(), TEXT("^(.+?)\\s+([0-9]+)$"), 2));
		const FShopRow* Row = Shop.FindByPredicate([&](const FShopRow& ShopRow)
		{
			const FItemRow* Item = Items.Find(ShopRow.ItemID);
			return Item != nullptr && Item->DisplayName.ToString() == Name;
		});
		if (TestNotNull(*FString::Printf(TEXT("'%s' 가 상점 표에 있다"), *Name), Row))
		{
			TestEqual(*FString::Printf(TEXT("'%s' 가격이 설계서와 같다"), *Name), Row->Price, Price);
		}
	}
	for (const FShopRow& Row : Shop)
	{
		TestTrue(*FString::Printf(TEXT("상점 품목 %s 가 아이템 표에 있다"), *Row.ItemID.ToString()), Items.Contains(Row.ItemID));
		TestTrue(*FString::Printf(TEXT("상점 품목 %s 가격·개수가 1 이상"), *Row.ItemID.ToString()), Row.Price > 0 && Row.Count > 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGrilledFishTest, "Snarehaven.Crafting.GrilledFish.HealMatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGrilledFishTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!LoadDesign(*this, Doc))
	{
		return false;
	}
	const TMap<FName, FItemRow> Items = IndexItems(LoadRows<FItemRow>(*this, ItemTablePath));
	const TArray<FFishingRow> Fish = LoadRows<FFishingRow>(*this, FishingTablePath);
	const TArray<FRecipeRow> Recipes = LoadRows<FRecipeRow>(*this, RecipeTablePath);

	// 8.10 "열이 `—` 인 붕어·잉어는 10" — 빈 칸 어종의 회복량도 설계서에서 읽는다.
	const int32 DashHeal = FCString::Atoi(*FirstCapture(ExtractSection(Doc, TEXT("### 8.10")), TEXT("붕어·잉어는 ([0-9]+)")));
	TestTrue(TEXT("DESIGN 8.10 에서 빈 칸 어종 회복량을 읽었다"), DashHeal > 0);

	// 8.4 어종 줄의 마지막 칸 = 레퍼런스 조리 회복량. 숫자로 시작하면 그 값, `—` 면 위 값, 그 밖(우파루파)은 13.7 요리 표가 본다.
	TArray<FString> Lines;
	ExtractSection(Doc, TEXT("### 8.4")).ParseIntoArrayLines(Lines);
	const FRegexPattern RowPattern(TEXT("^\\|[^|]+\\|\\s*`item\\.([a-z0-9_]+)`\\s*\\|.*\\|\\s*([^|]*?)\\s*\\|\\s*$"));
	int32 Checked = 0;
	for (const FString& Line : Lines)
	{
		FRegexMatcher Matcher(RowPattern, Line);
		if (!Matcher.FindNext())
		{
			continue;
		}
		const FString FishID = Matcher.GetCaptureGroup(1);
		const FString HealCell = Matcher.GetCaptureGroup(2);
		const bool bNumeric = HealCell.Len() > 0 && FChar::IsDigit(HealCell[0]);
		const int32 ExpectedHeal = bNumeric ? FCString::Atoi(*HealCell) : (HealCell.StartsWith(TEXT("—")) ? DashHeal : 0);

		TestTrue(*FString::Printf(TEXT("8.4 어종 %s 가 낚시 표에 있다"), *FishID),
			Fish.ContainsByPredicate([&](const FFishingRow& Row) { return Row.ProducedItemID == FName(TEXT("item.") + FishID); }));

		const FName GrilledID(TEXT("item.grilled_") + FishID);
		const FItemRow* Grilled = Items.Find(GrilledID);
		if (ExpectedHeal == 0)
		{
			continue; // Snarehaven.Crafting.Cooking.Table.MatchesDesign 이 13.7 값으로 본다
		}
		++Checked;
		if (TestNotNull(*FString::Printf(TEXT("%s 행이 있다"), *GrilledID.ToString()), Grilled))
		{
			TestTrue(*FString::Printf(TEXT("%s 는 음식"), *GrilledID.ToString()), Grilled->ItemType == EItemType::Food);
			TestEqual(*FString::Printf(TEXT("%s 회복량 = 8.4 열"), *GrilledID.ToString()), Grilled->HealAmount, ExpectedHeal);
		}
		TestTrue(*FString::Printf(TEXT("%s 를 불에서 굽는 레시피가 있다"), *GrilledID.ToString()),
			Recipes.ContainsByPredicate([&](const FRecipeRow& Recipe)
			{
				return Recipe.ResultItemID == GrilledID && Recipe.Station == ERecipeStation::Fire
					&& Recipe.Ingredients.Num() == 1 && Recipe.Ingredients[0].ItemID == FName(TEXT("item.") + FishID);
			}));
	}
	TestTrue(TEXT("생선구이를 하나 이상 보았다"), Checked > 0);
	AddInfo(FString::Printf(TEXT("생선구이 %d종 검사"), Checked));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFishSellPriceTest, "Snarehaven.Shop.SellPrice.FishRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFishSellPriceTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!LoadDesign(*this, Doc))
	{
		return false;
	}
	const TMap<FName, FItemRow> Items = IndexItems(LoadRows<FItemRow>(*this, ItemTablePath));
	const TArray<FFishingRow> Fish = LoadRows<FFishingRow>(*this, FishingTablePath);

	// 8.14 "물고기 = **낚시 시간(분) ÷ 15**" — 나누는 값을 읽는다. 나머지는 반올림(🟡, 표 스크립트와 같은 규칙).
	const int32 Divisor = FCString::Atoi(*FirstCapture(ExtractSection(Doc, TEXT("### 8.14")), TEXT("낚시 시간\\(분\\) ÷ ([0-9]+)")));
	if (!TestTrue(TEXT("DESIGN 8.14 에서 나누는 값을 읽었다"), Divisor > 0))
	{
		return false;
	}
	for (const FFishingRow& Row : Fish)
	{
		const FItemRow* Item = Items.Find(Row.ProducedItemID);
		if (TestNotNull(*FString::Printf(TEXT("%s 아이템 행"), *Row.ProducedItemID.ToString()), Item))
		{
			TestEqual(*FString::Printf(TEXT("%s 판매가 = %d분 ÷ %d"), *Row.ProducedItemID.ToString(), Row.CycleMinutes, Divisor),
				Item->SellPrice, (Row.CycleMinutes + Divisor / 2) / Divisor);
			TestTrue(*FString::Printf(TEXT("%s 재료 태그 Fish"), *Row.ProducedItemID.ToString()), Item->CraftTag == ECraftTag::Fish);
		}
	}
	TestTrue(TEXT("어종 행이 있다"), Fish.Num() > 0);
	return true;
}

// 8.14 "가공품 = 재료 값 합 +1, 합이 0 이면 0(못 판다)" — 나무·산딸기 같은 공짜 채집물을 가공해 코인을 찍어 내지 못하게.
// 기대값은 표 안에서 계산한다(재료의 SellPrice, 태그 재료는 그 태그 최저가 — 표 스크립트와 같은 규칙).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCraftedSellPriceTest, "Snarehaven.Shop.SellPrice.CraftedRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCraftedSellPriceTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!LoadDesign(*this, Doc))
	{
		return false;
	}
	const TMap<FName, FItemRow> Items = IndexItems(LoadRows<FItemRow>(*this, ItemTablePath));
	const TArray<FRecipeRow> Recipes = LoadRows<FRecipeRow>(*this, RecipeTablePath);
	// 8.11 「판매가」 줄의 정한 값(전구·배터리·키트·폴리머·전등)이 상점·재료 합 규칙보다 먼저 — 서버 `FIXED_SELL_PRICE` (M20-02·04a).
	const TMap<FName, int32> Fixed811 = ReadPower811(Doc, TEXT("판매가"), TEXT("^\\| (?:🟡 )?판매가 \\| ([^|]+)\\|"));
	TestEqual(TEXT("8.11 정한 판매가 5"), Fixed811.Num(), 5);
	// 상점 물건은 레시피가 있어도 0 — 사서 바로 팔기를 막는다(서버 `sellPriceOf` 첫 줄, 13.3 씨앗).
	TSet<FName> ShopIDs;
	for (const FShopRow& Row : LoadRows<FShopRow>(*this, ShopTablePath))
	{
		ShopIDs.Add(Row.ItemID);
	}

	TMap<ECraftTag, int32> CheapestByTag;
	for (const TPair<FName, FItemRow>& Pair : Items)
	{
		int32& Cheapest = CheapestByTag.FindOrAdd(Pair.Value.CraftTag, MAX_int32);
		Cheapest = FMath::Min(Cheapest, Pair.Value.SellPrice);
	}

	int32 FreeCount = 0;
	for (const FRecipeRow& Recipe : Recipes)
	{
		int32 Total = 0;
		for (const FRecipeIngredient& Ingredient : Recipe.Ingredients)
		{
			const FItemRow* Item = Items.Find(Ingredient.ItemID);
			const int32* TagPrice = CheapestByTag.Find(Ingredient.Tag);
			const int32 Unit = Ingredient.Tag != ECraftTag::None ? (TagPrice ? *TagPrice : 0) : (Item ? Item->SellPrice : 0);
			Total += Unit * Ingredient.Count;
		}
		const FItemRow* Result = Items.Find(Recipe.ResultItemID);
		if (Result == nullptr)
		{
			continue; // 없는 결과는 Recipe.MatchesDesign 이 잡는다
		}
		FreeCount += Total == 0 ? 1 : 0;
		// 여러 개 나오는 레시피(금속 부품 ×2, DESIGN 8.16)는 한 개 값 = (합 + 1) ÷ 개수 내림, 최소 1 — 서버 sellPriceOf 와 같은 식.
		const int32 Want = Fixed811.Contains(Recipe.ResultItemID) ? Fixed811[Recipe.ResultItemID]
			: Total > 0 && !ShopIDs.Contains(Recipe.ResultItemID) ? FMath::Max(1, (Total + 1) / FMath::Max(1, Recipe.ResultCount)) : 0;
		TestEqual(*FString::Printf(TEXT("%s 판매가 (재료 합 %d ÷ %d개)"), *Recipe.ResultItemID.ToString(), Total, Recipe.ResultCount),
			Result->SellPrice, Want);
	}
	// 공짜 재료 가공품이 실제로 표에 있어야 "0 이면 0" 갈래를 봤다고 할 수 있다.
	for (const TCHAR* FreeID : { TEXT("item.plank"), TEXT("item.campfire"), TEXT("item.berry_jam") })
	{
		const FItemRow* Item = Items.Find(FName(FreeID));
		if (TestNotNull(*FString::Printf(TEXT("%s 행"), FreeID), Item))
		{
			TestEqual(*FString::Printf(TEXT("공짜 재료 가공품 %s 는 못 판다"), FreeID), Item->SellPrice, 0);
		}
	}
	AddInfo(FString::Printf(TEXT("가공품 %d종 검사 · 재료 합 0 인 것 %d종"), Recipes.Num(), FreeCount));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFishHabitatTest, "Snarehaven.Fishing.Habitat.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFishHabitatTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!LoadDesign(*this, Doc))
	{
		return false;
	}
	const TMap<FName, FItemRow> Items = IndexItems(LoadRows<FItemRow>(*this, ItemTablePath));
	const TArray<FFishingRow> Fish = LoadRows<FFishingRow>(*this, FishingTablePath);

	// 8.13 "**민물 12**(붕어·잉어·…)" — 이름 목록을 읽어 아이템 표의 DisplayName 과 맞댄다.
	const FString Section = ExtractSection(Doc, TEXT("### 8.13"));
	TArray<FString> FreshNames;
	FirstCapture(Section, TEXT("\\*\\*민물 [0-9]+\\*\\*\\(([^)]*)\\)")).ParseIntoArray(FreshNames, TEXT("·"));
	TArray<FString> SwampNames;
	FirstCapture(Section, TEXT("\\*\\*늪 [0-9]+\\*\\*\\(([^)]*)\\)")).ParseIntoArray(SwampNames, TEXT("·"));
	const int32 DeepMinutes = FCString::Atoi(*FirstCapture(Section, TEXT("낚시 시간 \\*\\*([0-9]+)분 이상")));
	if (!TestTrue(TEXT("DESIGN 8.13 민물·늪 목록·먼바다 기준을 읽었다"), FreshNames.Num() > 0 && SwampNames.Num() > 0 && DeepMinutes > 0))
	{
		return false;
	}
	TestEqual(TEXT("민물 목록 수가 머리 숫자와 같다"),
		FreshNames.Num(), FCString::Atoi(*FirstCapture(Section, TEXT("\\*\\*민물 ([0-9]+)\\*\\*"))));
	TestEqual(TEXT("늪 목록 수가 머리 숫자와 같다"),
		SwampNames.Num(), FCString::Atoi(*FirstCapture(Section, TEXT("\\*\\*늪 ([0-9]+)\\*\\*"))));

	for (const FFishingRow& Row : Fish)
	{
		const FItemRow* Item = Items.Find(Row.ProducedItemID);
		if (Item == nullptr)
		{
			AddError(FString::Printf(TEXT("%s 아이템 행이 없다"), *Row.ProducedItemID.ToString()));
			continue;
		}
		const FString Name = Item->DisplayName.ToString();
		const EFishHabitat Expected = FreshNames.Contains(Name) ? EFishHabitat::Freshwater
			: SwampNames.Contains(Name) ? EFishHabitat::Swamp
			: (Row.CycleMinutes >= DeepMinutes ? EFishHabitat::DeepSea : EFishHabitat::Sea);
		TestEqual(*FString::Printf(TEXT("%s 서식지"), *Row.ProducedItemID.ToString()),
			static_cast<int32>(Row.Habitat), static_cast<int32>(Expected));
	}
	return true;
}

// DESIGN 13.6 요리 재료 9 + 태그 · 13.3 날로 먹기 · 8.10 산딸기잼 — 표가 설계서와 같은가 (M13-06).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCookingIngredientTest, "Snarehaven.Crafting.CookingIngredients.Table.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCookingIngredientTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!LoadDesign(*this, Doc))
	{
		return false;
	}
	const TMap<FName, FItemRow> Items = IndexItems(LoadRows<FItemRow>(*this, ItemTablePath));
	const TArray<FRecipeRow> Recipes = LoadRows<FRecipeRow>(*this, RecipeTablePath);
	const TArray<FShopRow> Shop = LoadRows<FShopRow>(*this, ShopTablePath);
	TMap<FString, FName> ByName;
	for (const TPair<FName, FItemRow>& Pair : Items)
	{
		ByName.Add(Pair.Value.DisplayName.ToString(), Pair.Key);
	}

	const TArray<FIngredient136> Rows = ReadIngredients136(Doc);
	if (!TestTrue(TEXT("DESIGN 13.6 재료 줄을 읽었다"), Rows.Num() > 0))
	{
		return false;
	}
	for (const FIngredient136& Row : Rows)
	{
		const FString ID = Row.ItemID.ToString();
		const FItemRow* const Item = Items.Find(Row.ItemID);
		if (!TestNotNull(*FString::Printf(TEXT("%s 가 아이템 표에 있다"), *ID), Item))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s 이름"), *ID), Item->DisplayName.ToString(), Row.Name);
		TestEqual(*FString::Printf(TEXT("%s 날로 먹기"), *ID), Item->HealAmount, RawHeal(Row.Raw));

		// 자리 칸 "작업대: 밀 2" · "어디서나: 밀 2"(배낭 제작, DESIGN 8.10) · "불: 밀 2"(모닥불 요리)
		const FString CraftPlace = FirstCapture(Row.Source, TEXT("^(작업대|어디서나|불): (.+) ([0-9]+)$"), 1);
		const FString CraftName = FirstCapture(Row.Source, TEXT("^(작업대|어디서나|불): (.+) ([0-9]+)$"), 2);
		const FString ShopPrice = FirstCapture(Row.Source, TEXT("잡화점 (?:각 )?\\*\\*([0-9]+)\\*\\*"));
		if (!CraftName.IsEmpty())
		{
			const FRecipeRow* const Recipe = Recipes.FindByPredicate([&](const FRecipeRow& R) { return R.ResultItemID == Row.ItemID; });
			const FName* const InputID = ByName.Find(CraftName);
			if (TestNotNull(*FString::Printf(TEXT("%s 레시피"), *ID), Recipe) && TestNotNull(*FString::Printf(TEXT("재료 이름 %s"), *CraftName), InputID)
				&& TestEqual(*FString::Printf(TEXT("%s 재료 칸 수"), *ID), Recipe->Ingredients.Num(), 1))
			{
				TestTrue(*FString::Printf(TEXT("%s %s"), *ID, *CraftPlace), Recipe->Station
					== (CraftPlace == TEXT("어디서나") ? ERecipeStation::Anywhere
						: CraftPlace == TEXT("불") ? ERecipeStation::Fire : ERecipeStation::Workbench));
				TestEqual(*FString::Printf(TEXT("%s 재료"), *ID), Recipe->Ingredients[0].ItemID, *InputID);
				TestEqual(*FString::Printf(TEXT("%s 재료 수"), *ID), Recipe->Ingredients[0].Count,
					FCString::Atoi(*FirstCapture(Row.Source, TEXT("^(작업대|어디서나|불): (.+) ([0-9]+)$"), 3)));
			}
		}
		else if (!ShopPrice.IsEmpty())
		{
			const FShopRow* const ShopRow = Shop.FindByPredicate([&](const FShopRow& R) { return R.ItemID == Row.ItemID; });
			if (TestNotNull(*FString::Printf(TEXT("%s 잡화점 행"), *ID), ShopRow))
			{
				TestEqual(*FString::Printf(TEXT("%s 가격"), *ID), ShopRow->Price, FCString::Atoi(*ShopPrice));
				TestEqual(*FString::Printf(TEXT("%s 한 번에 1개"), *ID), ShopRow->Count, 1);
			}
		}
		else if (Row.Source.Contains(TEXT("채우기")))
		{
			const FName* const Empty = ByName.Find(TEXT("빈 병"));
			const FItemRow* const EmptyRow = Empty != nullptr ? Items.Find(*Empty) : nullptr;
			if (TestNotNull(TEXT("빈 병 행"), EmptyRow))
			{
				TestEqual(TEXT("빈 병을 채우면 물 병"), EmptyRow->FilledItemID, Row.ItemID);
			}
			const FWaterActivityRow* const Water = SnareWater::LoadRow();
			if (TestNotNull(TEXT("물 활동 표"), Water))
			{
				TestEqual(TEXT("채우기 거리 = 낚시 포인트 거리"), Water->CastRangeCm,
					(float)FCString::Atoi(*FirstCapture(Row.Source, TEXT("\\*\\*([0-9]+)cm\\*\\*"))));
			}
		}
		else if (Row.Source.Contains(TEXT("상자")))
		{
			// 제작 없음 · 위험구역 상자 전리품(13.8) — 티백 (10-06 보정)
			TestFalse(*FString::Printf(TEXT("%s 레시피 없음"), *ID),
				Recipes.ContainsByPredicate([&](const FRecipeRow& R) { return R.ResultItemID == Row.ItemID; }));
			TestTrue(*FString::Printf(TEXT("%s 상자 전리품"), *ID), Algo::AnyOf(SnareChestLoot::GetDisclosureLines(),
				[&](const SnareChestLoot::FDisclosureLine& Line) { return FName(Line.ItemID) == Row.ItemID; }));
		}
		else
		{
			TestEqual(*FString::Printf(TEXT("%s 얻는 곳 칸을 읽었다"), *ID), Row.Source, FString(TEXT("—")));
		}
	}

	// 13.3 작물 표 날로 먹기 열 — `| 작물 | 원본 | 수확 | 씨앗 | 재배 | 코인 | 날로 먹기 |`
	TArray<FString> Lines133;
	ExtractSection(Doc, TEXT("### 13.3")).ParseIntoArrayLines(Lines133);
	int32 CropRows = 0;
	for (const FString& Line : Lines133)
	{
		TArray<FString> Cells;
		Line.ParseIntoArray(Cells, TEXT("|"), false);
		const FString CropID = Cells.Num() == 9 ? FirstCapture(Cells[3], TEXT("`(item\\.[a-z_]+)`")) : FString();
		if (CropID.IsEmpty() || !Cells[4].Contains(TEXT("item.seed_")))
		{
			continue;
		}
		++CropRows;
		const FItemRow* const Crop = Items.Find(FName(*CropID));
		if (TestNotNull(*FString::Printf(TEXT("%s 행"), *CropID), Crop))
		{
			TestEqual(*FString::Printf(TEXT("%s 날로 먹기(13.3)"), *CropID), Crop->HealAmount, RawHeal(Cells[7]));
		}
	}
	TestTrue(TEXT("DESIGN 13.3 작물 줄을 읽었다"), CropRows > 0);

	// 태그 — "`Fruit`(사과) · … · `Vegetable`(당근·감자·…)" 와 "`Berry` 에 블루베리·…를 넣는다".
	// 괄호 안 이름이 아이템 표 이름이 아니면(잼 6종·식용 버섯 3종) 건너뛴다 — 그 행은 아직 없다(M13-02·M13-07).
	const FString TagLine = FirstCapture(ExtractSection(Doc, TEXT("### 13.6")), TEXT("(\\*\\*재료 태그 추가\\*\\*[^\\n]+)"));
	TMap<FName, ECraftTag> WantTags;
	FRegexMatcher TagMatcher(FRegexPattern(TEXT("`([A-Za-z]+)`\\(([^)]+)\\)")), TagLine);
	while (TagMatcher.FindNext())
	{
		const int64 TagValue = StaticEnum<ECraftTag>()->GetValueByNameString(TagMatcher.GetCaptureGroup(1));
		TArray<FString> Names;
		TagMatcher.GetCaptureGroup(2).ParseIntoArray(Names, TEXT("·"));
		for (const FString& Name : Names)
		{
			if (const FName* const TaggedID = ByName.Find(Name.TrimStartAndEnd()))
			{
				TestTrue(*FString::Printf(TEXT("태그 %s 가 enum 에 있다"), *TagMatcher.GetCaptureGroup(1)), TagValue != INDEX_NONE);
				WantTags.Add(*TaggedID, static_cast<ECraftTag>(TagValue));
			}
		}
	}
	TArray<FString> Berries;
	FirstCapture(TagLine, TEXT("`Berry` 에 ([^를]+)를")).ParseIntoArray(Berries, TEXT("·"));
	for (const FString& Name : Berries)
	{
		if (TestTrue(*FString::Printf(TEXT("Berry 이름 %s 가 아이템 표에 있다"), *Name), ByName.Contains(Name.TrimStartAndEnd())))
		{
			WantTags.Add(ByName[Name.TrimStartAndEnd()], ECraftTag::Berry);
		}
	}
	TestTrue(TEXT("13.6 태그 문장에서 아이템을 읽었다"), WantTags.Num() > Berries.Num());
	for (const TPair<FName, ECraftTag>& Want : WantTags)
	{
		TestEqual(*FString::Printf(TEXT("%s 태그"), *Want.Key.ToString()),
			UEnum::GetValueAsString(Items[Want.Key].CraftTag), UEnum::GetValueAsString(Want.Value));
	}

	// 8.10 산딸기잼 — 근거 칸의 첫 "+N" (13.6 이 원본 +10 으로 고쳤다).
	const FString JamHeal = FirstCapture(ExtractSection(Doc, TEXT("### 8.10")), TEXT("`item\\.berry_jam`[^\\n]*\\|\\s*불\\s*\\|[^+\\n]*\\+([0-9]+)"));
	const FItemRow* const Jam = Items.Find(TEXT("item.berry_jam"));
	if (TestFalse(TEXT("8.10 산딸기잼 회복량을 읽었다"), JamHeal.IsEmpty()) && TestNotNull(TEXT("산딸기잼 행"), Jam))
	{
		TestEqual(TEXT("산딸기잼 회복량 = 8.10"), Jam->HealAmount, FCString::Atoi(*JamHeal));
		TestTrue(TEXT("산딸기잼 태그 Jam"), Jam->CraftTag == ECraftTag::Jam);
	}
	AddInfo(FString::Printf(TEXT("13.6 재료 %d칸 · 13.3 작물 %d줄 · 태그 %d개 대조"), Rows.Num(), CropRows, WantTags.Num()));
	return true;
}

// DESIGN 13.7 요리 44 — 표 줄마다 아이템 행(음식·체력)과 불 레시피(재료 ID·태그·개수)가 설계서와 같은가.
// 잼 두 줄은 ID 칸이 "13.6" 이라 13.6 버프 표(7칸)에서 ID·재료를 가져온다. 재료 이름 → ID 는 아이템 표 이름(띄어쓰기 무시)으로 찾는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCookingDishTest, "Snarehaven.Crafting.Cooking.Table.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCookingDishTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!LoadDesign(*this, Doc))
	{
		return false;
	}
	const TMap<FName, FItemRow> Items = IndexItems(LoadRows<FItemRow>(*this, ItemTablePath));
	const TArray<FRecipeRow> Recipes = LoadRows<FRecipeRow>(*this, RecipeTablePath);
	const auto Squash = [](FString Name) { return Name.Replace(TEXT(" "), TEXT("")); };
	TMap<FString, FName> ByName;
	for (const TPair<FName, FItemRow>& Pair : Items)
	{
		ByName.Add(Squash(Pair.Value.DisplayName.ToString()), Pair.Key);
	}
	ByName.Add(TEXT("달걀"), TEXT("item.egg")); // 13.7 🟡 — 목장 생산물 표 이름은 "알"(3.2)

	TArray<FString> Lines136;
	ExtractSection(Doc, TEXT("### 13.6")).ParseIntoArrayLines(Lines136);
	TArray<FString> Lines137;
	ExtractSection(Doc, TEXT("### 13.7")).ParseIntoArrayLines(Lines137);

	struct FDish { FString Name; FString ID; FString Inputs; int32 Heal; };
	TArray<FDish> Dishes;
	for (const FString& Line : Lines137)
	{
		TArray<FString> Cells;
		Line.ParseIntoArray(Cells, TEXT("|"), false);
		if (Cells.Num() != 7 || Cells[5].TrimStart().IsEmpty() || !FChar::IsDigit(Cells[5].TrimStart()[0]))
		{
			continue; // 5칸 요리 표의 본문 줄만
		}
		const int32 Heal = FCString::Atoi(*Cells[5].TrimStart());
		if (Cells[3].TrimStartAndEnd() != TEXT("13.6"))
		{
			Dishes.Add({ Cells[1].TrimStartAndEnd().Replace(TEXT("(바꿈)"), TEXT("")), FirstCapture(Cells[3], TEXT("`(item\\.[a-z_]+)`")),
				Cells[4].TrimStartAndEnd(), Heal });
			continue;
		}
		TArray<FString> Names;
		Cells[1].TrimStartAndEnd().ParseIntoArray(Names, TEXT(" · "));
		for (const FString& Name : Names)
		{
			const FString* const BuffLine = Lines136.FindByPredicate([&](const FString& L) { return L.StartsWith(TEXT("| ") + Name + TEXT(" |")); });
			TArray<FString> BuffCells;
			if (TestNotNull(*FString::Printf(TEXT("13.6 버프 표에 %s 줄이 있다"), *Name), BuffLine))
			{
				BuffLine->ParseIntoArray(BuffCells, TEXT("|"), false);
				Dishes.Add({ Name, FirstCapture(BuffCells[3], TEXT("`(item\\.[a-z_]+)`")),
					BuffCells[4].TrimStartAndEnd().Replace(TEXT("불: "), TEXT("")), Heal });
			}
		}
	}
	TestEqual(TEXT("DESIGN 13.7 요리 수"), Dishes.Num(), 44);

	TSet<FName> Jams = { FName(TEXT("item.berry_jam")) }; // 8.10 산딸기잼 + 13.7 잼 = 잼 6종(13.6)
	for (const FDish& Dish : Dishes)
	{
		const FName ID(*Dish.ID);
		if (Dish.Name.EndsWith(TEXT("잼")))
		{
			Jams.Add(ID);
		}
		const FItemRow* const Item = Items.Find(ID);
		if (TestNotNull(*FString::Printf(TEXT("%s 행이 있다"), *Dish.ID), Item))
		{
			TestEqual(*FString::Printf(TEXT("%s 이름"), *Dish.ID), Item->DisplayName.ToString(), Dish.Name);
			TestTrue(*FString::Printf(TEXT("%s 는 음식"), *Dish.ID), Item->ItemType == EItemType::Food);
			TestEqual(*FString::Printf(TEXT("%s 체력"), *Dish.ID), Item->HealAmount, Dish.Heal);
		}
		const FRecipeRow* const Recipe = Recipes.FindByPredicate([&](const FRecipeRow& R) { return R.ResultItemID == ID; });
		if (!TestNotNull(*FString::Printf(TEXT("%s 레시피"), *Dish.ID), Recipe))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s 는 불"), *Dish.ID), Recipe->Station == ERecipeStation::Fire);
		TestEqual(*FString::Printf(TEXT("%s 1개"), *Dish.ID), Recipe->ResultCount, 1);
		TArray<FString> Parts;
		Dish.Inputs.ParseIntoArray(Parts, TEXT(" + "));
		if (!TestEqual(*FString::Printf(TEXT("%s 재료 칸 수"), *Dish.ID), Recipe->Ingredients.Num(), Parts.Num()))
		{
			continue;
		}
		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			const FRecipeIngredient& Got = Recipe->Ingredients[Index];
			const FString What = FString::Printf(TEXT("%s 재료 %d (%s)"), *Dish.ID, Index + 1, *Parts[Index]);
			TestEqual(*(What + TEXT(" 개수")), Got.Count, FCString::Atoi(*FirstCapture(Parts[Index], TEXT(" ([0-9]+)$"))));
			const FString Tag = FirstCapture(Parts[Index], TEXT("^`([A-Za-z]+)`"));
			if (!Tag.IsEmpty())
			{
				TestEqual(*(What + TEXT(" 태그")), UEnum::GetValueAsString(Got.Tag), UEnum::GetValueAsString(
					static_cast<ECraftTag>(StaticEnum<ECraftTag>()->GetValueByNameString(Tag))));
				continue;
			}
			const FName* const WantID = ByName.Find(Squash(FirstCapture(Parts[Index], TEXT("^(.+) [0-9]+$"))));
			if (TestNotNull(*(What + TEXT(" 이름이 아이템 표에 있다")), WantID))
			{
				TestEqual(*(What + TEXT(" ID")), Got.ItemID, *WantID);
			}
		}
	}

	// 태그 — `Jam` = 잼 6종, `Mushroom` = 13.4 숲·늪 버섯 더미 중 광대버섯을 뺀 일곱 (13.6 「재료 태그 추가」, 독사갈기 M33-02).
	TSet<FName> Mushrooms;
	const FString Section134 = ExtractSection(Doc, TEXT("### 13.4"));
	const FString Pile = FirstCapture(Section134, TEXT("(`gatherable\\.mushroom_pile`[^\\n]+)"))
		+ FirstCapture(Section134, TEXT("(`gatherable\\.swamp_mushroom`[^\\n]+)"));
	TArray<FString> Excluded;
	FirstCapture(ExtractSection(Doc, TEXT("### 13.6")), TEXT("([^ (]+) 제외")).ParseIntoArray(Excluded, TEXT("·"));
	FRegexMatcher PileMatcher(FRegexPattern(TEXT("([^ ·—]+) `(item\\.[a-z_]+)`")), Pile);
	while (PileMatcher.FindNext())
	{
		if (!Excluded.Contains(PileMatcher.GetCaptureGroup(1)))
		{
			Mushrooms.Add(FName(*PileMatcher.GetCaptureGroup(2)));
		}
	}
	TestEqual(TEXT("잼 6종"), Jams.Num(), 6);
	TestEqual(TEXT("식용 버섯 7종"), Mushrooms.Num(), 7);
	for (const TPair<FName, FItemRow>& Pair : Items)
	{
		TestEqual(*FString::Printf(TEXT("%s 가 Jam 인가"), *Pair.Key.ToString()), Pair.Value.CraftTag == ECraftTag::Jam, Jams.Contains(Pair.Key));
		TestEqual(*FString::Printf(TEXT("%s 가 Mushroom 인가"), *Pair.Key.ToString()), Pair.Value.CraftTag == ECraftTag::Mushroom,
			Mushrooms.Contains(Pair.Key));
	}
	AddInfo(FString::Printf(TEXT("13.7 요리 %d · 잼 %d · 식용 버섯 %d 대조"), Dishes.Num(), Jams.Num(), Mushrooms.Num()));
	return true;
}

// DESIGN 13.5 — 모자 12(출석 새싹 모자·주간 우두머리 모자 포함)이 아이템 표에 머리 칸 장비로 서고, 잡화점 모자는 값대로 상점에 있으며,
// 몸 효과(달리기 배율·딸기 회복)는 설계서 숫자와 같고 다른 모자엔 없다. 서버 효과는 test_cloudscript.js 가 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHatTableTest, "Snarehaven.Equipment.Hats.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHatTableTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!LoadDesign(*this, Doc))
	{
		return false;
	}
	const TArray<FHat135> Hats = ReadHats135(Doc);
	if (!TestEqual(TEXT("DESIGN 13.5 모자 12줄을 읽었다"), Hats.Num(), 12))
	{
		return false;
	}
	const TMap<FName, FItemRow> Items = IndexItems(LoadRows<FItemRow>(*this, ItemTablePath));
	const TArray<FShopRow> Shop = LoadRows<FShopRow>(*this, ShopTablePath);
	int32 ShopHats = 0;
	for (const FHat135& Hat : Hats)
	{
		const FString Id = Hat.ItemID.ToString();
		const FItemRow* Item = Items.Find(Hat.ItemID);
		if (!TestNotNull(*FString::Printf(TEXT("%s 가 아이템 표에 있다"), *Id), Item))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s 이름"), *Id), Item->DisplayName.ToString(), Hat.Name);
		TestTrue(*FString::Printf(TEXT("%s 는 머리 칸 장비"), *Id),
			Item->ItemType == EItemType::Equipment && Item->EquipSlot == EEquipSlot::Head);

		const FString RunMul = FirstCapture(Hat.Effect, TEXT("달리기 속도 \\*\\*×([0-9.]+)\\*\\*"));
		TestEqual(*FString::Printf(TEXT("%s 달리기 배율"), *Id), Item->RunSpeedMul, RunMul.IsEmpty() ? 0.f : FCString::Atof(*RunMul));
		const FString PristineMul = FirstCapture(Hat.Effect, TEXT("트로피 확률 \\*\\*×([0-9.]+)\\*\\*"));
		TestEqual(*FString::Printf(TEXT("%s 최상급 배율"), *Id), Item->PristineChanceMul,
			PristineMul.IsEmpty() ? 0.f : FCString::Atof(*PristineMul));
		const FString RegenAmount = FirstCapture(Hat.Effect, TEXT("체력 \\*\\*\\+([0-9]+) / [0-9]+초\\*\\*"));
		const FString RegenSeconds = FirstCapture(Hat.Effect, TEXT("체력 \\*\\*\\+[0-9]+ / ([0-9]+)초\\*\\*"));
		TestEqual(*FString::Printf(TEXT("%s 초당 회복"), *Id), Item->HealthRegenPerSecond,
			RegenAmount.IsEmpty() ? 0.f : FCString::Atof(*RegenAmount) / FCString::Atof(*RegenSeconds));
		const FString LightCd = FirstCapture(Hat.Effect, TEXT("불빛 \\*\\*([0-9]+)cd"));
		const FString LightRadius = FirstCapture(Hat.Effect, TEXT("불빛 \\*\\*[0-9]+cd · 반경 ([0-9]+)cm\\*\\*"));
		TestEqual(*FString::Printf(TEXT("%s 머리 불빛 cd"), *Id), Item->HeadLightIntensityCd, LightCd.IsEmpty() ? 0.f : FCString::Atof(*LightCd));
		TestEqual(*FString::Printf(TEXT("%s 머리 불빛 반경"), *Id), Item->HeadLightRadiusCm,
			LightRadius.IsEmpty() ? 0.f : FCString::Atof(*LightRadius));

		const FShopRow* Row = Shop.FindByPredicate([&](const FShopRow& ShopRow) { return ShopRow.ItemID == Hat.ItemID; });
		if (Hat.ShopPrice > 0)
		{
			++ShopHats;
			if (TestNotNull(*FString::Printf(TEXT("%s 가 상점에 있다"), *Id), Row))
			{
				TestEqual(*FString::Printf(TEXT("%s 가격"), *Id), Row->Price, Hat.ShopPrice);
			}
		}
		else
		{
			TestNull(*FString::Printf(TEXT("%s 는 상점에 없다(상자)"), *Id), Row);
		}
	}
	TestEqual(TEXT("잡화점 모자 8"), ShopHats, 8);
	return true;
}

// DESIGN 13.5 — 도구 4가 도구 칸 장비로 서고 잡화점 값이 같으며, 표의 효과 칸(카메라 배율·좌표)이 설계서 문구대로다.
// 채집물 보너스 도구: 손도끼 나무는 전기톱, 더미(13.4)·베리 덤불은 마체테. +1 자체는 test_cloudscript.js 가 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenToolTableTest, "Snarehaven.Equipment.Tools.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenToolTableTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!LoadDesign(*this, Doc))
	{
		return false;
	}
	const TArray<FHat135> Tools = ReadTools135(Doc);
	if (!TestEqual(TEXT("DESIGN 13.5 도구 4줄을 읽었다"), Tools.Num(), 4))
	{
		return false;
	}
	const TMap<FName, FItemRow> Items = IndexItems(LoadRows<FItemRow>(*this, ItemTablePath));
	const TArray<FShopRow> Shop = LoadRows<FShopRow>(*this, ShopTablePath);
	for (const FHat135& Tool : Tools)
	{
		const FString Id = Tool.ItemID.ToString();
		const FItemRow* Item = Items.Find(Tool.ItemID);
		if (!TestNotNull(*FString::Printf(TEXT("%s 가 아이템 표에 있다"), *Id), Item))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s 이름"), *Id), Item->DisplayName.ToString(), Tool.Name);
		TestTrue(*FString::Printf(TEXT("%s 는 도구 칸 장비"), *Id),
			Item->ItemType == EItemType::Equipment && Item->EquipSlot == EEquipSlot::Tool);
		const FString CameraMul = FirstCapture(Tool.Effect, TEXT("카메라 거리 \\*\\*×([0-9.]+)\\*\\*"));
		TestEqual(*FString::Printf(TEXT("%s 카메라 배율"), *Id), Item->CameraDistanceMul, CameraMul.IsEmpty() ? 0.f : FCString::Atof(*CameraMul));
		TestEqual(*FString::Printf(TEXT("%s 좌표 표시"), *Id), Item->bShowsMapCoordinates, Tool.Effect.Contains(TEXT("좌표")));
		const FShopRow* Row = Shop.FindByPredicate([&](const FShopRow& ShopRow) { return ShopRow.ItemID == Tool.ItemID; });
		if (TestNotNull(*FString::Printf(TEXT("%s 가 상점에 있다"), *Id), Row))
		{
			TestEqual(*FString::Printf(TEXT("%s 가격"), *Id), Row->Price, Tool.ShopPrice);
		}
	}

	int32 Chainsaw = 0;
	int32 Machete = 0;
	for (const FGatherableRow& Row : LoadRows<FGatherableRow>(*this, GatherableTablePath))
	{
		const FName Want = Row.RequiredToolItemID == TEXT("item.hatchet") ? FName(TEXT("item.chainsaw"))
			: Row.RequiredToolItemID.ToString().StartsWith(TEXT("item.pickaxe_")) ? FName(TEXT("item.pickaxe_electric"))   // 8.16
			: (Row.GrantOptions.Num() > 0 || Row.GrantItemID == TEXT("item.berry")) ? FName(TEXT("item.machete")) : NAME_None;
		TestEqual(*FString::Printf(TEXT("[%s] 보너스 도구"), *Row.GatherableID.ToString()), Row.BonusToolItemID, Want);
		Chainsaw += Want == TEXT("item.chainsaw") ? 1 : 0;
		Machete += Want == TEXT("item.machete") ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("[Tools] 전기톱 나무 %d행 · 마체테 채집 %d행"), Chainsaw, Machete));
	TestTrue(TEXT("전기톱·마체테 행이 하나 이상씩"), Chainsaw > 0 && Machete > 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
