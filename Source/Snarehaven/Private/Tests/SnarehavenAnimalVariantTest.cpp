// ROADMAP M21-07a1 2층 검사 — 색 변종(DESIGN 10.6 「변종·우두머리 확률 고지」)이
// ① 굴림 비율이 표의 확률대로 나오고 ② 변종을 포획하면 변종 펫이 지급 알림으로 가고
// ③ 확률 0·변종 펫 없는 행은 변종이 안 서며 ④ 실제 DT_AnimalTable 의 값이 설계서와 같은지.
// 실행: UnrealEditor <uproject> -ExecCmds="Automation RunTests Snarehaven.Animal.Variant; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalStateComponent.h"
#include "Data/AnimalRow.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6) — 굴림 비율 시험은 설계 확률이 아니라 이 값으로 잰다.
	constexpr float TestVariantChance = 0.03f;
	constexpr int32 TestRollCount = 10000;
	constexpr float TestRatioTolerance = 0.005f;   // ±0.5%p (ROADMAP M21-07a 합격 줄)
	constexpr int32 TestSeed = 337;
	constexpr float TestMaxHP = 100.f;
	constexpr float TestCaptureThresholdPct = 50.f;

	const FName TestPetID = TEXT("item.pet_test_variant_base");
	const FName TestVariantPetID = TEXT("item.pet_test_variant");

	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");

	FAnimalRow MakeTestRow(float Chance, FName VariantPetID)
	{
		FAnimalRow Row;
		Row.AnimalID = TEXT("animal.test_variant");
		Row.MaxHP = TestMaxHP;
		Row.CaptureThresholdPct = TestCaptureThresholdPct;
		Row.PetItemID = TestPetID;
		Row.VariantPetItemID = VariantPetID;
		Row.VariantChance = Chance;
		return Row;
	}

	/** 임계까지 깎아 포획하고, 포획 알림(`OnCaptured`)이 내보내는 것과 같은 펫 ID 를 돌려준다. */
	FName CaptureAndReadPet(FAutomationTestBase& Test, UAnimalStateComponent* State)
	{
		Test.TestTrue(TEXT("임계까지 깎인다"), State->ApplyDamage(TestMaxHP * (1.f - TestCaptureThresholdPct / 100.f)));
		Test.TestTrue(TEXT("포획된다"), State->TryCapture());
		return State->GetPetItemID();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalVariantRollRatioTest, "Snarehaven.Animal.Variant.RollRatioMatchesChance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalVariantRollRatioTest::RunTest(const FString& Parameters)
{
	// ① 시드 고정 1만 번 — 정적 굴림과, 스폰 길(`RollVariantOnSpawn`) 둘 다 같은 비율이어야 한다.
	FRandomStream Stream(TestSeed);
	int32 Hits = 0;
	for (int32 Index = 0; Index < TestRollCount; ++Index)
	{
		Hits += UAnimalStateComponent::RollVariant(TestVariantChance, Stream) ? 1 : 0;
	}
	const float Ratio = static_cast<float>(Hits) / TestRollCount;

	FRandomStream SpawnStream(TestSeed);
	int32 SpawnHits = 0;
	for (int32 Index = 0; Index < TestRollCount; ++Index)
	{
		UAnimalStateComponent* const State = NewObject<UAnimalStateComponent>();
		State->InitializeFromRow(MakeTestRow(TestVariantChance, TestVariantPetID));
		SpawnHits += State->RollVariantOnSpawn(SpawnStream) && State->IsVariant() ? 1 : 0;
	}
	const float SpawnRatio = static_cast<float>(SpawnHits) / TestRollCount;

	AddInfo(FString::Printf(TEXT("[Variant] 1만 회 굴림 %d번 (%.2f%%) · 스폰 길 %d번 (%.2f%%) · 기대 %.1f%%±%.1f%%p"),
		Hits, Ratio * 100.f, SpawnHits, SpawnRatio * 100.f, TestVariantChance * 100.f, TestRatioTolerance * 100.f));
	TestTrue(TEXT("굴림 비율이 확률 ±0.5%p 안"), FMath::Abs(Ratio - TestVariantChance) <= TestRatioTolerance);
	TestTrue(TEXT("스폰 길 비율이 확률 ±0.5%p 안"), FMath::Abs(SpawnRatio - TestVariantChance) <= TestRatioTolerance);
	TestEqual(TEXT("같은 시드면 두 길이 같은 횟수"), SpawnHits, Hits);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalVariantCaptureTest, "Snarehaven.Animal.Variant.CaptureGivesVariantPet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalVariantCaptureTest::RunTest(const FString& Parameters)
{
	// ② 변종으로 정한 개체를 포획하면 변종 펫, 일반 개체는 일반 펫.
	UAnimalStateComponent* const Variant = NewObject<UAnimalStateComponent>();
	TestTrue(TEXT("변종 행으로 초기화"), Variant->InitializeFromRow(MakeTestRow(TestVariantChance, TestVariantPetID)));
	TestFalse(TEXT("초기화 직후엔 일반"), Variant->IsVariant());
	TestTrue(TEXT("변종으로 정할 수 있다"), Variant->SetVariant(true));
	TestEqual(TEXT("변종 포획 → 변종 펫"), CaptureAndReadPet(*this, Variant), TestVariantPetID);

	UAnimalStateComponent* const Plain = NewObject<UAnimalStateComponent>();
	Plain->InitializeFromRow(MakeTestRow(TestVariantChance, TestVariantPetID));
	TestEqual(TEXT("일반 포획 → 일반 펫"), CaptureAndReadPet(*this, Plain), TestPetID);

	// 다시 초기화하면 변종이 풀린다(스폰 재활용 대비).
	UAnimalStateComponent* const Reused = NewObject<UAnimalStateComponent>();
	Reused->InitializeFromRow(MakeTestRow(TestVariantChance, TestVariantPetID));
	Reused->SetVariant(true);
	Reused->InitializeFromRow(MakeTestRow(TestVariantChance, TestVariantPetID));
	TestFalse(TEXT("다시 초기화하면 일반"), Reused->IsVariant());

	// ③ 확률 1 이어도 변종 펫 칸이 비면 안 선다 · 확률 0·미정(-1)이면 안 선다.
	FRandomStream Stream(TestSeed);
	UAnimalStateComponent* const NoPet = NewObject<UAnimalStateComponent>();
	NoPet->InitializeFromRow(MakeTestRow(1.f, NAME_None));
	TestFalse(TEXT("변종 펫 없는 행은 확률 1 이어도 안 선다"), NoPet->RollVariantOnSpawn(Stream));
	TestFalse(TEXT("변종 펫 없는 행은 SetVariant 도 거절"), NoPet->SetVariant(true));
	TestEqual(TEXT("그 행 포획은 일반 펫"), NoPet->GetPetItemID(), TestPetID);

	for (const float Chance : { 0.f, -1.f })
	{
		UAnimalStateComponent* const Zero = NewObject<UAnimalStateComponent>();
		Zero->InitializeFromRow(MakeTestRow(Chance, TestVariantPetID));
		int32 Hits = 0;
		for (int32 Index = 0; Index < TestRollCount; ++Index)
		{
			Hits += Zero->RollVariantOnSpawn(Stream) ? 1 : 0;
		}
		TestEqual(*FString::Printf(TEXT("확률 %.0f 이면 1만 번 다 일반"), Chance), Hits, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalVariantTableTest, "Snarehaven.Animal.Variant.TableMatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalVariantTableTest::RunTest(const FString& Parameters)
{
	// ④ 설계서 10.6 「변종 확률」 줄의 **n%** 와 「변종 대상」 줄의 **n종** 을 읽는다.
	FString Doc;
	if (!TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	float DesignChance = -1.f;
	int32 DesignSpecies = -1;
	TArray<FString> Lines;
	Doc.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		if (Line.StartsWith(TEXT("| 변종 확률 |")))
		{
			FRegexMatcher Matcher(FRegexPattern(TEXT("\\*\\*([0-9.]+)%\\*\\*")), Line);
			if (Matcher.FindNext())
			{
				DesignChance = FCString::Atof(*Matcher.GetCaptureGroup(1)) / 100.f;
			}
		}
		else if (Line.StartsWith(TEXT("| 변종 대상 |")))
		{
			FRegexMatcher Matcher(FRegexPattern(TEXT("\\*\\*([0-9]+)종")), Line);
			if (Matcher.FindNext())
			{
				DesignSpecies = FCString::Atoi(*Matcher.GetCaptureGroup(1));
			}
		}
	}
	TestTrue(TEXT("설계서 변종 확률을 읽었다"), DesignChance > 0.f);
	TestTrue(TEXT("설계서 변종 대상 종 수를 읽었다"), DesignSpecies > 0);

	const UDataTable* const Table = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable"), Table))
	{
		return false;
	}
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	TestNotNull(TEXT("DT_ItemTable"), ItemTable);

	int32 VariantSpecies = 0;
	Table->ForeachRow<FAnimalRow>(TEXT("VariantTable"), [&](const FName& RowName, const FAnimalRow& Row)
	{
		if (Row.bPlaceholderSpecies || Row.bStorySpecies)
		{
			TestTrue(*FString::Printf(TEXT("[%s] 자리표시·이야기 종은 변종 없음"), *RowName.ToString()),
				Row.VariantPetItemID.IsNone() && Row.VariantChance == 0.f);
			return;
		}
		++VariantSpecies;
		const FString Expected = Row.PetItemID.ToString() + TEXT("_variant");
		TestEqual(*FString::Printf(TEXT("[%s] 변종 펫 ID = 펫 ID + _variant"), *RowName.ToString()), Row.VariantPetItemID.ToString(), Expected);
		TestEqual(*FString::Printf(TEXT("[%s] 변종 확률 = 설계서"), *RowName.ToString()), Row.VariantChance, DesignChance, KINDA_SMALL_NUMBER);

		const FItemRow* ItemRow = nullptr;
		if (ItemTable != nullptr)
		{
			for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
			{
				const FItemRow* const Candidate = reinterpret_cast<const FItemRow*>(Pair.Value);
				ItemRow = Candidate->ItemID == Row.VariantPetItemID ? Candidate : ItemRow;
			}
		}
		TestTrue(*FString::Printf(TEXT("[%s] 변종 펫 아이템 행이 펫 종류로 있다"), *RowName.ToString()),
			ItemRow != nullptr && ItemRow->ItemType == EItemType::Pet);
	});
	AddInfo(FString::Printf(TEXT("[Variant] 표 변종 종 %d · 설계서 %d종 · 확률 %.2f"), VariantSpecies, DesignSpecies, DesignChance));
	TestEqual(TEXT("변종이 서는 종 수 = 설계서"), VariantSpecies, DesignSpecies);
	return true;
}

#endif
