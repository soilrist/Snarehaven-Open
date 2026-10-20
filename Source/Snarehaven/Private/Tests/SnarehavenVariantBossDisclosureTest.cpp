// ROADMAP M21-07g 2층 검사 — 변종·우두머리 확률 고지 (DESIGN 10.6 「변종·우두머리 확률 고지」).
// 보는 것 둘:
//  ① **서버 표 = 클라 표 = DESIGN 글자** — 변종: CloudScript `VARIANT_CHANCE` = `DT_AnimalTable` 기본 플레이 종 `VariantChance`
//     (12종 모두 같은 값) = DESIGN 「변종 확률」 굵은 %. 우두머리 모자: `BOSS_HAT_CHANCE` = `DT_DangerZoneTable` 우두머리 행
//     `BossHatChance` = DESIGN 「우두머리 모자」 굵은 %. 한쪽만 고치면 고지가 실제 굴림과 어긋난다(10.4 표시 원칙).
//  ② **화면 글자** — 도감 변종 탭의 보상 줄 자리 = `확률 고지: 변종 3% …`, 사냥 HUD 우두머리 고지 = `우두머리 모자 10% (주 1회)`.
//     화면은 표 값을 읽는다 — 글자 함수에 검사 값을 넣어 숫자가 박혀 있지 않은지도 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Disclosure.VariantBoss; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Data/AnimalRow.h"
#include "Data/DangerZoneRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UI/CodexScreenWidget.h"
#include "UI/HuntHudWidget.h"

namespace
{
	const TCHAR* DangerTablePath = TEXT("/Game/Game/Data/World/DT_DangerZoneTable.DT_DangerZoneTable");

	// 검사 전용 값 (설계 숫자 아님) — 글자 함수가 넣은 값을 그대로 쓰는지 본다.
	constexpr float TestChance = 0.125f;

	/** DESIGN 표 줄 `| <Label> | <값> |` 의 값 칸. */
	FString FindDesignCell(const FString& Doc, const FString& Label)
	{
		TArray<FString> Lines;
		Doc.ParseIntoArrayLines(Lines);
		const FString Prefix = FString::Printf(TEXT("| %s |"), *Label);
		for (const FString& Line : Lines)
		{
			if (Line.StartsWith(Prefix))
			{
				FString Value;
				Line.Mid(Prefix.Len()).Split(TEXT("|"), &Value, nullptr);
				return Value;
			}
		}
		return FString();
	}

	/** 칸 안 첫 굵은 `**n%**` 를 0~1 로. 없으면 -1. */
	double ReadBoldPercent(const FString& Cell)
	{
		FRegexMatcher Matcher(FRegexPattern(TEXT("\\*\\*([0-9]+(?:\\.[0-9]+)?)%\\*\\*")), Cell);
		return Matcher.FindNext() ? FCString::Atod(*Matcher.GetCaptureGroup(1)) / 100.0 : -1.0;
	}

	/** CloudScript `var <Name> = <숫자>;` 를 읽는다. 없으면 -1. */
	double ReadServerNumber(const FString& Script, const FString& Name)
	{
		FRegexMatcher Matcher(FRegexPattern(FString::Printf(TEXT("var %s = ([0-9]+(?:\\.[0-9]+)?);"), *Name)), Script);
		return Matcher.FindNext() ? FCString::Atod(*Matcher.GetCaptureGroup(1)) : -1.0;
	}

	bool Same(double A, double B)
	{
		return FMath::IsNearlyEqual(A, B, 1e-6);
	}

	UWorld* CreateTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
}

// ① 서버 = 클라 표 = DESIGN.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDisclosureTablesTest, "Snarehaven.Disclosure.VariantBoss.ServerClientDesignMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDisclosureTablesTest::RunTest(const FString& Parameters)
{
	FString Design;
	FString Script;
	if (!TestTrue(TEXT("DESIGN 읽기"), FFileHelper::LoadFileToString(Design, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md"))))
		|| !TestTrue(TEXT("CloudScript 읽기"), FFileHelper::LoadFileToString(Script,
			*(FPaths::ProjectDir() / TEXT("tools/playfab/cloudscript_main.js")))))
	{
		return false;
	}

	const double DesignVariant = ReadBoldPercent(FindDesignCell(Design, TEXT("변종 확률")));
	const double DesignHat = ReadBoldPercent(FindDesignCell(Design, TEXT("우두머리 모자")));
	const double ServerVariant = ReadServerNumber(Script, TEXT("VARIANT_CHANCE"));
	const double ServerHat = ReadServerNumber(Script, TEXT("BOSS_HAT_CHANCE"));
	if (!TestTrue(TEXT("DESIGN 「변종 확률」·「우두머리 모자」 % 를 읽었다"), DesignVariant > 0.0 && DesignHat > 0.0)
		|| !TestTrue(TEXT("서버 VARIANT_CHANCE·BOSS_HAT_CHANCE 를 읽었다"), ServerVariant > 0.0 && ServerHat > 0.0))
	{
		return false;
	}

	// 클라 변종 — 기본 플레이 종 전부 같은 값이어야 한 줄 고지가 참이다.
	const UDataTable* const AnimalTable = GetDefault<AAnimalCharacter>()->GetAnimalTable();
	if (!TestNotNull(TEXT("동물 표"), AnimalTable))
	{
		return false;
	}
	int32 Playable = 0;
	int32 Mismatch = 0;
	double ClientVariant = -1.0;
	for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
	{
		const FAnimalRow* const Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
		if (Row == nullptr || Row->bPlaceholderSpecies || Row->bStorySpecies)
		{
			continue;
		}
		++Playable;
		if (ClientVariant < 0.0)
		{
			ClientVariant = Row->VariantChance;
		}
		Mismatch += Same(Row->VariantChance, ClientVariant) ? 0 : 1;
	}
	TestEqual(TEXT("기본 플레이 종 12 (DESIGN 10.6 「변종 대상」)"), Playable, 12);
	TestEqual(TEXT("종마다 변종 확률이 같다"), Mismatch, 0);

	const UDataTable* const DangerTable = LoadObject<UDataTable>(nullptr, DangerTablePath);
	const double ClientHat = UHuntHudWidget::FindBossHatChance(DangerTable);

	AddInfo(FString::Printf(TEXT("[Disclosure] 변종 서버 %.4f · 클라 %.4f · DESIGN %.4f | 모자 서버 %.4f · 클라 %.4f · DESIGN %.4f"),
		ServerVariant, ClientVariant, DesignVariant, ServerHat, ClientHat, DesignHat));
	TestTrue(TEXT("변종 확률 서버 = DESIGN"), Same(ServerVariant, DesignVariant));
	TestTrue(TEXT("변종 확률 클라 = DESIGN"), Same(ClientVariant, DesignVariant));
	TestTrue(TEXT("모자 확률 서버 = DESIGN"), Same(ServerHat, DesignHat));
	TestTrue(TEXT("모자 확률 클라 = DESIGN"), Same(ClientHat, DesignHat));

	// 역검증 — 서버 글자가 한 자리라도 다르면 운다.
	const FString Tampered = Script.Replace(TEXT("var BOSS_HAT_CHANCE = 0.1;"), TEXT("var BOSS_HAT_CHANCE = 0.2;"));
	TestFalse(TEXT("서버 모자 확률을 바꾸면 불일치"), Same(ReadServerNumber(Tampered, TEXT("BOSS_HAT_CHANCE")), DesignHat));
	return true;
}

// ② 화면 글자 — 도감 변종 탭·사냥 HUD 우두머리 고지.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDisclosureScreenTest, "Snarehaven.Disclosure.VariantBoss.ScreenTextReadsTables",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDisclosureScreenTest::RunTest(const FString& Parameters)
{
	// 글자 함수는 넣은 값을 쓴다 — 숫자가 박혀 있으면 12.5 가 안 나온다.
	TestEqual(TEXT("변종 고지 글자(검사 값)"), UCodexScreenWidget::FormatVariantDisclosure(TestChance).ToString(),
		FString(TEXT("확률 고지: 변종 12.5% (동물이 설 때마다)")));
	TestEqual(TEXT("모자 고지 글자(검사 값)"), UHuntHudWidget::FormatBossDisclosure(TestChance).ToString(),
		FString(TEXT("우두머리 모자 12.5% (주 1회)")));

	// 실제 표로 세운 화면.
	UWorld* World = CreateTestWorld();
	UCodexScreenWidget* const Codex = CreateWidget<UCodexScreenWidget>(World, UCodexScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("도감 화면 위젯"), Codex))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> Slate = Codex->TakeWidget();
	Codex->SetSelectedCategory(ECodexCategory::Variant);
	const FString VariantLine = Codex->GetVariantDisclosureText().ToString();
	const FString HatLine = UHuntHudWidget::FormatBossDisclosure(
		UHuntHudWidget::FindBossHatChance(LoadObject<UDataTable>(nullptr, DangerTablePath))).ToString();
	AddInfo(FString::Printf(TEXT("[Disclosure] 도감 변종 탭 '%s' · 우두머리 카드 '%s' · 사냥 탭 보상 줄 '%s'"),
		*VariantLine, *HatLine, *Codex->GetRewardText().ToString()));
	TestEqual(TEXT("도감 변종 탭 고지 = 표 3%"), VariantLine, FString(TEXT("확률 고지: 변종 3% (동물이 설 때마다)")));
	TestEqual(TEXT("우두머리 고지 = 표 10%"), HatLine, FString(TEXT("우두머리 모자 10% (주 1회)")));
	TestFalse(TEXT("다른 탭 보상 줄은 고지가 아니다"), Codex->GetRewardText().ToString().Contains(TEXT("확률 고지")));

	// 우두머리를 세우지 않는 표(빈 표)면 0 — 엉뚱한 행을 읽지 않는다.
	TestEqual(TEXT("표가 없으면 모자 확률 0"), UHuntHudWidget::FindBossHatChance(nullptr), 0.f);

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
