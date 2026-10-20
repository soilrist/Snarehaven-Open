// 상점 개편 B — 꾸미기 상자·열쇠 2층 검사 (DESIGN 10.8 · 10.4).
// ① 표 합 100% · 설계서 값 = 표 값 · 효과 없는 꾸미기만 ② 열쇠가 없으면 열기를 묻지도 않는다
// ③ 답 전에는 결과·칸이 그대로, 답 뒤에만 결과·다시받기 ④ 화면 고지 줄 = 표 값.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Components/SceneComponent.h"
#include "Data/CrateRow.h"
#include "Data/ItemRowLookup.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Internationalization/Regex.h"
#include "Inventory/BackpackComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/CrateScreenWidget.h"
#include "UI/SnareStoreSettings.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"

namespace SnareCrateTest
{
	const FName CrateID(TEXT("item.crate_autumn26"));

	TSharedPtr<FJsonObject> ParseJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	}

	struct FRecordedCall
	{
		FString FunctionName;
		TSharedPtr<FJsonObject> Args;
		USnareBackendSubsystem::FServerCallDone OnDone;
	};

	/** 게임 월드 + 백엔드 + 배낭 + 답을 붙잡아 두는 가짜 서버 (SnarehavenChestTest 와 같은 틀). */
	struct FRig
	{
		UWorld* World = nullptr;
		USnareBackendSubsystem* Backend = nullptr;
		UBackpackComponent* Backpack = nullptr;
		TArray<FRecordedCall> Calls;

		FRig()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());

			UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
			Backend = NewObject<USnareBackendSubsystem>(GameInstance);

			AActor* const Owner = World->SpawnActor<AActor>();
			USceneComponent* const Root = NewObject<USceneComponent>(Owner, TEXT("Root"));
			Owner->SetRootComponent(Root);
			Root->RegisterComponent();
			Backpack = NewObject<UBackpackComponent>(Owner, TEXT("Backpack"));
			Backpack->RegisterComponent();
			Backpack->BindTargets(Backend, nullptr);

			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
				{
					Calls.Add({FunctionName, Args, MoveTemp(OnDone)});
				};
		}

		~FRig()
		{
			USnareBackendSubsystem::TestServerOverride = nullptr;
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		int32 CountCalls(const TCHAR* Name) const
		{
			return Calls.FilterByPredicate([Name](const FRecordedCall& Call) { return Call.FunctionName == Name; }).Num();
		}

		const FRecordedCall* LastCall(const TCHAR* Name) const
		{
			for (int32 Index = Calls.Num() - 1; Index >= 0; --Index)
			{
				if (Calls[Index].FunctionName == Name)
				{
					return &Calls[Index];
				}
			}
			return nullptr;
		}

		void Reply(const TCHAR* Name, const FString& Json)
		{
			if (const FRecordedCall* const Call = LastCall(Name))
			{
				const USnareBackendSubsystem::FServerCallDone Done = Call->OnDone;
				Done(true, ParseJson(Json), FString());
			}
		}

		void SetOwned(int32 Crates, int32 Keys)
		{
			FSnarePlayerSnapshot Snapshot;
			Snapshot.PlayFabId = TEXT("crate-test");
			Snapshot.FetchedAtUtc = FDateTime::UtcNow();
			FInventorySlot CrateSlot;
			CrateSlot.ItemID = CrateID;
			CrateSlot.Count = Crates;
			FInventorySlot KeySlot;
			KeySlot.ItemID = UBackpackComponent::GetCrateKeyID();
			KeySlot.Count = Keys;
			Snapshot.InventorySlots = {CrateSlot, KeySlot};
			Backend->ApplyPlayerSnapshot(Snapshot, false);
		}
	};

	const UDataTable* CrateTable()
	{
		return GetDefault<UCrateScreenWidget>()->GetCrateTable();
	}
}

using namespace SnareCrateTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCrateTableTest, "Snarehaven.Crate.Table.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCrateTableTest::RunTest(const FString& Parameters)
{
	const FScopedGemsEnabled GemsOn(true);   // 젬 화면을 보는 검사 — 1.0 설정(꺼짐)과 무관하게 켠다
	const UDataTable* const Table = CrateTable();
	if (!TestNotNull(TEXT("DT_CrateTable 이 있다"), Table))
	{
		return false;
	}
	const UDataTable* const Items = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
	TestNotNull(TEXT("아이템 표"), Items);

	FString Doc;
	TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md"))));
	// 10.8 「품목별 확률 (가을)」 줄 — `item.x` **N%** 쌍.
	TMap<FName, float> Design;
	FString ItemLine;
	FRegexMatcher LineMatch(FRegexPattern(TEXT("\\| 품목별 확률 \\(가을\\) \\|([^|]+)\\|")), Doc);
	if (LineMatch.FindNext())
	{
		ItemLine = LineMatch.GetCaptureGroup(1);
	}
	FRegexMatcher PairMatch(FRegexPattern(TEXT("`(item\\.[a-z0-9_]+)` \\*\\*([0-9.]+)%\\*\\*")), ItemLine);
	while (PairMatch.FindNext())
	{
		Design.Add(FName(*PairMatch.GetCaptureGroup(1)), FCString::Atof(*PairMatch.GetCaptureGroup(2)));
	}
	FRegexMatcher GemMatch(FRegexPattern(TEXT("\\| 열쇠 젬 값 \\| \\*\\*([0-9]+)\\*\\* 젬")), Doc);
	const int32 DesignGem = GemMatch.FindNext() ? FCString::Atoi(*GemMatch.GetCaptureGroup(1)) : -1;

	float Sum = 0.f;
	int32 RowCount = 0;
	Table->ForeachRow<FCrateRow>(TEXT("CrateTableTest"), [&](const FName& Name, const FCrateRow& Row)
	{
		if (Row.CrateItemID != CrateID)
		{
			return;
		}
		++RowCount;
		Sum += Row.ChancePct;
		TestEqual(*FString::Printf(TEXT("%s 계절 열 = autumn26"), *Name.ToString()), Row.Season, FName(TEXT("autumn26")));
		TestEqual(*FString::Printf(TEXT("%s 열쇠 젬 값 = 10.8"), *Name.ToString()), Row.KeyGemPrice, DesignGem);
		const float* const Want = Design.Find(Row.ItemID);
		if (TestNotNull(*FString::Printf(TEXT("%s 가 10.8 에 있다"), *Row.ItemID.ToString()), Want))
		{
			TestEqual(*FString::Printf(TEXT("%s 확률 = 10.8"), *Row.ItemID.ToString()), Row.ChancePct, *Want);
		}
		const FItemRow* const Item = SnareItemRows::FindByItemID(Items, Row.ItemID);
		if (TestNotNull(*FString::Printf(TEXT("%s 가 아이템 표에 있다"), *Row.ItemID.ToString()), Item))
		{
			// 10.3 — 능력치 붙은 것 금지.
			TestTrue(*FString::Printf(TEXT("%s 는 효과 없는 꾸미기"), *Row.ItemID.ToString()),
				Item->HealAmount == 0 && Item->BuffKind == EBuffKind::None && !Item->bPassiveWhileCarried);
		}
	});
	TestEqual(TEXT("표 줄 수 = 10.8 품목 수"), RowCount, Design.Num());
	TestTrue(TEXT("표 합 100%"), FMath::IsNearlyEqual(Sum, 100.f, 0.01f));
	TestNotNull(TEXT("상자 아이템이 아이템 표에 있다"), SnareItemRows::FindByItemID(Items, CrateID));
	TestNotNull(TEXT("열쇠 아이템이 아이템 표에 있다"), SnareItemRows::FindByItemID(Items, UBackpackComponent::GetCrateKeyID()));
	TestTrue(TEXT("상자는 상자로 알아본다"), UCrateScreenWidget::IsCrateItem(Table, CrateID));
	TestFalse(TEXT("열쇠는 상자가 아니다"), UCrateScreenWidget::IsCrateItem(Table, UBackpackComponent::GetCrateKeyID()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCrateDisclosureTest, "Snarehaven.Crate.Disclosure.MatchesData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCrateDisclosureTest::RunTest(const FString& Parameters)
{
	const FScopedGemsEnabled GemsOn(true);   // 젬 화면을 보는 검사 — 1.0 설정(꺼짐)과 무관하게 켠다
	FRig Rig;
	UCrateScreenWidget* const Screen = CreateWidget<UCrateScreenWidget>(Rig.World, UCrateScreenWidget::StaticClass());
	Screen->BindToCrate(Rig.Backend, Rig.Backpack, CrateID);
	const TArray<const FCrateRow*> Rows = Screen->FindCrateRows();
	const TArray<FText> Lines = Screen->BuildDisclosureTexts();
	TSet<EItemGrade> Grades;
	TMap<EItemGrade, float> GradeSum;
	for (const FCrateRow* const Row : Rows)
	{
		Grades.Add(Row->Grade);
		GradeSum.FindOrAdd(Row->Grade) += Row->ChancePct;
	}
	if (!TestEqual(TEXT("고지 줄 = 등급 수 + 품목 수"), Lines.Num(), Grades.Num() + Rows.Num()))
	{
		return false;
	}
	TestTrue(TEXT("첫 줄 = 일반 등급 합"), Lines[0].ToString().EndsWith(FText::AsNumber(GradeSum.FindRef(EItemGrade::Common)).ToString() + TEXT("%")));
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const FString Line = Lines[Grades.Num() + Index].ToString();
		TestTrue(*FString::Printf(TEXT("품목 줄 %d 확률 = 표 값"), Index),
			Line.EndsWith(FText::AsNumber(Rows[Index]->ChancePct).ToString() + TEXT("%")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCrateGemsHiddenTest, "Snarehaven.Store.GemsHidden.CrateKeyButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCrateGemsHiddenTest::RunTest(const FString& Parameters)
{
	// INBOX M94 ④ — 젬이 꺼지면 「젬으로 열쇠 사기」 칸이 접힌다. 켜면 보인다(역검증).
	for (const bool bGemsEnabled : {true, false})
	{
		const FScopedGemsEnabled Scope(bGemsEnabled);
		FRig Rig;
		UCrateScreenWidget* const Screen = CreateWidget<UCrateScreenWidget>(Rig.World, UCrateScreenWidget::StaticClass());
		Screen->BindToCrate(Rig.Backend, Rig.Backpack, CrateID);
		const TSharedRef<SWidget> Slate = Screen->TakeWidget();
		const UWidget* const BuyKey = Screen->WidgetTree->FindWidget(TEXT("BuyKeyButton"));
		if (!TestNotNull(TEXT("열쇠 사기 버튼"), BuyKey))
		{
			return false;
		}
		TestEqual(bGemsEnabled ? TEXT("켜면 열쇠 사기 보임") : TEXT("끄면 열쇠 사기 접힘"), BuyKey->IsVisible(), bGemsEnabled);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCrateOpenTest, "Snarehaven.Crate.Open.KeyAndReply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCrateOpenTest::RunTest(const FString& Parameters)
{
	const FScopedGemsEnabled GemsOn(true);   // 젬 화면을 보는 검사 — 1.0 설정(꺼짐)과 무관하게 켠다
	FRig Rig;
	UCrateScreenWidget* const Screen = CreateWidget<UCrateScreenWidget>(Rig.World, UCrateScreenWidget::StaticClass());
	Screen->BindToCrate(Rig.Backend, Rig.Backpack, CrateID);

	// 열쇠 0 — 묻지도 않는다.
	Rig.SetOwned(1, 0);
	TestFalse(TEXT("열쇠 없으면 못 연다"), Screen->PressOpen());
	TestEqual(TEXT("서버에 안 물었다"), Rig.CountCalls(TEXT("OpenCrate")), 0);
	TestTrue(TEXT("열쇠 없음 알림"), Screen->GetStatusText().ToString().Contains(TEXT("열쇠")));

	// 열쇠 1 — 묻고, 답 전에는 결과·칸이 그대로다.
	Rig.SetOwned(1, 1);
	TestTrue(TEXT("열쇠 있으면 묻는다"), Screen->PressOpen());
	const FRecordedCall* const Call = Rig.LastCall(TEXT("OpenCrate"));
	FString CrateArg;
	TestTrue(TEXT("crateId 인자"), Call != nullptr && Call->Args->TryGetStringField(TEXT("crateId"), CrateArg));
	TestEqual(TEXT("crateId = 상자"), CrateArg, CrateID.ToString());
	TestTrue(TEXT("답 전: 기다린다"), Screen->IsRequestPending());
	TestTrue(TEXT("답 전: 결과가 비었다"), Screen->GetResultText().IsEmpty());
	TestEqual(TEXT("답 전: 열쇠 칸 그대로"), Screen->GetKeyCount(), 1);
	TestEqual(TEXT("답 전: 다시 받지 않았다"), Rig.Backpack->GetRefetchCount(), 0);
	TestEqual(TEXT("답 전: 상자 소리 0"), Screen->GetConfirmSoundCount(), 0);
	TestFalse(TEXT("기다리는 중엔 다시 안 묻는다"), Screen->PressOpen());
	TestEqual(TEXT("여전히 한 번"), Rig.CountCalls(TEXT("OpenCrate")), 1);

	Rig.Reply(TEXT("OpenCrate"), TEXT("{\"crateId\":\"item.crate_autumn26\",\"itemId\":\"item.harbor_whale\",\"grade\":\"epic\"}"));
	TestFalse(TEXT("답 뒤: 대기가 풀린다"), Screen->IsRequestPending());
	TestTrue(TEXT("답 뒤: 결과가 생겼다"), !Screen->GetResultText().IsEmpty());
	TestTrue(TEXT("답 뒤: 결과에 에픽"), Screen->GetResultText().ToString().Contains(TEXT("에픽")));
	const int32 SoundsAfterReply = Screen->GetConfirmSoundCount();
	TestEqual(TEXT("답 뒤: 상자 소리 1"), SoundsAfterReply, 1);
	TestEqual(TEXT("답 뒤: 소리 이름 = CrateOpen"), Screen->GetLastConfirmSoundCue(), UCrateScreenWidget::GetConfirmCue());
	TestTrue(TEXT("답 뒤: 짧은 연출 중"), Screen->IsRevealing());
	Screen->FinishReveal();
	TestFalse(TEXT("연출이 끝났다"), Screen->IsRevealing());
	TestEqual(TEXT("답 뒤: 보유 상태를 다시 받는다"), Rig.Backpack->GetRefetchCount(), 1);

	// 서버 거절 — 결과 없이 알림만, 다시받기 없음.
	TestTrue(TEXT("다시 연다"), Screen->PressOpen());
	Rig.Reply(TEXT("OpenCrate"), TEXT("{\"error\":\"열쇠가 없다\",\"code\":\"no_key\"}"));
	TestTrue(TEXT("거절: 결과가 비었다"), Screen->GetResultText().IsEmpty());
	TestTrue(TEXT("거절: 알림"), Screen->GetStatusText().ToString().Contains(TEXT("열쇠")));
	TestEqual(TEXT("거절은 다시 받지 않는다"), Rig.Backpack->GetRefetchCount(), 1);
	TestEqual(TEXT("거절: 상자 소리 그대로 1"), Screen->GetConfirmSoundCount(), 1);

	// 젬 열쇠 — `BuyItem{currency:GM}`.
	TestTrue(TEXT("젬 열쇠를 묻는다"), Screen->PressBuyKey());
	const FRecordedCall* const Buy = Rig.LastCall(TEXT("BuyItem"));
	FString ItemArg;
	FString CurrencyArg;
	TestTrue(TEXT("BuyItem 인자"), Buy != nullptr && Buy->Args->TryGetStringField(TEXT("itemId"), ItemArg)
		&& Buy->Args->TryGetStringField(TEXT("currency"), CurrencyArg));
	TestEqual(TEXT("itemId = 열쇠"), ItemArg, FString(TEXT("item.crate_key")));
	TestEqual(TEXT("currency = GM"), CurrencyArg, USnareBackendSubsystem::GetGemCurrencyCode());
	AddInfo(FString::Printf(TEXT("[CrateConfirm] 열기 확정 전 0 → 답 뒤 %d (%s) → 거절 뒤 %d"),
		SoundsAfterReply, *UCrateScreenWidget::GetConfirmCue().ToString(), Screen->GetConfirmSoundCount()));
	return true;
}

// ── M45-04 — 상자·뽑기 소리는 서버 성공 콜백 안에서만 (소스 훑기) ──
// 두 화면 소스에 `Play2D(` 가 딱 한 번씩, 그것도 거절 길(`!bAccepted` · `IsAccepted`)을 지난 뒤에만 있어야 한다.
// 누름 함수(`PressOpen` · `RequestPull`)로 옮겨지면 응답 전 재생이라 운다 (CLAUDE.md 8).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCrateSoundAfterReplyTest, "Snarehaven.Crate.Sound.OnlyAfterServerReply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCrateSoundAfterReplyTest::RunTest(const FString& Parameters)
{
	const FScopedGemsEnabled GemsOn(true);   // 젬 화면을 보는 검사 — 1.0 설정(꺼짐)과 무관하게 켠다
	struct FSource
	{
		const TCHAR* File;
		const TCHAR* Body;     // 소리가 있어야 할 함수 머리
		const TCHAR* Gate;     // 그 함수 안에서 소리보다 앞에 있어야 할 거절 길
	};
	const FSource Sources[] = {
		{ TEXT("Private/UI/CrateScreenWidget.cpp"), TEXT("void UCrateScreenWidget::ApplyReply("), TEXT("if (!bAccepted)") },
		{ TEXT("Private/UI/GachaScreenWidget.cpp"), TEXT("bool UGachaScreenWidget::RequestPull("), TEXT("Self->ShowPullResults(Results);") },
	};
	TArray<FString> Lines;
	for (const FSource& Source : Sources)
	{
		FString Text;
		const FString Path = FPaths::Combine(FPaths::GameSourceDir(), TEXT("Snarehaven"), Source.File);
		if (!TestTrue(*FString::Printf(TEXT("%s 읽기"), Source.File), FFileHelper::LoadFileToString(Text, *Path)))
		{
			return false;
		}
		int32 Count = 0;
		int32 Last = INDEX_NONE;
		for (int32 At = Text.Find(TEXT("Play2D(")); At != INDEX_NONE; At = Text.Find(TEXT("Play2D("), ESearchCase::CaseSensitive, ESearchDir::FromStart, At + 1))
		{
			++Count;
			Last = At;
		}
		const int32 BodyAt = Text.Find(Source.Body);
		const int32 GateAt = BodyAt == INDEX_NONE ? INDEX_NONE
			: Text.Find(Source.Gate, ESearchCase::CaseSensitive, ESearchDir::FromStart, BodyAt);
		TestEqual(*FString::Printf(TEXT("%s 소리 = 한 곳"), Source.File), Count, 1);
		TestTrue(*FString::Printf(TEXT("%s 소리는 거절 길 뒤 (함수 %d · 길 %d · 소리 %d)"), Source.File, BodyAt, GateAt, Last),
			BodyAt != INDEX_NONE && GateAt != INDEX_NONE && Last > GateAt);
		Lines.Add(FString::Printf(TEXT("%s 소리 %d곳 · 거절 길 뒤 %s"), Source.File, Count,
			GateAt != INDEX_NONE && Last > GateAt ? TEXT("예") : TEXT("아니오")));
	}
	AddInfo(FString::Printf(TEXT("[CrateSoundGate] %s"), *FString::Join(Lines, TEXT(" / "))));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
