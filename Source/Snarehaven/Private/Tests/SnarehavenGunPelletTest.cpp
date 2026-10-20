// INBOX 총 11종 펠릿 조각 2층 검사 — 산탄총 3종 행의 알 수·퍼짐이 DESIGN 8.6-c 펠릿 줄과 같은지, 알 하나짜리 총은 기본값인지,
// `CountPelletHits` 가 바로 앞에선 전부·멀수록 적게·알 하나는 늘 맞히는지. 숫자는 시험에 적지 않고 설계서 표에서 읽는다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Combat.Gun.Pellet; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/MeleeAttackComponent.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunPelletTest, "Snarehaven.Combat.Gun.Pellet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGunPelletTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	FString Cell;
	{
		TArray<FString> Lines;
		Doc.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			TArray<FString> Cells;
			Line.ParseIntoArray(Cells, TEXT("|"), true);
			if (Cells.Num() >= 2 && Cells[0].TrimStartAndEnd().StartsWith(TEXT("펠릿 (산탄총 3종")))
			{
				Cell = Cells[1];
				break;
			}
		}
	}
	if (!TestFalse(TEXT("DESIGN 펠릿 줄이 있다"), Cell.IsEmpty()))
	{
		return false;
	}

	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (!TestNotNull(TEXT("DT_ItemTable"), ItemTable))
	{
		return false;
	}

	struct FPelletGun { const TCHAR* Id; const TCHAR* Pattern; };
	const FPelletGun Guns[] = {
		{ TEXT("item.shotgun"), TEXT("^\\s*산탄총 ([0-9]+)알 ±([0-9.]+)°") },
		{ TEXT("item.shotgun_auto"), TEXT("연사 산탄총 ([0-9]+)알 ±([0-9.]+)°") },
		{ TEXT("item.shotgun_short"), TEXT("짧은 산탄총 ([0-9]+)알 ±([0-9.]+)°") } };
	TSet<FName> PelletIDs;
	for (const FPelletGun& Gun : Guns)
	{
		FRegexMatcher Matcher(FRegexPattern(Gun.Pattern), Cell);
		if (!TestTrue(FString::Printf(TEXT("%s 펠릿 줄 값을 읽었다"), Gun.Id), Matcher.FindNext()))
		{
			return false;
		}
		const int32 Count = FCString::Atoi(*Matcher.GetCaptureGroup(1));
		const float Spread = FCString::Atof(*Matcher.GetCaptureGroup(2));
		TestTrue(TEXT("알 수 > 1 · 퍼짐 > 0"), Count > 1 && Spread > 0.f);
		PelletIDs.Add(FName(Gun.Id));
		const FItemRow* Row = nullptr;
		for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
		{
			const FItemRow* Candidate = reinterpret_cast<const FItemRow*>(Pair.Value);
			if (Candidate->ItemID == FName(Gun.Id))
			{
				Row = Candidate;
			}
		}
		if (TestNotNull(FString::Printf(TEXT("%s 행이 있다"), Gun.Id), Row))
		{
			TestEqual(TEXT("알 수"), Row->PelletCount, Count);
			TestEqual(TEXT("퍼짐 반각"), Row->PelletSpreadHalfDegrees, Spread);
		}
	}
	// 산탄총 말고는 알 하나(기본값) — 펠릿이 새어 나가면 권총·소총 피해가 조용히 나뉜다.
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (!PelletIDs.Contains(Row->ItemID))
		{
			TestEqual(*FString::Printf(TEXT("%s 알 수 기본"), *Row->ItemID.ToString()), Row->PelletCount, 1);
		}
	}

	// 순수 함수 — 시험 전용 값(밸런스 숫자가 바뀌어도 안 깨지게).
	TestEqual(TEXT("알 하나는 늘 맞는다"), UMeleeAttackComponent::CountPelletHits(1, 0.f, 5000.f, 10.f), 1);
	TestEqual(TEXT("바로 앞은 전부"), UMeleeAttackComponent::CountPelletHits(8, 12.f, 10.f, 45.f), 8);
	TestEqual(TEXT("퍼짐 0 이면 한 줄이라 전부"), UMeleeAttackComponent::CountPelletHits(8, 0.f, 5000.f, 45.f), 8);
	const int32 Near = UMeleeAttackComponent::CountPelletHits(8, 12.f, 300.f, 45.f);
	const int32 Mid = UMeleeAttackComponent::CountPelletHits(8, 12.f, 600.f, 45.f);
	const int32 Far = UMeleeAttackComponent::CountPelletHits(8, 12.f, 3000.f, 45.f);
	TestTrue(FString::Printf(TEXT("가까울수록 많이 맞는다 (%d ≥ %d ≥ %d)"), Near, Mid, Far), Near >= Mid && Mid >= Far);
	TestTrue(TEXT("멀리선 일부만 맞는다"), Far < 8);
	TestEqual(TEXT("퍼짐 12°·알 8·반지름 45·300cm = 4알 (±1.7°·±5.1° 만 한계 8.5° 안)"), Near, 4);
	TestEqual(TEXT("같은 알 150cm = 전부"), UMeleeAttackComponent::CountPelletHits(8, 12.f, 150.f, 45.f), 8);
	TestEqual(TEXT("과녁이 아주 멀어 알이 짝수 개면 한가운데 알이 없어 0알"), UMeleeAttackComponent::CountPelletHits(8, 12.f, 100000.f, 1.f), 0);
	return true;
}

#endif
