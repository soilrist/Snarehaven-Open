// ROADMAP M48-02 2층 검사 — 업적 달성·도감 새 칸 알림 규칙(DESIGN 8.15 「달성 알림」).
// 이 검사가 잡는 것: ① 접속 첫 답·같은 답에 카드가 뜨는 것 ② 모르는 ID 가 빈 카드가 되는 것
// ③ 한 답에 카드가 3장 넘게 쌓이는 것 ④ 여러 장인데 소리가 여러 번이거나 업적보다 도감 소리가 이기는 것.
// 시험 전용 ID·이름이라 표 값이 바뀌어도 규칙만 본다.
// M48-03 `Notice.Wiring` — 주입한 `GetCodexRewards` 답 → 백엔드 차이 → 사냥·목장 HUD 알림 카드 + 소리 한 번.
// M48-03c — 카드는 한 칸에 두 줄이 아니라 **따로 두 장**, 오른쪽 위 색 점이 업적 금 · 도감 빨강.
// 감독 보정(10-08) — 카드 왼쪽 아이콘 48: 업적 = 코인 그림, 도감 = 그 칸 그림, 그림이 비면 칸을 접는다(빈 네모 0).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Data/AchievementRow.h"
#include "Data/AnimalRow.h"
#include "Data/ItemRow.h"
#include "Engine/Texture2D.h"
#include "Components/Image.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UI/HuntHudWidget.h"
#include "UI/RanchHudWidget.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "UI/SnareNoticeCards.h"
#include "UI/SnareAchievementNotice.h"
#include "UI/SnareUiStyle.h"

namespace
{
	TArray<FName> Ids(std::initializer_list<const TCHAR*> List)
	{
		TArray<FName> Out;
		for (const TCHAR* Name : List)
		{
			Out.Add(FName(Name));
		}
		return Out;
	}

	SnareAchievementNotice::FNames TestNames()
	{
		SnareAchievementNotice::FNames Names;
		Names.AchievementNames.Add(TEXT("ach.test_a"), FText::FromString(TEXT("시험 업적 가")));
		Names.AchievementCoins.Add(TEXT("ach.test_a"), 7);
		Names.AchievementNames.Add(TEXT("ach.test_b"), FText::FromString(TEXT("시험 업적 나")));
		Names.CodexNames.Add(TEXT("animal.test_x"), FText::FromString(TEXT("시험 동물")));
		Names.CodexIcons.Add(TEXT("animal.test_x"), TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Test/T_TestAnimal.T_TestAnimal"))));
		Names.AchievementIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Test/T_TestCoin.T_TestCoin")));
		return Names;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAchievementNoticeDiffTest, "Snarehaven.Achievement.Notice.Diff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAchievementNoticeDiffTest::RunTest(const FString& Parameters)
{
	using namespace SnareAchievementNotice;
	const TArray<FName> WasAch = Ids({ TEXT("ach.test_a") });
	const TArray<FName> WasCodex = Ids({ TEXT("animal.test_x") });

	const FDiff OneNew = Diff(WasAch, Ids({ TEXT("ach.test_a"), TEXT("ach.test_b") }), WasCodex, WasCodex, false);
	TestEqual(TEXT("새 업적 1 → 1"), OneNew.NewAchievements.Num(), 1);
	TestEqual(TEXT("새 업적은 ach.test_b"), OneNew.NewAchievements.Num() > 0 ? OneNew.NewAchievements[0] : NAME_None,
		FName(TEXT("ach.test_b")));
	TestEqual(TEXT("도감 그대로 → 0"), OneNew.NewCodexEntries.Num(), 0);

	TestTrue(TEXT("같은 답 → 0"), Diff(WasAch, WasAch, WasCodex, WasCodex, false).IsEmpty());

	const FDiff First = Diff({}, Ids({ TEXT("ach.test_a"), TEXT("ach.test_b") }), {}, WasCodex, true);
	TestTrue(TEXT("첫 답 → 0"), First.IsEmpty());
	TestEqual(TEXT("첫 답 → 소리 없음"), SoundCue(First), FName());

	const FDiff Removed = Diff(Ids({ TEXT("ach.test_a"), TEXT("ach.test_b") }), WasAch, WasCodex, {}, false);
	TestTrue(TEXT("빠진 것은 안 알림"), Removed.IsEmpty());

	const FDiff Codex = Diff(WasAch, WasAch, {}, WasCodex, false);
	TestEqual(TEXT("새 도감 칸 1 → 1"), Codex.NewCodexEntries.Num(), 1);
	TestEqual(TEXT("도감만 → 도감 소리"), SoundCue(Codex), FName(TEXT("CodexNew")));
	TestEqual(TEXT("도감 소리 0.6"), SoundVolume(SoundCue(Codex)), 0.6f);

	const FDiff Both = Diff({}, WasAch, {}, WasCodex, false);
	TestEqual(TEXT("업적+도감 → 업적 소리 한 번"), SoundCue(Both), FName(TEXT("Achievement")));
	TestEqual(TEXT("업적 소리 0.8"), SoundVolume(SoundCue(Both)), 0.8f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAchievementNoticeCardsTest, "Snarehaven.Achievement.Notice.Cards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAchievementNoticeCardsTest::RunTest(const FString& Parameters)
{
	using namespace SnareAchievementNotice;
	const FNames Names = TestNames();

	FDiff Two;
	Two.NewCodexEntries = Ids({ TEXT("animal.test_x") });
	Two.NewAchievements = Ids({ TEXT("ach.test_a") });
	const TArray<FCard> TwoCards = MakeCards(Two, Names);
	if (TestEqual(TEXT("업적 1 + 도감 1 → 카드 2"), TwoCards.Num(), 2))
	{
		TestTrue(TEXT("업적 먼저"), TwoCards[0].bAchievement && !TwoCards[1].bAchievement);
		TestEqual(TEXT("업적 제목"), TwoCards[0].Title.ToString(), FString(TEXT("업적 달성!")));
		TestEqual(TEXT("업적 설명 = 이름 + 코인"), TwoCards[0].Body.ToString(), FString(TEXT("시험 업적 가 +7 코인")));
		TestEqual(TEXT("도감 제목"), TwoCards[1].Title.ToString(), FString(TEXT("새 발견!")));
		TestEqual(TEXT("도감 설명"), TwoCards[1].Body.ToString(), FString(TEXT("시험 동물 — 도감이 갱신됨")));
		TestTrue(TEXT("업적 아이콘 = 코인 그림"), TwoCards[0].Icon == Names.AchievementIcon);
		TestTrue(TEXT("도감 아이콘 = 그 칸 그림"), TwoCards[1].Icon == Names.CodexIcons.FindRef(TEXT("animal.test_x")));
	}

	FDiff Unknown;
	Unknown.NewAchievements = Ids({ TEXT("ach.test_unknown") });
	Unknown.NewCodexEntries = Ids({ TEXT("item.test_unknown") });
	const TArray<FCard> UnknownCards = MakeCards(Unknown, Names);
	if (TestEqual(TEXT("모르는 ID 도 카드 2"), UnknownCards.Num(), 2))
	{
		TestEqual(TEXT("모르는 업적 = ID 글"), UnknownCards[0].Body.ToString(), FString(TEXT("ach.test_unknown")));
		TestEqual(TEXT("모르는 도감 칸 = ID 글"), UnknownCards[1].Body.ToString(),
			FString(TEXT("item.test_unknown — 도감이 갱신됨")));
		TestTrue(TEXT("그림 없는 도감 칸 → 빈 아이콘"), UnknownCards[1].Icon.IsNull());
	}

	FDiff Many;
	Many.NewAchievements = Ids({ TEXT("ach.test_a"), TEXT("ach.test_b") });
	Many.NewCodexEntries = Ids({ TEXT("animal.test_x"), TEXT("item.test_y") });
	const TArray<FCard> ManyCards = MakeCards(Many, Names);
	if (TestEqual(TEXT("4장 → 최대 2장"), ManyCards.Num(), MaxCards))
	{
		TestEqual(TEXT("둘째 카드가 남은 것을 합침"), ManyCards[1].Body.ToString(), FString(TEXT("시험 업적 나 외 2개")));
	}

	TestEqual(TEXT("빈 차이 → 카드 0"), MakeCards(FDiff(), Names).Num(), 0);

	FNames FromTable;
	AddAchievementNames(nullptr, FromTable);
	TestEqual(TEXT("표 없음 → 이름 0"), FromTable.AchievementNames.Num(), 0);
	const UDataTable* const Table = LoadObject<UDataTable>(nullptr,
		TEXT("/Game/Game/Data/Achievements/DT_AchievementTable.DT_AchievementTable"));
	if (TestNotNull(TEXT("DT_AchievementTable"), Table))
	{
		AddAchievementNames(Table, FromTable);
		TestNotNull(TEXT("업적 코인 그림이 실제로 열림"), FromTable.AchievementIcon.LoadSynchronous());
		TestEqual(TEXT("표 행마다 이름 칸"), FromTable.AchievementNames.Num(), Table->GetRowMap().Num());
		// M48-03b — 표의 표시 전용 코인이 카드 글 「<이름> +<코인> 코인」 으로 이어진다(값은 표에서 읽어 숫자에 안 묶임).
		TestEqual(TEXT("표 행마다 코인 칸"), FromTable.AchievementCoins.Num(), Table->GetRowMap().Num());
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FAchievementRow* const Row = reinterpret_cast<const FAchievementRow*>(Pair.Value);
			FDiff One;
			One.NewAchievements.Add(Row->AchievementID);
			const TArray<FCard> Card = MakeCards(One, FromTable);
			const FString Expected = FString::Printf(TEXT("%s +%d 코인"), *Row->DisplayName.ToString(), Row->RewardCoins);
			TestEqual(FString::Printf(TEXT("%s 카드 글"), *Row->AchievementID.ToString()),
				Card.Num() == 1 ? Card[0].Body.ToString() : FString(), Expected);
		}
	}

	// 도감 칸 그림 — 아이템은 표 그림, 종은 펫 아이템 행 그림(도감 화면 M25-02 와 같은 출처).
	const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable"));
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (TestNotNull(TEXT("DT_AnimalTable"), AnimalTable) && TestNotNull(TEXT("DT_ItemTable"), ItemTable))
	{
		FNames Codex;
		AddCodexNames(AnimalTable, ItemTable, Codex);
		TMap<FName, const FItemRow*> Items;
		for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
		{
			const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
			Items.Add(Row->ItemID, Row);
			if (!Row->IconRef.IsNull())
			{
				TestTrue(FString::Printf(TEXT("%s 도감 그림 = 표 그림"), *Row->ItemID.ToString()),
					Codex.CodexIcons.FindRef(Row->ItemID) == Row->IconRef);
			}
		}
		int32 AnimalIcons = 0;
		for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
		{
			const FAnimalRow* const Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
			const FItemRow* const* Pet = Items.Find(Row->PetItemID);
			if (Pet != nullptr && !(*Pet)->IconRef.IsNull())
			{
				++AnimalIcons;
				TestTrue(FString::Printf(TEXT("%s 도감 그림 = 펫 아이템 그림"), *Row->AnimalID.ToString()),
					Codex.CodexIcons.FindRef(Row->AnimalID) == (*Pet)->IconRef);
			}
		}
		TestTrue(TEXT("그림 있는 종 1 이상"), AnimalIcons > 0);
	}

	for (const TArray<FCard>* List : { &TwoCards, &UnknownCards, &ManyCards })
	{
		for (const FCard& Card : *List)
		{
			TestFalse(TEXT("빈 제목 0"), Card.Title.IsEmptyOrWhitespace());
			TestFalse(TEXT("빈 설명 0"), Card.Body.IsEmptyOrWhitespace());
		}
	}
	return true;
}

namespace
{
	/** `GetCodexRewards` 답 한 벌 — 서버 모양 그대로(`entries` + `achievements.done`). */
	TSharedPtr<FJsonObject> CodexReply(std::initializer_list<const TCHAR*> Entries, std::initializer_list<const TCHAR*> Done)
	{
		TArray<TSharedPtr<FJsonValue>> EntryValues;
		for (const TCHAR* ID : Entries)
		{
			EntryValues.Add(MakeShared<FJsonValueString>(ID));
		}
		TArray<TSharedPtr<FJsonValue>> DoneValues;
		for (const TCHAR* ID : Done)
		{
			DoneValues.Add(MakeShared<FJsonValueString>(ID));
		}
		const TSharedPtr<FJsonObject> Ach = MakeShared<FJsonObject>();
		Ach->SetObjectField(TEXT("counters"), MakeShared<FJsonObject>());
		Ach->SetArrayField(TEXT("done"), DoneValues);
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetArrayField(TEXT("entries"), EntryValues);
		Reply->SetObjectField(TEXT("achievements"), Ach);
		return Reply;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAchievementNoticeWiringTest, "Snarehaven.Achievement.Notice.Wiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAchievementNoticeWiringTest::RunTest(const FString& Parameters)
{
	// 게임 인스턴스 서브시스템은 `UGameInstance` 안에서만 만든다 — `Initialize` 를 안 불러 PlayFab 계정을 안 만든다.
	UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(GameInstance);
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	TestTrue(TEXT("시험 스냅샷 반영"), Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true));

	// ① 듣는 HUD 가 없으면(로비) 카드를 버리고 소리도 없다.
	TestTrue(TEXT("첫 답 받아들임"), Backend->ApplyCodexReply(CodexReply({ TEXT("codex.test_a") }, {})));
	TestEqual(TEXT("첫 답 → 카드 0"), Backend->GetLastNoticeCardCount(), 0);
	TestTrue(TEXT("새 칸 답 받아들임"), Backend->ApplyCodexReply(CodexReply({ TEXT("codex.test_a"), TEXT("codex.test_b") }, {})));
	TestEqual(TEXT("HUD 없음 → 카드는 세지만"), Backend->GetLastNoticeCardCount(), 1);
	TestEqual(TEXT("HUD 없음 → 소리 0"), Backend->GetNoticeSoundCount(), 0);

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	UHuntHudWidget* const Hunt = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	URanchHudWidget* const Ranch = CreateWidget<URanchHudWidget>(World, URanchHudWidget::StaticClass());
	if (TestNotNull(TEXT("사냥 HUD"), Hunt) && TestNotNull(TEXT("목장 HUD"), Ranch))
	{
		Hunt->TakeWidget();
		Ranch->TakeWidget();
		Hunt->BindToBackend(Backend);
		Ranch->BindToBackend(Backend);
		USnareNoticeCards* const HuntCards = Hunt->GetAchievementCards();
		USnareNoticeCards* const RanchCards = Ranch->GetAchievementCards();
		if (!TestNotNull(TEXT("사냥 HUD 카드 두 장 칸"), HuntCards) || !TestNotNull(TEXT("목장 HUD 카드 두 장 칸"), RanchCards))
		{
			Hunt->BindToBackend(nullptr);
			Ranch->BindToBackend(nullptr);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			return false;
		}
		TestEqual(TEXT("답 전엔 사냥 카드 0장"), HuntCards->GetShownCardCount(), 0);
		TestTrue(TEXT("사냥 카드는 알림 줄 안"), HuntCards->GetParent() == Hunt->WidgetTree->FindWidget(TEXT("NoticeStack")));

		// ② 같은 답 → 카드·소리 0.
		Backend->ApplyCodexReply(CodexReply({ TEXT("codex.test_a"), TEXT("codex.test_b") }, {}));
		TestEqual(TEXT("같은 답 → 소리 0"), Backend->GetNoticeSoundCount(), 0);
		TestEqual(TEXT("같은 답 → 사냥 카드 0장"), HuntCards->GetShownCardCount(), 0);

		// ③ 새 업적 1 + 새 도감 칸 1 → 카드 2 · 소리 1(업적이 이김) · 두 HUD 모두 같은 글.
		TestTrue(TEXT("업적·도감 답 받아들임"), Backend->ApplyCodexReply(CodexReply(
			{ TEXT("codex.test_a"), TEXT("codex.test_b"), TEXT("codex.test_c") }, { TEXT("ach.test_wire") })));
		TestEqual(TEXT("새 칸 2 → 카드 2"), Backend->GetLastNoticeCardCount(), 2);
		TestEqual(TEXT("한 답 → 소리 1"), Backend->GetNoticeSoundCount(), 1);
		TestEqual(TEXT("업적이 있으면 업적 소리"), Backend->GetLastNoticeCue(), FName(TEXT("Achievement")));
		// 카드 두 장 따로 — 첫째 = 업적(금 점), 둘째 = 도감(빨강 점). 두 HUD 가 같은 글.
		for (USnareNoticeCards* const Stack : { HuntCards, RanchCards })
		{
			TestEqual(TEXT("카드 2장 펴짐"), Stack->GetShownCardCount(), 2);
			if (!Stack->GetTitle(1) || !Stack->GetBody(1) || !Stack->GetTag(1))
			{
				AddError(TEXT("카드 칸이 비었다"));
				continue;
			}
			TestEqual(TEXT("첫째 카드 제목"), Stack->GetTitle(0)->GetText().ToString(), FString(TEXT("업적 달성!")));
			TestEqual(TEXT("첫째 카드 설명"), Stack->GetBody(0)->GetText().ToString(), FString(TEXT("ach.test_wire")));
			TestEqual(TEXT("둘째 카드 제목"), Stack->GetTitle(1)->GetText().ToString(), FString(TEXT("새 발견!")));
			TestEqual(TEXT("둘째 카드 설명"), Stack->GetBody(1)->GetText().ToString(), FString(TEXT("codex.test_c — 도감이 갱신됨")));
			TestTrue(TEXT("첫째 「!」 = 금"), Stack->GetTag(0)->GetColorAndOpacity().GetSpecifiedColor().Equals(USnareNoticeCards::DotColor(true)));
			TestTrue(TEXT("둘째 「!」 = 빨강"), Stack->GetTag(1)->GetColorAndOpacity().GetSpecifiedColor().Equals(USnareNoticeCards::DotColor(false)));
			TestEqual(TEXT("꼬리표 글 = 「!」"), Stack->GetTag(0)->GetText().ToString(), FString(TEXT("!")));
			// 원본 카드 테두리 2px 는 점 색과 같다(INBOX 보정 10-08 05:10) — 테두리 색 = 「!」 색, 바탕은 크림 그대로.
			for (int32 CardIndex = 0; CardIndex < 2; ++CardIndex)
			{
				const FSlateBrush& Brush = Stack->GetCard(CardIndex)->Background;
				TestTrue(TEXT("테두리 색 = 「!」 색"), Brush.OutlineSettings.Color.GetSpecifiedColor().Equals(
					Stack->GetTag(CardIndex)->GetColorAndOpacity().GetSpecifiedColor()));
				TestEqual(TEXT("테두리 두께 2"), Brush.OutlineSettings.Width, USnareNoticeCards::OutlineWidth);
				TestTrue(TEXT("카드 바탕 = 크림"), Brush.TintColor.GetSpecifiedColor().Equals(SnareUiStyle::UiPanelCream()));
			}
			TestFalse(TEXT("금 ≠ 빨강"), USnareNoticeCards::DotColor(true).Equals(USnareNoticeCards::DotColor(false)));
			TestFalse(TEXT("카드 한 칸에 두 줄로 몰지 않는다"), Stack->GetBody(0)->GetText().ToString().Contains(TEXT("\n")));
			// 왼쪽 아이콘 48 — 업적 = 코인 그림이 펴짐, 그림 없는 시험 도감 칸 = 접힘(빈 네모 0).
			const UImage* const AchIcon = Stack->GetIcon(0);
			const UImage* const CodexIcon = Stack->GetIcon(1);
			if (TestNotNull(TEXT("첫째 아이콘 칸"), AchIcon) && TestNotNull(TEXT("둘째 아이콘 칸"), CodexIcon))
			{
				TestTrue(TEXT("업적 카드 아이콘 펴짐"), AchIcon->GetVisibility() != ESlateVisibility::Collapsed);
				TestTrue(TEXT("아이콘 칸 48"), FVector2D(AchIcon->GetBrush().ImageSize).Equals(FVector2D(USnareNoticeCards::IconSize)));
				TestEqual(TEXT("아이콘 = 48"), USnareNoticeCards::IconSize, 48.f);
				const UObject* const Shown = AchIcon->GetBrush().GetResourceObject();
				TestTrue(TEXT("업적 아이콘 = 코인 그림"), Shown != nullptr
					&& Shown->GetPathName() == FString(SnareAchievementNotice::AchievementIconPath));
				TestTrue(TEXT("그림 없는 도감 칸 → 아이콘 접힘"), CodexIcon->GetVisibility() == ESlateVisibility::Collapsed);
			}
		}
		TestTrue(TEXT("사냥 알림 줄 한 칸엔 안 몰림"), Hunt->GetCaptureBannerText().IsEmpty());

		// ④ 도감만 새로 → 도감 소리, 소리는 답마다 한 번.
		Backend->ApplyCodexReply(CodexReply(
			{ TEXT("codex.test_a"), TEXT("codex.test_b"), TEXT("codex.test_c"), TEXT("codex.test_d") }, { TEXT("ach.test_wire") }));
		TestEqual(TEXT("다음 답 → 소리 2"), Backend->GetNoticeSoundCount(), 2);
		TestEqual(TEXT("도감만 → 도감 소리"), Backend->GetLastNoticeCue(), FName(TEXT("CodexNew")));
		TestEqual(TEXT("새 답 1장 → 둘째 카드 접힘"), HuntCards->GetShownCardCount(), 1);
		if (HuntCards->GetTag(0))
		{
			TestTrue(TEXT("도감 한 장 → 첫째 점 빨강"), HuntCards->GetTag(0)->GetColorAndOpacity().GetSpecifiedColor().Equals(USnareNoticeCards::DotColor(false)));
			TestTrue(TEXT("도감 한 장 → 첫째 테두리도 빨강(금에서 바뀜)"), HuntCards->GetCard(0)->Background.OutlineSettings.Color
				.GetSpecifiedColor().Equals(USnareNoticeCards::DotColor(false)));
		}
		// 도감 카드도 그림이 있으면 아이콘이 펴지고, 깨진 경로면 접힌다(빈 네모 0).
		{
			SnareAchievementNotice::FCard WithIcon;
			WithIcon.Title = FText::FromString(TEXT("새 발견!"));
			WithIcon.Body = FText::FromString(TEXT("시험"));
			WithIcon.Icon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(SnareAchievementNotice::AchievementIconPath));
			SnareAchievementNotice::FCard Broken = WithIcon;
			Broken.Icon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Test/T_NoSuchIcon.T_NoSuchIcon")));
			HuntCards->ShowCards({ WithIcon, Broken }, SnareAchievementNotice::CardSeconds);
			if (HuntCards->GetIcon(0) && HuntCards->GetIcon(1))
			{
				TestTrue(TEXT("그림 있는 도감 카드 → 아이콘 펴짐"), HuntCards->GetIcon(0)->GetVisibility() != ESlateVisibility::Collapsed);
				TestTrue(TEXT("깨진 그림 경로 → 아이콘 접힘"), HuntCards->GetIcon(1)->GetVisibility() == ESlateVisibility::Collapsed);
			}
		}
		HuntCards->HideCards();
		TestEqual(TEXT("시간이 다 되면 카드 0장"), HuntCards->GetShownCardCount(), 0);

		// ⑤ 다른 계정의 첫 답은 다시 기준만 잡는다.
		FSnarePlayerSnapshot Other;
		Other.PlayFabId = TEXT("PFTESTOTHER");
		Other.FetchedAtUtc = FDateTime::UtcNow();
		Backend->ApplyPlayerSnapshot(Other, /*bFromBackend=*/true);
		Backend->ApplyCodexReply(CodexReply({ TEXT("codex.test_z") }, { TEXT("ach.test_other") }));
		TestEqual(TEXT("다른 계정 첫 답 → 소리 그대로"), Backend->GetNoticeSoundCount(), 2);

		Hunt->BindToBackend(nullptr);
		Ranch->BindToBackend(nullptr);
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
