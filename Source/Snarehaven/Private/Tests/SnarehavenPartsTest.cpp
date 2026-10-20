// ROADMAP M13-09 2층 검사 — DESIGN 13.8 비금속 부품 4 + 텐트 제작.
// ① `DT_ItemTable`·`DT_RecipeTable` 의 부품 행·작업대 레시피 5·구명조끼 배율이 13.8 표와 같은가 — **숫자는 설계서에서 읽는다.**
// ② `UPlayerVitalsComponent` 장비 수영 배율이 수영 소모에만 곱해지고 SwimSave 버프와 곱해지는가 — **검사 전용 값**(Verification.md 6).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Parts; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/ItemRow.h"
#include "Data/PlayerVitalsRow.h"
#include "Data/RecipeRow.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/PlayerVitalsComponent.h"

namespace SnarehavenPartsTest
{
	FString Capture(const FString& Text, const TCHAR* Pattern, int32 Group = 1)
	{
		FRegexMatcher Matcher(FRegexPattern(Pattern), Text);
		return Matcher.FindNext() ? Matcher.GetCaptureGroup(Group) : FString();
	}

	template <typename RowType>
	TArray<RowType> LoadRows(FAutomationTestBase& Test, const TCHAR* Path)
	{
		TArray<RowType> Rows;
		const UDataTable* const Table = LoadObject<UDataTable>(nullptr, Path);
		if (Test.TestNotNull(*FString::Printf(TEXT("표 로드 %s"), Path), Table))
		{
			for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
			{
				Rows.Add(*reinterpret_cast<const RowType*>(Pair.Value));
			}
		}
		return Rows;
	}

	// ② 검사 전용 값 — 설계 숫자가 아니다.
	constexpr float TestMaxStamina = 100.f;
	constexpr float TestSwimCost = 10.f;
	constexpr float TestJacketMul = 0.25f;
	constexpr float TestBuffValue = 0.4f;
	constexpr float TestBuffSeconds = 3.f;
	constexpr float Tolerance = 0.01f;

	UPlayerVitalsComponent* MakeSwimmer(FAutomationTestBase& Test, bool bSwimming)
	{
		FPlayerVitalsRow Row;
		Row.MaxHealth = TestMaxStamina;
		Row.MaxStamina = TestMaxStamina;
		Row.RunStaminaPerSecond = 0.f;
		Row.AttackStaminaCost = 0.f;
		Row.StaminaRegenPerSecond = 0.f; // 수영 소모만 보이게
		Row.StaminaRegenDelaySeconds = 0.f;
		Row.ExhaustionHealthPerSecond = 0.f;
		Row.HealthRegenPerSecond = 0.f;
		Row.HealthRegenStaminaThreshold = 0.f;
		Row.RespawnHealth = TestMaxStamina;
		Row.RespawnStamina = TestMaxStamina;
		Row.DeathLingerSeconds = 0.f;
		UPlayerVitalsComponent* const Vitals = NewObject<UPlayerVitalsComponent>();
		Test.TestTrue(TEXT("검사 전용 행으로 초기화"), Vitals->InitializeFromRow(Row));
		Vitals->SetSwimState(bSwimming, TestSwimCost, 0.f);
		return Vitals;
	}

	float SpentAfterOneSecond(UPlayerVitalsComponent& Vitals)
	{
		for (int32 Step = 0; Step < 10; ++Step)
		{
			Vitals.AdvanceTime(0.1f);
		}
		return TestMaxStamina - Vitals.GetStamina();
	}
}

using namespace SnarehavenPartsTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPartsTableTest, "Snarehaven.Parts.Table.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPartsTableTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	const int32 Start = Doc.Find(TEXT("### 13.8"));
	const int32 End = Doc.Find(TEXT("\n### "), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + 1);
	const FString Section = Start == INDEX_NONE ? FString() : Doc.Mid(Start, End == INDEX_NONE ? MAX_int32 : End - Start);

	TMap<FName, FItemRow> Items;
	TMap<FString, FName> ByName; // 띄어쓰기 무시
	for (const FItemRow& Row : LoadRows<FItemRow>(*this, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable")))
	{
		Items.Add(Row.ItemID, Row);
		ByName.Add(Row.DisplayName.ToString().Replace(TEXT(" "), TEXT("")), Row.ItemID);
	}
	const TArray<FRecipeRow> Recipes = LoadRows<FRecipeRow>(*this, TEXT("/Game/Game/Data/Crafting/DT_RecipeTable.DT_RecipeTable"));

	TArray<FString> Lines;
	Section.ParseIntoArrayLines(Lines);
	int32 Checked = 0;
	int32 Worn = 0;
	for (const FString& Line : Lines)
	{
		TArray<FString> Cells;
		Line.ParseIntoArray(Cells, TEXT("|"), false);
		const FString Source = Cells.Num() == 8 ? Cells[4].TrimStartAndEnd() : FString();
		// 자리 칸 "작업대: …" · "어디서나: …"(배낭 제작, DESIGN 8.10)
		const bool bAnywhere = Source.StartsWith(TEXT("어디서나: "));
		if (!(bAnywhere || Source.StartsWith(TEXT("작업대: "))) || !Cells[3].Contains(TEXT("`item.")))
		{
			continue; // 6칸 부품 표의 제작 줄만
		}
		const FName ID(*Capture(Cells[3], TEXT("`(item\\.[a-z_]+)`")));
		const FItemRow* const Item = Items.Find(ID);
		++Checked;
		if (!TestNotNull(*FString::Printf(TEXT("%s 행"), *ID.ToString()), Item))
		{
			continue;
		}

		// 쓰는 곳 칸 "수영 스태미나 소모 **×0.5**" → 장비 배율
		const FString Mul = Capture(Cells[5], TEXT("수영 스태미나 소모 \\*\\*×([0-9.]+)\\*\\*"));
		if (!Mul.IsEmpty())
		{
			++Worn;
			TestEqual(*FString::Printf(TEXT("%s SwimStaminaMul"), *ID.ToString()), Item->SwimStaminaMul, FCString::Atof(*Mul), Tolerance);
			TestTrue(*FString::Printf(TEXT("%s 몸 장비 = 상의 칸"), *ID.ToString()),
				Item->ItemType == EItemType::Equipment && Item->EquipSlot == EEquipSlot::OutfitTop);
		}
		else
		{
			TestEqual(*FString::Printf(TEXT("%s 수영 배율 없음"), *ID.ToString()), Item->SwimStaminaMul, 0.f);
		}

		const FRecipeRow* const Recipe = Recipes.FindByPredicate([&](const FRecipeRow& R) { return R.ResultItemID == ID; });
		if (!TestNotNull(*FString::Printf(TEXT("%s 레시피"), *ID.ToString()), Recipe))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s %s"), *ID.ToString(), bAnywhere ? TEXT("어디서나") : TEXT("작업대")),
			Recipe->Station == (bAnywhere ? ERecipeStation::Anywhere : ERecipeStation::Workbench));
		TArray<FString> Parts;
		Source.Mid(Source.Find(TEXT(": ")) + 2).ParseIntoArray(Parts, TEXT(" + "));
		if (!TestEqual(*FString::Printf(TEXT("%s 재료 칸 수"), *ID.ToString()), Recipe->Ingredients.Num(), Parts.Num()))
		{
			continue;
		}
		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			const FRecipeIngredient& Got = Recipe->Ingredients[Index];
			const FString Name = Capture(Parts[Index], TEXT("^(.+) [0-9]+$")).Replace(TEXT(" "), TEXT(""));
			TestEqual(*FString::Printf(TEXT("%s 재료 %d 수"), *ID.ToString(), Index), Got.Count,
				FCString::Atoi(*Capture(Parts[Index], TEXT(" ([0-9]+)$"))));
			if (const FName* const Want = ByName.Find(Name))
			{
				TestEqual(*FString::Printf(TEXT("%s 재료 %d"), *ID.ToString(), Index), Got.ItemID, *Want);
			}
			else
			{
				AddError(FString::Printf(TEXT("%s 재료 이름 → ID 를 모른다: %s"), *ID.ToString(), *Name));
			}
		}
	}
	TestEqual(TEXT("13.8 제작 줄 수(접착제·원단·방수포·조끼·텐트)"), Checked, 5);
	TestEqual(TEXT("수영 배율 줄 수(구명조끼)"), Worn, 1);
	AddInfo(FString::Printf(TEXT("[Parts] 13.8 %d줄 대조 · 수영 배율 %d"), Checked, Worn));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPartsVitalsTest, "Snarehaven.Parts.Vitals.LifeJacketSwimCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPartsVitalsTest::RunTest(const FString& Parameters)
{
	UPlayerVitalsComponent* const Plain = MakeSwimmer(*this, true);
	UPlayerVitalsComponent* const Jacket = MakeSwimmer(*this, true);
	UPlayerVitalsComponent* const Both = MakeSwimmer(*this, true);
	UPlayerVitalsComponent* const Unset = MakeSwimmer(*this, true);
	UPlayerVitalsComponent* const Land = MakeSwimmer(*this, false);
	Jacket->SetEquipmentSwimStaminaMul(TestJacketMul);
	Both->SetEquipmentSwimStaminaMul(TestJacketMul);
	Both->ApplyBuff(EBuffKind::SwimSave, TestBuffValue, TestBuffSeconds);
	Unset->SetEquipmentSwimStaminaMul(TestJacketMul);
	Unset->SetEquipmentSwimStaminaMul(0.f); // 벗으면 행 값 0 → ×1
	Land->SetEquipmentSwimStaminaMul(TestJacketMul);

	TestEqual(TEXT("맨몸 수영 소모"), SpentAfterOneSecond(*Plain), TestSwimCost, Tolerance);
	TestEqual(TEXT("조끼 = 소모 × 배율"), SpentAfterOneSecond(*Jacket), TestSwimCost * TestJacketMul, Tolerance);
	TestEqual(TEXT("조끼 + SwimSave 버프 = 곱"), SpentAfterOneSecond(*Both), TestSwimCost * TestJacketMul * (1.f - TestBuffValue), Tolerance);
	TestEqual(TEXT("벗으면(0) 맨몸과 같다"), SpentAfterOneSecond(*Unset), TestSwimCost, Tolerance);
	TestEqual(TEXT("물 밖에선 스태미나를 안 쓴다"), SpentAfterOneSecond(*Land), 0.f, Tolerance);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
