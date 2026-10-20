// ROADMAP M3-04 2층 검사 — 로비 화면.
// 이 검사가 잡는 것: ① 로그인 전에 방 만들기 버튼이 눌려 EOS 가 계정 없이 불리는 것
// ② 이미 방에 있는데 또 만들기·찾기를 눌러 방이 둘이 되는 것 ③ 찾은 방이 0개인데 참가가 눌리는 것
// ④ 로비 자리표시 트리에 버튼 넷·상태 줄이 실제로 안 들어가는 것
// ⑤ 방이 해산돼 돌아왔는데 화면이 그 사실을 한 마디도 안 하는 것.
// 실제 EOS 왕복은 3층(두 프로세스)이 본다.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "SnarehavenGameMode.h"
#include "UI/LobbyWidget.h"
#include "UI/SnareIntroCardsWidget.h"
#include "Backend/SnareStory.h"
#include "UI/SnareUiStyle.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLobbyButtonRulesTest, "Snarehaven.Lobby.ButtonsFollowRoomState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLobbyButtonRulesTest::RunTest(const FString& Parameters)
{
	// 로그인이 끝나야 방을 만들 계정이 있다. 여기가 참이 되면 계정 없이 EOS 를 불러 실패 로그만 쌓인다.
	TestFalse(TEXT("시작 전엔 방을 못 연다"),
		ULobbyWidget::CanOpenRoom(ESnareLoginState::NotStarted, ESnareRoomState::None));
	TestFalse(TEXT("접속 중에도 못 연다"),
		ULobbyWidget::CanOpenRoom(ESnareLoginState::InProgress, ESnareRoomState::None));
	TestFalse(TEXT("접속 실패면 못 연다"),
		ULobbyWidget::CanOpenRoom(ESnareLoginState::Failed, ESnareRoomState::None));
	TestTrue(TEXT("접속됐고 방이 없으면 연다"),
		ULobbyWidget::CanOpenRoom(ESnareLoginState::LoggedIn, ESnareRoomState::None));

	// 한 프로세스는 방을 하나만 갖는다(`RoomSessionName` 고정) — 두 번째 호출은 EOS 가 거절한다.
	TestFalse(TEXT("만드는 중엔 다시 못 누른다"),
		ULobbyWidget::CanOpenRoom(ESnareLoginState::LoggedIn, ESnareRoomState::Creating));
	TestFalse(TEXT("이미 호스트면 못 누른다"),
		ULobbyWidget::CanOpenRoom(ESnareLoginState::LoggedIn, ESnareRoomState::Hosting));
	TestFalse(TEXT("이미 들어가 있으면 못 누른다"),
		ULobbyWidget::CanOpenRoom(ESnareLoginState::LoggedIn, ESnareRoomState::Joined));
	// 실패한 방은 막다른 길이 아니다 — 다시 시도할 수 있어야 로비가 쓸모 있다.
	TestTrue(TEXT("한 번 실패한 뒤에는 다시 연다"),
		ULobbyWidget::CanOpenRoom(ESnareLoginState::LoggedIn, ESnareRoomState::Failed));

	// 찾기 결과 0개는 오류가 아니라 "들어갈 방이 없다" 다 (명세 net-eos-session.md 2장).
	TestFalse(TEXT("찾은 방이 없으면 참가가 꺼진다"),
		ULobbyWidget::CanJoinRoom(ESnareRoomState::None, 0));
	TestTrue(TEXT("찾은 방이 있으면 참가한다"),
		ULobbyWidget::CanJoinRoom(ESnareRoomState::None, 1));
	TestFalse(TEXT("들어가는 중엔 또 못 누른다"),
		ULobbyWidget::CanJoinRoom(ESnareRoomState::Joining, 3));
	TestFalse(TEXT("내가 호스트면 내 방에 참가하지 않는다"),
		ULobbyWidget::CanJoinRoom(ESnareRoomState::Hosting, 3));

	// 꺼진 참가 단추는 말이 없다 — 이유 한 줄이 있어야 한다. 켜졌거나 방이 움직이는 중이면 없다.
	TestFalse(TEXT("방이 없어 꺼졌으면 이유가 있다"),
		ULobbyWidget::MakeJoinDisabledReason(true, ESnareRoomState::None, 0).IsEmpty());
	TestFalse(TEXT("온라인이 없어 꺼졌으면 이유가 있다"),
		ULobbyWidget::MakeJoinDisabledReason(false, ESnareRoomState::None, 0).IsEmpty());
	TestTrue(TEXT("참가가 켜져 있으면 이유가 없다"),
		ULobbyWidget::MakeJoinDisabledReason(true, ESnareRoomState::None, 2).IsEmpty());
	TestTrue(TEXT("방 만드는 중엔 상태 줄이 말해서 이유를 덧붙이지 않는다"),
		ULobbyWidget::MakeJoinDisabledReason(true, ESnareRoomState::Creating, 0).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLobbyStatusTextTest, "Snarehaven.Lobby.StatusTextTellsWhatHappened",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLobbyStatusTextTest::RunTest(const FString& Parameters)
{
	// 상태 줄이 비면 사람은 눌러도 되는지 알 수 없다 — 어떤 조합에서도 빈 글자가 나오면 안 된다.
	const ESnareLoginState LoginStates[] = { ESnareLoginState::NotStarted, ESnareLoginState::InProgress,
		ESnareLoginState::LoggedIn, ESnareLoginState::Failed };
	const ESnareRoomState RoomStates[] = { ESnareRoomState::None, ESnareRoomState::Creating, ESnareRoomState::Hosting,
		ESnareRoomState::Searching, ESnareRoomState::Joining, ESnareRoomState::Joined, ESnareRoomState::Failed };
	for (const ESnareLoginState LoginState : LoginStates)
	{
		for (const ESnareRoomState RoomState : RoomStates)
		{
			TestFalse(TEXT("상태 줄이 비어 있지 않다"),
				ULobbyWidget::MakeStatusText(LoginState, RoomState, 0, /*bReturnedFromRoom=*/false).IsEmpty());
		}
	}

	// M17-02: EOS 로그인 실패를 "접속 실패" 라 적으면 PlayFab 저장까지 끊긴 것처럼 읽힌다.
	const FString FailedLoginText =
		ULobbyWidget::MakeStatusText(ESnareLoginState::Failed, ESnareRoomState::None, 0, /*bReturnedFromRoom=*/false).ToString();
	TestTrue(TEXT("EOS 로그인 실패는 서버 연결 실패라 적는다"), FailedLoginText.Contains(TEXT("서버에 연결하지 못했어요")));
	TestFalse(TEXT("\"접속 실패\" 라 적지 않는다"), FailedLoginText.Contains(TEXT("접속 실패")));

	// M3-03: 방이 터져서 돌아온 사람에게 그 사실을 말해 주지 않으면 자기가 잘못 눌렀다고 생각한다.
	const FText ReturnedText =
		ULobbyWidget::MakeStatusText(ESnareLoginState::LoggedIn, ESnareRoomState::None, 0, /*bReturnedFromRoom=*/true);
	const FText PlainText =
		ULobbyWidget::MakeStatusText(ESnareLoginState::LoggedIn, ESnareRoomState::None, 0, /*bReturnedFromRoom=*/false);
	TestFalse(TEXT("방 해산 안내는 평소 글자와 다르다"), ReturnedText.EqualTo(PlainText));

	// 새 소식이 생기면 옛 안내가 이긴 채로 남으면 안 된다 — 방을 다시 만들기 시작하면 그게 보여야 한다.
	const FText CreatingText =
		ULobbyWidget::MakeStatusText(ESnareLoginState::LoggedIn, ESnareRoomState::Creating, 0, /*bReturnedFromRoom=*/true);
	TestFalse(TEXT("방이 다시 움직이면 새 소식이 이긴다"), CreatingText.EqualTo(ReturnedText));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLobbyPlaceholderTreeTest, "Snarehaven.Lobby.PlaceholderTreeHasButtons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLobbyPlaceholderTreeTest::RunTest(const FString& Parameters)
{
	// WBP 에 디자인이 없어도 로비는 화면에 떠야 한다(3층 캡처가 이걸 찍는다).
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	ULobbyWidget* Lobby = CreateWidget<ULobbyWidget>(World, ULobbyWidget::StaticClass());
	if (Lobby == nullptr)
	{
		AddError(TEXT("로비 위젯을 만들지 못했다"));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// Slate 위젯을 실제로 만들어야 RebuildWidget 이 돌고, 그때 자리표시 트리가 세워진다.
	Lobby->TakeWidget();

	// 게임 이름은 큰 로고 하나 — 판 위 작은 제목 탭은 화면에 없다 (정체성 감독 지적).
	TestNotNull(TEXT("큰 로고가 있다"), Lobby->WidgetTree->FindWidget(FName(TEXT("LobbyLogo"))));
	{
		const UWidget* const Pill = Lobby->WidgetTree->FindWidget(FName(TEXT("MenuTitlePill")));
		TestTrue(TEXT("작은 제목 탭은 화면에 없다"), Pill == nullptr || Pill->GetParent() == nullptr);
	}

	// 버튼 넷 중 하나라도 빠지면 그 길은 화면에서 영영 닿을 수 없다.
	for (const TCHAR* ButtonName : { TEXT("HostButton"), TEXT("FindButton"), TEXT("JoinButton"), TEXT("SoloButton"),
		TEXT("SettingsButton"), TEXT("DeleteAccountButton"), TEXT("DeleteCancelButton") })
	{
		TestNotNull(FString::Printf(TEXT("자리표시에 %s 가 있다"), ButtonName),
			Lobby->WidgetTree->FindWidget<UButton>(FName(ButtonName)));
	}
	// 계정 삭제 단추는 확인 전(0단계)에 「닉네임 바꾸기」·「뒤로」 와 같은 폭·가운데여야 한다 (심사 2.1 — 쏠리면 미완성처럼 보임).
	{
		UButton* const DeleteButton = Lobby->WidgetTree->FindWidget<UButton>(TEXT("DeleteAccountButton"));
		UButton* const BackButton = Lobby->WidgetTree->FindWidget<UButton>(TEXT("SettingsButton"));
		UButton* const NickButton = Lobby->WidgetTree->FindWidget<UButton>(TEXT("NicknameEditButton"));
		const UCanvasPanelSlot* DeleteSlot = DeleteButton ? Cast<UCanvasPanelSlot>(DeleteButton->Slot) : nullptr;
		const UCanvasPanelSlot* BackSlot = BackButton ? Cast<UCanvasPanelSlot>(BackButton->Slot) : nullptr;
		const UCanvasPanelSlot* NickSlot = NickButton ? Cast<UCanvasPanelSlot>(NickButton->Slot) : nullptr;
		if (TestNotNull(TEXT("계정 삭제 슬롯"), DeleteSlot) && TestNotNull(TEXT("뒤로 슬롯"), BackSlot) && TestNotNull(TEXT("닉네임 슬롯"), NickSlot))
		{
			TestEqual(TEXT("계정 삭제 폭 = 뒤로 폭"), DeleteSlot->GetSize().X, BackSlot->GetSize().X);
			TestEqual(TEXT("계정 삭제 폭 = 닉네임 바꾸기 폭"), DeleteSlot->GetSize().X, NickSlot->GetSize().X);
			TestEqual(TEXT("계정 삭제 가운데 = 뒤로 가운데"), DeleteSlot->GetPosition().X, BackSlot->GetPosition().X);
		}
	}
	TestNotNull(TEXT("자리표시에 상태 줄이 있다"), Lobby->WidgetTree->FindWidget<UTextBlock>(TEXT("StatusText")));

	// 서브시스템이 없는 검사 월드에서는 아무 버튼도 켜지면 안 된다 — 누르면 널을 만진다.
	TestFalse(TEXT("온라인이 없으면 방 만들기가 꺼져 있다"), Lobby->IsOpenRoomEnabled());
	TestFalse(TEXT("온라인이 없으면 참가가 꺼져 있다"), Lobby->IsJoinEnabled());

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLobbyPanelSpecTest, "Snarehaven.Lobby.Tree.FollowsPanelSpec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M15-24 — 시트가 없는 화면이라 `hud_ingame`·`inventory_backpack` 규격을 준용한다(README "아직 없는 시트").
// 판 색·모서리·테두리 없음·버튼이 판 안에 듦을 **실제 트리 값**으로 잰다. 게임 이름은 판 밖 큰 로고(크림+진갈색 외곽선).
bool FSnarehavenLobbyPanelSpecTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	ULobbyWidget* Screen = CreateWidget<ULobbyWidget>(World, ULobbyWidget::StaticClass());
	if (!TestNotNull(TEXT("로비 위젯"), Screen))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	Screen->TakeWidget();

	const UBorder* const Panel = Screen->WidgetTree->FindWidget<UBorder>(TEXT("MenuPanel"));
	const UTextBlock* const Title = Screen->WidgetTree->FindWidget<UTextBlock>(TEXT("LogoText"));
	if (TestNotNull(TEXT("크림 판"), Panel) && TestNotNull(TEXT("로고 글자"), Title))
	{
		// 시트 값 (검사 전용 사본): 판 #F5EBD2 · 탭 #5A3E36 · 모서리 12~16px.
		const FLinearColor SheetCream = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#F5EBD2")));
		const FLinearColor SheetTab = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#5A3E36")));
		TestTrue(TEXT("판 색 #F5EBD2"), Panel->GetBrushColor().Equals(SheetCream, 0.001f));
		const FSlateBrush& PanelBrush = Panel->Background;
		TestEqual(TEXT("판 모양은 둥근 네모"), PanelBrush.DrawAs, ESlateBrushDrawType::RoundedBox);
		const float Radius = PanelBrush.OutlineSettings.CornerRadii.X;
		TestTrue(FString::Printf(TEXT("판 모서리 6px — DESIGN 7.1 (지금 %.1f)"), Radius), FMath::IsNearlyEqual(Radius, SnareUiStyle::ScreenPanelRadius));
		TestEqual(TEXT("판 테두리 없음"), PanelBrush.OutlineSettings.Width, 0.f);
		TestTrue(TEXT("로고 외곽선 #5A3E36"), Title->GetFont().OutlineSettings.OutlineColor.Equals(SheetTab, 0.001f));
		TestTrue(TEXT("로고 외곽선이 있다"), Title->GetFont().OutlineSettings.OutlineSize > 0);

		const UCanvasPanelSlot* const PanelSlot = Cast<UCanvasPanelSlot>(Panel->Slot);
		if (TestNotNull(TEXT("판 캔버스 자리"), PanelSlot))
		{
			const float PanelTop = PanelSlot->GetPosition().Y;

			// 버튼은 전부 판 안에 든다 (가로 가운데 정렬이라 폭과 세로 범위로 잰다).
			const float PanelBottom = PanelTop + PanelSlot->GetSize().Y;
			int32 ButtonCount = 0;
			Screen->WidgetTree->ForEachWidget([&](UWidget* Child)
			{
				const UButton* const Button = Cast<UButton>(Child);
				const UCanvasPanelSlot* const ButtonSlot = Button != nullptr ? Cast<UCanvasPanelSlot>(Button->Slot) : nullptr;
				if (ButtonSlot == nullptr)
				{
					return;
				}
				++ButtonCount;
				const float Top = ButtonSlot->GetPosition().Y;
				TestTrue(FString::Printf(TEXT("%s 가 판 세로 범위 안"), *Button->GetName()),
					Top >= PanelTop && Top + ButtonSlot->GetSize().Y <= PanelBottom);
				TestTrue(FString::Printf(TEXT("%s 가 판 가로 폭 안"), *Button->GetName()),
					ButtonSlot->GetSize().X <= PanelSlot->GetSize().X);
			});
			TestEqual(TEXT("판 위 버튼 수"), ButtonCount, 10); // 시작·방 셋 + 접속 다시 시도(DESIGN 4.4) + 설정 + 계정 삭제·취소(DESIGN 4.4) + 닉네임 바꾸기·확인(DESIGN 2.2)
		}
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLobbyRoundButtonsTest, "Snarehaven.Lobby.Tree.RoundButtons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M27-04 — 시트 `hud_ingame` 「모든 패널 모서리 둥글게 12~16px」. 로비 버튼 전부의 네 상태 브러시를 실제 값으로 잰다.
bool FSnarehavenLobbyRoundButtonsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	ULobbyWidget* Screen = CreateWidget<ULobbyWidget>(World, ULobbyWidget::StaticClass());
	if (!TestNotNull(TEXT("로비 위젯"), Screen))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	Screen->TakeWidget();

	const FVector4 Radius(SnareUiStyle::HudCornerRadius, SnareUiStyle::HudCornerRadius, SnareUiStyle::HudCornerRadius,
		SnareUiStyle::HudCornerRadius);
	auto IsRound = [&Radius](const FSlateBrush& Brush)
	{
		return Brush.DrawAs == ESlateBrushDrawType::RoundedBox && Brush.OutlineSettings.CornerRadii == Radius;
	};
	int32 All = 0;
	int32 Round = 0;
	Screen->WidgetTree->ForEachWidget([&](UWidget* Child)
	{
		const UButton* const Button = Cast<UButton>(Child);
		if (Button == nullptr)
		{
			return;
		}
		++All;
		const FButtonStyle& Style = Button->GetStyle();
		const bool bRound = IsRound(Style.Normal) && IsRound(Style.Hovered) && IsRound(Style.Pressed) && IsRound(Style.Disabled);
		Round += bRound ? 1 : 0;
		TestTrue(FString::Printf(TEXT("%s 네 상태 둥근 네모 · 반지름 = HudCornerRadius"), *Button->GetName()), bRound);
	});
	AddInfo(FString::Printf(TEXT("[LobbyRound] 둥근 버튼 %d/%d"), Round, All));
	TestEqual(TEXT("로비 버튼 수"), All, 10); // 시작·방 셋 + 다시 시도 + 설정 + 계정 삭제·취소 + 닉네임 바꾸기·확인 — 시험이 버튼을 다 봤는지

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLobbyNicknamePreviewTest, "Snarehaven.Lobby.NicknamePreviewSkipsServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLobbyNicknamePreviewTest::RunTest(const FString& Parameters)
{
	// 미리보기에서 「확인」 을 눌러도 올리는 중(서버 호출 표시)으로 안 넘어가고 줄이 그대로여야 한다.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	ULobbyWidget* Lobby = CreateWidget<ULobbyWidget>(World, ULobbyWidget::StaticClass());
	if (Lobby == nullptr)
	{
		AddError(TEXT("로비 위젯을 만들지 못했다"));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	Lobby->TakeWidget();
	Lobby->SetNicknamePreview(true);
	TestTrue(TEXT("미리보기면 입력 줄이 펴진다"), Lobby->IsNicknameOpen());
	UButton* const Confirm = Lobby->WidgetTree->FindWidget<UButton>(TEXT("NicknameConfirmButton"));
	if (TestNotNull(TEXT("확인 단추가 있다"), Confirm))
	{
		Confirm->OnClicked.Broadcast();
		TestTrue(TEXT("확인 뒤에도 줄이 펴져 있다(서버 답 없이 닫히지 않음)"), Lobby->IsNicknameOpen());
		TestTrue(TEXT("확인 단추가 꺼지지 않는다(올리는 중 아님)"), Confirm->GetIsEnabled());
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLobbyTaglineTest, "Snarehaven.Lobby.TaglineTellsTheGame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLobbyTaglineTest::RunTest(const FString& Parameters)
{
	// 타이틀 아래 한 줄이 비면 처음 켠 사람이 무슨 게임인지 모른다. 동물·등대 두 낱말이 들어 있어야 한다.
	const FString Tagline = ULobbyWidget::GetTagline().ToString();
	TestFalse(TEXT("한 줄이 비어 있지 않다"), Tagline.IsEmpty());
	TestTrue(TEXT("동물이 들어 있다"), Tagline.Contains(TEXT("동물")));
	TestTrue(TEXT("등대가 들어 있다"), Tagline.Contains(TEXT("등대")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLobbyLoadingTipsTest, "Snarehaven.Lobby.LoadingTipsAreWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLobbyLoadingTipsTest::RunTest(const FString& Parameters)
{
	// 팁은 5개, 비지 않고 서로 달라야 한다. 범위 밖 인덱스도 죽지 않는다.
	TestEqual(TEXT("팁은 5개"), ULobbyWidget::GetLoadingTipCount(), 5);
	TSet<FString> Seen;
	for (int32 i = 0; i < ULobbyWidget::GetLoadingTipCount(); ++i)
	{
		const FString Tip = ULobbyWidget::GetLoadingTip(i).ToString();
		TestFalse(*FString::Printf(TEXT("팁 %d 이 비지 않는다"), i), Tip.IsEmpty());
		Seen.Add(Tip);
	}
	TestEqual(TEXT("팁이 서로 다르다"), Seen.Num(), 5);
	TestEqual(TEXT("범위 밖은 돌려 쓴다"), ULobbyWidget::GetLoadingTip(7).ToString(), ULobbyWidget::GetLoadingTip(2).ToString());
	TestFalse(TEXT("음수도 안전"), ULobbyWidget::GetLoadingTip(-1).ToString().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLobbyIntroCardsTest, "Snarehaven.Lobby.IntroCardsThree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLobbyIntroCardsTest::RunTest(const FString& Parameters)
{
	// 카드는 3장, 글이 있고 서로 다르며 그림 자산이 실제로 있다.
	TestEqual(TEXT("카드는 3장"), USnareIntroCardsWidget::GetCardCount(), 3);
	TSet<FString> Seen;
	for (int32 i = 0; i < USnareIntroCardsWidget::GetCardCount(); ++i)
	{
		Seen.Add(USnareIntroCardsWidget::GetCardText(i).ToString());
		TestFalse(*FString::Printf(TEXT("카드 %d 글이 있다"), i), USnareIntroCardsWidget::GetCardText(i).IsEmpty());
		TestNotNull(*FString::Printf(TEXT("카드 %d 그림이 있다"), i),
			LoadObject<UTexture2D>(nullptr, *USnareIntroCardsWidget::GetCardIconPath(i)));
	}
	TestEqual(TEXT("글이 서로 다르다"), Seen.Num(), 3);
	TestTrue(TEXT("마지막 카드가 첫 목표 줄로 이어 준다"),
		USnareIntroCardsWidget::GetCardNote(USnareIntroCardsWidget::GetCardCount() - 1).ToString().Contains(TEXT("지금 할 일")));
	// 본문은 2줄로 짧게 — 첫 할 일·안내는 작은 글씨 줄로 따로 (감독 지적 ③).
	for (int32 i = 0; i < USnareIntroCardsWidget::GetCardCount(); ++i)
	{
		TArray<FString> Lines;
		USnareIntroCardsWidget::GetCardText(i).ToString().ParseIntoArrayLines(Lines);
		TestTrue(*FString::Printf(TEXT("카드 %d 본문은 2줄 이하"), i), Lines.Num() <= 2);
	}

	// 넘기기: 3번 넘기면 끝나고 저장 안 하는 모드에선 봤다는 표시를 안 남긴다. 건너뛰기도 끝낸다.
	const bool bOriginal = USnareIntroCardsWidget::IsIntroSeen();
	USnareIntroCardsWidget::SetIntroSeen(false);
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	{
		USnareIntroCardsWidget* const Cards = CreateWidget<USnareIntroCardsWidget>(World, USnareIntroCardsWidget::StaticClass());
		if (TestNotNull(TEXT("카드 위젯"), Cards))
		{
			Cards->bPersistSeen = false;
			bool bFinished = false;
			Cards->OnFinished.AddLambda([&bFinished]() { bFinished = true; });
			Cards->Advance();
			Cards->Advance();
			TestFalse(TEXT("두 장 넘겨선 안 끝난다"), bFinished);
			Cards->Advance();
			TestTrue(TEXT("세 장 넘기면 끝난다"), bFinished);
			TestFalse(TEXT("저장 끈 모드는 봤다고 안 남긴다"), USnareIntroCardsWidget::IsIntroSeen());

			USnareIntroCardsWidget* const Skipped = CreateWidget<USnareIntroCardsWidget>(World, USnareIntroCardsWidget::StaticClass());
			if (TestNotNull(TEXT("건너뛸 위젯"), Skipped))
			{
				Skipped->bPersistSeen = true;
				Skipped->Skip();
				TestTrue(TEXT("건너뛰면 봤다고 저장"), USnareIntroCardsWidget::IsIntroSeen());
				TestFalse(TEXT("봤으면 다시 안 띄운다"), USnareIntroCardsWidget::ShouldShowIntro());
			}
		}
	}
	USnareIntroCardsWidget::SetIntroSeen(bOriginal);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLobbyIntroFirstQuestTest, "Snarehaven.Lobby.IntroNamesFirstQuest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLobbyIntroFirstQuestTest::RunTest(const FString& Parameters)
{
	// 마지막 카드가 카드 뒤에 바로 이어지는 스토리 1장 첫 단계를 스토리 표 글 그대로 말한다 (정체성 ③).
	const FString Last = USnareIntroCardsWidget::GetCardNote(USnareIntroCardsWidget::GetCardCount() - 1).ToString();
	const FString Chapter = SnareStory::ChapterTitle(1).ToString();
	const FString Step = SnareStory::StepGoal(1).ToString();
	TestFalse(TEXT("1장 제목이 있다"), Chapter.IsEmpty());
	TestFalse(TEXT("첫 단계 글이 있다"), Step.IsEmpty());
	TestTrue(*FString::Printf(TEXT("마지막 카드에 1장 제목 「%s」"), *Chapter), Last.Contains(Chapter));
	TestTrue(*FString::Printf(TEXT("마지막 카드에 첫 단계 「%s」"), *Step), Last.Contains(Step));
	TestFalse(TEXT("자리 글자가 남지 않는다"), Last.Contains(TEXT("{")));
	AddInfo(FString::Printf(TEXT("[Intro] 마지막 카드: %s"), *Last.Replace(TEXT("\n"), TEXT(" / "))));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
