// ROADMAP M7-05 2층 검사 — 백엔드가 준 뽑기 결과가 화면에 **그대로, 서로 다르게** 뜨는지.
// 이 검사가 잡는 것: ① 결과 줄이 화면에서 빠진 것 ② 등급이 달라도 같은 글자로 뜨는 것
// ③ **중복 환급이 보통 획득과 구분되지 않는 것**(그러면 플레이어는 재화가 왜 늘었는지 모른다 —
// DESIGN 10.2 "중복 처리") ④ 못 들은 칸을 0 이나 빈 줄처럼 **있는 값인 척** 보여 주는 것.
//
// **응답은 검사가 직접 만든다.** 부를 백엔드(CloudScript `RollGacha`, M7-06)가 아직 없기 때문이고,
// 클라이언트가 스스로 굴려 흉내 내면 그건 치트 경로다 (DESIGN 10.5 · CLAUDE.md 8).
// 그래서 여기 적힌 아이템 이름·환급 양은 전부 **검사 전용 값**이다 — 게임 숫자가 아니라서 설계서에
// 없어도 된다(PROMPT ④-6 "테스트는 테스트 전용 값으로"). 진짜 풀·환급 값은 M7-07 과 백엔드 몫이다.
// 실제 응답이 이 모양으로 오는지는 이 층이 못 본다 — M7-06 이 서야 3층에서 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Gacha; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/GachaPullResult.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Data/GachaRateRow.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UI/GachaScreenWidget.h"

namespace
{
	const TCHAR* GachaRateTablePath = TEXT("/Game/Game/Data/Gacha/DT_GachaRates.DT_GachaRates");

	// 검사 전용 값 — 게임에 나가는 숫자가 아니다. 환급 양을 7 로 둔 이유는 표·설계서 어디에도 없는
	// 값이라 **코드가 슬쩍 만들어 낸 숫자와 섞이지 않기** 때문이다(0 이나 100 이면 진짜 값처럼 읽힌다).
	constexpr int32 TestRefundAmount = 7;

	// 검사 전용 코인 경품 양 — 같은 이유로 문서의 50·150·400 이 아닌 값(M35-02).
	constexpr int32 TestPrizeCoins = 37;

	/** 결과 줄만 모은다. 확률 줄까지 섞으면 "화면에 `커먼` 이 있다" 가 확률 줄만 보고도 참이 된다. */
	TArray<FString> CollectResultLines(const UUserWidget& Screen)
	{
		TArray<FString> Lines;
		Screen.WidgetTree->ForEachWidget([&Lines](UWidget* Child)
		{
			const UVerticalBox* const Box = Cast<UVerticalBox>(Child);
			if (Box == nullptr || Box->GetFName() != TEXT("ResultList"))
			{
				return;
			}

			for (int32 Index = 0; Index < Box->GetChildrenCount(); ++Index)
			{
				if (const UTextBlock* const RowText = Cast<UTextBlock>(Box->GetChildAt(Index)))
				{
					Lines.Add(RowText->GetText().ToString());
				}
			}
		});
		return Lines;
	}

	/** 그 등급이 표에 적어 둔 화면 이름 (`커먼`). 검사가 이름을 따로 적으면 표와 갈라진다. */
	FString GradeDisplayName(const UDataTable& RateTable, EGachaGrade Grade)
	{
		for (const TPair<FName, uint8*>& Pair : RateTable.GetRowMap())
		{
			const FGachaRateRow* const Row = reinterpret_cast<const FGachaRateRow*>(Pair.Value);
			if (Row != nullptr && Row->Grade == Grade)
			{
				return Row->DisplayName.ToString();
			}
		}
		return FString();
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

	/** 백엔드가 줬다고 치는 결과 한 줄. **클라이언트가 굴려 만든 값이 아니다** — 검사가 적은 값이다. */
	FGachaPullResult MakeResult(EGachaGrade Grade, const TCHAR* ItemId, const TCHAR* ItemName)
	{
		FGachaPullResult Result;
		Result.Grade = Grade;
		Result.ItemID = FName(ItemId);
		Result.ItemName = FText::FromString(ItemName);
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGachaResultTest, "Snarehaven.Gacha.Result.ShowsBackendResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGachaResultTest::RunTest(const FString& Parameters)
{
	UDataTable* const RateTable = LoadObject<UDataTable>(nullptr, GachaRateTablePath);
	if (!TestNotNull(TEXT("DT_GachaRates 로드"), RateTable))
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

	// Slate 위젯을 실제로 만들어야 자리표시 트리(결과 칸 포함)가 선다. 반환값을 붙잡고 있어야 한다 —
	// 놓으면 그 자리에서 해제된다 (`SnarehavenGachaScreenTest` 와 같은 함정).
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();

	// 뽑기 전에는 결과 칸이 비어 있어야 한다 — 예시 줄이 미리 서 있으면 뽑지 않았는데 뽑은 화면이다.
	TestEqual(TEXT("뽑기 전 결과 줄은 0개"), CollectResultLines(*Screen).Num(), 0);

	// 응답 세 종류 — 흔한 것 · 가장 귀한 것 · 중복 환급 (ROADMAP M7-05 합격선).
	TArray<FGachaPullResult> Results;
	Results.Add(MakeResult(EGachaGrade::Common, TEXT("item.test_gacha_common"), TEXT("검사용 밀짚모자")));
	Results.Add(MakeResult(EGachaGrade::Legendary, TEXT("item.test_gacha_legendary"), TEXT("검사용 황금 올가미")));

	FGachaPullResult Duplicate =
		MakeResult(EGachaGrade::Common, TEXT("item.test_gacha_common"), TEXT("검사용 밀짚모자"));
	Duplicate.bDuplicate = true;
	Duplicate.RefundAmount = TestRefundAmount;
	Duplicate.RefundCurrency = USnareBackendSubsystem::GetCoinCurrencyCode();
	Results.Add(Duplicate);

	Screen->ShowPullResults(Results);
	const TArray<FString> Lines = CollectResultLines(*Screen);
	AddInfo(FString::Printf(TEXT("결과 줄:\n%s"), *FString::Join(Lines, TEXT("\n"))));

	if (!TestEqual(TEXT("결과 줄 수가 응답 수와 같다"), Lines.Num(), Results.Num()))
	{
		DestroyTestWorld(World);
		return false;
	}

	// ① 등급 이름은 표에서 온다 — 검사가 `커먼` 을 적어 두면 표를 고쳐도 검사는 옛 이름을 통과시킨다.
	const FString CommonName = GradeDisplayName(*RateTable, EGachaGrade::Common);
	const FString LegendaryName = GradeDisplayName(*RateTable, EGachaGrade::Legendary);
	TestTrue(TEXT("표에서 커먼·레전더리 이름을 찾았다"), !CommonName.IsEmpty() && !LegendaryName.IsEmpty());

	TestTrue(*FString::Printf(TEXT("첫 줄에 등급 이름 %s 이 있다"), *CommonName), Lines[0].Contains(CommonName));
	TestTrue(TEXT("첫 줄에 아이템 이름이 있다"), Lines[0].Contains(TEXT("검사용 밀짚모자")));
	TestTrue(*FString::Printf(TEXT("둘째 줄에 등급 이름 %s 이 있다"), *LegendaryName),
		Lines[1].Contains(LegendaryName));
	TestTrue(TEXT("둘째 줄에 아이템 이름이 있다"), Lines[1].Contains(TEXT("검사용 황금 올가미")));

	// ② 세 줄이 **서로 다르게** 뜬다. 특히 셋째 줄은 첫 줄과 등급·아이템이 같고 중복 여부만 다르다 —
	// 그래서 이 비교가 "중복을 구분해 보여 주는가" 를 곧바로 묻는다.
	TestNotEqual(TEXT("커먼과 레전더리 줄이 다르다"), Lines[0], Lines[1]);
	TestNotEqual(TEXT("같은 아이템이라도 중복 환급 줄은 보통 획득 줄과 다르다"), Lines[0], Lines[2]);

	// ③ 중복 줄은 환급 재화와 양을 말한다. 양은 **백엔드가 준 값 그대로**여야 한다.
	TestTrue(TEXT("중복 줄에 중복임이 적혀 있다"), Lines[2].Contains(TEXT("중복")));
	TestTrue(TEXT("중복 줄에 환급 재화(코인)가 적혀 있다"), Lines[2].Contains(TEXT("코인")));
	TestTrue(*FString::Printf(TEXT("중복 줄에 환급 양 %d 이 적혀 있다"), TestRefundAmount),
		Lines[2].Contains(FString::FromInt(TestRefundAmount)));
	TestFalse(TEXT("보통 획득 줄에는 중복이라 적히지 않는다"), Lines[0].Contains(TEXT("중복")));
	TestFalse(TEXT("보통 획득 줄에는 환급이 적히지 않는다"), Lines[0].Contains(TEXT("환급")));

	// ④ 다시 뽑으면 지난 줄이 남지 않는다 — 쌓이면 10연차 두 번이 20줄이 된다.
	Screen->ShowPullResults(TArray<FGachaPullResult>());
	TestEqual(TEXT("빈 응답을 주면 결과 칸이 비워진다"), CollectResultLines(*Screen).Num(), 0);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGachaResultUnknownTest,
	"Snarehaven.Gacha.Result.ShowsDashWhenResponseIncomplete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGachaResultUnknownTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UGachaScreenWidget* const Screen = CreateWidget<UGachaScreenWidget>(World, UGachaScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("뽑기 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}

	const TSharedRef<SWidget> Slate = Screen->TakeWidget();

	// 칸이 빈 응답 — 백엔드가 아직 안 채운 것이지 "이름 없는 아이템을 0개 환급" 이 아니다.
	// 여기서 0 을 보여 주면 화면이 없는 사실을 말하게 된다 (Verification.md 7 · `SnareBalanceHud` 와 같은 규칙).
	FGachaPullResult Incomplete;
	Incomplete.Grade = EGachaGrade::Common;
	Incomplete.bDuplicate = true;

	Screen->ShowPullResults({ Incomplete });
	const TArray<FString> Lines = CollectResultLines(*Screen);
	AddInfo(FString::Printf(TEXT("덜 채워진 응답의 줄: [%s]"),
		Lines.Num() > 0 ? *Lines[0] : TEXT("(줄 없음)")));

	if (!TestEqual(TEXT("덜 채워진 응답도 줄은 뜬다"), Lines.Num(), 1))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("모르는 칸은 `-` 로 뜬다"), Lines[0].Contains(TEXT("-")));
	TestFalse(TEXT("모르는 환급 양을 0 으로 보여 주지 않는다"), Lines[0].Contains(TEXT("0")));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGachaResultCoinPrizeTest,
	"Snarehaven.Gacha.Result.ShowsCoinPrize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGachaResultCoinPrizeTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UGachaScreenWidget* const Screen = CreateWidget<UGachaScreenWidget>(World, UGachaScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("뽑기 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}

	const TSharedRef<SWidget> Slate = Screen->TakeWidget();

	// ① 서버 응답 `coins` 가 결과 줄 값으로 옮겨진다 — 코인 경품 하나·보통 경품 하나.
	const TSharedPtr<FJsonObject> CoinEntry = MakeShared<FJsonObject>();
	CoinEntry->SetStringField(TEXT("itemId"), TEXT("item.test_gacha_coin"));
	CoinEntry->SetStringField(TEXT("grade"), TEXT("common"));
	CoinEntry->SetBoolField(TEXT("duplicate"), false);
	CoinEntry->SetNumberField(TEXT("coins"), TestPrizeCoins);
	const TSharedPtr<FJsonObject> ItemEntry = MakeShared<FJsonObject>();
	ItemEntry->SetStringField(TEXT("itemId"), TEXT("item.test_gacha_common"));
	ItemEntry->SetStringField(TEXT("grade"), TEXT("common"));
	ItemEntry->SetBoolField(TEXT("duplicate"), false);
	const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetArrayField(TEXT("results"),
		{ MakeShared<FJsonValueObject>(CoinEntry), MakeShared<FJsonValueObject>(ItemEntry) });

	TArray<FGachaPullResult> Parsed;
	TestTrue(TEXT("코인 경품 응답을 읽는다"), Screen->ParsePullResponse(Response, Parsed));
	if (!TestEqual(TEXT("결과 2줄"), Parsed.Num(), 2))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("coins 가 PrizeCoins 로"), Parsed[0].PrizeCoins, TestPrizeCoins);
	TestEqual(TEXT("보통 경품은 PrizeCoins 0"), Parsed[1].PrizeCoins, 0);

	// ② 화면 줄은 「+N 코인」 이고 중복·환급이라 적지 않는다.
	Parsed[0].ItemName = FText::FromString(TEXT("검사용 코인 꾸러미"));
	Screen->ShowPullResults(Parsed);
	const TArray<FString> Lines = CollectResultLines(*Screen);
	AddInfo(FString::Printf(TEXT("결과 줄:\n%s"), *FString::Join(Lines, TEXT("\n"))));
	if (TestEqual(TEXT("결과 줄 2개"), Lines.Num(), 2))
	{
		const FString Plus = FString::Printf(TEXT("+%d 코인"), TestPrizeCoins);
		TestTrue(*FString::Printf(TEXT("코인 경품 줄에 「%s」"), *Plus), Lines[0].Contains(Plus));
		TestTrue(TEXT("코인 경품 줄에 경품 이름"), Lines[0].Contains(TEXT("검사용 코인 꾸러미")));
		TestFalse(TEXT("코인 경품 줄은 중복이 아니다"), Lines[0].Contains(TEXT("중복")));
		TestFalse(TEXT("보통 경품 줄에는 + 코인이 없다"), Lines[1].Contains(TEXT("코인")));
	}

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
