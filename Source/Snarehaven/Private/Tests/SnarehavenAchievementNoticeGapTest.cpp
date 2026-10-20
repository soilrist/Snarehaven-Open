// ROADMAP M48-04 2층 단정 — 달성 알림 빈 길 0 (DESIGN 8.15 「달성 알림」).
// `DT_AchievementTable` 행 전부와 도감 화면 칸 전부(여섯 갈래 — 다섯 원본 + 변종)가 **표에서 찾은 이름**으로 카드 글이 되고,
// 업적은 「+N 코인」 까지 붙으며, 알림 소리 두 개가 **실제로 들어온 소리 에셋**(`CountVariants` > 0)인가.
// 모르는 ID 는 `MakeCards` 가 ID 글자로 메워 빈 카드는 안 나오지만, 그건 표·서버 어긋남이라 여기서는 빈 길로 센다.
// 판단은 글자 모양이 아니라 이름 맵에서 실제로 찾았는지·소리 변형을 실제로 찾았는지로 한다 (Verification.md).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Achievement.Notice.NoGaps; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/AchievementRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UI/CodexScreenWidget.h"
#include "UI/SnareAchievementNotice.h"
#include "World/SnareSound.h"

namespace
{
	const TCHAR* NoticeAchievementTablePath = TEXT("/Game/Game/Data/Achievements/DT_AchievementTable.DT_AchievementTable");
	const TCHAR* NoticeAnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");
	const TCHAR* NoticeItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");

	bool HasName(const TMap<FName, FText>& Names, FName ID)
	{
		const FText* const Found = Names.Find(ID);
		return Found != nullptr && !Found->IsEmptyOrWhitespace();
	}

	/**
	 * 빈 길 목록. 업적 행 = 이름 없음 · 코인 없음 · 카드 글 빔, 도감 칸 = 이름 없음 · 카드 글 빔, 소리 = 변형 0.
	 * `Names` 는 게임이 쓰는 것과 같은 `AddAchievementNames`·`AddCodexNames` 로 채운 것이어야 한다.
	 */
	TArray<FString> SurveyNoticeGaps(const UDataTable& AchievementTable, const TArray<FName>& CodexIDs,
		const SnareAchievementNotice::FNames& Names, USnareSoundSubsystem* Sounds, const TArray<FName>& Cues)
	{
		using namespace SnareAchievementNotice;
		TArray<FString> Gaps;
		for (const TPair<FName, uint8*>& Pair : AchievementTable.GetRowMap())
		{
			const FAchievementRow* const Row = reinterpret_cast<const FAchievementRow*>(Pair.Value);
			const FName ID = Row != nullptr ? Row->AchievementID : NAME_None;
			if (ID.IsNone())
			{
				Gaps.Add(Pair.Key.ToString() + TEXT(" 업적 ID 없음"));
				continue;
			}
			if (!HasName(Names.AchievementNames, ID))
			{
				Gaps.Add(ID.ToString() + TEXT(" 이름"));
			}
			if (!Names.AchievementCoins.Contains(ID))
			{
				Gaps.Add(ID.ToString() + TEXT(" 코인"));
			}
			FDiff One;
			One.NewAchievements.Add(ID);
			const TArray<FCard> Cards = MakeCards(One, Names);
			if (Cards.Num() != 1 || Cards[0].Title.IsEmptyOrWhitespace() || Cards[0].Body.IsEmptyOrWhitespace())
			{
				Gaps.Add(ID.ToString() + TEXT(" 카드"));
			}
		}
		for (const FName& ID : CodexIDs)
		{
			if (!HasName(Names.CodexNames, ID))
			{
				Gaps.Add(ID.ToString() + TEXT(" 도감 이름"));
			}
			FDiff One;
			One.NewCodexEntries.Add(ID);
			const TArray<FCard> Cards = MakeCards(One, Names);
			if (Cards.Num() != 1 || Cards[0].Title.IsEmptyOrWhitespace() || Cards[0].Body.IsEmptyOrWhitespace())
			{
				Gaps.Add(ID.ToString() + TEXT(" 도감 카드"));
			}
		}
		for (const FName& Cue : Cues)
		{
			if (Cue.IsNone() || Sounds->CountVariants(Cue) <= 0)
			{
				Gaps.Add(TEXT("소리 ") + Cue.ToString());
			}
		}
		return Gaps;
	}

	/** 게임이 실제로 고르는 소리 이름 둘 — 이름을 시험에 적지 않고 `SoundCue` 에게 받는다. */
	TArray<FName> NoticeCues()
	{
		SnareAchievementNotice::FDiff Achievement;
		Achievement.NewAchievements.Add(TEXT("ach.cue_probe"));
		SnareAchievementNotice::FDiff Codex;
		Codex.NewCodexEntries.Add(TEXT("codex.cue_probe"));
		return { SnareAchievementNotice::SoundCue(Achievement), SnareAchievementNotice::SoundCue(Codex) };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAchievementNoticeNoGapsTest, "Snarehaven.Achievement.Notice.NoGaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAchievementNoticeNoGapsTest::RunTest(const FString& Parameters)
{
	using namespace SnareAchievementNotice;
	UDataTable* const AchievementTable = LoadObject<UDataTable>(nullptr, NoticeAchievementTablePath);
	const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, NoticeAnimalTablePath);
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, NoticeItemTablePath);
	if (!TestNotNull(TEXT("DT_AchievementTable"), AchievementTable) || !TestNotNull(TEXT("DT_AnimalTable"), AnimalTable)
		|| !TestNotNull(TEXT("DT_ItemTable"), ItemTable))
	{
		return false;
	}

	// 백엔드와 같은 출처로 이름을 채운다(`USnareBackendSubsystem::AnnounceAchievementNotice`).
	FNames Names;
	AddAchievementNames(AchievementTable, Names);
	AddCodexNames(AnimalTable, ItemTable, Names);

	// 도감 칸 = 도감 화면이 세우는 칸 전부 — 그 로스터가 서버 `CODEX_CATEGORIES` 와 같은지는 `Codex.Screen.RosterMatchesTables` 몫.
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	TArray<FName> CodexIDs;
	int32 PerCategory[static_cast<int32>(ECodexCategory::Count)] = {};
	if (UCodexScreenWidget* const Screen = CreateWidget<UCodexScreenWidget>(World, UCodexScreenWidget::StaticClass()))
	{
		const TSharedRef<SWidget> Slate = Screen->TakeWidget(); // 붙잡아 둬야 칸을 세는 동안 안 풀린다
		for (const FCodexSlotView& Slot : Screen->BuildSlots())
		{
			CodexIDs.AddUnique(Slot.EntryID);
			++PerCategory[static_cast<int32>(Slot.Category)];
		}
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	for (int32 Index = 0; Index < static_cast<int32>(ECodexCategory::Count); ++Index)
	{
		TestTrue(FString::Printf(TEXT("도감 갈래 %d 칸 1 이상"), Index), PerCategory[Index] > 0);
	}

	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	const TArray<FName> Cues = NoticeCues();
	const TArray<FString> Gaps = SurveyNoticeGaps(*AchievementTable, CodexIDs, Names, Sounds, Cues);
	for (const FString& Gap : Gaps)
	{
		AddError(TEXT("알림 빈 길: ") + Gap);
	}
	TestEqual(TEXT("빈 길 0"), Gaps.Num(), 0);

	// 역검증 — 이름 없는 가짜 업적 한 줄 · 표에 없는 도감 칸 하나 · 반입 안 된 소리 하나가 각각 빈 길로 잡혀야 한다.
	// 진짜 표는 건드리지 않고 행을 베낀 사본 표에 한 줄을 더한다 (PROMPT ③-13).
	UDataTable* const Fake = NewObject<UDataTable>(GetTransientPackage());
	Fake->RowStruct = FAchievementRow::StaticStruct();
	for (const TPair<FName, uint8*>& Pair : AchievementTable->GetRowMap())
	{
		Fake->AddRow(Pair.Key, *reinterpret_cast<const FAchievementRow*>(Pair.Value));
	}
	FAchievementRow NoName;
	NoName.AchievementID = TEXT("ach.test_noname");
	NoName.RewardCoins = 5;
	Fake->AddRow(TEXT("ach_test_noname"), NoName);
	FNames FakeNames;
	AddAchievementNames(Fake, FakeNames);
	AddCodexNames(AnimalTable, ItemTable, FakeNames);
	TArray<FName> FakeCodex = CodexIDs;
	FakeCodex.Add(TEXT("item.test_not_in_tables"));
	TArray<FName> FakeCues = Cues;
	FakeCues.Add(TEXT("TestNoSuchNoticeCue"));
	const TArray<FString> FakeGaps = SurveyNoticeGaps(*Fake, FakeCodex, FakeNames, Sounds, FakeCues);
	TestEqual(TEXT("역검증 — 빈 길 3"), FakeGaps.Num(), 3);
	TestTrue(TEXT("역검증 — 이름 없는 업적"), FakeGaps.Contains(TEXT("ach.test_noname 이름")));
	TestTrue(TEXT("역검증 — 표에 없는 도감 칸"), FakeGaps.Contains(TEXT("item.test_not_in_tables 도감 이름")));
	TestTrue(TEXT("역검증 — 반입 안 된 소리"), FakeGaps.Contains(TEXT("소리 TestNoSuchNoticeCue")));

	AddInfo(FString::Printf(TEXT("[NoticeGaps] 업적 행 %d · 도감 칸 %d · 소리 %d · 빈 길 %d · 역검증 빈 길 %d"),
		AchievementTable->GetRowMap().Num(), CodexIDs.Num(), Cues.Num(), Gaps.Num(), FakeGaps.Num()));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
