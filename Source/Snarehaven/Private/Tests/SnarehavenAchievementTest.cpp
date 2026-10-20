// ROADMAP M12-04·M12-06 2층 검사 — 업적 표가 **설계서·서버와 같은가**, 화면이 **스냅샷만 읽는가**.
// 이 검사가 잡는 것: ① DT_AchievementTable 과 DESIGN 8.15 가 어긋나는 것 ② 서버 `ACHIEVEMENTS` 와
// ID·카운터·목표가 어긋나 화면이 서버가 모르는 업적을 그리는 것 ③ 화면이 진행 수로 "달성" 을 지어내는 것
// (보상은 서버만 준다 — CLAUDE.md 8).
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Data/AchievementRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UI/AchievementScreenWidget.h"
#include "UI/CodexScreenWidget.h"
#include "UI/SnareUiStyle.h"

namespace
{
	const TCHAR* AchievementTablePath = TEXT("/Game/Game/Data/Achievements/DT_AchievementTable.DT_AchievementTable");

	/** 업적 한 줄에서 맞대는 것 — ID → `카운터:목표:코인`. 표의 코인은 알림 카드 글 전용(M48-03b)이라 서버 값과 같아야 한다. */
	using FAchSpecMap = TMap<FString, FString>;

	FString Spec(const FString& Counter, int32 Target, int32 Coins)
	{
		return FString::Printf(TEXT("%s:%d:%d"), *Counter, Target, Coins);
	}

	/** DESIGN 8.15 — `| 이름 | `ach.x` | `counter` | 목표 | 코인 | 근거 |`. */
	FAchSpecMap ParseDesign(const FString& Doc, TMap<FString, FString>& OutNames)
	{
		FAchSpecMap Out;
		const int32 Start = Doc.Find(TEXT("### 8.15"), ESearchCase::CaseSensitive);
		if (Start == INDEX_NONE)
		{
			return Out;
		}
		const int32 End = Doc.Find(TEXT("\n#"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + 8);
		const FString Section = End == INDEX_NONE ? Doc.Mid(Start) : Doc.Mid(Start, End - Start);

		const FRegexPattern Row(TEXT("^\\| ([^|]+?) \\| `(ach\\.[a-z0-9_]+)` \\| `([A-Za-z]+)` \\| (\\d+) \\| (\\d+) \\|"));
		TArray<FString> Lines;
		Section.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			FRegexMatcher M(Row, Line);
			if (M.FindNext())
			{
				Out.Add(M.GetCaptureGroup(2), Spec(M.GetCaptureGroup(3), FCString::Atoi(*M.GetCaptureGroup(4)),
					FCString::Atoi(*M.GetCaptureGroup(5))));
				OutNames.Add(M.GetCaptureGroup(2), M.GetCaptureGroup(1));
			}
		}
		return Out;
	}

	/** 서버 `ACHIEVEMENTS` — `{ id: "ach.x", counter: "c", target: n, coins: m }` 한 줄에 하나. */
	FAchSpecMap ParseServer(const FString& Script)
	{
		FAchSpecMap Out;
		const FRegexPattern Row(TEXT("\\{ id: \"(ach\\.[a-z0-9_]+)\", counter: \"([A-Za-z]+)\", target: (\\d+), coins: (\\d+) \\}"));
		FRegexMatcher M(Row, Script);
		while (M.FindNext())
		{
			Out.Add(M.GetCaptureGroup(1), Spec(M.GetCaptureGroup(2), FCString::Atoi(*M.GetCaptureGroup(3)),
				FCString::Atoi(*M.GetCaptureGroup(4))));
		}
		return Out;
	}

	FAchSpecMap ReadTable(const UDataTable& Table, TMap<FString, FString>& OutNames)
	{
		FAchSpecMap Out;
		for (const TPair<FName, uint8*>& Pair : Table.GetRowMap())
		{
			if (const FAchievementRow* const Row = reinterpret_cast<const FAchievementRow*>(Pair.Value))
			{
				Out.Add(Row->AchievementID.ToString(), Spec(Row->Counter.ToString(), Row->Target, Row->RewardCoins));
				OutNames.Add(Row->AchievementID.ToString(), Row->DisplayName.ToString());
			}
		}
		return Out;
	}

	/** 두 목록의 차이를 사람이 읽을 문장으로. 비었으면 같다. */
	TArray<FString> CollectIssues(const FAchSpecMap& Expected, const FAchSpecMap& Actual, const TCHAR* What)
	{
		TArray<FString> Issues;
		for (const TPair<FString, FString>& Pair : Expected)
		{
			const FString* const Found = Actual.Find(Pair.Key);
			if (Found == nullptr)
			{
				Issues.Add(FString::Printf(TEXT("%s 에 %s 가 없다"), What, *Pair.Key));
			}
			else if (*Found != Pair.Value)
			{
				Issues.Add(FString::Printf(TEXT("%s 의 %s = %s (기대 %s)"), What, *Pair.Key, **Found, *Pair.Value));
			}
		}
		for (const TPair<FString, FString>& Pair : Actual)
		{
			if (!Expected.Contains(Pair.Key))
			{
				Issues.Add(FString::Printf(TEXT("%s 에만 있는 %s"), What, *Pair.Key));
			}
		}
		return Issues;
	}

	bool LoadDesign(FAutomationTestBase& Test, FAchSpecMap& Out, TMap<FString, FString>& Names)
	{
		FString Doc;
		if (!Test.TestTrue(TEXT("DESIGN.md 읽기"),
			FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
		{
			return false;
		}
		Out = ParseDesign(Doc, Names);
		return Test.TestTrue(TEXT("DESIGN 8.15 업적 줄 10개 이상"), Out.Num() >= 10);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAchievementDesignTest, "Snarehaven.Achievement.Table.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAchievementDesignTest::RunTest(const FString& Parameters)
{
	FAchSpecMap Design;
	TMap<FString, FString> DesignNames;
	if (!LoadDesign(*this, Design, DesignNames))
	{
		return false;
	}
	UDataTable* const Table = LoadObject<UDataTable>(nullptr, AchievementTablePath);
	if (!TestNotNull(TEXT("DT_AchievementTable 로드"), Table))
	{
		return false;
	}

	TMap<FString, FString> TableNames;
	const FAchSpecMap FromTable = ReadTable(*Table, TableNames);
	for (const FString& Issue : CollectIssues(Design, FromTable, TEXT("DT_AchievementTable")))
	{
		AddError(Issue);
	}
	for (const TPair<FString, FString>& Pair : DesignNames)
	{
		const FString* const Name = TableNames.Find(Pair.Key);
		TestEqual(FString::Printf(TEXT("%s 이름"), *Pair.Key), Name != nullptr ? *Name : FString(), Pair.Value);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAchievementServerTest, "Snarehaven.Achievement.Table.MatchesServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAchievementServerTest::RunTest(const FString& Parameters)
{
	FAchSpecMap Design;
	TMap<FString, FString> Names;
	if (!LoadDesign(*this, Design, Names))
	{
		return false;
	}
	FString Script;
	if (!TestTrue(TEXT("cloudscript_main.js 읽기"), FFileHelper::LoadFileToString(Script,
		*(FPaths::ProjectDir() / TEXT("tools/playfab/cloudscript_main.js")))))
	{
		return false;
	}

	for (const FString& Issue : CollectIssues(Design, ParseServer(Script), TEXT("서버 ACHIEVEMENTS")))
	{
		AddError(Issue);
	}

	// 역검증 — 목표 하나를 바꾼 사본은 걸려야 한다(검사가 빈 통과를 하지 않는지).
	const FString Tampered = Script.Replace(TEXT("counter: \"capture\", target: 10,"), TEXT("counter: \"capture\", target: 11,"));
	TestTrue(TEXT("목표를 바꾼 서버 사본은 걸린다"), CollectIssues(Design, ParseServer(Tampered), TEXT("사본")).Num() > 0);

	// M48-03b — 표의 표시 전용 코인도 서버 `coins` 와 같아야 한다(카드 「+N 코인」 이 실제 지급과 어긋나지 않게).
	UDataTable* const Table = LoadObject<UDataTable>(nullptr, AchievementTablePath);
	if (TestNotNull(TEXT("DT_AchievementTable 로드"), Table))
	{
		TMap<FString, FString> TableNames;
		for (const FString& Issue : CollectIssues(ParseServer(Script), ReadTable(*Table, TableNames), TEXT("DT_AchievementTable(서버 대비)")))
		{
			AddError(Issue);
		}
	}
	const FString CoinTampered = Script.Replace(TEXT("target: 1, coins: 10 }"), TEXT("target: 1, coins: 11 }"));
	TestTrue(TEXT("코인을 바꾼 서버 사본은 걸린다"), CollectIssues(Design, ParseServer(CoinTampered), TEXT("사본")).Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAchievementScreenTest, "Snarehaven.Achievement.Screen.ReadsSnapshotOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAchievementScreenTest::RunTest(const FString& Parameters)
{
	UDataTable* const Table = LoadObject<UDataTable>(nullptr, AchievementTablePath);
	if (!TestNotNull(TEXT("DT_AchievementTable 로드"), Table))
	{
		return false;
	}

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	UAchievementScreenWidget* const Screen = CreateWidget<UAchievementScreenWidget>(World, UAchievementScreenWidget::StaticClass());
	if (TestNotNull(TEXT("업적 화면 위젯"), Screen))
	{
		const TSharedRef<SWidget> Slate = Screen->TakeWidget();
		Screen->SetSourceTable(Table);
		const int32 Total = Table->GetRowMap().Num();

		// ① 백엔드 없음 — 0 이 아니라 모른다.
		TestEqual(TEXT("백엔드 전 머리글"), Screen->GetProgressText().ToString(),
			FString::Printf(TEXT("업적 - / %d"), Total));
		TestTrue(TEXT("백엔드 전 진행 수는 -"), Screen->BuildLines().Num() == Total
			&& Screen->BuildLines()[0].Text.ToString().Contains(TEXT(" -/")));

		// ② 카운터가 목표를 한참 넘어도 `done` 이 비었으면 달성이 아니다.
		USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
		Screen->BindToBackend(Backend);
		FSnarePlayerSnapshot Snapshot;
		Snapshot.PlayFabId = TEXT("PFTESTOWNER");
		Snapshot.FetchedAtUtc = FDateTime::UtcNow();
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			Snapshot.Achievements.Counters.Add(reinterpret_cast<const FAchievementRow*>(Pair.Value)->Counter, 9999);
		}
		Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);
		TestEqual(TEXT("카운터만 넘친 머리글"), Screen->GetProgressText().ToString(),
			FString::Printf(TEXT("업적 0 / %d"), Total));
		for (const FAchievementLineView& Line : Screen->BuildLines())
		{
			TestFalse(FString::Printf(TEXT("달성 기록 없으면 미달 — %s"), *Line.Text.ToString()), Line.bDone);
		}

		// ③ 서버가 적은 ID 만 달성.
		Snapshot.Achievements.Completed = { FName(TEXT("ach.capture_1")) };
		Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);
		int32 DoneLines = 0;
		for (const FAchievementLineView& Line : Screen->BuildLines())
		{
			DoneLines += Line.bDone ? 1 : 0;
		}
		TestEqual(TEXT("달성 줄 = 서버 done 개수"), DoneLines, 1);
		TestEqual(TEXT("달성 머리글"), Screen->GetProgressText().ToString(), FString::Printf(TEXT("업적 1 / %d"), Total));
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// ROADMAP M13-31 — 업적 화면에 도감과 **같은 값의** 전체 화면 판이 있고 글자가 전부 그 판 안에 있는가.
// 판이 없으면 글자가 월드·HUD 위에 바로 떠서 풀밭 위 `[ ]` 줄이 안 읽히고 뒤 HUD 버튼이 눌린다(감독 3층 #0214).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAchievementPanelTest, "Snarehaven.Achievement.Screen.SitsOnCodexPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAchievementPanelTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	UAchievementScreenWidget* const Screen = CreateWidget<UAchievementScreenWidget>(World, UAchievementScreenWidget::StaticClass());
	UCodexScreenWidget* const Codex = CreateWidget<UCodexScreenWidget>(World, UCodexScreenWidget::StaticClass());
	if (TestNotNull(TEXT("업적 화면 위젯"), Screen) && TestNotNull(TEXT("도감 화면 위젯"), Codex))
	{
		Screen->TakeWidget();
		Codex->TakeWidget();
		auto Find = [](UUserWidget* Widget, const TCHAR* Name)
		{
			return Widget->WidgetTree != nullptr ? Cast<UBorder>(Widget->WidgetTree->FindWidget(Name)) : nullptr;
		};
		// 실제로 칠해진 불투명도 = 테두리 색 × 브러시 색 (바깥은 `SetBrushColor`, 판은 브러시 색으로 칠한다).
		auto Alpha = [](const UBorder* Border)
		{
			return Border != nullptr ? Border->GetBrushColor().A * Border->Background.TintColor.GetSpecifiedColor().A : -1.f;
		};
		UBorder* const Backdrop = Find(Screen, TEXT("AchievementBackdrop"));
		UBorder* const Panel = Find(Screen, TEXT("AchievementPanel"));
		const UBorder* const CodexBackdrop = Find(Codex, TEXT("CodexBackdrop"));
		const UBorder* const CodexPanel = Find(Codex, TEXT("CodexPanel"));
		const UWidget* const Root = Screen->WidgetTree != nullptr ? Screen->WidgetTree->RootWidget : nullptr;
		AddInfo(FString::Printf(TEXT("업적 바깥 %.2f 판 %.2f / 도감 바깥 %.2f 판 %.2f"),
			Alpha(Backdrop), Alpha(Panel), Alpha(CodexBackdrop), Alpha(CodexPanel)));

		TestTrue(TEXT("업적 뿌리에 바깥 어둡게 판이 있다"), Backdrop != nullptr && Backdrop->GetParent() == Root);
		TestTrue(TEXT("업적 뿌리에 판이 있다"), Panel != nullptr && Panel->GetParent() == Root);
		TestTrue(TEXT("바깥 어둡게 불투명도가 도감과 같다"),
			CodexBackdrop != nullptr && FMath::IsNearlyEqual(Alpha(Backdrop), Alpha(CodexBackdrop)));
		TestTrue(TEXT("판 불투명도가 도감과 같다"), CodexPanel != nullptr && FMath::IsNearlyEqual(Alpha(Panel), Alpha(CodexPanel)));
		TestTrue(TEXT("판 모서리가 도감과 같다"), Panel != nullptr && CodexPanel != nullptr
			&& Panel->Background.OutlineSettings.CornerRadii == CodexPanel->Background.OutlineSettings.CornerRadii);
		TestTrue(TEXT("바깥·판이 눌림을 먹는다(뒤 HUD 버튼이 안 눌린다)"), Backdrop != nullptr && Panel != nullptr
			&& Backdrop->GetVisibility() == ESlateVisibility::Visible && Panel->GetVisibility() == ESlateVisibility::Visible);

		// 글자가 판 **안**이다 — 부모를 거슬러 올라가 판을 만나야 한다.
		for (const TCHAR* Name : {TEXT("TitleText"), TEXT("ProgressText"), TEXT("LineList"), TEXT("CloseButton")})
		{
			const UWidget* Widget = Screen->WidgetTree != nullptr ? Screen->WidgetTree->FindWidget(Name) : nullptr;
			TestNotNull(FString::Printf(TEXT("%s 있다"), Name), Widget);
			while (Widget != nullptr && Widget != Panel)
			{
				Widget = Widget->GetParent();
			}
			TestTrue(FString::Printf(TEXT("%s 가 판의 자손이다"), Name), Panel != nullptr && Widget == Panel);
		}
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}


// ROADMAP M25-03 — 업적 줄 달성 표시가 `[O]`·`[ ]` 글자가 아니라 줄 왼쪽 원판이다(시트 「`[ ]` 대신 원판」).
// 원판 색은 서버 `done` 하나로만 정한다 — 카운터가 넘쳐도 미달 원판(CLAUDE.md 8).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAchievementDiscTest, "Snarehaven.Achievement.Screen.RowsShowDiscs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAchievementDiscTest::RunTest(const FString& Parameters)
{
	UDataTable* const Table = LoadObject<UDataTable>(nullptr, AchievementTablePath);
	if (!TestNotNull(TEXT("DT_AchievementTable 로드"), Table))
	{
		return false;
	}

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	UAchievementScreenWidget* const Screen = CreateWidget<UAchievementScreenWidget>(World, UAchievementScreenWidget::StaticClass());
	if (TestNotNull(TEXT("업적 화면 위젯"), Screen))
	{
		Screen->TakeWidget();
		Screen->SetSourceTable(Table);
		USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
		Screen->BindToBackend(Backend);
		FSnarePlayerSnapshot Snapshot;
		Snapshot.PlayFabId = TEXT("PFTESTOWNER");
		Snapshot.FetchedAtUtc = FDateTime::UtcNow();
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			Snapshot.Achievements.Counters.Add(reinterpret_cast<const FAchievementRow*>(Pair.Value)->Counter, 9999);
		}
		Snapshot.Achievements.Completed = { FName(TEXT("ach.capture_1")) };
		Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);

		const TArray<FAchievementLineView> Lines = Screen->BuildLines();
		const FColor Mint = SnareUiStyle::UiIconMint().ToFColorSRGB();
		const FColor Empty = SnareUiStyle::UiEmptySlot().ToFColorSRGB();
		int32 Discs = 0;
		int32 MintDiscs = 0;
		int32 Brackets = 0;
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			const UBorder* const Disc = Screen->WidgetTree->FindWidget<UBorder>(FName(*FString::Printf(TEXT("AchievementDisc_%d"), Index)));
			const UTextBlock* const Text = Screen->WidgetTree->FindWidget<UTextBlock>(FName(*FString::Printf(TEXT("AchievementLine_%d"), Index)));
			if (!TestNotNull(FString::Printf(TEXT("줄 %d 원판"), Index), Disc) || !TestNotNull(FString::Printf(TEXT("줄 %d 글"), Index), Text))
			{
				continue;
			}
			++Discs;
			const FString Shown = Text->GetText().ToString();
			Brackets += Shown.Contains(TEXT("[")) ? 1 : 0;
			TestEqual(FString::Printf(TEXT("줄 %d 글 = 줄 보기"), Index), Shown, Lines[Index].Text.ToString());
			const FColor DiscColor = Disc->Background.TintColor.GetSpecifiedColor().ToFColorSRGB();
			MintDiscs += DiscColor == Mint ? 1 : 0;
			TestTrue(FString::Printf(TEXT("줄 %d 원판 색 = %s"), Index, Lines[Index].bDone ? TEXT("민트") : TEXT("빈 칸 색")),
				DiscColor == (Lines[Index].bDone ? Mint : Empty));
			TestTrue(FString::Printf(TEXT("줄 %d 원판은 둥글다"), Index), Disc->Background.DrawAs == ESlateBrushDrawType::RoundedBox);
		}
		TestEqual(TEXT("원판 수 = 업적 줄 수"), Discs, Lines.Num());
		TestEqual(TEXT("줄 글자에 [ 0"), Brackets, 0);
		TestEqual(TEXT("민트 원판 = 서버 done 개수"), MintDiscs, 1);
		AddInfo(FString::Printf(TEXT("[AchievementDiscs] 줄 %d · 원판 %d · 민트 %d · 글자 [ %d"), Lines.Num(), Discs, MintDiscs, Brackets));
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// ROADMAP M38-03 — 업적·도감 판 닫기 버튼(도감은 같은 줄 「업적」 버튼도)이 손가락 한 마디(DESIGN 2 「🟡 누르는 칸 최소 크기」 72)
// 이상이고, 키운 버튼이 아래 진행률·목록·보상 줄과 겹치지 않는다. 잰 길 = 캔버스 칸 크기(두 판 다 고정 크기 칸).

#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBoxSlot.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenScreenCloseTouchSizeTest, "Snarehaven.UI.TouchSize.ScreenCloseButtons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenScreenCloseTouchSizeTest::RunTest(const FString& Parameters)
{
	constexpr float DesignMinTouch = 72.f;
	TestTrue(TEXT("ScreenCloseButtonHeight ≥ DESIGN 바닥 72"), SnareUiStyle::ScreenCloseButtonHeight >= DesignMinTouch);

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	// 가운데 닻(0.5, 0)·정렬(0.5, 0) 칸의 사각형 — 같은 캔버스 안 칸끼리만 비교한다.
	auto CenteredRect = [](const UWidget* Widget)
	{
		const UCanvasPanelSlot* const CanvasSlot = Widget != nullptr ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
		if (CanvasSlot == nullptr)
		{
			return FBox2D(ForceInit);
		}
		const FVector2D Pos = CanvasSlot->GetPosition();
		const FVector2D Size = CanvasSlot->GetSize();
		return FBox2D(FVector2D(Pos.X - Size.X * 0.5f, Pos.Y), FVector2D(Pos.X + Size.X * 0.5f, Pos.Y + Size.Y));
	};
	auto Overlaps = [](const FBox2D& A, const FBox2D& B)
	{
		return A.Min.X < B.Max.X && B.Min.X < A.Max.X && A.Min.Y < B.Max.Y && B.Min.Y < A.Max.Y;
	};

	UAchievementScreenWidget* const Screen = CreateWidget<UAchievementScreenWidget>(World, UAchievementScreenWidget::StaticClass());
	if (TestNotNull(TEXT("업적 화면 위젯"), Screen))
	{
		Screen->TakeWidget();
		const UWidget* const Close = Screen->WidgetTree->FindWidget(TEXT("CloseButton"));
		const FBox2D CloseRect = CenteredRect(Close);
		const FBox2D ProgressRect = CenteredRect(Screen->WidgetTree->FindWidget(TEXT("ProgressText")));
		const FBox2D ListRect = CenteredRect(Screen->WidgetTree->FindWidget(TEXT("LineList")));
		AddInfo(FString::Printf(TEXT("[ScreenCloseTouch] 업적 닫기 (%.0f,%.0f)~(%.0f,%.0f) · 진행률 (%.0f,%.0f)~(%.0f,%.0f) · 목록 위 %.0f"),
			CloseRect.Min.X, CloseRect.Min.Y, CloseRect.Max.X, CloseRect.Max.Y,
			ProgressRect.Min.X, ProgressRect.Min.Y, ProgressRect.Max.X, ProgressRect.Max.Y, ListRect.Min.Y));
		TestTrue(TEXT("업적 닫기 칸이 잡힌다"), CloseRect.bIsValid && ProgressRect.bIsValid && ListRect.bIsValid);
		TestTrue(TEXT("업적 닫기 높이 ≥ 72"), CloseRect.GetSize().Y >= DesignMinTouch);
		TestTrue(TEXT("업적 닫기 짧은 변 ≥ 72"), FMath::Min(CloseRect.GetSize().X, CloseRect.GetSize().Y) >= DesignMinTouch);
		TestFalse(TEXT("업적 닫기가 진행률 줄과 안 겹친다"), Overlaps(CloseRect, ProgressRect));
		TestFalse(TEXT("업적 닫기가 목록과 안 겹친다"), Overlaps(CloseRect, ListRect));
	}

	UCodexScreenWidget* const Codex = CreateWidget<UCodexScreenWidget>(World, UCodexScreenWidget::StaticClass());
	if (TestNotNull(TEXT("도감 화면 위젯"), Codex))
	{
		Codex->TakeWidget();
		const UWidget* const Row = Codex->WidgetTree->FindWidget(TEXT("TitleButtonRow"));
		const UCanvasPanelSlot* const RowSlot = Row != nullptr ? Cast<UCanvasPanelSlot>(Row->Slot) : nullptr;
		const UWidget* const Reward = Codex->WidgetTree->FindWidget(TEXT("RewardText"));
		const UCanvasPanelSlot* const RewardSlot = Reward != nullptr ? Cast<UCanvasPanelSlot>(Reward->Slot) : nullptr;
		if (TestNotNull(TEXT("도감 제목 버튼 줄 칸"), RowSlot) && TestNotNull(TEXT("도감 보상 줄 칸"), RewardSlot))
		{
			const float RowBottom = RowSlot->GetPosition().Y + RowSlot->GetSize().Y;
			const float RewardTop = RewardSlot->GetOffsets().Top;
			AddInfo(FString::Printf(TEXT("[ScreenCloseTouch] 도감 제목 버튼 줄 %.0f×%.0f · 위 %.0f · 아래 %.0f · 보상 줄 위 %.0f"),
				RowSlot->GetSize().X, RowSlot->GetSize().Y, RowSlot->GetPosition().Y, RowBottom, RewardTop));
			TestTrue(TEXT("도감 제목 버튼 줄 높이 ≥ 72"), RowSlot->GetSize().Y >= DesignMinTouch);
			TestTrue(TEXT("도감 제목 버튼 줄이 보상 줄 위에서 끝난다"), RowBottom <= RewardTop);
		}
		for (const TCHAR* Name : {TEXT("CodexCloseButton"), TEXT("AchievementButton")})
		{
			const UWidget* const Button = Codex->WidgetTree->FindWidget(Name);
			const UHorizontalBoxSlot* const BoxSlot = Button != nullptr ? Cast<UHorizontalBoxSlot>(Button->Slot) : nullptr;
			TestTrue(FString::Printf(TEXT("%s 가 줄 높이를 꽉 채운다"), Name),
				BoxSlot != nullptr && BoxSlot->GetVerticalAlignment() == VAlign_Fill);
		}
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
