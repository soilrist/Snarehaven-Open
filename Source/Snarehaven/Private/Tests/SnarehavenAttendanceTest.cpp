// DESIGN 11 「7일 출석」 · ROADMAP M21-05b 2층 검사 — 출석 달력 화면(`UAttendanceScreenWidget`).
// 잡는 것: ① 펴면 `GetAttendance` 를 묻고 답을 칸 상태(받음·오늘·남음)로 읽는다 ② 「받기」 = `ClaimAttendance`
// ③ **답 전에 코인·칸을 안 바꾸고 「받기」 를 잠근다** ④ 성공 답 뒤 달력을 다시 묻는다 ⑤ 오늘 받았으면 버튼이 꺼진다
// ⑥ 7일째를 받은 날은 일곱 칸 다 받음 ⑦ 두 번째 모자 칸은 서버 `next`(코인) ⑧ 거절·깨진 답.
// 서버는 `USnareBackendSubsystem::TestServerOverride` 가짜로 대신한다. 검사 끝에 반드시 비운다.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Data/ItemRow.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/AttendanceScreenWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "UI/SnareUiStyle.h"

namespace
{
	// 검사 전용 값 (Verification.md 6) — 서버 표와 다른 숫자를 일부러 쓴다(밸런스가 바뀌어도 안 깨지게).
	const TCHAR* AttendanceRewardsJson = TEXT("\"rewards\":[{\"coins\":11},{\"coins\":12},{\"coins\":13},"
		"{\"itemId\":\"item.test_seed\",\"count\":4},{\"coins\":15},{\"coins\":16},{\"itemId\":\"item.test_hat\",\"count\":1}]");
	constexpr int32 TestCoins = 9;

	struct FAttendanceCall
	{
		FString FunctionName;
		USnareBackendSubsystem::FServerCallDone OnDone;
	};

	TSharedPtr<FJsonObject> ParseJson(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object);
		return Object;
	}

	TSharedPtr<FJsonObject> MakeReply(int32 NextSlot, bool bClaimedToday, const TCHAR* NextJson)
	{
		return ParseJson(FString::Printf(TEXT("{\"day\":20000,\"nextSlot\":%d,\"claimedToday\":%s,\"next\":%s,%s}"),
			NextSlot, bClaimedToday ? TEXT("true") : TEXT("false"), NextJson, AttendanceRewardsJson));
	}

	FString DescribeStates(const UAttendanceScreenWidget& Screen)
	{
		FString Out;
		for (int32 Slot = 1; Slot <= 7; ++Slot)
		{
			const EAttendanceSlotState State = Screen.GetSlotState(Slot);
			Out += State == EAttendanceSlotState::Claimed ? TEXT("O") : State == EAttendanceSlotState::Today ? TEXT("*") : TEXT(".");
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAttendanceScreenTest, "Snarehaven.Attendance.Screen.WaitsForServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAttendanceScreenTest::RunTest(const FString& Parameters)
{
	TArray<FAttendanceCall> Calls;
	USnareBackendSubsystem::TestServerOverride =
		[&Calls](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			FAttendanceCall Call;
			Call.FunctionName = FunctionName;
			Call.OnDone = MoveTemp(OnDone);
			Calls.Add(MoveTemp(Call));
		};

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	UAttendanceScreenWidget* const Screen = CreateWidget<UAttendanceScreenWidget>(World, UAttendanceScreenWidget::StaticClass());
	const TSharedPtr<SWidget> Slate = Screen->TakeWidget();

	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	Snapshot.CoinBalance = TestCoins;
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);

	const UButton* const ClaimButton = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("AttendanceClaimButton")));
	TestNotNull(TEXT("「받기」 버튼이 트리에 있다"), ClaimButton);
	// 여는 버튼은 배낭 버튼처럼 달력 아이콘 버튼(감독 보정 10-06 02:45) + 아래 라벨.
	const UImage* const OpenIcon = Cast<UImage>(Screen->WidgetTree->FindWidget(TEXT("AttendanceOpenIcon")));
	const UObject* const OpenIconTexture = OpenIcon != nullptr ? OpenIcon->GetBrush().GetResourceObject() : nullptr;
	TestTrue(TEXT("여는 버튼에 달력 그림"), OpenIconTexture != nullptr && OpenIconTexture->GetName() == TEXT("T_UI_Calendar"));
	TestNull(TEXT("여는 버튼에 아이콘 대신 쓰던 큰 글자 없음"), Screen->WidgetTree->FindWidget(TEXT("AttendanceOpenButtonText")));
	// 아이콘 아래 작은 라벨 「출석」(HUD 기둥 공용 — 처음 하는 사람이 그림만으론 뜻을 모른다).
	TestNotNull(TEXT("여는 버튼 아이콘 아래 라벨"), Screen->WidgetTree->FindWidget(TEXT("AttendanceOpenLabel")));
	AddInfo(FString::Printf(TEXT("[AttendanceOpenIcon] %s"), OpenIconTexture != nullptr ? *OpenIconTexture->GetPathName() : TEXT("없음")));
	TestFalse(TEXT("답 전엔 칸을 모른다"), Screen->IsKnown());
	TestFalse(TEXT("답 전엔 받을 수 없다"), Screen->CanClaim());

	// ① 펴면 달력을 묻는다 — 3일째 차례(1·2일 받음).
	Screen->SetOpen(true);
	TestEqual(TEXT("펴면 서버를 한 번 부른다"), Calls.Num(), 1);
	TestEqual(TEXT("달력 함수"), Calls.Num() > 0 ? Calls[0].FunctionName : FString(), FString(TEXT("GetAttendance")));
	if (Calls.Num() > 0)
	{
		Calls[0].OnDone(true, MakeReply(3, false, TEXT("{\"coins\":13}")), FString());
	}
	TestTrue(TEXT("답 뒤엔 칸을 안다"), Screen->IsKnown());
	TestEqual(TEXT("칸 상태 — 1·2 받음, 3 오늘"), DescribeStates(*Screen), FString(TEXT("OO*....")));
	TestEqual(TEXT("4일째는 아이템 칸"), Screen->GetSlotReward(4).ItemID, FName(TEXT("item.test_seed")));
	TestEqual(TEXT("4일째 개수"), Screen->GetSlotReward(4).Count, 4);
	TestEqual(TEXT("3일째 코인"), Screen->GetSlotReward(3).Coins, 13);
	AddInfo(FString::Printf(TEXT("칸: %s / 4일째 '%s'"), *DescribeStates(*Screen), *Screen->GetRewardText(Screen->GetSlotReward(4)).ToString()));
	TestTrue(TEXT("오늘 칸이 있으면 받을 수 있다"), Screen->CanClaim());
	TestTrue(TEXT("「받기」 가 켜진다"), ClaimButton != nullptr && ClaimButton->GetIsEnabled());

	// ② 받기 — `ClaimAttendance`. ③ 답 전엔 코인·칸 그대로, 잠김.
	TestTrue(TEXT("받기 요청을 띄운다"), Screen->RequestClaim());
	TestEqual(TEXT("받기 함수"), Calls.Num() > 1 ? Calls[1].FunctionName : FString(), FString(TEXT("ClaimAttendance")));
	TestEqual(TEXT("답 전 코인 그대로"), Backend->GetPlayerSnapshot().CoinBalance, TestCoins);
	TestEqual(TEXT("답 전 칸 그대로"), DescribeStates(*Screen), FString(TEXT("OO*....")));
	TestFalse(TEXT("답 전엔 또 못 띄운다"), Screen->RequestClaim());
	TestTrue(TEXT("답 전엔 「받기」 가 꺼진다"), ClaimButton != nullptr && !ClaimButton->GetIsEnabled());

	// ④ 성공 답 → 달력을 다시 묻는다. 코인은 화면이 손대지 않는다.
	if (Calls.Num() > 1)
	{
		Calls[1].OnDone(true, ParseJson(TEXT("{\"day\":20000,\"slot\":3,\"reward\":{\"coins\":13}}")), FString());
	}
	TestFalse(TEXT("받은 문장이 남는다"), Screen->GetMessage().IsEmpty());
	TestEqual(TEXT("화면은 코인을 안 바꾼다"), Backend->GetPlayerSnapshot().CoinBalance, TestCoins);
	TestEqual(TEXT("받은 뒤 달력을 다시 묻는다"), Calls.Num() > 2 ? Calls[2].FunctionName : FString(), FString(TEXT("GetAttendance")));
	if (Calls.Num() > 2)
	{
		Calls[2].OnDone(true, MakeReply(4, true, TEXT("{\"itemId\":\"item.test_seed\",\"count\":4}")), FString());
	}
	// ⑤ 오늘 받았으면 오늘 칸이 없고 버튼이 꺼진다.
	TestEqual(TEXT("받은 뒤 — 1~3 받음, 오늘 칸 없음"), DescribeStates(*Screen), FString(TEXT("OOO....")));
	TestFalse(TEXT("오늘 받았으면 못 받는다"), Screen->CanClaim());
	TestFalse(TEXT("오늘 받았으면 요청을 안 띄운다"), Screen->RequestClaim());
	TestTrue(TEXT("오늘 받았으면 「받기」 가 꺼진다"), ClaimButton != nullptr && !ClaimButton->GetIsEnabled());

	// ⑥ 7일째를 받은 날 — 서버 nextSlot 은 1로 돌았지만 일곱 칸 다 받음.
	TestTrue(TEXT("7일째 받은 날 답"), Screen->ApplyAttendanceReply(MakeReply(1, true, TEXT("{\"coins\":11}"))));
	TestEqual(TEXT("7일째 받은 날 — 일곱 칸 다 받음"), DescribeStates(*Screen), FString(TEXT("OOOOOOO")));
	// 다음 날 — 처음부터(아무것도 안 받음, 1일 오늘).
	TestTrue(TEXT("8일째 답"), Screen->ApplyAttendanceReply(MakeReply(1, false, TEXT("{\"coins\":11}"))));
	TestEqual(TEXT("8일째 — 1일이 오늘"), DescribeStates(*Screen), FString(TEXT("*......")));

	// ⑦ 두 번째 모자 — 7일째 칸은 서버 `next` 코인으로 보인다.
	TestTrue(TEXT("모자 두 번째 답"), Screen->ApplyAttendanceReply(MakeReply(7, false, TEXT("{\"coins\":16}"))));
	TestTrue(TEXT("7일째 칸이 아이템이 아니다"), Screen->GetSlotReward(7).ItemID.IsNone());
	TestEqual(TEXT("7일째 칸 = 서버 next 코인"), Screen->GetSlotReward(7).Coins, 16);

	// ⑧ 거절 — 사유가 남고 달력을 다시 묻는다. 깨진 답은 안 옮긴다.
	TestTrue(TEXT("거절 시험용 받기"), Screen->RequestClaim());
	const int32 BeforeReject = Calls.Num();
	if (BeforeReject > 0)
	{
		Calls.Last().OnDone(true, ParseJson(TEXT("{\"error\":\"가방에 자리가 없다\",\"code\":\"inventory_full\"}")), FString());
	}
	TestFalse(TEXT("거절 사유를 삼키지 않는다"), Screen->GetMessage().IsEmpty());
	TestEqual(TEXT("거절 뒤 달력을 다시 묻는다"), Calls.Num() > BeforeReject ? Calls.Last().FunctionName : FString(), FString(TEXT("GetAttendance")));
	TestFalse(TEXT("rewards 없는 답은 안 옮긴다"), Screen->ApplyAttendanceReply(ParseJson(TEXT("{\"nextSlot\":2}"))));
	TestFalse(TEXT("칸 수가 7이 아닌 답은 안 옮긴다"),
		Screen->ApplyAttendanceReply(ParseJson(TEXT("{\"nextSlot\":2,\"rewards\":[{\"coins\":1}]}"))));
	TestEqual(TEXT("깨진 답 뒤에도 앞 칸 그대로"), Screen->GetSlotReward(7).Coins, 16);

	USnareBackendSubsystem::TestServerOverride = nullptr;
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// M23-05 칸 보상 원판 · 원 ID 새지 않게 (ROADMAP M23-05, 시트 `inventory_backpack.md` 「아이템은 색 원판 위 그림」).
// 잡는 것: ① 일곱 칸 글자 어디에도 `item.` 이 없다 ② 아이템 표에 없는 ID → `보상` ③ 아이템 칸 원판 = 등급 색 · 그림 = 아이템 표
// `IconRef` ④ 코인 칸 그림 = 재화 그림(`T_Icon_CashBundle`) ⑤ 답 전엔 그림 0개.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAttendanceDiscTest, "Snarehaven.Attendance.Screen.SlotsShowRewardDiscs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAttendanceDiscTest::RunTest(const FString& Parameters)
{
	UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	UTexture2D* const CoinTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Game/UI/Icons/Items/T_Icon_CashBundle.T_Icon_CashBundle"));
	if (!TestNotNull(TEXT("DT_ItemTable"), ItemTable) || !TestNotNull(TEXT("코인 그림"), CoinTexture))
	{
		return false;
	}
	// 그림·이름이 있는 아무 행 하나 — 표 값이 바뀌어도 안 깨지게 고르기만 한다.
	const FItemRow* RealRow = nullptr;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && !Row->ItemID.IsNone() && !Row->IconRef.IsNull() && !Row->DisplayName.IsEmpty())
		{
			RealRow = Row;
			break;
		}
	}
	if (!TestNotNull(TEXT("그림 있는 아이템 행"), RealRow))
	{
		return false;
	}
	UTexture2D* const RealTexture = RealRow->IconRef.LoadSynchronous();   // 비동기 그림을 그 자리에서 붙게
	const FString RealID = RealRow->ItemID.ToString();
	const FName MissingID(TEXT("item.test_not_in_table"));

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	UAttendanceScreenWidget* const Screen = CreateWidget<UAttendanceScreenWidget>(World, UAttendanceScreenWidget::StaticClass());
	const TSharedPtr<SWidget> Slate = Screen->TakeWidget();

	auto FindIcon = [Screen](int32 Slot)
	{
		return Screen->WidgetTree->FindWidget<UImage>(FName(*FString::Printf(TEXT("AttendanceSlotIcon_%d"), Slot)));
	};
	int32 IconsBefore = 0;
	for (int32 Slot = 1; Slot <= 7; ++Slot)
	{
		TestNotNull(*FString::Printf(TEXT("답 전 %d일 원판"), Slot),
			Screen->WidgetTree->FindWidget<UBorder>(FName(*FString::Printf(TEXT("AttendanceSlotDisc_%d"), Slot))));
		IconsBefore += FindIcon(Slot) != nullptr ? 1 : 0;
	}
	TestEqual(TEXT("답 전엔 그림 0개"), IconsBefore, 0);

	// 1·3·5·6 코인 · 2 표에 있는 아이템 · 4·7 표에 없는 ID (검사 전용 값).
	const TSharedPtr<FJsonObject> Reply = ParseJson(FString::Printf(TEXT("{\"nextSlot\":2,\"claimedToday\":false,"
		"\"next\":{\"itemId\":\"%s\",\"count\":2},\"rewards\":[{\"coins\":11},{\"itemId\":\"%s\",\"count\":2},"
		"{\"coins\":13},{\"itemId\":\"%s\",\"count\":3},{\"coins\":15},{\"coins\":16},{\"itemId\":\"%s\",\"count\":1}]}"),
		*RealID, *RealID, *MissingID.ToString(), *MissingID.ToString()));
	if (!TestTrue(TEXT("달력 답을 옮긴다"), Screen->ApplyAttendanceReply(Reply)))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	int32 RawIDTexts = 0;
	int32 Icons = 0;
	TArray<FString> Lines;
	for (int32 Slot = 1; Slot <= 7; ++Slot)
	{
		for (const TCHAR* Kind : {TEXT("Day"), TEXT("Reward"), TEXT("State")})
		{
			const UTextBlock* const Text = Screen->WidgetTree->FindWidget<UTextBlock>(
				FName(*FString::Printf(TEXT("AttendanceSlot%s_%d"), Kind, Slot)));
			const FString Shown = Text != nullptr ? Text->GetText().ToString() : FString();
			RawIDTexts += Shown.Contains(TEXT("item.")) ? 1 : 0;
			if (FCString::Strcmp(Kind, TEXT("Reward")) == 0)
			{
				Lines.Add(Shown);
			}
		}
		Icons += FindIcon(Slot) != nullptr ? 1 : 0;
	}
	TestEqual(TEXT("일곱 칸 글자에 `item.` 0"), RawIDTexts, 0);
	TestEqual(TEXT("표에 없는 ID 칸(4일) = 보상 x3"), Lines.IsValidIndex(3) ? Lines[3] : FString(), FString(TEXT("보상 x3")));
	TestEqual(TEXT("표에 없는 ID 칸(7일) = 보상"), Lines.IsValidIndex(6) ? Lines[6] : FString(), FString(TEXT("보상")));
	TestTrue(TEXT("표에 있는 아이템 칸(2일) = 표 이름"),
		Lines.IsValidIndex(1) && Lines[1].StartsWith(RealRow->DisplayName.ToString()));

	const UBorder* const RealDisc = Screen->WidgetTree->FindWidget<UBorder>(TEXT("AttendanceSlotDisc_2"));
	if (TestNotNull(TEXT("2일 원판"), RealDisc))
	{
		TestEqual(TEXT("2일 원판 색 = 등급 색"), RealDisc->Background.TintColor.GetSpecifiedColor(),
			SnareUiStyle::ItemGradeColor(RealRow->Grade));
	}
	const UImage* const RealIcon = FindIcon(2);
	TestTrue(TEXT("2일 그림 = 아이템 표 IconRef"),
		RealIcon != nullptr && RealIcon->GetBrush().GetResourceObject() == static_cast<UObject*>(RealTexture));
	const UImage* const CoinIcon = FindIcon(1);
	TestTrue(TEXT("1일 코인 그림 = 재화 그림"),
		CoinIcon != nullptr && CoinIcon->GetBrush().GetResourceObject() == static_cast<UObject*>(CoinTexture));
	TestNull(TEXT("표에 없는 ID 칸엔 그림이 없다"), FindIcon(4));
	TestEqual(TEXT("그림 수 = 코인 4 + 표에 있는 아이템 1"), Icons, 5);
	AddInfo(FString::Printf(TEXT("[AttendanceDisc] 칸 글자 %s · item. %d · 그림 %d · 2일 그림 %s"),
		*FString::Join(Lines, TEXT(" | ")), RawIDTexts, Icons, RealIcon != nullptr ? *GetNameSafe(RealIcon->GetBrush().GetResourceObject()) : TEXT("없음")));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// M24-03 출석 판 제목 알약 (ROADMAP M24-03, 시트 `inventory_backpack.md` 「제목 탭: 진갈색 알약, 패널 왼쪽 위 모서리에 걸쳐 위로 튀어나옴」).
// 잡는 것: ① 제목 글자 부모 = 진갈색(`UiTitleTab`) 알약, 판 안 줄엔 제목 글자 0 ② 알약과 판이 한 겹에서 왼쪽 위로 맞붙고
// 알약 위 변 = 판 위 변 − `MenuTitlePillRise` ③ 판 위 변은 옛 자리 그대로(겹 자리 + 판 위 여백) ④ 칸 7개 그대로
// ⑤ 닫히면 알약도 접히고 열면 펴진다(M23-01 닫힌 상점 제목 꼴).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAttendanceTitlePillTest, "Snarehaven.Attendance.Screen.TitlePillAbovePanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAttendanceTitlePillTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	UAttendanceScreenWidget* const Screen = CreateWidget<UAttendanceScreenWidget>(World, UAttendanceScreenWidget::StaticClass());
	const TSharedPtr<SWidget> Slate = Screen->TakeWidget();
	UWidgetTree* const Tree = Screen->WidgetTree;

	UTextBlock* const Title = Tree->FindWidget<UTextBlock>(TEXT("AttendanceTitle"));
	UBorder* const Pill = Tree->FindWidget<UBorder>(TEXT("AttendanceTitlePill"));
	USizeBox* const PillBox = Tree->FindWidget<USizeBox>(TEXT("AttendanceTitlePillBox"));
	UBorder* const Panel = Tree->FindWidget<UBorder>(TEXT("AttendancePanel"));
	UOverlay* const Frame = Tree->FindWidget<UOverlay>(TEXT("AttendanceFrame"));
	if (!TestNotNull(TEXT("제목 글자"), Title) || !TestNotNull(TEXT("제목 알약"), Pill) || !TestNotNull(TEXT("알약 상자"), PillBox)
		|| !TestNotNull(TEXT("판"), Panel) || !TestNotNull(TEXT("판·알약 겹"), Frame))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// ① 제목은 알약 안 — 판 아래 어디에도 제목 글자가 없다.
	TestTrue(TEXT("제목 글자 부모 = 알약"), Title->GetParent() == Pill);
	TestEqual(TEXT("알약 색 = UiTitleTab"), Pill->Background.TintColor.GetSpecifiedColor(), SnareUiStyle::UiTitleTab());
	TestEqual(TEXT("제목 = 7일 출석"), Title->GetText().ToString(), FString(TEXT("7일 출석")));
	bool bTitleInPanel = false;
	for (const UWidget* Walk = Title; Walk != nullptr; Walk = Walk->GetParent())
	{
		bTitleInPanel |= Walk == Panel;
	}
	TestFalse(TEXT("판 안에 제목 글자 0"), bTitleInPanel);

	// ② 알약·판이 같은 겹, 알약은 왼쪽 위 · 판은 알약이 위로 나온 몫만큼 아래로.
	const UOverlaySlot* const PillSlot = Cast<UOverlaySlot>(PillBox->Slot);
	const UOverlaySlot* const PanelSlot = Cast<UOverlaySlot>(Panel->Slot);
	const UCanvasPanelSlot* const FrameSlot = Cast<UCanvasPanelSlot>(Frame->Slot);
	const bool bSameFrame = PillSlot != nullptr && PanelSlot != nullptr && PillBox->GetParent() == Frame && Panel->GetParent() == Frame;
	TestTrue(TEXT("알약·판 같은 겹"), bSameFrame);
	TestTrue(TEXT("알약 뒤에 넣어 판 위에 그린다"), Frame->GetChildIndex(PillBox) > Frame->GetChildIndex(Panel));
	const float PillTop = 0.f;
	const float PanelTop = PanelSlot != nullptr ? PanelSlot->GetPadding().Top : -1.f;
	TestTrue(TEXT("알약 왼쪽 위 정렬"), PillSlot != nullptr && PillSlot->GetHorizontalAlignment() == HAlign_Left
		&& PillSlot->GetVerticalAlignment() == VAlign_Top && PillSlot->GetPadding() == FMargin(0.f));
	TestTrue(TEXT("판 왼쪽 여백 0 — 알약 왼 변 = 판 왼 변"), PanelSlot != nullptr && PanelSlot->GetPadding().Left == 0.f);
	TestEqual(TEXT("알약 위 변 = 판 위 변 - MenuTitlePillRise"), PillTop, PanelTop - SnareUiStyle::MenuTitlePillRise);
	TestTrue(TEXT("알약 높이가 판 위로 나온 몫보다 크다(판에 걸친다)"), PillBox->GetHeightOverride() > SnareUiStyle::MenuTitlePillRise);
	TestTrue(TEXT("판 안 위 여백이 알약이 내려온 몫을 덮는다"),
		Panel->GetPadding().Top >= PillBox->GetHeightOverride() - SnareUiStyle::MenuTitlePillRise);
	// ③ 판 위 변 화면 자리 = 겹 자리 + 판 위 여백 (M24-03 전 판 자리 160 그대로 — 검사 전용 비교값).
	const float ScreenPanelTop = FrameSlot != nullptr ? FrameSlot->GetPosition().Y + PanelTop : -1.f;
	TestEqual(TEXT("판 위 변 화면 자리 그대로"), ScreenPanelTop, 160.f);

	// ④ 칸 7개 그대로.
	int32 Discs = 0;
	for (int32 Slot = 1; Slot <= 7; ++Slot)
	{
		Discs += Tree->FindWidget<UBorder>(FName(*FString::Printf(TEXT("AttendanceSlotDisc_%d"), Slot))) != nullptr ? 1 : 0;
	}
	TestEqual(TEXT("칸 7개"), Discs, 7);

	// ⑤ 닫힘 → 알약 접힘 · 열림 → 펴짐.
	Screen->SetOpen(false);
	const ESlateVisibility Closed = PillBox->GetVisibility();
	TestEqual(TEXT("닫힌 판 알약 접힘"), Closed, ESlateVisibility::Collapsed);
	Screen->SetOpen(true);
	const ESlateVisibility Opened = PillBox->GetVisibility();
	TestTrue(TEXT("연 판 알약 보임"), Opened != ESlateVisibility::Collapsed && Opened != ESlateVisibility::Hidden);
	Screen->SetOpen(false);

	AddInfo(FString::Printf(TEXT("[AttendanceTitle] 알약 위 %.0f · 판 위 %.0f(화면 %.0f) · 제목 %s · 판 안 제목 %d · 칸 %d · 닫힘 %s · 열림 %s"),
		PillTop, PanelTop, ScreenPanelTop, *Title->GetText().ToString(), bTitleInPanel ? 1 : 0, Discs,
		*UEnum::GetValueAsString(Closed), *UEnum::GetValueAsString(Opened)));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAttendanceRetryTest, "Snarehaven.Attendance.Retry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAttendanceRetryTest::RunTest(const FString& Parameters)
{
	// 달력을 못 받으면 이유 곁에 「다시 시도」 — 누르면 `GetAttendance` 를 다시 묻는다.
	TArray<FAttendanceCall> Calls;
	USnareBackendSubsystem::TestServerOverride =
		[&Calls](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			Calls.Add({FunctionName, MoveTemp(OnDone)});
		};

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	UAttendanceScreenWidget* const Screen = CreateWidget<UAttendanceScreenWidget>(World, UAttendanceScreenWidget::StaticClass());
	const TSharedPtr<SWidget> Slate = Screen->TakeWidget();
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	Snapshot.CoinBalance = TestCoins;
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);

	const UWidget* const RetryBox = Screen->WidgetTree->FindWidget(TEXT("AttendanceRetryBox"));
	UButton* const RetryButton = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("AttendanceRetryButton")));
	TestNotNull(TEXT("다시 시도 단추가 있다"), RetryButton);
	if (RetryBox != nullptr && RetryButton != nullptr)
	{
		Screen->SetOpen(true);
		TestFalse(TEXT("묻는 중엔 숨김"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
		if (Calls.Num() > 0)
		{
			Calls.Last().OnDone(false, nullptr, TEXT("net"));
		}
		TestTrue(TEXT("달력 실패 → 다시 시도 보임"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
		const int32 Before = Calls.Num();
		RetryButton->OnClicked.Broadcast();
		TestEqual(TEXT("다시 시도 → GetAttendance 다시"), Calls.Num() > Before ? Calls.Last().FunctionName : FString(), FString(TEXT("GetAttendance")));
		TestFalse(TEXT("누른 뒤 숨김"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
		if (Calls.Num() > Before)
		{
			Calls.Last().OnDone(true, MakeReply(3, false, TEXT("{\"coins\":13}")), FString());
		}
		TestTrue(TEXT("다시 받은 달력을 읽는다"), Screen->IsKnown());
		TestFalse(TEXT("성공 뒤 숨김"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
	}

	USnareBackendSubsystem::TestServerOverride = nullptr;
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
