// ROADMAP M7-06·M7-07 2층 검사 — 뽑기 버튼이 `RollGacha` 를 부르고 **응답 그대로만** 결과를 거는지, 그리고
// 뽑기 풀 표(`DT_GachaPool`)가 공개 문서·서버 풀과 같은지.
// 이 검사가 잡는 것: ① 답 전에 결과가 뜨는 것(= 클라이언트가 굴린 것) ② 응답과 다른 줄 ③ 거절 사유를 삼키는 것
// ④ 모르는 등급을 커먼으로 떨궈 적는 것 ⑤ 풀 19줄·합 100%·등급 합 = 등급 확률·ID = 서버 `GACHA_POOL` 이 어긋난 것.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Gacha; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Data/GachaPoolRow.h"
#include "Data/GachaRateRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UI/CrateScreenWidget.h"
#include "UI/GachaScreenWidget.h"

namespace SnareGachaPullTest
{
	const TCHAR* PoolTablePath = TEXT("/Game/Game/Data/Gacha/DT_GachaPool.DT_GachaPool");
	const TCHAR* RateTablePath = TEXT("/Game/Game/Data/Gacha/DT_GachaRates.DT_GachaRates");
	// M7-07 인수 조건 — 공개 문서 "아이템별 확률" 표 줄 수.
	constexpr int32 DisclosedItemCount = 19;
	constexpr float PercentTolerance = 0.05f;
	// 검사 전용 값 — 게임 숫자가 아니다(환급 양을 표·설계서에 없는 7 로 둬 진짜 값과 섞이지 않게).
	constexpr int32 TestRefund = 7;

	using FDone = USnareBackendSubsystem::FServerCallDone;

	struct FFakeServer
	{
		FString LastFunction;
		TSharedPtr<FJsonObject> LastArgs;
		TArray<FDone> Waiting;
		int32 Calls = 0;

		void Install()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>& Args, FDone OnDone)
				{
					++Calls;
					LastFunction = Name;
					LastArgs = Args;
					Waiting.Add(MoveTemp(OnDone));
				};
		}

		~FFakeServer() { USnareBackendSubsystem::TestServerOverride = nullptr; }

		void Answer(const TSharedPtr<FJsonObject>& Response)
		{
			if (Waiting.IsEmpty())
			{
				return;
			}
			FDone Done = MoveTemp(Waiting[0]);
			Waiting.RemoveAt(0);
			Done(true, Response, FString());
		}
	};

	struct FPoolEntry
	{
		FString ItemID;
		FString Grade; // 소문자 서버 표기 (`common`)
		float Percent = -1.f;
	};

	/** `RollGacha` 응답 `{results:[{itemId, grade, duplicate, refund?}], pity}` 모양. */
	TSharedPtr<FJsonObject> MakeRollResponse(const TArray<FPoolEntry>& Picks, int32 DuplicateIndex)
	{
		TArray<TSharedPtr<FJsonValue>> Results;
		for (int32 Index = 0; Index < Picks.Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> One = MakeShared<FJsonObject>();
			One->SetStringField(TEXT("itemId"), Picks[Index].ItemID);
			One->SetStringField(TEXT("grade"), Picks[Index].Grade);
			One->SetBoolField(TEXT("duplicate"), Index == DuplicateIndex);
			if (Index == DuplicateIndex)
			{
				One->SetNumberField(TEXT("refund"), TestRefund);
			}
			Results.Add(MakeShared<FJsonValueObject>(One));
		}
		const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetArrayField(TEXT("results"), Results);
		Response->SetNumberField(TEXT("pity"), 0);
		return Response;
	}

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

	FString GradeKey(EGachaGrade Grade)
	{
		return StaticEnum<EGachaGrade>()->GetNameStringByValue(static_cast<int64>(Grade)).ToLower();
	}

	TArray<FPoolEntry> ReadTable(const UDataTable& Table)
	{
		TArray<FPoolEntry> Out;
		Table.ForeachRow<FGachaPoolRow>(TEXT("GachaPullTest"), [&Out](const FName&, const FGachaPoolRow& Row)
		{
			Out.Add({ Row.ItemID.ToString(), GradeKey(Row.Grade), Row.ProbabilityPct });
		});
		return Out;
	}

	/** 서버 `var GACHA_POOL = [ { id: "...", grade: "..." }, ... ];` 를 순서대로 읽는다. */
	TArray<FPoolEntry> ReadServerPool(const FString& Script)
	{
		TArray<FPoolEntry> Out;
		const int32 Start = Script.Find(TEXT("var GACHA_POOL = ["));
		const int32 End = Start == INDEX_NONE ? INDEX_NONE : Script.Find(TEXT("];"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
		if (End == INDEX_NONE)
		{
			return Out;
		}
		const FRegexPattern Pattern(TEXT("id:\\s*\"([^\"]+)\",\\s*grade:\\s*\"([a-z]+)\""));
		FRegexMatcher Matcher(Pattern, Script.Mid(Start, End - Start));
		while (Matcher.FindNext())
		{
			Out.Add({ Matcher.GetCaptureGroup(1), Matcher.GetCaptureGroup(2), -1.f });
		}
		return Out;
	}

	/** 공개 문서 5칸 줄 `| gacha.x | 이름 | 커먼 | 카테고리 | 10.0% |` 을 순서대로 읽는다. */
	TArray<FPoolEntry> ReadDisclosure(const FString& Doc)
	{
		static const TMap<FString, FString> GradeByLabel = {
			{ TEXT("커먼"), TEXT("common") }, { TEXT("레어"), TEXT("rare") },
			{ TEXT("에픽"), TEXT("epic") }, { TEXT("레전더리"), TEXT("legendary") } };
		TArray<FPoolEntry> Out;
		TArray<FString> Lines;
		Doc.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			TArray<FString> Cells;
			Line.TrimStartAndEnd().ParseIntoArray(Cells, TEXT("|"));
			if (Cells.Num() != 5 || !Cells[0].TrimStartAndEnd().StartsWith(TEXT("gacha.")))
			{
				continue;
			}
			const FString* const Grade = GradeByLabel.Find(Cells[2].TrimStartAndEnd());
			Out.Add({ Cells[0].TrimStartAndEnd(), Grade != nullptr ? *Grade : FString(),
				FCString::Atof(*Cells[4].TrimStartAndEnd().LeftChop(1)) });
		}
		return Out;
	}

	/** 두 목록이 순서·ID·등급(그리고 둘 다 있으면 확률)까지 같은지. 다른 줄 수를 돌려준다. */
	int32 CountMismatches(const TArray<FPoolEntry>& A, const TArray<FPoolEntry>& B)
	{
		int32 Bad = FMath::Abs(A.Num() - B.Num());
		for (int32 Index = 0; Index < FMath::Min(A.Num(), B.Num()); ++Index)
		{
			const bool bPercentKnown = A[Index].Percent >= 0.f && B[Index].Percent >= 0.f;
			if (A[Index].ItemID != B[Index].ItemID || A[Index].Grade != B[Index].Grade
				|| (bPercentKnown && !FMath::IsNearlyEqual(A[Index].Percent, B[Index].Percent, PercentTolerance)))
			{
				++Bad;
			}
		}
		return Bad;
	}

	struct FRig
	{
		UWorld* World = nullptr;
		USnareBackendSubsystem* Backend = nullptr;
		UGachaScreenWidget* Screen = nullptr;
		TSharedPtr<SWidget> Slate;

		FRig()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
			Screen = CreateWidget<UGachaScreenWidget>(World, UGachaScreenWidget::StaticClass());
			Screen->BindToBackend(Backend);
			Slate = Screen->TakeWidget();
		}

		~FRig()
		{
			Slate.Reset();
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

using namespace SnareGachaPullTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGachaPullShowsResponseTest, "Snarehaven.Gacha.Pull.ShowsOnlyServerResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGachaPullShowsResponseTest::RunTest(const FString& Parameters)
{
	const UDataTable* const PoolTable = LoadObject<UDataTable>(nullptr, PoolTablePath);
	const UDataTable* const RateTable = LoadObject<UDataTable>(nullptr, RateTablePath);
	if (!TestNotNull(TEXT("DT_GachaPool"), PoolTable) || !TestNotNull(TEXT("DT_GachaRates"), RateTable))
	{
		return false;
	}
	const TArray<FPoolEntry> Pool = ReadTable(*PoolTable);
	FFakeServer Server;
	Server.Install();
	FRig Rig;

	TestTrue(TEXT("10연차 요청을 보낸다"), Rig.Screen->RequestPull(USnareBackendSubsystem::GetGemCurrencyCode(), 10));
	TestEqual(TEXT("부른 함수 = RollGacha"), Server.LastFunction, FString(TEXT("RollGacha")));
	TestEqual(TEXT("currency = GM"), Server.LastArgs->GetStringField(TEXT("currency")), FString(TEXT("GM")));
	TestEqual(TEXT("rollCount = 10"), static_cast<int32>(Server.LastArgs->GetNumberField(TEXT("rollCount"))), 10);

	// 답 전: 결과 칸 비어 있고, 한 번 더 눌러도 안 나간다.
	TestEqual(TEXT("답 전 결과 0줄"), CollectResultLines(*Rig.Screen).Num(), 0);
	TestFalse(TEXT("답 전 또 안 보낸다"), Rig.Screen->RequestPull(USnareBackendSubsystem::GetGemCurrencyCode(), 1));
	TestEqual(TEXT("서버 호출 1번"), Server.Calls, 1);
	TestEqual(TEXT("답 전 뽑기 소리 0"), Rig.Screen->GetConfirmSoundCount(), 0);

	// 서버가 준 10줄 — 풀 뒤쪽부터 골라 등급이 섞이게.
	TArray<FPoolEntry> Picks;
	for (int32 Index = 0; Index < 10 && Pool.Num() > 0; ++Index)
	{
		Picks.Add(Pool[Pool.Num() - 1 - (Index * 2) % Pool.Num()]);
	}
	constexpr int32 DuplicateIndex = 3;
	Server.Answer(MakeRollResponse(Picks, DuplicateIndex));

	const TArray<FString> Lines = CollectResultLines(*Rig.Screen);
	TestEqual(TEXT("결과 줄 수 = 응답 줄 수"), Lines.Num(), Picks.Num());
	TestEqual(TEXT("답 뒤 뽑기 소리 1 (10연차도 한 번)"), Rig.Screen->GetConfirmSoundCount(), 1);
	TestEqual(TEXT("뽑기 소리 = 상자 소리 빌림"), Rig.Screen->GetLastConfirmSoundCue(), UCrateScreenWidget::GetConfirmCue());
	for (int32 Index = 0; Index < FMath::Min(Lines.Num(), Picks.Num()); ++Index)
	{
		const FGachaPoolRow* PoolRow = nullptr;
		PoolTable->ForeachRow<FGachaPoolRow>(TEXT("GachaPullTest"), [&](const FName&, const FGachaPoolRow& Row)
		{
			if (Row.ItemID.ToString() == Picks[Index].ItemID)
			{
				PoolRow = &Row;
			}
		});
		const FString Name = PoolRow != nullptr ? PoolRow->DisplayName.ToString() : FString(TEXT("?"));
		TestTrue(FString::Printf(TEXT("%d번째 줄에 응답 아이템 이름 '%s' (실제 '%s')"), Index + 1, *Name, *Lines[Index]),
			Lines[Index].Contains(Name));
		FString GradeName = TEXT("?");
		if (RateTable != nullptr)
		{
			RateTable->ForeachRow<FGachaRateRow>(TEXT("GachaPullTest"), [&](const FName&, const FGachaRateRow& Row)
			{
				if (GradeKey(Row.Grade) == Picks[Index].Grade)
				{
					GradeName = Row.DisplayName.ToString();
				}
			});
		}
		TestTrue(FString::Printf(TEXT("%d번째 줄에 응답 등급 '%s'"), Index + 1, *GradeName), Lines[Index].StartsWith(GradeName));
	}
	if (Lines.IsValidIndex(DuplicateIndex))
	{
		TestTrue(TEXT("중복 줄에 응답 환급 양"), Lines[DuplicateIndex].Contains(FString::FromInt(TestRefund)));
		TestFalse(TEXT("중복 아닌 줄엔 환급 없음"), Lines[0].Contains(TEXT("환급")));
	}
	TestEqual(TEXT("성공 뒤 다시 받는다"), Rig.Screen->GetRefetchCount(), 1);
	TestTrue(TEXT("답이 온 뒤엔 다시 누를 수 있다"), Rig.Screen->RequestPull(USnareBackendSubsystem::GetCoinCurrencyCode(), 1));
	TestEqual(TEXT("코인 1회 currency = SC"), Server.LastArgs->GetStringField(TEXT("currency")), FString(TEXT("SC")));
	AddInfo(FString::Printf(TEXT("[Gacha] 10연차 - 응답 %d줄 → 화면 %d줄 · 다시받기 %d · 첫 줄 '%s'"),
		Picks.Num(), Lines.Num(), Rig.Screen->GetRefetchCount(), Lines.Num() > 0 ? *Lines[0] : TEXT("")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGachaPullRejectTest, "Snarehaven.Gacha.Pull.RejectOrUnknownShowsNoResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGachaPullRejectTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();
	{
		FRig Rig;
		Rig.Screen->RequestPull(USnareBackendSubsystem::GetCoinCurrencyCode(), 1);
		const TSharedPtr<FJsonObject> Reject = MakeShared<FJsonObject>();
		Reject->SetStringField(TEXT("error"), TEXT("재화 부족"));
		Server.Answer(Reject);
		TestEqual(TEXT("거절 사유를 그대로"), Rig.Screen->GetStatusText().ToString(), FString(TEXT("재화 부족")));
		TestEqual(TEXT("거절엔 결과 0줄"), CollectResultLines(*Rig.Screen).Num(), 0);
		TestEqual(TEXT("거절엔 다시 받지 않는다"), Rig.Screen->GetRefetchCount(), 0);
		TestEqual(TEXT("거절엔 뽑기 소리 0"), Rig.Screen->GetConfirmSoundCount(), 0);
	}
	{
		// 반대 방향 — 모르는 등급이 섞이면 **한 줄도** 걸지 않는다(커먼으로 떨궈 적으면 오표시).
		FRig Rig;
		Rig.Screen->RequestPull(USnareBackendSubsystem::GetGemCurrencyCode(), 1);
		Server.Answer(MakeRollResponse({ { TEXT("gacha.pet_ribbon"), TEXT("mythic"), -1.f } }, INDEX_NONE));
		TestEqual(TEXT("모르는 등급이면 결과 0줄"), CollectResultLines(*Rig.Screen).Num(), 0);
		TestFalse(TEXT("모르는 등급이면 상태 줄에 알린다"), Rig.Screen->GetStatusText().IsEmpty());
		TestEqual(TEXT("재화는 빠졌으니 다시 받는다"), Rig.Screen->GetRefetchCount(), 1);
		TestEqual(TEXT("결과를 못 걸면 뽑기 소리 0"), Rig.Screen->GetConfirmSoundCount(), 0);
	}
	AddInfo(TEXT("[Gacha] 거절 - 결과 0줄 · 소리 0 · 사유 '재화 부족' / 모르는 등급 - 결과 0줄 · 소리 0"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGachaPoolMatchTest, "Snarehaven.Gacha.Pool.MatchesDisclosureAndServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGachaPoolMatchTest::RunTest(const FString& Parameters)
{
	const UDataTable* const PoolTable = LoadObject<UDataTable>(nullptr, PoolTablePath);
	const UDataTable* const RateTable = LoadObject<UDataTable>(nullptr, RateTablePath);
	FString Doc;
	FString Script;
	if (!TestNotNull(TEXT("DT_GachaPool"), PoolTable) || !TestNotNull(TEXT("DT_GachaRates"), RateTable)
		|| !TestTrue(TEXT("공개 문서 읽기"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/GACHA_DISCLOSURE.md"))))
		|| !TestTrue(TEXT("CloudScript 읽기"), FFileHelper::LoadFileToString(Script, *(FPaths::ProjectDir() / TEXT("tools/playfab/cloudscript_main.js")))))
	{
		return false;
	}

	const TArray<FPoolEntry> Table = ReadTable(*PoolTable);
	const TArray<FPoolEntry> Disclosed = ReadDisclosure(Doc);
	const TArray<FPoolEntry> ServerPool = ReadServerPool(Script);

	TestEqual(TEXT("표 줄 수 = 19"), Table.Num(), DisclosedItemCount);
	TestEqual(TEXT("문서 줄 수 = 19"), Disclosed.Num(), DisclosedItemCount);
	TestEqual(TEXT("서버 풀 줄 수 = 19"), ServerPool.Num(), DisclosedItemCount);

	float Sum = 0.f;
	TMap<FString, float> SumByGrade;
	for (const FPoolEntry& Entry : Table)
	{
		TestTrue(FString::Printf(TEXT("%s 확률이 정해져 있다(-1 아님)"), *Entry.ItemID), Entry.Percent >= 0.f);
		Sum += Entry.Percent;
		SumByGrade.FindOrAdd(Entry.Grade) += Entry.Percent;
	}
	TestEqual(TEXT("확률 합 = 100%"), Sum, 100.f, PercentTolerance);
	RateTable->ForeachRow<FGachaRateRow>(TEXT("GachaPullTest"), [&](const FName&, const FGachaRateRow& Row)
	{
		const float* const GradeSum = SumByGrade.Find(GradeKey(Row.Grade));
		TestTrue(FString::Printf(TEXT("%s 아이템 합 = 등급 확률 %.1f%%"), *Row.DisplayName.ToString(), Row.ProbabilityPct),
			GradeSum != nullptr && FMath::IsNearlyEqual(*GradeSum, Row.ProbabilityPct, PercentTolerance));
	});

	const int32 DocBad = CountMismatches(Table, Disclosed);
	const int32 ServerBad = CountMismatches(Table, ServerPool);
	TestEqual(TEXT("표 = 공개 문서 (순서·ID·등급·확률)"), DocBad, 0);
	TestEqual(TEXT("표 = 서버 GACHA_POOL (순서·ID·등급)"), ServerBad, 0);

	// 반대 방향 — 한 줄의 ID 를 바꾸거나 한 줄을 빼면 맞대기가 운다(항상 0 을 내는 비교가 아니다).
	TArray<FPoolEntry> Tampered = ServerPool;
	if (Tampered.Num() > 0)
	{
		Tampered[0].ItemID += TEXT("_x");
	}
	TArray<FPoolEntry> Shorter = Disclosed;
	if (Shorter.Num() > 0)
	{
		Shorter.Pop();
	}
	TestTrue(TEXT("ID 하나 바꾸면 불일치"), CountMismatches(Table, Tampered) > 0);
	TestTrue(TEXT("한 줄 빼면 불일치"), CountMismatches(Table, Shorter) > 0);
	AddInfo(FString::Printf(TEXT("[GachaPool] 표 %d줄 · 문서 %d줄 · 서버 %d줄 · 합 %.1f%% · 문서불일치 %d · 서버불일치 %d"),
		Table.Num(), Disclosed.Num(), ServerPool.Num(), Sum, DocBad, ServerBad));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
