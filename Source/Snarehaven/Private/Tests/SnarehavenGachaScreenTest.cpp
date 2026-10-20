// ROADMAP M7-04 2층 검사 — 뽑기 화면(`UGachaScreenWidget`)에 뜨는 글자가 **표에서 오는지**.
// 이 검사가 잡는 것: ① 등급 한 줄이 화면에서 빠진 것 ② 화면 숫자가 `DT_GachaRates` 값과 다른 것
// (반올림해 `7%` 로 적는 것도 포함 — 그게 곧 확률 오표시다, DESIGN 10.4) ③ 버튼에 적힌 비용이
// `DT_GachaConfig` 와 다른 것 ④ 표가 없을 때 0% 처럼 **거짓 숫자**를 보여 주는 것.
// 표 자체가 성한지(합 100%·미정 -1 없음)는 M7-01, 표가 공개 문서와 같은지는 M7-02 가 본다 —
// 이 파일은 그 사이의 마지막 한 칸, **표 → 화면**만 본다. 셋이 이어져야 화면 = 문서가 된다.
// 화면에서 글자가 실제로 읽히는 모양(네모가 아닌지·잘리지 않는지)은 3층(캡처)이 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/Button.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Data/ItemRow.h"
#include "Layout/ArrangedChildren.h"
#include "UI/SnareUiStyle.h"
#include "UI/SnareStoreSettings.h"
#include "Data/GachaCostRow.h"
#include "Data/GachaRateRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "UI/GachaScreenWidget.h"

namespace
{
	const TCHAR* GachaRateTablePath = TEXT("/Game/Game/Data/Gacha/DT_GachaRates.DT_GachaRates");
	const TCHAR* GachaConfigTablePath = TEXT("/Game/Game/Data/Gacha/DT_GachaConfig.DT_GachaConfig");

	// 화면 글자에서 읽어 낸 백분율과 표 값의 허용 오차. 표기가 소수 한 자리라 그보다 작은 차이는
	// 화면에 나타나지도 않는다.
	constexpr float PercentTolerance = 0.05f;

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

	/** 화면에 뜬 글자 전부를 한 덩어리로. 어느 위젯에 적혔든 "화면에 그 글자가 있다" 를 보는 눈이다. */
	FString CollectScreenText(const UUserWidget& Screen)
	{
		FString All;
		Screen.WidgetTree->ForEachWidget([&All](UWidget* Child)
		{
			if (const UTextBlock* const Text = Cast<UTextBlock>(Child))
			{
				All += Text->GetText().ToString();
				All += TEXT("\n");
			}
		});
		return All;
	}

	/**
	 * `커먼 70.0%` 같은 줄에서 백분율을 읽어 낸다. 화면 글자를 **다시 파싱**하는 이유는,
	 * 위젯이 쓴 함수를 그대로 불러 비교하면 같은 코드를 두 번 부르는 것이라 틀려도 같이 틀리기 때문이다.
	 * @param ScreenText 화면 글자 전부
	 * @param DisplayName 찾을 등급 이름 (`커먼`)
	 * @param OutPercent 읽어 낸 백분율
	 * @return 그 이름으로 시작하는 줄에서 `숫자%` 를 찾았으면 true
	 */
	bool ParsePercentFor(const FString& ScreenText, const FString& DisplayName, float& OutPercent)
	{
		TArray<FString> Lines;
		ScreenText.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			if (!Line.Contains(DisplayName))
			{
				continue;
			}

			int32 PercentIndex = INDEX_NONE;
			if (!Line.FindChar(TEXT('%'), PercentIndex))
			{
				return false;
			}

			// `%` 앞의 숫자·소수점만 거꾸로 훑어 잘라낸다.
			int32 Start = PercentIndex;
			while (Start > 0 && (FChar::IsDigit(Line[Start - 1]) || Line[Start - 1] == TEXT('.')))
			{
				--Start;
			}

			if (Start == PercentIndex)
			{
				return false;
			}

			OutPercent = FCString::Atof(*Line.Mid(Start, PercentIndex - Start));
			return true;
		}

		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGachaScreenTest, "Snarehaven.Gacha.Screen.ShowsRatesFromTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGachaScreenTest::RunTest(const FString& Parameters)
{
	const FScopedGemsEnabled GemsOn(true);   // 젬 화면을 보는 검사 — 1.0 설정(꺼짐)과 무관하게 켠다
	UDataTable* const RateTable = LoadObject<UDataTable>(nullptr, GachaRateTablePath);
	UDataTable* const ConfigTable = LoadObject<UDataTable>(nullptr, GachaConfigTablePath);
	if (!TestNotNull(TEXT("DT_GachaRates 로드"), RateTable) || !TestNotNull(TEXT("DT_GachaConfig 로드"), ConfigTable))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	UGachaScreenWidget* const Screen = CreateWidget<UGachaScreenWidget>(World, UGachaScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("뽑기 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}

	// Slate 위젯을 실제로 만들어야 `RebuildWidget` 이 돌고, 그때 자리표시 트리와 글자가 들어간다.
	// 반환값을 붙잡고 있어야 한다 — 놓으면 그 자리에서 해제된다 (`SnarehavenRanchHudTest` 와 같은 함정).
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	const FString ScreenText = CollectScreenText(*Screen);
	AddInfo(FString::Printf(TEXT("화면 글자:\n%s"), *ScreenText));

	// ① 표의 등급이 하나도 빠짐없이 화면에 있고, ② 화면 숫자가 표 값 그대로인지.
	int32 CheckedGrades = 0;
	for (const TPair<FName, uint8*>& Pair : RateTable->GetRowMap())
	{
		const FGachaRateRow* const Row = reinterpret_cast<const FGachaRateRow*>(Pair.Value);
		if (Row == nullptr)
		{
			continue;
		}

		++CheckedGrades;
		const FString DisplayName = Row->DisplayName.ToString();
		float ShownPercent = -1.0f;
		if (!TestTrue(*FString::Printf(TEXT("화면에 %s 등급 줄이 있고 백분율이 적혀 있다"), *DisplayName),
			ParsePercentFor(ScreenText, DisplayName, ShownPercent)))
		{
			continue;
		}

		AddInfo(FString::Printf(TEXT("%s - 화면 %.2f%% / 표 %.2f%%"), *DisplayName, ShownPercent, Row->ProbabilityPct));
		TestTrue(*FString::Printf(TEXT("%s 의 화면 확률(%.2f%%)이 표 값(%.2f%%)과 같다"),
			*DisplayName, ShownPercent, Row->ProbabilityPct),
			FMath::IsNearlyEqual(ShownPercent, Row->ProbabilityPct, PercentTolerance));
	}

	TestTrue(TEXT("표에서 본 등급이 하나 이상이다"), CheckedGrades > 0);

	// 반대 방향 — 화면 줄 수가 등급 수와 같은가. 위 반복은 **표에 있는 것**만 훑어서, 화면에 정체 모를
	// 다섯 번째 줄이 늘어도 모른다 (루프#91 이 목장 생산에서 겪은 구멍).
	const UEnum* const GradeEnum = StaticEnum<EGachaGrade>();
	if (TestNotNull(TEXT("EGachaGrade enum"), GradeEnum))
	{
		// 세로 상자를 아무거나 집으면 안 된다 — M7-05 가 결과 줄 상자(`ResultList`)를 같은 화면에
		// 더해서, 마지막에 만난 상자를 집으면 **뽑기 전이라 비어 있는 결과 칸**을 세게 된다.
		int32 RateRowCount = INDEX_NONE;
		Screen->WidgetTree->ForEachWidget([&RateRowCount](UWidget* Child)
		{
			const UVerticalBox* const Box = Cast<UVerticalBox>(Child);
			if (Box != nullptr && Box->GetFName() == TEXT("RateList"))
			{
				RateRowCount = Box->GetChildrenCount();
			}
		});
		// 마지막 항목은 UHT 가 붙이는 `_MAX` 라 뺀다.
		TestEqual(TEXT("화면 등급 줄 수가 EGachaGrade 항목 수와 같다"), RateRowCount, GradeEnum->NumEnums() - 1);
	}

	// ③ 버튼 비용도 표에서 온다 — 젬 100·코인 500·10연차 젬 900 을 코드가 아니라 표에서 읽어 맞댄다.
	const TArray<FName> ConfigRowNames = ConfigTable->GetRowNames();
	const FGachaCostRow* const Cost = ConfigRowNames.Num() > 0
		? ConfigTable->FindRow<FGachaCostRow>(ConfigRowNames[0], TEXT("GachaScreenTest")) : nullptr;
	if (TestNotNull(TEXT("비용 행"), Cost))
	{
		const FString SingleText = Screen->GetSinglePullButtonText().ToString();
		const FString CoinText = Screen->GetCoinPullButtonText().ToString();
		const FString TenText = Screen->GetTenPullButtonText().ToString();
		AddInfo(FString::Printf(TEXT("버튼 글자: [%s] [%s] [%s]"), *SingleText, *CoinText, *TenText));

		TestTrue(*FString::Printf(TEXT("1회 버튼에 젬 비용 %d 이 적혀 있다"), Cost->SinglePullGems),
			SingleText.Contains(FString::FromInt(Cost->SinglePullGems)));
		// M7-06: 코인 1회는 서버(`RollGacha` currency SC)가 따로 받으므로 버튼도 따로다.
		TestTrue(*FString::Printf(TEXT("코인 1회 버튼에 코인 비용 %d 이 적혀 있다"), Cost->SinglePullCoins),
			CoinText.Contains(FString::FromInt(Cost->SinglePullCoins)));
		TestTrue(*FString::Printf(TEXT("10연차 버튼에 젬 비용 %d 이 적혀 있다"), Cost->TenPullGems),
			TenText.Contains(FString::FromInt(Cost->TenPullGems)));
	}

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGachaScreenUnknownTest, "Snarehaven.Gacha.Screen.ShowsDashWhenTableMissing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGachaScreenUnknownTest::RunTest(const FString& Parameters)
{
	const FScopedGemsEnabled GemsOn(true);   // 젬 화면을 보는 검사 — 1.0 설정(꺼짐)과 무관하게 켠다
	UWorld* World = CreateTestWorld();
	UGachaScreenWidget* const Screen = CreateWidget<UGachaScreenWidget>(World, UGachaScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("뽑기 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 표를 떼어 낸다 — 에셋이 안 실렸을 때(포장 빌드에서 표를 빠뜨리는 사고)를 흉내 낸 것이다.
	// 그때 화면이 `0.0%` 나 `젬 0` 을 보여 주면 **없는 확률·공짜 가격을 광고하는 화면**이 된다.
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->SetSourceTables(nullptr, nullptr);
	const FString ScreenText = CollectScreenText(*Screen);
	const FString SingleText = Screen->GetSinglePullButtonText().ToString();
	AddInfo(FString::Printf(TEXT("표 없는 화면 글자:\n%s버튼 [%s]"), *ScreenText, *SingleText));

	TestFalse(TEXT("표가 없을 때 화면에 백분율(%) 이 뜨지 않는다"), ScreenText.Contains(TEXT("%")));
	TestTrue(TEXT("표가 없을 때 등급 줄이 `-` 로 뜬다"), ScreenText.Contains(TEXT("-")));
	TestTrue(TEXT("표가 없을 때 비용도 `-` 로 뜬다"), SingleText.Contains(TEXT("-")));
	TestFalse(TEXT("표가 없을 때 비용 0 을 보여 주지 않는다"), SingleText.Contains(TEXT("0")));
	const FString CoinText = Screen->GetCoinPullButtonText().ToString();
	TestTrue(TEXT("표가 없을 때 코인 비용도 `-` 로 뜬다"), CoinText.Contains(TEXT("-")));
	TestFalse(TEXT("표가 없을 때 코인 비용 0 을 보여 주지 않는다"), CoinText.Contains(TEXT("0")));

	DestroyTestWorld(World);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGachaSheetTest, "Snarehaven.Gacha.Tree.MatchesReferenceSheet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M15-18 — 뽑기 화면도 상점과 **같은 판 규격**이다 (시트 `docs/reference/shop_vendor.md` 「우리 것과 다르게 할 점」
// 뽑기 줄: 왼쪽 칸 = 등급 원판·이름·확률 + 사는-것 규격 뽑기 줄, 오른쪽 칸 = 기계 명함 + 천장·중복 환급 설명).
// 화면 숫자가 표에서 오는지는 위 `ShowsRatesFromTable` 이 본다 — 이 검사는 **모양**만 본다(두 검사가 겹치지 않게).
bool FSnarehavenGachaSheetTest::RunTest(const FString& Parameters)
{
	const FScopedGemsEnabled GemsOn(true);   // 젬 화면을 보는 검사 — 1.0 설정(꺼짐)과 무관하게 켠다
	// 시트가 적은 값 (검사 전용 사본 — 게임 규칙 숫자가 아니다).
	constexpr float SheetDiscSize = 52.f;
	constexpr float SheetDividerWidth = 4.f;
	constexpr float SheetPillRise = 20.f;
	constexpr float SheetPanelWidth = 600.f;
	constexpr float SheetPanelHeight = 600.f;
	constexpr float Tolerance = 0.5f;

	UWorld* World = CreateTestWorld();
	UGachaScreenWidget* const Screen = CreateWidget<UGachaScreenWidget>(World, UGachaScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("뽑기 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();

	TMap<TSharedRef<SWidget>, FArrangedWidget> Arranged;
	const FVector2D ViewSize(1920.f, 1080.f);
	auto Measure = [&Slate, &Arranged, &ViewSize]()
	{
		Arranged.Reset();
		Slate->SlatePrepass(1.f);
		TFunction<void(const FArrangedWidget&)> Walk = [&Walk, &Arranged](const FArrangedWidget& Node)
		{
			Arranged.Add(Node.Widget, Node);
			FArrangedChildren Children(EVisibility::Visible);
			Node.Widget->ArrangeChildren(Node.Geometry, Children);
			for (int32 Index = 0; Index < Children.Num(); ++Index)
			{
				Walk(Children[Index]);
			}
		};
		Walk(FArrangedWidget(Slate, FGeometry::MakeRoot(ViewSize, FSlateLayoutTransform())));
	};
	auto SheetRect = [&Arranged, Screen](const TCHAR* Name, FSlateRect& Out)
	{
		const UWidget* const Widget = Screen->WidgetTree->FindWidget(Name);
		const FArrangedWidget* const Node = Widget != nullptr && Widget->GetCachedWidget().IsValid()
			? Arranged.Find(Widget->GetCachedWidget().ToSharedRef()) : nullptr;
		if (Node != nullptr)
		{
			Out = Node->Geometry.GetLayoutBoundingRect();
		}
		return Node != nullptr;
	};
	Measure();

	// ── ① 판이 두 칸 ──
	FSlateRect Panel;
	FSlateRect Divider;
	FSlateRect Rates;
	FSlateRect Card;
	if (TestTrue(TEXT("두 칸 판 배치를 읽었다"), SheetRect(TEXT("GachaPanel"), Panel)
		&& SheetRect(TEXT("ColumnDividerBox"), Divider) && SheetRect(TEXT("RateList"), Rates)
		&& SheetRect(TEXT("MachineCard"), Card)))
	{
		AddInfo(FString::Printf(TEXT("[Gacha] 판 %.0fx%.0f · 구분선 폭 %.1f · 등급 칸 오른쪽 %.0f · 명함 왼쪽 %.0f"),
			Panel.GetSize().X, Panel.GetSize().Y, Divider.GetSize().X, Rates.Right, Card.Left));
		TestEqual(TEXT("판 크기 = 시트 600x600"), Panel.GetSize().X, SheetPanelWidth, Tolerance);
		TestEqual(TEXT("판 높이 = 시트 600"), Panel.GetSize().Y, SheetPanelHeight, Tolerance);
		TestEqual(TEXT("구분선 폭 = 시트 4px"), Divider.GetSize().X, SheetDividerWidth, Tolerance);
		TestTrue(TEXT("등급 줄이 구분선 왼쪽"), Rates.Right <= Divider.Left + Tolerance);
		TestTrue(TEXT("기계 명함이 구분선 오른쪽"), Card.Left >= Divider.Right - Tolerance);
	}

	// ── ② 왼쪽 칸 등급 줄 = 등급 원판(색) + 이름·확률. 원판 색은 **등급마다 달라야** 뜻이 있다 ──
	const UEnum* const GradeEnum = StaticEnum<EGachaGrade>();
	TSet<FString> DiscColors;
	if (TestNotNull(TEXT("EGachaGrade enum"), GradeEnum))
	{
		for (int32 Index = 0; Index < GradeEnum->NumEnums() - 1; ++Index)
		{
			const UBorder* const Disc = Cast<UBorder>(Screen->WidgetTree->FindWidget(
				*FString::Printf(TEXT("RateDisc_%d"), Index)));
			FSlateRect DiscRect;
			if (!TestNotNull(*FString::Printf(TEXT("등급 %d 원판"), Index), Disc)
				|| !SheetRect(*FString::Printf(TEXT("RateDiscBox_%d"), Index), DiscRect))
			{
				continue;
			}
			const FVector4 Radii = Disc->Background.OutlineSettings.CornerRadii;
			const FLinearColor Tint = Disc->Background.TintColor.GetSpecifiedColor();
			const FLinearColor Want = SnareUiStyle::ItemGradeColor(static_cast<EItemGrade>(Index));
			DiscColors.Add(Tint.ToFColor(true).ToHex());
			AddInfo(FString::Printf(TEXT("[Gacha] 등급 %d 원판 %.0fx%.0f · 모서리 %.0f · 색 %s (아이템 등급 색 %s)"),
				Index, DiscRect.GetSize().X, DiscRect.GetSize().Y, Radii.X,
				*Tint.ToFColor(true).ToHex(), *Want.ToFColor(true).ToHex()));
			TestEqual(TEXT("등급 원판 = 시트 52px"), DiscRect.GetSize().X, SheetDiscSize, Tolerance);
			TestEqual(TEXT("등급 원판 모서리 = 지름 절반(= 원)"), static_cast<float>(Radii.X), SheetDiscSize * 0.5f, Tolerance);
			// 배낭 칸과 **같은 색표**를 쓴다 — 같은 "레어" 가 화면마다 다른 색이면 등급이 뜻을 잃는다.
			TestEqual(TEXT("등급 원판 색 = 아이템 등급 색"), Tint.ToFColor(true).ToHex(), Want.ToFColor(true).ToHex());
		}
		TestEqual(TEXT("등급 수만큼 서로 다른 원판 색"), DiscColors.Num(), GradeEnum->NumEnums() - 1);
	}

	// ── ③ 뽑기 줄은 사는-것 규격 `가격(초록) > 이름` ──
	for (const TCHAR* Name : {TEXT("SinglePullButton"), TEXT("CoinPullButton"), TEXT("TenPullButton")})
	{
		FSlateRect Price;
		FSlateRect Arrow;
		FSlateRect Label;
		const FString Base(Name);
		if (!TestTrue(*FString::Printf(TEXT("%s 줄 배치"), Name),
			SheetRect(*(Base + TEXT("Price")), Price) && SheetRect(*(Base + TEXT("ArrowBox")), Arrow)
			&& SheetRect(*(Base + TEXT("Label")), Label)))
		{
			continue;
		}
		const UTextBlock* const PriceText = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(*(Base + TEXT("Price"))));
		const UTextBlock* const LabelText = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(*(Base + TEXT("Label"))));
		const FLinearColor Shown = PriceText != nullptr
			? PriceText->GetColorAndOpacity().GetSpecifiedColor() : FLinearColor::Black;
		AddInfo(FString::Printf(TEXT("[Gacha] %s - '%s' > '%s' · 가격 글 색 %s (시트 초록 %s)"), Name,
			PriceText != nullptr ? *PriceText->GetText().ToString() : TEXT("-"),
			LabelText != nullptr ? *LabelText->GetText().ToString() : TEXT("-"),
			*Shown.ToFColor(true).ToHex(), *SnareUiStyle::UiShopPriceGreen().ToFColor(true).ToHex()));
		TestTrue(TEXT("가격 > 이름 차례"), Price.Right <= Arrow.Left + Tolerance && Arrow.Right <= Label.Left + Tolerance);
		TestEqual(TEXT("가격 글이 크림(나무 위, M97-c-5)"), Shown.ToFColor(true).ToHex(),
			SnareUiStyle::UiPanelCream().ToFColor(true).ToHex());
		TestTrue(TEXT("이름 글이 비어 있지 않다"), LabelText != nullptr && !LabelText->GetText().IsEmpty());
		// 감독 캡처(957): 가격이 버튼 왼쪽 밖으로 나가고 문구보다 두 배 컸다.
		FSlateRect ButtonRect;
		if (TestTrue(*FString::Printf(TEXT("%s 버튼 배치"), Name), SheetRect(Name, ButtonRect)))
		{
			TestTrue(*FString::Printf(TEXT("%s 가격 왼쪽 끝이 버튼 안 (%.1f >= %.1f)"), Name, Price.Left, ButtonRect.Left),
				Price.Left >= ButtonRect.Left - Tolerance);
			TestTrue(*FString::Printf(TEXT("%s 문구 오른쪽 끝이 버튼 안"), Name), Label.Right <= ButtonRect.Right + Tolerance);
		}
		TestTrue(TEXT("가격 글자 크기 = 문구 크기"),
			PriceText != nullptr && LabelText != nullptr && PriceText->GetFont().Size == LabelText->GetFont().Size);
	}

	// ── ④ 오른쪽 칸 = 기계 명함(빨간 알약) + 천장 줄 + 편지체 본문 ──
	const UBorder* const NamePill = Cast<UBorder>(Screen->WidgetTree->FindWidget(TEXT("MachineNamePill")));
	const UTextBlock* const NameText = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(TEXT("MachineNameText")));
	const UTextBlock* const PityText = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(TEXT("MachinePityText")));
	const UTextBlock* const Letter = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(TEXT("MachineLetterText")));
	if (TestNotNull(TEXT("기계 이름 알약"), NamePill) && TestNotNull(TEXT("기계 이름"), NameText)
		&& TestNotNull(TEXT("천장 줄"), PityText) && TestNotNull(TEXT("명함 본문"), Letter))
	{
		const FString Body = Letter->GetText().ToString();
		AddInfo(FString::Printf(TEXT("[Gacha] 명함 - 알약 색 %s · 이름 '%s' · 천장 '%s' · 본문 %d자"),
			*NamePill->Background.TintColor.GetSpecifiedColor().ToFColor(true).ToHex(),
			*NameText->GetText().ToString(), *PityText->GetText().ToString(), Body.Len()));
		TestEqual(TEXT("이름 알약이 시트 빨강"), NamePill->Background.TintColor.GetSpecifiedColor().ToFColor(true).ToHex(),
			SnareUiStyle::UiAccentRed().ToFColor(true).ToHex());
		TestFalse(TEXT("기계 이름이 비어 있지 않다"), NameText->GetText().IsEmpty());
		TestTrue(TEXT("본문 끝이 서명 줄"), Body.Contains(TEXT("- ")));
		// 본문에 숫자를 적으면 표와 어긋난 보장을 광고하게 된다 (DESIGN 10.4) — 숫자는 천장 줄이 표에서 읽는다.
		bool bHasDigit = false;
		for (const TCHAR Ch : Body)
		{
			bHasDigit = bHasDigit || FChar::IsDigit(Ch);
		}
		TestFalse(TEXT("편지 본문에 숫자가 없다"), bHasDigit);
		TestTrue(TEXT("천장 줄이 회 수를 말한다"), PityText->GetText().ToString().Contains(TEXT("회")));
	}

	// 표를 떼면 천장 줄도 `-` 여야 한다 — 없는 보장을 적으면 그것이 곧 오표시다.
	Screen->SetSourceTables(nullptr, nullptr);
	const FString PityWithoutTable = Screen->GetPityLineText().ToString();
	AddInfo(FString::Printf(TEXT("[Gacha] 표 없을 때 천장 줄 '%s'"), *PityWithoutTable));
	TestTrue(TEXT("표가 없으면 천장 횟수가 `-`"), PityWithoutTable.Contains(TEXT("-")));
	TestFalse(TEXT("표가 없을 때 천장 0 을 보여 주지 않는다"), PityWithoutTable.Contains(TEXT("0")));

	// ── ⑤ 제목 탭이 판 위로 걸침 ──
	Measure();
	FSlateRect Pill;
	if (TestTrue(TEXT("제목 탭 배치"), SheetRect(TEXT("TitlePill"), Pill)))
	{
		const UBorder* const TitlePill = Cast<UBorder>(Screen->WidgetTree->FindWidget(TEXT("TitlePill")));
		AddInfo(FString::Printf(TEXT("[Gacha] 제목 탭 %.0fx%.0f · 판 위로 %.1f px · 왼쪽 차 %.1f px · 색 %s"),
			Pill.GetSize().X, Pill.GetSize().Y, Panel.Top - Pill.Top, Pill.Left - Panel.Left,
			TitlePill != nullptr ? *TitlePill->Background.TintColor.GetSpecifiedColor().ToFColor(true).ToHex() : TEXT("-")));
		TestEqual(TEXT("제목 탭이 판보다 시트만큼 위로"), Panel.Top - Pill.Top, SheetPillRise, Tolerance);
		TestTrue(TEXT("제목 탭이 판에 걸쳐 있다(아래가 판 안)"), Pill.Bottom > Panel.Top + Tolerance);
		TestEqual(TEXT("제목 탭이 판 왼쪽 위 모서리에"), Pill.Left, Panel.Left, Tolerance);
	}

	DestroyTestWorld(World);
	return true;
}

// M27-03 — 뽑기 줄 셋(젬 1회·코인·젬 10회) 네 상태 둥근 HudCornerRadius.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGachaRoundRowsTest, "Snarehaven.Gacha.Screen.RoundPullRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGachaRoundRowsTest::RunTest(const FString& Parameters)
{
	const FScopedGemsEnabled GemsOn(true);   // 젬 화면을 보는 검사 — 1.0 설정(꺼짐)과 무관하게 켠다
	UWorld* World = CreateTestWorld();
	UGachaScreenWidget* const Screen = CreateWidget<UGachaScreenWidget>(World, UGachaScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("뽑기 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();

	const FVector4 ButtonRadius(SnareUiStyle::HudCornerRadius, SnareUiStyle::HudCornerRadius, SnareUiStyle::HudCornerRadius,
		SnareUiStyle::HudCornerRadius);
	auto IsRound = [&ButtonRadius](const FSlateBrush& Brush)
	{
		return Brush.DrawAs == ESlateBrushDrawType::RoundedBox && Brush.OutlineSettings.CornerRadii == ButtonRadius;
	};
	int32 Round = 0;
	int32 All = 0;
	Screen->WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		const UButton* const Button = Cast<UButton>(Widget);
		if (Button == nullptr)
		{
			return;
		}
		const FButtonStyle& Style = Button->GetStyle();
		const bool bRound = IsRound(Style.Normal) && IsRound(Style.Hovered) && IsRound(Style.Pressed) && IsRound(Style.Disabled);
		TestTrue(FString::Printf(TEXT("%s 네 상태 둥근 네모 · 반지름 = HudCornerRadius"), *Button->GetName()), bRound);
		Round += bRound ? 1 : 0;
		++All;
	});
	for (const TCHAR* Name : {TEXT("SinglePullButton"), TEXT("CoinPullButton"), TEXT("TenPullButton")})
	{
		TestNotNull(FString::Printf(TEXT("뽑기 줄 %s"), Name), Screen->WidgetTree->FindWidget<UButton>(Name));
	}
	TestNotNull(TEXT("다시 시도 단추"), Screen->WidgetTree->FindWidget<UButton>(TEXT("GachaRetryButton")));
	TestEqual(TEXT("뽑기 화면 버튼 = 뽑기 줄 3개 + 다시 시도 + 닫기"), All, 5);
	AddInfo(FString::Printf(TEXT("[GachaRound] 둥근 버튼 %d/%d"), Round, All));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGachaPoolListReadableTest, "Snarehaven.Gacha.Screen.PoolListReadable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 감독 캡처(956)에서 「아이템별 확률」 목록이 흰 글자(크림 바탕에 안 보임)·큰 글자·오른쪽 잘림이었다.
// 줄마다 ① 글자색이 바탕(크림)과 충분히 다르고 ② 등급 줄과 같은 크기이며 ③ 퍼센트 칸이 오른쪽 정렬로
// 따로 있고 이름 칸은 줄바꿈하는지 본다 (칸 너비 안에 들어오는지 실제 그림은 캡처가 본다).
bool FSnarehavenGachaPoolListReadableTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UGachaScreenWidget* const Screen = CreateWidget<UGachaScreenWidget>(World, UGachaScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("뽑기 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();

	const FLinearColor Cream = SnareUiStyle::UiPanelCream();
	int32 NameCount = 0;
	int32 PctCount = 0;
	Screen->WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		const UTextBlock* const Text = Cast<UTextBlock>(Widget);
		if (Text == nullptr)
		{
			return;
		}
		const FString Name = Text->GetName();
		const bool bName = Name.StartsWith(TEXT("PoolRowText_"));
		const bool bPct = Name.StartsWith(TEXT("PoolRowPct_"));
		if (!bName && !bPct)
		{
			return;
		}
		bName ? ++NameCount : ++PctCount;
		const FLinearColor Color = Text->GetColorAndOpacity().GetSpecifiedColor();
		const float Contrast = FMath::Abs(Color.R - Cream.R) + FMath::Abs(Color.G - Cream.G) + FMath::Abs(Color.B - Cream.B);
		TestTrue(*FString::Printf(TEXT("%s 글자색이 크림 바탕과 대비된다 (%.2f)"), *Name, Contrast), Contrast > 1.0f);
		TestEqual(*FString::Printf(TEXT("%s 글자 크기가 등급 줄과 같다"), *Name), Text->GetFont().Size, 18.f);
		if (bName)
		{
			TestTrue(*FString::Printf(TEXT("%s 이름은 칸 안에서 줄바꿈한다"), *Name), Text->GetAutoWrapText());
		}
		else
		{
			const UHorizontalBoxSlot* const PctSlot = Cast<UHorizontalBoxSlot>(Text->Slot);
			TestTrue(*FString::Printf(TEXT("%s 퍼센트는 줄 오른쪽 끝에 붙는다"), *Name),
				PctSlot != nullptr && PctSlot->GetHorizontalAlignment() == HAlign_Right);
		}
	});
	TestTrue(TEXT("목록 이름 줄이 있다"), NameCount > 0);
	TestEqual(TEXT("이름 줄 수 = 퍼센트 줄 수"), NameCount, PctCount);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGachaRetryTest, "Snarehaven.Gacha.Screen.Retry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGachaRetryTest::RunTest(const FString& Parameters)
{
	// 뽑기가 서버에 닿지 못하면 이유 곁에 「다시 시도」 — 누르면 같은 재화·같은 횟수로 `RollGacha` 를 다시.
	const FScopedGemsEnabled GemsOn(true);
	TArray<TPair<TSharedPtr<FJsonObject>, USnareBackendSubsystem::FServerCallDone>> Rolls;
	USnareBackendSubsystem::TestServerOverride =
		[&Rolls](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			if (FunctionName == TEXT("RollGacha"))
			{
				Rolls.Emplace(Args, MoveTemp(OnDone));
			}
		};
	UWorld* World = CreateTestWorld();
	UGachaScreenWidget* const Screen = CreateWidget<UGachaScreenWidget>(World, UGachaScreenWidget::StaticClass());
	Screen->TakeWidget();
	Screen->BindToBackend(NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage())));

	const UWidget* const RetryBox = Screen->WidgetTree->FindWidget(TEXT("GachaRetryBox"));
	UButton* const RetryButton = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("GachaRetryButton")));
	TestNotNull(TEXT("다시 시도 단추가 있다"), RetryButton);
	if (RetryBox != nullptr && RetryButton != nullptr && TestTrue(TEXT("뽑기 요청"), Screen->RequestPull(TEXT("GM"), 10)) && Rolls.Num() == 1)
	{
		TestFalse(TEXT("처음엔 숨김"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
		Rolls[0].Value(false, nullptr, TEXT("net"));
		TestTrue(TEXT("네트워크 실패 → 다시 시도 보임"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
		TestFalse(TEXT("이유를 보인다"), Screen->GetStatusText().IsEmpty());
		RetryButton->OnClicked.Broadcast();
		TestEqual(TEXT("다시 시도 → RollGacha 다시"), Rolls.Num(), 2);
		if (Rolls.Num() == 2 && Rolls[1].Key.IsValid())
		{
			TestEqual(TEXT("같은 재화"), Rolls[1].Key->GetStringField(TEXT("currency")), FString(TEXT("GM")));
			TestEqual(TEXT("같은 횟수"), static_cast<int32>(Rolls[1].Key->GetNumberField(TEXT("rollCount"))), 10);
			TSharedPtr<FJsonObject> Rejected = MakeShared<FJsonObject>();
			Rejected->SetStringField(TEXT("error"), TEXT("x"));
			Rejected->SetStringField(TEXT("code"), TEXT("not_enough_gems"));
			Rolls[1].Value(true, Rejected, FString());
		}
		TestFalse(TEXT("서버 거절엔 다시 시도 없음"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
	}

	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyTestWorld(World);
	return true;
}

// M107-05 ⑥ — 뽑기 판에 「닫기」 가 없어 갇혔다. 단추가 있고, 공용 닫기 모양(높이 × 판 배율 ≥ 121)이고, 누르면 판이 떨어지는지.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGachaCloseButtonTest, "Snarehaven.Gacha.Screen.CloseButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGachaCloseButtonTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UGachaScreenWidget* const Screen = CreateWidget<UGachaScreenWidget>(World, UGachaScreenWidget::StaticClass());
	Screen->TakeWidget();
	UButton* const Close = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("CloseButton")));
	const USizeBox* const Box = Cast<USizeBox>(Screen->WidgetTree->FindWidget(TEXT("GachaCloseBox")));
	TestNotNull(TEXT("닫기 단추가 있다"), Close);
	TestNotNull(TEXT("닫기 상자가 있다"), Box);
	if (Close != nullptr && Box != nullptr)
	{
		// 판(GachaSheetRoot)은 1.6 배로 키워 그린다 — 화면 높이 = 상자 높이 × 그 배율.
		const UWidget* const Sheet = Screen->WidgetTree->FindWidget(TEXT("GachaSheetRoot"));
		const float Scale = Sheet != nullptr ? Sheet->GetRenderTransform().Scale.Y : 1.f;
		TestTrue(FString::Printf(TEXT("닫기 화면 높이 %.1f ≥ %.0f"), Box->GetHeightOverride() * Scale, SnareUiStyle::MenuTouchMin),
			Box->GetHeightOverride() * Scale >= SnareUiStyle::MenuTouchMin);
		TestTrue(TEXT("닫기가 보인다"), Close->GetVisibility() == ESlateVisibility::Visible);

		UCanvasPanel* const Holder = NewObject<UCanvasPanel>(GetTransientPackage());
		Holder->AddChild(Screen);
		TestNotNull(TEXT("붙어 있다"), Screen->GetParent());
		Close->OnClicked.Broadcast();
		TestNull(TEXT("닫기를 누르면 판이 떨어진다"), Screen->GetParent());
		AddInfo(FString::Printf(TEXT("[GachaClose] 단추 있음 · 화면 높이 %.1f · 누른 뒤 부모 %s"),
			Box->GetHeightOverride() * Scale, Screen->GetParent() == nullptr ? TEXT("없음") : TEXT("있음")));
	}
	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
