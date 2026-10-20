// ROADMAP M13-08 2층 검사 — DESIGN 13.6 음료·버프 소모품.
// ① `DT_ItemTable` 의 체력·BuffKind·BuffValue·BuffSeconds 와 레시피·잡화점 행이 13.6 표와 같은가 — **숫자는 설계서에서 읽는다.**
// ② `UPlayerVitalsComponent` 버프가 덮어쓰기·만료·추위·수영·회복·사망 해제대로 도는가 — **검사 전용 값**(Verification.md 6).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Buff; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/ItemRow.h"
#include "Data/PlayerVitalsRow.h"
#include "Data/RecipeRow.h"
#include "Data/ShopRow.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/PlayerVitalsComponent.h"

namespace SnarehavenBuffTest
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

	/** 13.6 「버프」 칸 → 종류. 칸의 낱말로 고른다 — "이동" · "추위" · "수영" · "체력 재생". "—" 는 None. */
	EBuffKind KindOf(const FString& Cell)
	{
		if (Cell.Contains(TEXT("이동"))) { return EBuffKind::Speed; }
		if (Cell.Contains(TEXT("추위"))) { return EBuffKind::ColdResist; }
		if (Cell.Contains(TEXT("수영"))) { return EBuffKind::SwimSave; }
		if (Cell.Contains(TEXT("체력 재생"))) { return EBuffKind::Regen; }
		return EBuffKind::None;
	}

	// ② 검사 전용 값 — 설계 숫자가 아니다.
	constexpr float TestMaxHealth = 100.f;
	constexpr float TestMaxStamina = 100.f;
	constexpr float TestHealthRegen = 2.f;
	constexpr float TestZoneDrain = 4.f;
	constexpr float TestSwimCost = 10.f;
	constexpr float TestBuffValue = 0.25f;
	constexpr float TestBuffSeconds = 3.f;
	constexpr float Tolerance = 0.01f;

	UPlayerVitalsComponent* MakeVitals(FAutomationTestBase& Test)
	{
		FPlayerVitalsRow Row;
		Row.MaxHealth = TestMaxHealth;
		Row.MaxStamina = TestMaxStamina;
		Row.RunStaminaPerSecond = 0.f;
		Row.AttackStaminaCost = 0.f;
		Row.StaminaRegenPerSecond = 0.f; // 수영 소모만 보이게
		Row.StaminaRegenDelaySeconds = 0.f;
		Row.ExhaustionHealthPerSecond = 0.f;
		Row.HealthRegenPerSecond = TestHealthRegen;
		Row.HealthRegenStaminaThreshold = 0.f;
		Row.RespawnHealth = TestMaxHealth;
		Row.RespawnStamina = TestMaxStamina;
		Row.DeathLingerSeconds = 0.f;
		UPlayerVitalsComponent* const Vitals = NewObject<UPlayerVitalsComponent>();
		Test.TestTrue(TEXT("검사 전용 행으로 초기화"), Vitals->InitializeFromRow(Row));
		return Vitals;
	}

	void TickFor(UPlayerVitalsComponent& Vitals, float Seconds, float Step = 0.1f)
	{
		for (float Done = 0.f; Done < Seconds - KINDA_SMALL_NUMBER; Done += Step)
		{
			Vitals.AdvanceTime(FMath::Min(Step, Seconds - Done));
		}
	}
}

using namespace SnarehavenBuffTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBuffTableTest, "Snarehaven.Buff.Table.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBuffTableTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	const int32 Start = Doc.Find(TEXT("### 13.6"));
	const int32 End = Doc.Find(TEXT("\n### "), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + 1);
	const FString Section = Start == INDEX_NONE ? FString() : Doc.Mid(Start, End == INDEX_NONE ? MAX_int32 : End - Start);
	const float Seconds = FCString::Atof(*Capture(Section, TEXT("\\| 지속 \\| 전부 \\*\\*([0-9]+)초\\*\\*")));
	if (!TestTrue(TEXT("13.6 「지속」 줄을 읽었다"), Seconds > 0.f))
	{
		return false;
	}

	TMap<FName, FItemRow> Items;
	TMap<FString, FName> ByName; // 띄어쓰기 무시
	for (const FItemRow& Row : LoadRows<FItemRow>(*this, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable")))
	{
		Items.Add(Row.ItemID, Row);
		ByName.Add(Row.DisplayName.ToString().Replace(TEXT(" "), TEXT("")), Row.ItemID);
	}
	const TArray<FRecipeRow> Recipes = LoadRows<FRecipeRow>(*this, TEXT("/Game/Game/Data/Crafting/DT_RecipeTable.DT_RecipeTable"));
	const TArray<FShopRow> Shop = LoadRows<FShopRow>(*this, TEXT("/Game/Game/Data/Shop/DT_ShopTable.DT_ShopTable"));

	TArray<FString> Lines;
	Section.ParseIntoArrayLines(Lines);
	int32 Checked = 0;
	int32 Buffed = 0;
	for (const FString& Line : Lines)
	{
		TArray<FString> Cells;
		Line.ParseIntoArray(Cells, TEXT("|"), false);
		if (Cells.Num() != 9 || !Cells[3].Contains(TEXT("`item.")))
		{
			continue; // 7칸 버프 소모품 표만
		}
		const FName ID(*Capture(Cells[3], TEXT("`(item\\.[a-z_]+)`")));
		const FString Source = Cells[4].TrimStartAndEnd();
		const FString HealCell = Capture(Cells[5], TEXT("\\+([0-9]+)"));
		const FString BuffCell = Cells[6].TrimStartAndEnd();
		const FItemRow* const Item = Items.Find(ID);
		++Checked;
		if (!TestNotNull(*FString::Printf(TEXT("%s 행"), *ID.ToString()), Item))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s 이름"), *ID.ToString()), Item->DisplayName.ToString(), Cells[1].TrimStartAndEnd());
		TestEqual(*FString::Printf(TEXT("%s 체력"), *ID.ToString()), Item->HealAmount, HealCell.IsEmpty() ? 0 : FCString::Atoi(*HealCell));

		const EBuffKind Kind = KindOf(BuffCell);
		TestEqual(*FString::Printf(TEXT("%s BuffKind"), *ID.ToString()), static_cast<int32>(Item->BuffKind), static_cast<int32>(Kind));
		if (Kind != EBuffKind::None)
		{
			++Buffed;
			const float Value = FCString::Atof(*Capture(BuffCell, TEXT("\\*\\*[+−-]?([0-9]+)%\\*\\*"))) / 100.f;
			TestTrue(*FString::Printf(TEXT("%s 버프 퍼센트를 읽었다"), *ID.ToString()), Value > 0.f);
			TestEqual(*FString::Printf(TEXT("%s BuffValue"), *ID.ToString()), Item->BuffValue, Value, Tolerance);
			TestEqual(*FString::Printf(TEXT("%s BuffSeconds = 지속"), *ID.ToString()), Item->BuffSeconds, Seconds);
		}

		const FString Place = Capture(Source, TEXT("^(불|작업대): "));
		const FString ShopPrice = Capture(Source, TEXT("잡화점 \\*\\*([0-9]+)\\*\\*"));
		if (!Place.IsEmpty())
		{
			const FRecipeRow* const Recipe = Recipes.FindByPredicate([&](const FRecipeRow& R) { return R.ResultItemID == ID; });
			if (!TestNotNull(*FString::Printf(TEXT("%s 레시피"), *ID.ToString()), Recipe))
			{
				continue;
			}
			TestTrue(*FString::Printf(TEXT("%s 자리"), *ID.ToString()),
				Recipe->Station == (Place == TEXT("불") ? ERecipeStation::Fire : ERecipeStation::Workbench));
			TArray<FString> Parts;
			Source.Mid(Place.Len() + 2).ParseIntoArray(Parts, TEXT(" + "));
			if (!TestEqual(*FString::Printf(TEXT("%s 재료 칸 수"), *ID.ToString()), Recipe->Ingredients.Num(), Parts.Num()))
			{
				continue;
			}
			for (int32 Index = 0; Index < Parts.Num(); ++Index)
			{
				const FRecipeIngredient& Got = Recipe->Ingredients[Index];
				const FString Tag = Capture(Parts[Index], TEXT("^`([A-Za-z]+)` [0-9]+$"));
				const FString Name = Capture(Parts[Index], TEXT("^(.+) [0-9]+$")).Replace(TEXT(" "), TEXT(""));
				TestEqual(*FString::Printf(TEXT("%s 재료 %d 수"), *ID.ToString(), Index), Got.Count,
					FCString::Atoi(*Capture(Parts[Index], TEXT(" ([0-9]+)$"))));
				if (!Tag.IsEmpty())
				{
					TestEqual(*FString::Printf(TEXT("%s 재료 %d 태그"), *ID.ToString(), Index),
						StaticEnum<ECraftTag>()->GetNameStringByValue(static_cast<int64>(Got.Tag)), Tag);
				}
				else if (const FName* const Want = ByName.Find(Name))
				{
					TestEqual(*FString::Printf(TEXT("%s 재료 %d"), *ID.ToString(), Index), Got.ItemID, *Want);
				}
				else
				{
					AddError(FString::Printf(TEXT("%s 재료 이름 → ID 를 모른다: %s"), *ID.ToString(), *Name));
				}
			}
		}
		else if (!ShopPrice.IsEmpty())
		{
			const FShopRow* const Row = Shop.FindByPredicate([&](const FShopRow& R) { return R.ItemID == ID; });
			if (TestNotNull(*FString::Printf(TEXT("%s 잡화점 행"), *ID.ToString()), Row))
			{
				TestEqual(*FString::Printf(TEXT("%s 가격"), *ID.ToString()), Row->Price, FCString::Atoi(*ShopPrice));
				TestEqual(*FString::Printf(TEXT("%s 한 번에 1개"), *ID.ToString()), Row->Count, 1);
			}
		}
	}
	TestTrue(TEXT("13.6 버프 표 줄을 읽었다"), Checked >= 10 && Buffed >= 9);
	AddInfo(FString::Printf(TEXT("[Buff] 13.6 %d줄 대조 · 버프 %d · 지속 %.0f초"), Checked, Buffed, Seconds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBuffVitalsTest, "Snarehaven.Buff.Vitals.OverwriteExpireAndEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBuffVitalsTest::RunTest(const FString& Parameters)
{
	// 걸기 거절 — None · 0초
	UPlayerVitalsComponent* const Vitals = MakeVitals(*this);
	TestFalse(TEXT("None 은 안 걸린다"), Vitals->ApplyBuff(EBuffKind::None, TestBuffValue, TestBuffSeconds));
	TestFalse(TEXT("0초는 안 걸린다"), Vitals->ApplyBuff(EBuffKind::Speed, TestBuffValue, 0.f));
	TestEqual(TEXT("안 걸렸으면 0"), Vitals->GetBuffValue(EBuffKind::Speed), 0.f);

	// 같은 종류는 덮어쓰고 다른 종류는 같이 간다, 시간이 다 되면 0
	TestTrue(TEXT("속도 건다"), Vitals->ApplyBuff(EBuffKind::Speed, TestBuffValue, TestBuffSeconds));
	Vitals->ApplyBuff(EBuffKind::Regen, TestBuffValue, TestBuffSeconds * 2.f);
	TickFor(*Vitals, 1.f);
	Vitals->ApplyBuff(EBuffKind::Speed, TestBuffValue * 2.f, TestBuffSeconds);
	TestEqual(TEXT("덮어쓴 값"), Vitals->GetBuffValue(EBuffKind::Speed), TestBuffValue * 2.f);
	TestEqual(TEXT("덮어쓴 시간"), Vitals->GetBuffSecondsLeft(EBuffKind::Speed), TestBuffSeconds, Tolerance);
	TestEqual(TEXT("다른 종류는 그대로"), Vitals->GetBuffValue(EBuffKind::Regen), TestBuffValue);
	TickFor(*Vitals, TestBuffSeconds + 0.2f);
	TestEqual(TEXT("다 되면 0"), Vitals->GetBuffValue(EBuffKind::Speed), 0.f);

	// 추위 — 존 감소 × (1 − 값)
	UPlayerVitalsComponent* const Cold = MakeVitals(*this);
	UPlayerVitalsComponent* const Warm = MakeVitals(*this);
	for (UPlayerVitalsComponent* const V : { Cold, Warm })
	{
		V->SetZoneHealthDrain(TestZoneDrain + TestHealthRegen); // 자연 회복을 빼면 순 감소 = TestZoneDrain
	}
	Warm->ApplyBuff(EBuffKind::ColdResist, TestBuffValue, TestBuffSeconds);
	TickFor(*Cold, 1.f);
	TickFor(*Warm, 1.f);
	TestEqual(TEXT("추위 버프 = 감소 × (1 − 값) 만큼 덜 깎인다"), Warm->GetHealth() - Cold->GetHealth(),
		(TestZoneDrain + TestHealthRegen) * TestBuffValue, Tolerance);

	// 수영 — 스태미나 소모 × (1 − 값)
	UPlayerVitalsComponent* const Swim = MakeVitals(*this);
	UPlayerVitalsComponent* const SwimBuff = MakeVitals(*this);
	Swim->SetSwimState(true, TestSwimCost, 0.f);
	SwimBuff->SetSwimState(true, TestSwimCost, 0.f);
	SwimBuff->ApplyBuff(EBuffKind::SwimSave, TestBuffValue, TestBuffSeconds);
	TickFor(*Swim, 1.f);
	TickFor(*SwimBuff, 1.f);
	TestEqual(TEXT("수영 소모"), TestMaxStamina - Swim->GetStamina(), TestSwimCost, Tolerance);
	TestEqual(TEXT("수영 버프 소모"), TestMaxStamina - SwimBuff->GetStamina(), TestSwimCost * (1.f - TestBuffValue), Tolerance);

	// 회복 — 자연 회복 × (1 + 값)
	UPlayerVitalsComponent* const Plain = MakeVitals(*this);
	UPlayerVitalsComponent* const Regen = MakeVitals(*this);
	Plain->ApplyDamage(TestMaxHealth / 2.f);
	Regen->ApplyDamage(TestMaxHealth / 2.f);
	Regen->ApplyBuff(EBuffKind::Regen, TestBuffValue, TestBuffSeconds);
	TickFor(*Plain, 1.f);
	TickFor(*Regen, 1.f);
	TestEqual(TEXT("회복 버프 = 자연 회복 × 값 만큼 더"), Regen->GetHealth() - Plain->GetHealth(), TestHealthRegen * TestBuffValue, Tolerance);

	// 쓰러지면 버프가 끝난다 (13.6 🟡)
	Regen->KillForCheck();
	Regen->ReviveAtRespawnValues();
	TestEqual(TEXT("부활 뒤 버프 없음"), Regen->GetBuffValue(EBuffKind::Regen), 0.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
