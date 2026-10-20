// DESIGN 2.2 「이름표」 닉네임 2층 검사 — 클라 미리 거름이 서버 `SetNickname` 과 같은 규칙인가,
// 그리고 서버·PlayFab 표시 이름이 **둘 다** 받아 준 뒤에만 표시 이름이 바뀌는가 (CLAUDE.md 8).
// 가짜 서버(`TestServerOverride`)·가짜 표시 이름 API(`TestDisplayNameOverride`)로 진짜 응답과 같은 길을 태운다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Nickname; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareNickname.h"
#include "Engine/GameInstance.h"
#include "Internationalization/Regex.h"
#include "SnarehavenGameMode.h"
#if WITH_EDITOR
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "UI/LobbyWidget.h"
#include "UI/PauseMenuWidget.h"
#endif
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6).
	const TCHAR* const TestGoodName = TEXT("달빛여우");
	const TCHAR* const TestTakenMessage = TEXT("Name not available");
	const TCHAR* const TestOldName = TEXT("산들바람");

	using FDone = USnareBackendSubsystem::FServerCallDone;
	using FDisplayDone = TFunction<void(const FString&, const FString&)>;

	/** 서버·표시 이름 API 둘 다 대기열에 잡아 두고, 검사가 하나씩 답한다. */
	struct FFakeBackend
	{
		int32 ServerCalls = 0;
		int32 DisplayCalls = 0;
		FString LastServerName;
		FString LastDisplayName;
		TArray<FDone> ServerWaiting;
		TArray<FDisplayDone> DisplayWaiting;

		FFakeBackend()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Function, const TSharedPtr<FJsonObject>& Args, FDone OnDone)
				{
					++ServerCalls;
					Args->TryGetStringField(TEXT("name"), LastServerName);
					ServerWaiting.Add(MoveTemp(OnDone));
				};
			USnareBackendSubsystem::TestDisplayNameOverride = [this](const FString& Name, FDisplayDone OnDone)
				{
					++DisplayCalls;
					LastDisplayName = Name;
					DisplayWaiting.Add(MoveTemp(OnDone));
				};
		}

		~FFakeBackend()
		{
			USnareBackendSubsystem::TestServerOverride = nullptr;
			USnareBackendSubsystem::TestDisplayNameOverride = nullptr;
		}

		void ServerAccept(const FString& Name)
		{
			const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("name"), Name);
			ServerWaiting.Pop()(true, Response, FString());
		}

		/** CloudScript `fail(code, message)` 모양 — HTTP 는 성공이다. */
		void ServerFail(const FString& Code, const FString& Message)
		{
			const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("error"), Message);
			Response->SetStringField(TEXT("code"), Code);
			ServerWaiting.Pop()(true, Response, FString());
		}
	};

	struct FResult
	{
		int32 Calls = 0;
		bool bAccepted = false;
		FString Code;
		FString Message;

		USnareBackendSubsystem::FNicknameDone Bind()
		{
			return [this](bool bInAccepted, const FString& InCode, const FString& InMessage)
				{
					++Calls;
					bAccepted = bInAccepted;
					Code = InCode;
					Message = InMessage;
				};
		}
	};

	/** 원본 `cloudscript_main.js` 의 `var <Name> = [ ... ];` 안 따옴표 글자들. */
	TArray<FString> ReadJsStringArray(const FString& Source, const FString& Name)
	{
		TArray<FString> Out;
		const int32 Start = Source.Find(FString::Printf(TEXT("var %s = ["), *Name));
		if (Start == INDEX_NONE)
		{
			return Out;
		}
		const int32 End = Source.Find(TEXT("];"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
		const FString Body = Source.Mid(Start, End - Start);
		FRegexMatcher Matcher(FRegexPattern(TEXT("\"([^\"]*)\"")), Body);
		while (Matcher.FindNext())
		{
			Out.Add(Matcher.GetCaptureGroup(1));
		}
		return Out;
	}

	int32 ReadJsInt(const FString& Source, const FString& Name)
	{
		FRegexMatcher Matcher(FRegexPattern(FString::Printf(TEXT("var %s = (\\d+);"), *Name)), Source);
		return Matcher.FindNext() ? FCString::Atoi(*Matcher.GetCaptureGroup(1)) : -1;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnareNicknameRulesTest, "Snarehaven.Nickname.Rules.MatchServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnareNicknameRulesTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("앞뒤 공백 자름"), SnareNickname::Trim(TEXT("  달빛여우 ")), FString(TestGoodName));
	TestEqual(TEXT("맞는 이름"), SnareNickname::ProblemCode(TestGoodName), FString());
	TestEqual(TEXT("영문·숫자"), SnareNickname::ProblemCode(TEXT("Fox77")), FString());
	TestEqual(TEXT("1자 = 짧음"), SnareNickname::ProblemCode(TEXT("여")), FString(TEXT("bad_length")));
	TestEqual(TEXT("2자 = 짧음(PlayFab 표시 이름 최소 3자)"), SnareNickname::ProblemCode(TEXT("곰돌")), FString(TEXT("bad_length")));
	TestEqual(TEXT("3자 한글 = 됨"), SnareNickname::ProblemCode(TEXT("곰돌이")), FString());
	TestEqual(TEXT("길이 안내 = 3~12자"), SnareNickname::MessageFor(TEXT("bad_length")), FString(TEXT("닉네임은 3~12자")));
	// PlayFab 표시 이름 거절은 영어 원문 대신 한국어 — 2자 한글이 표시 이름에서 막혀도 이유가 읽힌다(사람 폰 10-08).
	TestEqual(TEXT("표시 이름 InvalidParams = 길이 안내"), SnareNickname::DisplayNameMessage(TEXT("InvalidParams")), FString(TEXT("닉네임은 3~12자")));
	TestEqual(TEXT("표시 이름 NameNotAvailable = 겹침 안내"), SnareNickname::DisplayNameMessage(TEXT("NameNotAvailable")), FString(TEXT("누가 이미 쓰는 닉네임이다")));
	TestEqual(TEXT("표시 이름 ProfaneDisplayName = 금칙어 안내"), SnareNickname::DisplayNameMessage(TEXT("ProfaneDisplayName")), SnareNickname::MessageFor(TEXT("banned_word")));
	TestFalse(TEXT("모르는 오류도 영어 원문이 아니다"), SnareNickname::DisplayNameMessage(TEXT("ServiceUnavailable")).Contains(TEXT("ServiceUnavailable")));
	TestEqual(TEXT("13자 = 김"), SnareNickname::ProblemCode(TEXT("가나다라마바사아자차카타파")), FString(TEXT("bad_length")));
	TestEqual(TEXT("12자 = 됨"), SnareNickname::ProblemCode(TEXT("가나다라마바사아자차카타")), FString());
	TestEqual(TEXT("사이 띄어쓰기"), SnareNickname::ProblemCode(TEXT("달빛 여우")), FString(TEXT("bad_chars")));
	TestEqual(TEXT("낱자모"), SnareNickname::ProblemCode(TEXT("ㅅㅂ여우")), FString(TEXT("bad_chars")));
	TestEqual(TEXT("기호"), SnareNickname::ProblemCode(TEXT("fox_1")), FString(TEXT("bad_chars")));
	TestEqual(TEXT("금칙어 한글"), SnareNickname::ProblemCode(TEXT("시발여우")), FString(TEXT("banned_word")));
	TestEqual(TEXT("숫자 끼운 금칙어"), SnareNickname::ProblemCode(TEXT("시1발")), FString(TEXT("banned_word")));
	TestEqual(TEXT("대문자 낱말 금칙어"), SnareNickname::ProblemCode(TEXT("AdminFox")), FString(TEXT("banned_word")));
	TestEqual(TEXT("숫자로 끊긴 낱말 금칙어"), SnareNickname::ProblemCode(TEXT("sex99")), FString(TEXT("banned_word")));
	TestEqual(TEXT("낱말 속 우연은 통과(서버와 같게)"), SnareNickname::ProblemCode(TEXT("Badminton")), FString());
	TestEqual(TEXT("낱말 속 우연은 통과 Essex"), SnareNickname::ProblemCode(TEXT("Essex")), FString());

	// 사본이 원본과 같은가 — 서버 목록만 고치고 클라를 잊으면 여기서 깨진다.
	FString Source;
	const FString JsPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("tools/playfab/cloudscript_main.js"));
	if (!TestTrue(TEXT("CloudScript 원본을 읽음"), FFileHelper::LoadFileToString(Source, *JsPath)))
	{
		return false;
	}
	const TArray<FString> ServerWords = ReadJsStringArray(Source, TEXT("NICKNAME_BANNED_WORDS"));
	TestTrue(TEXT("원본 금칙어가 있다"), ServerWords.Num() > 0);
	TestEqual(TEXT("금칙어 사본 = 원본"), SnareNickname::BannedWords(), ServerWords);
	TestEqual(TEXT("낱말 금칙어 사본 = 원본"), SnareNickname::BannedTokens(), ReadJsStringArray(Source, TEXT("NICKNAME_BANNED_TOKENS")));
	TestEqual(TEXT("최소 길이 = 원본"), SnareNickname::MinLength, ReadJsInt(Source, TEXT("NICKNAME_MIN_LENGTH")));
	TestEqual(TEXT("최대 길이 = 원본"), SnareNickname::MaxLength, ReadJsInt(Source, TEXT("NICKNAME_MAX_LENGTH")));
	return true;
}

// 닉네임 c — 리슨 서버가 알려 온 닉네임을 「플레이어 N」 자리에 쓰는가, 못 쓸 때는 「플레이어 N」 으로 돌아가는가.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnareNicknameRoomNameTest, "Snarehaven.Nickname.RoomName.UsesAcceptedNickname",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnareNicknameRoomNameTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Room = {TEXT("플레이어 1"), TestOldName};
	TestEqual(TEXT("맞는 닉네임 = 그대로"), ASnarehavenGameMode::PickRoomPlayerName(TestGoodName, Room), FString(TestGoodName));
	TestEqual(TEXT("앞뒤 공백 자름"), ASnarehavenGameMode::PickRoomPlayerName(TEXT(" Fox77 "), Room), FString(TEXT("Fox77")));
	TestEqual(TEXT("못 받음(빈 이름) = 빈 번호"), ASnarehavenGameMode::PickRoomPlayerName(FString(), Room), FString(TEXT("플레이어 2")));
	TestEqual(TEXT("방에서 겹침 = 빈 번호"), ASnarehavenGameMode::PickRoomPlayerName(TestOldName, Room), FString(TEXT("플레이어 2")));
	TestEqual(TEXT("길이 어김 = 빈 번호"), ASnarehavenGameMode::PickRoomPlayerName(TEXT("여"), Room), FString(TEXT("플레이어 2")));
	TestEqual(TEXT("금칙어 = 빈 번호"), ASnarehavenGameMode::PickRoomPlayerName(TEXT("시발여우"), Room), FString(TEXT("플레이어 2")));
	TestEqual(TEXT("「플레이어 N」 흉내 = 빈 번호"), ASnarehavenGameMode::PickRoomPlayerName(TEXT("플레이어 3"), {}), FString(TEXT("플레이어 1")));
	TestEqual(TEXT("13자 = 빈 번호"), ASnarehavenGameMode::PickRoomPlayerName(TEXT("가나다라마바사아자차카타파"), {}), FString(TEXT("플레이어 1")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnareNicknameSubmitTest, "Snarehaven.Nickname.Submit.AfterBothAccept",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnareNicknameSubmitTest::RunTest(const FString& Parameters)
{
	UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(GameInstance);
	FFakeBackend Fake;

	// ① 미리 막힘 — 서버에 안 간다.
	{
		FResult R;
		Backend->SubmitNickname(TEXT("병신놈"), R.Bind());
		TestEqual(TEXT("① 금칙어는 바로 답"), R.Calls, 1);
		TestEqual(TEXT("① 코드"), R.Code, FString(TEXT("banned_word")));
		TestEqual(TEXT("① 서버 왕복 0"), Fake.ServerCalls, 0);
	}

	// ② 서버 거절 — 표시 이름 API 를 부르지 않고 이름도 그대로.
	{
		FResult R;
		Backend->SubmitNickname(TEXT(" 달빛여우 "), R.Bind());
		TestEqual(TEXT("② 서버엔 자른 이름"), Fake.LastServerName, FString(TestGoodName));
		TestTrue(TEXT("② 답 오기 전 진행 중"), Backend->IsSubmittingNickname());
		TestEqual(TEXT("② 답 오기 전 표시 이름 그대로"), Backend->GetDisplayName(), FString());
		Fake.ServerFail(TEXT("banned_word"), TEXT("쓸 수 없는 말이 들어 있다"));
		TestEqual(TEXT("② 거절 코드 그대로"), R.Code, FString(TEXT("banned_word")));
		TestFalse(TEXT("② 받아들여지지 않음"), R.bAccepted);
		TestEqual(TEXT("② 표시 이름 API 0"), Fake.DisplayCalls, 0);
		TestFalse(TEXT("② 진행 끝"), Backend->IsSubmittingNickname());
	}

	// ③ 서버는 받았지만 PlayFab 이 겹침으로 거절 — 문구 그대로, 이름 그대로.
	{
		FResult R;
		Backend->SubmitNickname(TestGoodName, R.Bind());
		Fake.ServerAccept(TestGoodName);
		TestEqual(TEXT("③ 서버가 준 이름으로 올림"), Fake.LastDisplayName, FString(TestGoodName));
		TestEqual(TEXT("③ PlayFab 답 전엔 아직 안 끝남"), R.Calls, 0);
		Fake.DisplayWaiting.Pop()(FString(), TestTakenMessage);
		TestEqual(TEXT("③ 코드"), R.Code, FString(TEXT("display_name")));
		TestEqual(TEXT("③ PlayFab 문구 그대로"), R.Message, FString(TestTakenMessage));
		TestEqual(TEXT("③ 표시 이름 그대로"), Backend->GetDisplayName(), FString());
	}

	// ④ 둘 다 받아 줌 — 그때 처음 바뀌고 신호가 한 번.
	{
		FResult R;
		Backend->SubmitNickname(TestGoodName, R.Bind());
		FResult Busy;
		Backend->SubmitNickname(TEXT("Fox77"), Busy.Bind());
		TestEqual(TEXT("④ 겹친 요청은 busy"), Busy.Code, FString(TEXT("busy")));
		Fake.ServerAccept(TestGoodName);
		Fake.DisplayWaiting.Pop()(TestGoodName, FString());
		TestTrue(TEXT("④ 받아들여짐"), R.bAccepted);
		TestEqual(TEXT("④ 표시 이름 바뀜"), Backend->GetDisplayName(), FString(TestGoodName));
		TestFalse(TEXT("④ 진행 끝"), Backend->IsSubmittingNickname());
	}
	TestEqual(TEXT("서버 왕복은 ②③④ 셋"), Fake.ServerCalls, 3);
	return true;
}

#if WITH_EDITOR
// 로비 입력 줄 (INBOX 지시 13:40 ①) — 이름이 없으면 받아 주기 전엔 「시작」·설정으로 못 가고, 거절 문구는 알약에 그대로,
// 받아 준 뒤에야 줄이 접힌다. 설정의 「닉네임 바꾸기」 는 지금 이름을 채워 열고 「뒤로」 로 닫힌다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnareNicknameLobbyTest, "Snarehaven.Nickname.Lobby.BlocksUntilAccepted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnareNicknameLobbyTest::RunTest(const FString& Parameters)
{
	FFakeBackend Fake;
	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	ON_SCOPE_EXIT { GameInstance->Shutdown(); };
	USnareBackendSubsystem* const Backend = GameInstance->GetSubsystem<USnareBackendSubsystem>();
	if (!TestNotNull(TEXT("백엔드 창구"), Backend))
	{
		return false;
	}
	Backend->SetLoggedInForTest(FString());
	TestTrue(TEXT("이름 없는 로그인 = 닉네임 필요"), Backend->NeedsNickname());

	ULobbyWidget* const Lobby = CreateWidget<ULobbyWidget>(GameInstance, ULobbyWidget::StaticClass());
	if (!TestNotNull(TEXT("로비 위젯"), Lobby))
	{
		return false;
	}
	Lobby->TakeWidget();
	UWidgetTree* const Tree = Lobby->WidgetTree;
	UEditableTextBox* const Box = Tree->FindWidget<UEditableTextBox>(TEXT("NicknameBox"));
	UButton* const Confirm = Tree->FindWidget<UButton>(TEXT("NicknameConfirmButton"));
	UButton* const Edit = Tree->FindWidget<UButton>(TEXT("NicknameEditButton"));
	UButton* const Solo = Tree->FindWidget<UButton>(TEXT("SoloButton"));
	UButton* const Settings = Tree->FindWidget<UButton>(TEXT("SettingsButton"));
	const UBorder* const Pill = Tree->FindWidget<UBorder>(TEXT("NicknameErrorPill"));
	const UTextBlock* const PillText = Tree->FindWidget<UTextBlock>(TEXT("NicknameErrorText"));
	if (!TestNotNull(TEXT("입력 칸"), Box) || !TestNotNull(TEXT("확인"), Confirm) || !TestNotNull(TEXT("바꾸기"), Edit)
		|| !TestNotNull(TEXT("시작"), Solo) || !TestNotNull(TEXT("설정"), Settings) || !TestNotNull(TEXT("알약"), Pill)
		|| !TestNotNull(TEXT("알약 글자"), PillText))
	{
		return false;
	}
	auto Shown = [](const UWidget* W) { return W->GetVisibility() != ESlateVisibility::Collapsed && W->GetVisibility() != ESlateVisibility::Hidden; };

	// ① 처음 — 입력 줄만, 시작·설정 없음.
	TestTrue(TEXT("① 입력 줄이 열려 있다"), Lobby->IsNicknameOpen());
	TestTrue(TEXT("① 입력 칸 보임"), Shown(Box));
	TestTrue(TEXT("① 확인 보임"), Shown(Confirm));
	TestFalse(TEXT("① 시작 숨음"), Shown(Solo));
	TestFalse(TEXT("① 설정 숨음 (뒤로 갈 곳 없음)"), Shown(Settings));
	TestFalse(TEXT("① 알약 접힘"), Shown(Pill));
	Lobby->SetNicknameOpen(false);
	TestTrue(TEXT("① 이름 없으면 못 접는다"), Lobby->IsNicknameOpen());

	// ② 미리 막힌 이름 — 서버 왕복 없이 문구가 알약에.
	Box->SetText(FText::FromString(TEXT("여")));
	Confirm->OnClicked.Broadcast();
	TestEqual(TEXT("② 서버 왕복 0"), Fake.ServerCalls, 0);
	TestTrue(TEXT("② 알약 펴짐"), Shown(Pill));
	TestTrue(FString::Printf(TEXT("② 알약 = 길이 문구 (%s)"), *PillText->GetText().ToString()),
		PillText->GetText().ToString().Contains(SnareNickname::MessageFor(TEXT("bad_length"))));
	TestTrue(TEXT("② 여전히 입력 줄"), Lobby->IsNicknameOpen());

	// ③ PlayFab 겹침 거절 — 그 문구 그대로, 줄 그대로.
	Box->SetText(FText::FromString(TEXT(" 달빛여우 ")));
	Confirm->OnClicked.Broadcast();
	TestEqual(TEXT("③ 서버엔 자른 이름"), Fake.LastServerName, FString(TestGoodName));
	TestFalse(TEXT("③ 답 전엔 확인이 꺼진다"), Confirm->GetIsEnabled());
	TestFalse(TEXT("③ 답 전엔 알약 접힘"), Shown(Pill));
	Fake.ServerAccept(TestGoodName);
	Fake.DisplayWaiting.Pop()(FString(), TestTakenMessage);
	TestTrue(TEXT("③ 확인 다시 켜짐"), Confirm->GetIsEnabled());
	TestTrue(FString::Printf(TEXT("③ 알약 = PlayFab 문구 (%s)"), *PillText->GetText().ToString()),
		PillText->GetText().ToString().Contains(TestTakenMessage));
	TestFalse(TEXT("③ 아직 시작 숨음"), Shown(Solo));

	// ④ 둘 다 받아 줌 — 그제서야 줄이 접히고 시작·설정이 선다.
	Confirm->OnClicked.Broadcast();
	TestTrue(TEXT("④ 답 전엔 아직 줄"), Lobby->IsNicknameOpen());
	Fake.ServerAccept(TestGoodName);
	Fake.DisplayWaiting.Pop()(TestGoodName, FString());
	TestFalse(TEXT("④ 줄 접힘"), Lobby->IsNicknameOpen());
	TestTrue(TEXT("④ 시작 보임"), Shown(Solo));
	TestTrue(TEXT("④ 설정 보임"), Shown(Settings));
	TestFalse(TEXT("④ 입력 칸 숨음"), Shown(Box));

	// ⑤ 설정 → 바꾸기 → 지금 이름이 채워져 열림 → 뒤로는 설정으로.
	TestFalse(TEXT("⑤ 첫 화면엔 바꾸기 없음"), Shown(Edit));
	Settings->OnClicked.Broadcast();
	TestTrue(TEXT("⑤ 설정 안에 바꾸기"), Shown(Edit));
	Edit->OnClicked.Broadcast();
	TestTrue(TEXT("⑤ 줄 열림"), Lobby->IsNicknameOpen());
	TestEqual(TEXT("⑤ 지금 이름이 채워짐"), Box->GetText().ToString(), FString(TestGoodName));
	TestTrue(TEXT("⑤ 이름이 있으니 뒤로 보임"), Shown(Settings));
	TestFalse(TEXT("⑤ 줄 위엔 바꾸기 숨음"), Shown(Edit));
	Settings->OnClicked.Broadcast();
	TestFalse(TEXT("⑤ 뒤로 = 줄 접힘"), Lobby->IsNicknameOpen());
	TestTrue(TEXT("⑤ 설정으로 돌아옴"), Lobby->IsSettingsOpen());
	TestEqual(TEXT("서버 왕복은 ③④ 둘"), Fake.ServerCalls, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnareNicknamePauseMenuTest, "Snarehaven.Nickname.PauseMenu.ChangeAfterAccept",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnareNicknamePauseMenuTest::RunTest(const FString& Parameters)
{
	// 닉네임 b3 — 설정(ESC 옵션 카드)에서 언제든 바꾼다. 거절은 알약에 문구 그대로, 표시 이름은 둘 다 받아 준 뒤에만.
	FFakeBackend Fake;
	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	ON_SCOPE_EXIT { GameInstance->Shutdown(); };
	USnareBackendSubsystem* const Backend = GameInstance->GetSubsystem<USnareBackendSubsystem>();
	if (!TestNotNull(TEXT("백엔드 창구"), Backend))
	{
		return false;
	}
	Backend->SetLoggedInForTest(TestOldName);

	UPauseMenuWidget* const Menu = CreateWidget<UPauseMenuWidget>(GameInstance, UPauseMenuWidget::StaticClass());
	if (!TestNotNull(TEXT("일시정지 위젯"), Menu))
	{
		return false;
	}
	Menu->TakeWidget();
	UWidgetTree* const Tree = Menu->WidgetTree;
	UEditableTextBox* const Box = Tree->FindWidget<UEditableTextBox>(TEXT("OptionsNicknameBox"));
	UButton* const Change = Tree->FindWidget<UButton>(TEXT("OptionsNicknameButton"));
	const UTextBlock* const ChangeText = Tree->FindWidget<UTextBlock>(TEXT("OptionsNicknameButtonText"));
	const UBorder* const Pill = Tree->FindWidget<UBorder>(TEXT("OptionsNicknamePill"));
	const UTextBlock* const PillText = Tree->FindWidget<UTextBlock>(TEXT("OptionsNicknamePillText"));
	UBorder* const Card = Tree->FindWidget<UBorder>(TEXT("OptionsCard"));
	if (!TestNotNull(TEXT("칸"), Box) || !TestNotNull(TEXT("바꾸기"), Change) || !TestNotNull(TEXT("바꾸기 글"), ChangeText)
		|| !TestNotNull(TEXT("알약"), Pill) || !TestNotNull(TEXT("알약 글자"), PillText) || !TestNotNull(TEXT("옵션 카드"), Card))
	{
		return false;
	}
	TestTrue(TEXT("닉네임 줄은 옵션 카드 안"), Box->IsChildOf(Card) && Change->IsChildOf(Card));
	auto Shown = [](const UWidget* W) { return W->GetVisibility() != ESlateVisibility::Collapsed && W->GetVisibility() != ESlateVisibility::Hidden; };

	// ① 카드를 펴면 지금 이름 — 그대로 누르면 왕복 없음.
	Menu->SetOptionsOpen(true);
	TestEqual(TEXT("① 칸 = 지금 이름"), Box->GetText().ToString(), FString(TestOldName));
	TestTrue(TEXT("① 로그인했으니 바꾸기 켜짐"), Change->GetIsEnabled());
	TestFalse(TEXT("① 알약 접힘"), Shown(Pill));
	Change->OnClicked.Broadcast();
	TestEqual(TEXT("① 같은 이름 = 서버 왕복 0"), Fake.ServerCalls, 0);

	// ② 미리 막힌 이름 — 왕복 없이 길이 문구가 빨간 알약에, 친 글자는 남는다.
	Box->SetText(FText::FromString(TEXT("여")));
	Change->OnClicked.Broadcast();
	TestEqual(TEXT("② 서버 왕복 0"), Fake.ServerCalls, 0);
	TestTrue(TEXT("② 알약 펴짐"), Shown(Pill));
	TestTrue(FString::Printf(TEXT("② 알약 = 길이 문구 (%s)"), *PillText->GetText().ToString()),
		PillText->GetText().ToString().Contains(SnareNickname::MessageFor(TEXT("bad_length"))));
	TestEqual(TEXT("② 친 글자 남음"), Box->GetText().ToString(), FString(TEXT("여")));

	// ③ PlayFab 겹침 거절 — 문구 그대로, 표시 이름 그대로.
	Box->SetText(FText::FromString(TEXT(" 달빛여우 ")));
	Change->OnClicked.Broadcast();
	TestEqual(TEXT("③ 서버엔 자른 이름"), Fake.LastServerName, FString(TestGoodName));
	TestFalse(TEXT("③ 답 전엔 바꾸기 꺼짐"), Change->GetIsEnabled());
	TestFalse(TEXT("③ 답 전엔 칸 꺼짐"), Box->GetIsEnabled());
	TestFalse(TEXT("③ 답 전엔 알약 접힘"), Shown(Pill));
	Fake.ServerAccept(TestGoodName);
	Fake.DisplayWaiting.Pop()(FString(), TestTakenMessage);
	TestTrue(TEXT("③ 바꾸기 다시 켜짐"), Change->GetIsEnabled());
	TestTrue(FString::Printf(TEXT("③ 알약 = PlayFab 문구 (%s)"), *PillText->GetText().ToString()),
		PillText->GetText().ToString().Contains(TestTakenMessage));
	TestEqual(TEXT("③ 표시 이름 그대로"), Backend->GetDisplayName(), FString(TestOldName));
	TestEqual(TEXT("③ 친 글자 남음"), Box->GetText().ToString(), FString(TEXT(" 달빛여우 ")));

	// ④ 둘 다 받아 줌 — 그제서야 표시 이름·칸·알약이 새 이름.
	Change->OnClicked.Broadcast();
	TestEqual(TEXT("④ 답 전엔 표시 이름 그대로"), Backend->GetDisplayName(), FString(TestOldName));
	Fake.ServerAccept(TestGoodName);
	Fake.DisplayWaiting.Pop()(TestGoodName, FString());
	TestEqual(TEXT("④ 표시 이름 바뀜"), Backend->GetDisplayName(), FString(TestGoodName));
	TestEqual(TEXT("④ 칸 = 새 이름"), Box->GetText().ToString(), FString(TestGoodName));
	TestTrue(FString::Printf(TEXT("④ 알약 = 바뀜 문구 (%s)"), *PillText->GetText().ToString()),
		Shown(Pill) && PillText->GetText().ToString().Contains(TestGoodName));
	TestEqual(TEXT("④ 바꾸기 글 = 바꾸기"), ChangeText->GetText().ToString(), FString(TEXT("바꾸기")));

	// ⑤ 접었다 다시 펴면 쳐 두고 안 올린 글자·알약이 남지 않는다.
	Menu->SetOptionsOpen(false);
	Box->SetText(FText::FromString(TEXT("엉뚱이")));
	Menu->SetOptionsOpen(true);
	TestEqual(TEXT("⑤ 다시 펴면 지금 이름"), Box->GetText().ToString(), FString(TestGoodName));
	TestFalse(TEXT("⑤ 알약 접힘"), Shown(Pill));
	TestEqual(TEXT("서버 왕복은 ③④ 둘"), Fake.ServerCalls, 2);
	return true;
}
#endif

#endif
