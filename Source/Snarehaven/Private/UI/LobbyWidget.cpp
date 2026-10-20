#include "UI/LobbyWidget.h"
#include "Snarehaven.h"
#include "SnareCheckSwitch.h"
#include "SnarehavenGameMode.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareEntryReject.h"
#include "Backend/SnareNickname.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ScaleBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "UI/SnareIntroCardsWidget.h"
#include "UI/SnareUiStyle.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 자리표시 UI 배치 (Autonomy.md 🟡 — 게임 규칙 숫자가 아니라 "화면 어디에 얼마나 크게").
	// 진짜 디자인이 WBP_Lobby 에 얹히면 아래 트리는 아예 만들어지지 않는다.
	// 판은 화면 높이(설계 기준 1080)의 80% 남짓. 부제 아래 끝(176+40)과 「시작」 위(248) 사이 32 — 1334x616 화면에서 약 18px.
	constexpr float PlaceholderPanelTop = 70.f;
	constexpr float PlaceholderPanelHeight = 880.f;
	constexpr float PlaceholderTitlePillWidth = 320.f;
	// 큰 로고 (감독 지적 — 화면 높이 12~15%). 설계 높이 1080 기준: 글자 88+30 줄 높이 합 ~160 ≈ 15%, 1334x616 에서 ~85px.
	constexpr float LogoFontSize = 88.f;
	constexpr float LogoSubFontSize = 30.f;
	constexpr int32 LogoOutline = 6;
	constexpr int32 LogoSubOutline = 3;
	constexpr float LogoTop = 30.f;
	constexpr float LogoHeight = 170.f;
	constexpr float LogoMargin = 40.f;
	constexpr float PlaceholderStatusTop = 132.f;
	constexpr float PlaceholderStatusHeight = 36.f;
// 「다시 시도」 — 가운데 정렬된 「서버 연결 실패」(약 150px) 오른쪽 끝에서 띄운다. 높이 56 은 엄지 최소 칸(44)+여유.
constexpr float RetryButtonOffsetX = 110.f;
constexpr float RetryButtonWidth = 170.f;
constexpr float RetryButtonHeight = 56.f;
	constexpr float PlaceholderTaglineTop = 176.f;
	constexpr float PlaceholderTaglineHeight = 40.f;
	constexpr float PlaceholderTaglineFontSize = 22.f;
	constexpr float TipBottomOffset = 16.f;
	constexpr float TipHeight = 76.f;
	constexpr float TipFontSize = 20.f;
	constexpr float PlaceholderPanelWidth = 720.f;
	constexpr float PlaceholderButtonWidth = 480.f;
	constexpr float PlaceholderButtonHeight = 110.f;
	constexpr float PlaceholderButtonGap = 24.f;
	constexpr float PlaceholderFirstButtonTop = 248.f;
	// 계정 삭제 줄은 삭제·취소 두 버튼이 반씩 나눠 쓴다. 설정 화면에서는 시작 버튼 자리(맨 윗줄)에 선다.
	constexpr float PlaceholderHalfButtonWidth = 230.f;
	constexpr int32 DeleteAccountRow = 0;
	// 설정 버튼은 방 버튼 아래 — 첫 화면에서 「시작」 보다 눈에 덜 띄는 맨 아랫줄 (M22 ⑧).
	constexpr int32 SettingsRow = 4;
	// 닉네임 입력 줄 (DESIGN 2.2 「닉네임」) — 시작 버튼 자리에 입력 칸, 그 아래 확인. 설정 화면의 「닉네임 바꾸기」 는
	// 계정 삭제 아랫줄. 거절 알약은 확인 아랫줄 자리에 선다 — 입력 칸·버튼을 가리지 않게.
	constexpr int32 NicknameBoxRow = 0;
	constexpr int32 NicknameConfirmRow = 1;
	constexpr int32 NicknameEditRow = 1;
	constexpr int32 NicknameErrorRow = 2;
	constexpr float NicknameFontSize = 24.f;
	// 오류 알약 모양은 DESIGN 2.1 「왼쪽 위 오류 알약」 값 그대로 — 최소 480×34 · 크림 글자 20 · 왼쪽 `!`.
	constexpr float ErrorPillMinWidth = 480.f;
	constexpr float ErrorPillHeight = 34.f;
	constexpr float ErrorPillFontSize = 20.f;
	constexpr float ErrorPillPaddingX = 16.f;

	float RowTop(int32 Row)
	{
		return PlaceholderFirstButtonTop + Row * (PlaceholderButtonHeight + PlaceholderButtonGap);
	}
}

bool ULobbyWidget::CanOpenRoom(ESnareLoginState LoginState, ESnareRoomState RoomState)
{
	// 로그인 전에는 EOS 를 부를 계정이 없다. 실패했을 때도 눌러 봐야 같은 자리에서 또 실패한다.
	if (LoginState != ESnareLoginState::LoggedIn)
	{
		return false;
	}

	// 이미 방에 있거나 진행 중이면 두 번 부르지 않는다 — 한 프로세스는 방을 하나만 갖는다
	// (`USnareOnlineSubsystem::RoomSessionName` 이 고정이라 두 번째 호출은 EOS 에서 거절된다).
	switch (RoomState)
	{
	case ESnareRoomState::None:
	case ESnareRoomState::Failed:
		return true;
	default:
		return false;
	}
}

bool ULobbyWidget::CanJoinRoom(ESnareRoomState RoomState, int32 FoundRoomCount)
{
	// 찾기 결과 0개는 오류가 아니다(명세 `net-eos-session.md` 2장) — 그냥 들어갈 방이 없는 것이다.
	return FoundRoomCount > 0 && RoomState != ESnareRoomState::Joining && RoomState != ESnareRoomState::Joined
		&& RoomState != ESnareRoomState::Hosting && RoomState != ESnareRoomState::Creating;
}

FText ULobbyWidget::MakeJoinDisabledReason(bool bHasOnline, ESnareRoomState RoomState, int32 FoundRoomCount)
{
	if (!bHasOnline)
	{
		return LOCTEXT("LobbyJoinOff", "참가하려면 온라인이어야 해요");
	}
	if (CanJoinRoom(RoomState, FoundRoomCount))
	{
		return FText::GetEmpty();
	}
	// 방이 움직이는 중이면 상태 줄이 이미 그 소식을 전한다.
	if (FoundRoomCount == 0 && (RoomState == ESnareRoomState::None || RoomState == ESnareRoomState::Failed))
	{
		return LOCTEXT("LobbyJoinNoRoom", "참가할 방이 없어요. 먼저 방 찾기");
	}
	return FText::GetEmpty();
}

FText ULobbyWidget::MakeStatusText(ESnareLoginState LoginState, ESnareRoomState RoomState, int32 FoundRoomCount,
	bool bReturnedFromRoom, const FText& RejectMessage)
{
	// 방이 터져서 돌아온 사람에게는 그 사실이 가장 먼저 알 소식이다. 방이 다시 움직이기 시작하면
	// (아래 switch) 그때부터는 새 소식이 이긴다. 거절 사유(버전 불일치 등)를 알면 그게 더 구체적이다.
	if (bReturnedFromRoom && RoomState == ESnareRoomState::None && FoundRoomCount == 0)
	{
		return !RejectMessage.IsEmpty() ? RejectMessage
			: LOCTEXT("LobbyReturnedFromRoom", "방이 해산됐다. 다시 고르면 된다");
	}
	if (RoomState == ESnareRoomState::Failed && !RejectMessage.IsEmpty())
	{
		return RejectMessage;
	}

	// 방 상태가 먼저다 — 방이 움직이고 있으면 그게 사람이 기다리는 소식이다.
	switch (RoomState)
	{
	case ESnareRoomState::Creating:
		return LOCTEXT("LobbyRoomCreating", "방 만드는 중...");
	case ESnareRoomState::Hosting:
		return LOCTEXT("LobbyRoomHosting", "방을 열었다. 다른 사람을 기다린다");
	case ESnareRoomState::Searching:
		return LOCTEXT("LobbyRoomSearching", "방 찾는 중...");
	case ESnareRoomState::Joining:
		return LOCTEXT("LobbyRoomJoining", "방에 들어가는 중...");
	case ESnareRoomState::Joined:
		return LOCTEXT("LobbyRoomJoined", "방에 들어갔다");
	case ESnareRoomState::Failed:
		return LOCTEXT("LobbyRoomFailed", "방에 문제가 생겼다");
	default:
		break;
	}

	// 방이 가만히 있을 때 — 찾아 둔 결과가 있으면 그걸, 없으면 로그인 상태를 보여 준다.
	if (FoundRoomCount > 0)
	{
		return FText::Format(LOCTEXT("LobbyRoomsFound", "방 {0}개를 찾았다"), FText::AsNumber(FoundRoomCount));
	}

	switch (LoginState)
	{
	case ESnareLoginState::LoggedIn:
		return LOCTEXT("LobbyLoggedIn", "접속됨");
	case ESnareLoginState::InProgress:
		return LOCTEXT("LobbyLoggingIn", "접속 중...");
	case ESnareLoginState::Failed:
		// 이 로그인은 EOS(멀티 방) 몫이다 — PlayFab(재화·인벤토리)과는 별개라 "접속 실패" 라 적으면
		// 저장까지 끊긴 것처럼 읽힌다 (폰 로그: PlayFab 로그인 성공 · EOS 제품 키 미설정).
		return LOCTEXT("LobbyLoginFailed", "서버에 연결하지 못했어요 - 혼자 시작");
	default:
		return LOCTEXT("LobbyOffline", "오프라인 - 시작은 된다");
	}
}

USnareOnlineSubsystem* ULobbyWidget::GetOnline() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance != nullptr ? GameInstance->GetSubsystem<USnareOnlineSubsystem>() : nullptr;
}

FText ULobbyWidget::GetTagline()
{
	return LOCTEXT("LobbyTagline", "동물을 잡고, 돌보고, 섬의 등대를 다시 밝혀라");
}

int32 ULobbyWidget::GetLoadingTipCount()
{
	return 5;
}

FText ULobbyWidget::GetLoadingTip(int32 Index)
{
	// 세계관 문장 5개 (docs/STORY.md 2절) — 처음 켠 사람이 타이틀에서 이 섬이 어떤 곳인지 읽는다.
	switch (((Index % 5) + 5) % 5)
	{
	case 0: return LOCTEXT("LobbyTip0", "스네어헤이븐은 올가미지기들이 동물을 죽이지 않고 잡아 돌보던 섬이다.");
	case 1: return LOCTEXT("LobbyTip1", "섬 한가운데 언덕의 등대는 십 년 전 꺼진 뒤로 안개가 걷히지 않는다.");
	case 2: return LOCTEXT("LobbyTip2", "동물을 돌볼 때 생기는 온기가 꺼진 등대를 다시 밝힌다고 한다.");
	case 3: return LOCTEXT("LobbyTip3", "안개를 틈타 회색 작살단이 동물을 마구 잡아 팔아넘기고 있다.");
	default: return LOCTEXT("LobbyTip4", "섬에 남은 마지막 올가미지기, 솔방울이 새내기를 기다린다.");
	}
}

FText ULobbyWidget::GetStatusText() const
{
	const USnareOnlineSubsystem* Online = GetOnline();
	// EOS 가 붙었어도 PlayFab 이 안 붙으면 보유 상태·상점이 다 막힌다 — 방에 들어가기 전엔 그쪽을 먼저 알린다.
	if (HasBackendProblem() && (Online == nullptr || Online->GetRoomState() == ESnareRoomState::None))
	{
		return LOCTEXT("LobbyBackendFailed", "서버 연결 실패");
	}
	if (Online == nullptr)
	{
		return MakeStatusText(ESnareLoginState::NotStarted, ESnareRoomState::None, 0, /*bReturnedFromRoom=*/false);
	}

	const FString& RejectCode = Online->GetLastRejectCode();
	return MakeStatusText(Online->GetLoginState(), Online->GetRoomState(), Online->GetFoundRooms().Num(),
		Online->HasReturnedFromRoom(),
		RejectCode.IsEmpty() ? FText::GetEmpty() : SnareEntryReject::MessageFor(SnareEntryReject::LoadTable(), RejectCode));
}

FText ULobbyWidget::GetStatusWithJoinReason() const
{
	const USnareOnlineSubsystem* Online = GetOnline();
	const FText Reason = MakeJoinDisabledReason(Online != nullptr,
		Online != nullptr ? Online->GetRoomState() : ESnareRoomState::None,
		Online != nullptr ? Online->GetFoundRooms().Num() : 0);
	return Reason.IsEmpty() ? GetStatusText()
		: FText::Format(LOCTEXT("LobbyStatusWithReason", "{0} · {1}"), GetStatusText(), Reason);
}

bool ULobbyWidget::IsOpenRoomEnabled() const
{
	const USnareOnlineSubsystem* Online = GetOnline();
	return Online != nullptr && CanOpenRoom(Online->GetLoginState(), Online->GetRoomState());
}

bool ULobbyWidget::IsJoinEnabled() const
{
	const USnareOnlineSubsystem* Online = GetOnline();
	return Online != nullptr && CanJoinRoom(Online->GetRoomState(), Online->GetFoundRooms().Num());
}

void ULobbyWidget::RefreshDisplay()
{
	if (IsValid(StatusText))
	{
		// 입력 줄이 펴져 있으면 무엇을 적어야 하는지가 먼저다 — 규칙을 알아야 거절을 덜 받는다.
		StatusText->SetText(bNicknameOpen
			? FText::Format(LOCTEXT("LobbyNicknamePrompt", "닉네임을 정해 주세요 ({0}~{1}자, 한글·영문·숫자)"),
				FText::AsNumber(SnareNickname::MinLength), FText::AsNumber(SnareNickname::MaxLength))
			: (DeleteNotice.IsEmpty() ? GetStatusWithJoinReason() : DeleteNotice));
	}

	// 버튼은 숨기지 않고 꺼 둔다 — 모바일에서 버튼이 나타났다 사라지면 손가락 위치를 잃는다
	// (`UHuntHudWidget::RefreshDisplay` 와 같은 이유).
	const bool bCanOpen = IsOpenRoomEnabled();
	if (IsValid(HostButton))
	{
		HostButton->SetIsEnabled(bCanOpen);
	}
	if (IsValid(FindButton))
	{
		FindButton->SetIsEnabled(bCanOpen);
	}
	if (IsValid(JoinButton))
	{
		JoinButton->SetIsEnabled(IsJoinEnabled());
	}
	if (IsValid(RetryButton))
	{
		RetryButton->SetVisibility(HasBackendProblem() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

bool ULobbyWidget::HasBackendProblem() const
{
	const UGameInstance* const GameInstance = GetGameInstance();
	const USnareBackendSubsystem* const Backend =
		GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
	return Backend != nullptr && Backend->HasConnectionProblem();
}

void ULobbyWidget::HandleRetryClicked()
{
	const UGameInstance* const GameInstance = GetGameInstance();
	if (USnareBackendSubsystem* const Backend =
		GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr)
	{
		Backend->RetryNow();
	}
	RefreshDisplay();
}

void ULobbyWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 3층 검사용 — 그림 카드를 저장·맵 이동 없이 띄운다 (`-SnareIntroPreview`, 몇째 장부터는 `-SnareIntroCard=<0~2>`).
	int32 PreviewCard = 0;
	const bool bPreviewCard = SnareCheckSwitch::Value(TEXT("SnareIntroCard="), PreviewCard);
	if (!bIntroPreviewShown && (bPreviewCard || SnareCheckSwitch::Param(TEXT("SnareIntroPreview"))))
	{
		bIntroPreviewShown = true;
		if (USnareIntroCardsWidget* const Preview = CreateWidget<USnareIntroCardsWidget>(GetOwningPlayer(), USnareIntroCardsWidget::StaticClass()))
		{
			Preview->bPersistSeen = false;
			for (int32 i = 0; i < FMath::Min(PreviewCard, USnareIntroCardsWidget::GetCardCount() - 1); ++i)
			{
				Preview->Advance();
			}
			Preview->AddToViewport(50);
		}
	}

	// PlayFab 로그인은 로비가 뜬 뒤에 끝난다 — 「표시 이름 없음」 이 새로 생긴 순간 입력 줄을 편다.
	const bool bNeedsNickname = NeedsNickname();
	if (bNeedsNickname != bLastNeedsNickname)
	{
		bLastNeedsNickname = bNeedsNickname;
		if (bNeedsNickname)
		{
			SetNicknameOpen(true);
		}
	}

	// 3층 검사용 스위치 (M107-05). 헤드리스에는 입력칸에 칠 손가락이 없다 — `-SnareNicknameSet=<이름>` 이면 입력 줄이
	// 열린 순간 그 이름을 넣고 「확인」 과 **같은 함수**를 한 번 부른다. 이름표는 그대로 서버 답 뒤에만 바뀐다 (CLAUDE.md 8).
	static bool bCheckNicknameUsed = false;
	FString CheckNickname;
	if (!bCheckNicknameUsed && bNicknameOpen && !bNicknamePreview && IsValid(NicknameBox)
		&& SnareCheckSwitch::Value(TEXT("SnareNicknameSet="), CheckNickname) && !CheckNickname.IsEmpty())
	{
		bCheckNicknameUsed = true;
		NicknameBox->SetText(FText::FromString(CheckNickname));
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 닉네임 넣기 (-SnareNicknameSet=%s)"), *CheckNickname);
		HandleNicknameConfirmClicked();
	}

	const bool bBackendProblem = HasBackendProblem();
	if (bBackendProblem != bLastBackendProblem)
	{
		bLastBackendProblem = bBackendProblem;
		RefreshDisplay();
	}

	const USnareOnlineSubsystem* Online = GetOnline();
	if (Online == nullptr)
	{
		return;
	}

	const ESnareLoginState LoginState = Online->GetLoginState();
	const ESnareRoomState RoomState = Online->GetRoomState();
	const int32 FoundRoomCount = Online->GetFoundRooms().Num();
	if (LoginState == LastLoginState && RoomState == LastRoomState && FoundRoomCount == LastFoundRoomCount)
	{
		return;
	}

	LastLoginState = LoginState;
	LastRoomState = RoomState;
	LastFoundRoomCount = FoundRoomCount;
	RefreshDisplay();
}

TSharedRef<SWidget> ULobbyWidget::RebuildWidget()
{
	if (NeedsPlaceholderTree())
	{
		BuildPlaceholderTree();
	}

	if (IsValid(RetryButton) && !RetryButton->OnClicked.IsAlreadyBound(this, &ULobbyWidget::HandleRetryClicked))
	{
		RetryButton->OnClicked.AddDynamic(this, &ULobbyWidget::HandleRetryClicked);
	}
	if (IsValid(HostButton) && !HostButton->OnClicked.IsAlreadyBound(this, &ULobbyWidget::HandleHostClicked))
	{
		HostButton->OnClicked.AddDynamic(this, &ULobbyWidget::HandleHostClicked);
	}
	if (IsValid(FindButton) && !FindButton->OnClicked.IsAlreadyBound(this, &ULobbyWidget::HandleFindClicked))
	{
		FindButton->OnClicked.AddDynamic(this, &ULobbyWidget::HandleFindClicked);
	}
	if (IsValid(JoinButton) && !JoinButton->OnClicked.IsAlreadyBound(this, &ULobbyWidget::HandleJoinClicked))
	{
		JoinButton->OnClicked.AddDynamic(this, &ULobbyWidget::HandleJoinClicked);
	}
	if (IsValid(SoloButton) && !SoloButton->OnClicked.IsAlreadyBound(this, &ULobbyWidget::HandleSoloClicked))
	{
		SoloButton->OnClicked.AddDynamic(this, &ULobbyWidget::HandleSoloClicked);
	}
	if (IsValid(SettingsButton) && !SettingsButton->OnClicked.IsAlreadyBound(this, &ULobbyWidget::HandleSettingsClicked))
	{
		SettingsButton->OnClicked.AddDynamic(this, &ULobbyWidget::HandleSettingsClicked);
	}
	if (IsValid(DeleteAccountButton)
		&& !DeleteAccountButton->OnClicked.IsAlreadyBound(this, &ULobbyWidget::HandleDeleteAccountClicked))
	{
		DeleteAccountButton->OnClicked.AddDynamic(this, &ULobbyWidget::HandleDeleteAccountClicked);
	}
	if (IsValid(DeleteCancelButton)
		&& !DeleteCancelButton->OnClicked.IsAlreadyBound(this, &ULobbyWidget::HandleDeleteCancelClicked))
	{
		DeleteCancelButton->OnClicked.AddDynamic(this, &ULobbyWidget::HandleDeleteCancelClicked);
	}

	if (IsValid(NicknameEditButton) && !NicknameEditButton->OnClicked.IsAlreadyBound(this, &ULobbyWidget::HandleNicknameEditClicked))
	{
		NicknameEditButton->OnClicked.AddDynamic(this, &ULobbyWidget::HandleNicknameEditClicked);
	}
	if (IsValid(NicknameConfirmButton)
		&& !NicknameConfirmButton->OnClicked.IsAlreadyBound(this, &ULobbyWidget::HandleNicknameConfirmClicked))
	{
		NicknameConfirmButton->OnClicked.AddDynamic(this, &ULobbyWidget::HandleNicknameConfirmClicked);
	}
	if (IsValid(NicknameBox) && !NicknameBox->OnTextCommitted.IsAlreadyBound(this, &ULobbyWidget::HandleNicknameCommitted))
	{
		NicknameBox->OnTextCommitted.AddDynamic(this, &ULobbyWidget::HandleNicknameCommitted);
	}

	// 로그인이 로비보다 먼저 끝났으면 틱을 기다리지 않고 처음부터 입력 줄로 연다 — 「시작」 이 한 프레임도 보이지 않게.
	bLastNeedsNickname = NeedsNickname();
	if (bLastNeedsNickname)
	{
		bNicknameOpen = true;
	}
	if (SnareCheckSwitch::Param(TEXT("SnareNicknamePreview")))
	{
		bNicknamePreview = true;
		bNicknameOpen = true;
	}
	SetDeleteStep(DeleteStep, DeleteNotice);

	// 글자·버튼 모양은 화면마다 따로 정하지 않는다 — 네 화면이 같은 글꼴·색을 쓴다 (M5-05a·b).
	// `Super` **뒤**라야 한다 — Slate 위젯이 생긴 뒤에 칠해야 화면까지 바뀐다.
	TSharedRef<SWidget> Built = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	return Built;
}

bool ULobbyWidget::NeedsPlaceholderTree() const
{
	if (WidgetTree == nullptr)
	{
		return false;
	}

	if (WidgetTree->RootWidget == nullptr)
	{
		return true;
	}

	// 위젯 블루프린트를 새로 만들면 빈 CanvasPanel 하나가 자동으로 들어간다 — 그건 디자인이 아니라 빈 도화지다.
	const UPanelWidget* RootPanel = Cast<UPanelWidget>(WidgetTree->RootWidget);
	return RootPanel != nullptr && RootPanel->GetChildrenCount() == 0;
}

UButton* ULobbyWidget::AddPlaceholderButton(UCanvasPanel* Root, const TCHAR* Name, const FText& Label, int32 Row)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
	SnareUiStyle::MakeRoundButton(*Button, SnareUiStyle::HudCornerRadius);
	if (UCanvasPanelSlot* ButtonSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Button)))
	{
		// 세로 한 줄로 가운데 정렬 — 세로 화면에서도 엄지가 닿는 곳에 모인다.
		ButtonSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		ButtonSlot->SetAlignment(FVector2D(0.5f, 0.f));
		ButtonSlot->SetPosition(FVector2D(0.f, RowTop(Row)));
		ButtonSlot->SetSize(FVector2D(PlaceholderButtonWidth, PlaceholderButtonHeight));
	}

	UTextBlock* ButtonLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(FString(Name) + TEXT("Text")));
	ButtonLabel->SetText(Label);
	ButtonLabel->SetJustification(ETextJustify::Center);
	Button->AddChild(ButtonLabel);

	return Button;
}

void ULobbyWidget::BuildPlaceholderTree()
{
	// WBP 가 만들어 둔 빈 캔버스가 있으면 그걸 쓴다 (루트를 갈아 끼우면 WBP 쪽 설정이 날아간다).
	UCanvasPanel* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (Root == nullptr)
	{
		Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LobbyRoot"));
		WidgetTree->RootWidget = Root;
	}

	// 바탕 그림 — 화면을 꽉 채운다(비율 유지, 넘치는 쪽은 잘림). tools/blender/render_intro_cards.py 의 등대 섬 풍경.
	UScaleBox* const Backdrop = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("LobbyBackdrop"));
	Backdrop->SetStretch(EStretch::ScaleToFill);
	Backdrop->SetVisibility(ESlateVisibility::HitTestInvisible);
	UImage* const BackdropImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("LobbyBackdropImage"));
	if (UTexture2D* const BackdropTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Game/UI/Intro/T_TitleBackdrop.T_TitleBackdrop")))
	{
		BackdropImage->SetBrushFromTexture(BackdropTexture, true);
	}
	Backdrop->SetContent(BackdropImage);
	if (UCanvasPanelSlot* const BackdropSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Backdrop)))
	{
		BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BackdropSlot->SetOffsets(FMargin(0.f));
	}

	// 크림 판 (M15-24 — 〔시트 없음 — hud_ingame 규격 준용〕). 글자·버튼은 판 위에 얹힌다.
	// 판 위 작은 제목 탭은 떼어 낸다 — 게임 이름은 아래 큰 로고가 맡는다 (감독 지적: 작은 띠 글씨는 첫눈에 안 들어온다).
	const SnareUiStyle::FMenuPanelParts PanelParts = SnareUiStyle::AddMenuPanel(*WidgetTree, *Root, FText::GetEmpty(),
		PlaceholderPanelTop, PlaceholderPanelWidth, PlaceholderPanelHeight, PlaceholderTitlePillWidth);
	PanelParts.TitlePill->RemoveFromParent();

	// 큰 로고 — 판 왼쪽 하늘(등대 위). 크림 글자 + 진갈색 외곽선, 아래 한글 이름 작게. 높이는 화면의 ~14%.
	// 화면이 좁아도 판과 겹치지 않게 칸은 화면 왼쪽 절반에서 판 반폭+여백을 뺀 곳, 넘치면 줄여 맞춘다(키우지는 않음).
	auto MakeLogoLine = [this](const TCHAR* Name, const FText& Text, float Size, int32 OutlinePx)
	{
		UTextBlock* const Line = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Line->SetText(Text);
		Line->SetJustification(ETextJustify::Center);
		Line->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiPanelCream()));
		FSlateFontInfo Font = Line->GetFont();
		Font.Size = Size;
		Font.OutlineSettings.OutlineSize = OutlinePx;
		Font.OutlineSettings.OutlineColor = SnareUiStyle::UiTitleTab();
		Line->SetFont(Font);
		return Line;
	};
	TitleText = MakeLogoLine(TEXT("LogoText"), LOCTEXT("LobbyTitle", "Snarehaven"), LogoFontSize, LogoOutline);
	UVerticalBox* const LogoColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LogoColumn"));
	LogoColumn->AddChildToVerticalBox(TitleText)->SetHorizontalAlignment(HAlign_Center);
	LogoColumn->AddChildToVerticalBox(MakeLogoLine(TEXT("LogoSubText"), LOCTEXT("LobbyTitleKo", "스네어헤이븐"),
		LogoSubFontSize, LogoSubOutline))->SetHorizontalAlignment(HAlign_Center);
	UScaleBox* const Logo = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("LobbyLogo"));
	Logo->SetStretch(EStretch::ScaleToFit);
	Logo->SetStretchDirection(EStretchDirection::DownOnly);
	Logo->SetVisibility(ESlateVisibility::HitTestInvisible);
	Logo->SetContent(LogoColumn);
	if (UCanvasPanelSlot* const LogoSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Logo)))
	{
		LogoSlot->SetAnchors(FAnchors(0.f, 0.f, 0.5f, 0.f));
		LogoSlot->SetOffsets(FMargin(LogoMargin, LogoTop, PlaceholderPanelWidth * 0.5f + LogoMargin, LogoHeight));
	}

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	// 판 폭보다 긴 문구(상태 + 참가 못 하는 까닭)는 줄을 바꾼다 — 줄 아래 끝을 고정해 두 줄이면 위로 자란다(제목 줄 아래 한 줄과 안 겹치게).
	USizeBox* const StatusBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("StatusBox"));
	StatusBox->SetWidthOverride(PlaceholderPanelWidth);
	StatusBox->SetContent(StatusText);
	if (UCanvasPanelSlot* StatusSlot = Cast<UCanvasPanelSlot>(Root->AddChild(StatusBox)))
	{
		StatusSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		StatusSlot->SetAlignment(FVector2D(0.5f, 1.f));
		StatusSlot->SetPosition(FVector2D(0.f, PlaceholderStatusTop + PlaceholderStatusHeight));
		StatusSlot->SetAutoSize(true);
	}
	StatusText->SetAutoWrapText(true);
	StatusText->SetJustification(ETextJustify::Center);
	StatusText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));

	// 「서버 연결 실패」 옆 작은 「다시 시도」 — 실패일 때만 보인다(RefreshDisplay). 재시도 대기(최대 60초)를 건너뛴다.
	RetryButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("RetryButton"));
	SnareUiStyle::MakeRoundButton(*RetryButton, SnareUiStyle::HudCornerRadius);
	if (UCanvasPanelSlot* const RetrySlot = Cast<UCanvasPanelSlot>(Root->AddChild(RetryButton)))
	{
		RetrySlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		RetrySlot->SetAlignment(FVector2D(0.f, 0.5f));
		RetrySlot->SetPosition(FVector2D(RetryButtonOffsetX, PlaceholderStatusTop + PlaceholderStatusHeight * 0.5f));
		RetrySlot->SetSize(FVector2D(RetryButtonWidth, RetryButtonHeight));
	}
	UTextBlock* const RetryLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RetryButtonText"));
	RetryLabel->SetText(LOCTEXT("LobbyRetry", "다시 시도"));
	RetryLabel->SetJustification(ETextJustify::Center);
	RetryButton->AddChild(RetryLabel);
	RetryButton->SetVisibility(ESlateVisibility::Collapsed);

	// 제목 아래 한 줄 — 처음 켠 사람이 30초 안에 무슨 게임인지 안다 (INBOX 정체성 결함 ①).
	UTextBlock* const TaglineText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TaglineText"));
	TaglineText->SetText(GetTagline());
	TaglineText->SetJustification(ETextJustify::Center);
	TaglineText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	SnareUiStyle::SetFontSize(*TaglineText, PlaceholderTaglineFontSize);
	if (UCanvasPanelSlot* TaglineSlot = Cast<UCanvasPanelSlot>(Root->AddChild(TaglineText)))
	{
		TaglineSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		TaglineSlot->SetAlignment(FVector2D(0.5f, 0.f));
		TaglineSlot->SetPosition(FVector2D(0.f, PlaceholderTaglineTop));
		TaglineSlot->SetSize(FVector2D(PlaceholderPanelWidth, PlaceholderTaglineHeight));
	}

	// 세계관 팁 한 줄 — 화면 아래쪽, 켤 때마다 바뀐다 (정체성 ④).
	UTextBlock* const TipText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TipText"));
	TipText->SetText(GetLoadingTip(FMath::RandRange(0, GetLoadingTipCount() - 1)));
	TipText->SetJustification(ETextJustify::Center);
	TipText->SetAutoWrapText(true);
	// 판 밖(그림 바탕) 글이라 반투명 검은 띠 위에 밝은 크림색으로 — 바다·하늘 위에서도 읽힌다.
	TipText->SetColorAndOpacity(FSlateColor(FLinearColor(0.93f, 0.87f, 0.72f)));
	SnareUiStyle::SetFontSize(*TipText, TipFontSize);
	UBorder* const TipBand = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TipBand"));
	TipBand->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, SnareUiStyle::ScreenPanelRadius));
	TipBand->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.55f));
	TipBand->SetPadding(FMargin(16.f, 6.f));
	TipBand->SetVerticalAlignment(VAlign_Center);
	TipBand->SetVisibility(ESlateVisibility::HitTestInvisible);
	TipBand->SetContent(TipText);
	if (UCanvasPanelSlot* TipSlot = Cast<UCanvasPanelSlot>(Root->AddChild(TipBand)))
	{
		TipSlot->SetAnchors(FAnchors(0.5f, 1.f, 0.5f, 1.f));
		TipSlot->SetAlignment(FVector2D(0.5f, 1.f));
		TipSlot->SetPosition(FVector2D(0.f, -TipBottomOffset));
		TipSlot->SetSize(FVector2D(PlaceholderPanelWidth, TipHeight));
	}

	// 타이틀 첫 버튼은 "시작" 하나 (M15-06 · DESIGN 3.4) — 레퍼런스처럼 누르면 바로 게임이다. 방 버튼은 그 아래.
	SoloButton = AddPlaceholderButton(Root, TEXT("SoloButton"), LOCTEXT("LobbySoloButton", "시작"), 0);
	HostButton = AddPlaceholderButton(Root, TEXT("HostButton"), LOCTEXT("LobbyHostButton", "방 만들기"), 1);
	FindButton = AddPlaceholderButton(Root, TEXT("FindButton"), LOCTEXT("LobbyFindButton", "방 찾기"), 2);
	JoinButton = AddPlaceholderButton(Root, TEXT("JoinButton"), LOCTEXT("LobbyJoinButton", "참가"), 3);

	SettingsButton = AddPlaceholderButton(Root, TEXT("SettingsButton"), FText::GetEmpty(), SettingsRow);

	// 계정 삭제 (Apple 5.1.1(v) — 앱 안에서 시작할 수 있어야 한다). 첫 화면이 아니라 설정 안에 둔다 —
	// 「시작」 옆에 있으면 잘못 누르기 쉽다 (M22 ⑧). 글자는 SetDeleteStep 이 단계마다 바꾼다.
	DeleteAccountButton = AddPlaceholderButton(Root, TEXT("DeleteAccountButton"), FText::GetEmpty(), DeleteAccountRow);
	DeleteCancelButton = AddPlaceholderButton(Root, TEXT("DeleteCancelButton"),
		LOCTEXT("LobbyDeleteCancel", "취소"), DeleteAccountRow);
	// 취소 단추는 삭제 확인 중에만 보이고 오른쪽 반폭이다. 삭제 단추 폭은 SetDeleteStep 이 단계마다 정한다.
	if (UCanvasPanelSlot* CancelSlot = Cast<UCanvasPanelSlot>(DeleteCancelButton->Slot))
	{
		CancelSlot->SetPosition(FVector2D((PlaceholderHalfButtonWidth + PlaceholderButtonGap) * 0.5f, CancelSlot->GetPosition().Y));
		CancelSlot->SetSize(FVector2D(PlaceholderHalfButtonWidth, PlaceholderButtonHeight));
	}

	// 닉네임 (DESIGN 2.2) — 설정 안 「바꾸기」 버튼과, 처음 켤 때·바꿀 때 같이 쓰는 입력 줄 하나.
	NicknameEditButton = AddPlaceholderButton(Root, TEXT("NicknameEditButton"),
		LOCTEXT("LobbyNicknameEdit", "닉네임 바꾸기"), NicknameEditRow);

	NicknameBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("NicknameBox"));
	NicknameBox->SetHintText(LOCTEXT("LobbyNicknameHint", "닉네임"));
	NicknameBox->SetJustification(ETextJustify::Center);
	NicknameBox->KeyboardType = EVirtualKeyboardType::Default;
	// `ApplyUiStyle` 은 글자 블록만 칠한다 — 입력 칸도 한글 글꼴(Jua)이어야 친 글자가 이름표와 같은 모양이다.
	if (UFont* const UiFont = SnareUiStyle::GetUiFont())
	{
		NicknameBox->WidgetStyle.TextStyle.Font = FSlateFontInfo(UiFont, NicknameFontSize);
	}
	// 친 글자는 진갈색, 빈 칸 안내(힌트)만 엔진 기본 회색 — 옅은 회색 글자가 안내문처럼 보인다는 감독 검수(10-19). 설정 입력 칸과 같은 흰 칸.
	{
		FEditableTextBoxStyle& Field = NicknameBox->WidgetStyle;
		const FSlateRoundedBoxBrush FieldBrush(FLinearColor::White, 10.f, SnareUiStyle::UiTitleTab(), 3.f);
		Field.BackgroundImageNormal = FieldBrush;
		Field.BackgroundImageHovered = FieldBrush;
		Field.BackgroundImageFocused = FSlateRoundedBoxBrush(FLinearColor::White, 10.f, SnareUiStyle::UiShopPriceGreen(), 3.f);
		Field.BackgroundImageReadOnly = FieldBrush;
		Field.BackgroundColor = FSlateColor(FLinearColor::White);
		Field.ForegroundColor = FSlateColor(SnareUiStyle::UiTextOnButton());
		Field.FocusedForegroundColor = FSlateColor(SnareUiStyle::UiTextOnButton());
		Field.ReadOnlyForegroundColor = FSlateColor(SnareUiStyle::UiTextOnButton());
	}
	if (UCanvasPanelSlot* BoxSlot = Cast<UCanvasPanelSlot>(Root->AddChild(NicknameBox)))
	{
		BoxSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		BoxSlot->SetAlignment(FVector2D(0.5f, 0.f));
		BoxSlot->SetPosition(FVector2D(0.f, RowTop(NicknameBoxRow)));
		BoxSlot->SetSize(FVector2D(PlaceholderButtonWidth, PlaceholderButtonHeight));
	}

	NicknameConfirmButton = AddPlaceholderButton(Root, TEXT("NicknameConfirmButton"), FText::GetEmpty(), NicknameConfirmRow);

	NicknameErrorPill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("NicknameErrorPill"));
	NicknameErrorPill->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, ErrorPillHeight * 0.5f));
	NicknameErrorPill->SetBrushColor(SnareUiStyle::UiAccentRed());
	NicknameErrorPill->SetPadding(FMargin(ErrorPillPaddingX, 0.f));
	NicknameErrorPill->SetHorizontalAlignment(HAlign_Center);
	NicknameErrorPill->SetVerticalAlignment(VAlign_Center);
	NicknameErrorPill->SetVisibility(ESlateVisibility::Collapsed);
	NicknameErrorText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NicknameErrorText"));
	NicknameErrorText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiPanelCream()));
	SnareUiStyle::SetFontSize(*NicknameErrorText, ErrorPillFontSize);
	NicknameErrorPill->SetContent(NicknameErrorText);
	// 최소 크기만 정하고 긴 문구(PlayFab 거절 문구 그대로)에는 알약이 늘어난다.
	USizeBox* const PillBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("NicknameErrorBox"));
	PillBox->SetMinDesiredWidth(ErrorPillMinWidth);
	PillBox->SetMinDesiredHeight(ErrorPillHeight);
	PillBox->SetContent(NicknameErrorPill);
	// 알약이 접혀도 상자는 「방 찾기」 줄 위에 남는다 — 눌림을 먹지 않게.
	PillBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UCanvasPanelSlot* PillSlot = Cast<UCanvasPanelSlot>(Root->AddChild(PillBox)))
	{
		PillSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		PillSlot->SetAlignment(FVector2D(0.5f, 0.f));
		PillSlot->SetPosition(FVector2D(0.f, RowTop(NicknameErrorRow)));
		PillSlot->SetAutoSize(true);
	}
}

void ULobbyWidget::SetSettingsOpen(bool bOpen)
{
	if (DeleteStep == 3)
	{
		return;
	}
	bSettingsOpen = bOpen;
	// 설정을 열고 닫을 때마다 삭제 확인은 처음부터 — 반쯤 눌러 둔 확인이 다음에 열었을 때 남아 있지 않게.
	SetDeleteStep(0, FText::GetEmpty());
}

void ULobbyWidget::HandleSettingsClicked()
{
	// 설정에서 연 입력 줄이면 「뒤로」 는 설정으로 돌아간다.
	if (bNicknameOpen)
	{
		SetNicknameOpen(false);
		return;
	}
	SetSettingsOpen(!bSettingsOpen);
}

void ULobbyWidget::SetDeleteStep(int32 Step, const FText& Notice)
{
	DeleteStep = Step;
	DeleteNotice = Notice;

	// 설정 화면이면 시작·방 버튼을 숨기고, 아니면 계정 삭제 줄을 숨긴다 — 같은 자리를 번갈아 쓴다.
	// 닉네임 입력 줄은 둘 다 덮는다 — 처음 켤 때는 이름을 받아 주기 전에 「시작」 으로 갈 길이 없어야 한다.
	const bool bSettingsRows = bSettingsOpen && !bNicknameOpen;
	const ESlateVisibility MainVisibility =
		(bSettingsOpen || bNicknameOpen) ? ESlateVisibility::Collapsed : ESlateVisibility::Visible;
	for (UButton* const MainButton : { SoloButton.Get(), HostButton.Get(), FindButton.Get(), JoinButton.Get() })
	{
		if (IsValid(MainButton))
		{
			MainButton->SetVisibility(MainVisibility);
		}
	}
	if (IsValid(SettingsButton))
	{
		if (UTextBlock* Label = Cast<UTextBlock>(SettingsButton->GetContent()))
		{
			Label->SetText((bSettingsOpen || bNicknameOpen) ? LOCTEXT("LobbySettingsBack", "뒤로") : LOCTEXT("LobbySettings", "설정"));
		}
		SettingsButton->SetIsEnabled(Step != 3 && !bNicknameSubmitting);
		// 이름이 없어 연 입력 줄은 뒤로 갈 곳이 없다 — 설정(계정 삭제)도 이름을 정한 뒤다.
		SettingsButton->SetVisibility(bNicknameOpen && NeedsNickname() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}

	if (IsValid(DeleteAccountButton))
	{
		DeleteAccountButton->SetVisibility(bSettingsRows ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		static const FText Labels[] = {
			LOCTEXT("LobbyDeleteAccount", "계정 삭제"),
			LOCTEXT("LobbyDeleteConfirm1", "삭제 (1/2)"),
			LOCTEXT("LobbyDeleteConfirm2", "지운다 (2/2)"),
			LOCTEXT("LobbyDeleteWorking", "지우는 중")
		};
		if (UTextBlock* Label = Cast<UTextBlock>(DeleteAccountButton->GetContent()))
		{
			Label->SetText(Labels[FMath::Clamp(Step, 0, 3)]);
		}
		DeleteAccountButton->SetIsEnabled(Step != 3);
		// 취소 단추가 없는 단계(0·3)는 다른 설정 단추와 같은 폭으로 가운데에 선다 — 반폭으로 한쪽에 쏠리면 미완성처럼 보인다.
		const bool bWithCancel = (Step == 1 || Step == 2);
		if (UCanvasPanelSlot* DeleteSlot = Cast<UCanvasPanelSlot>(DeleteAccountButton->Slot))
		{
			DeleteSlot->SetPosition(FVector2D(bWithCancel ? -(PlaceholderHalfButtonWidth + PlaceholderButtonGap) * 0.5f : 0.f,
				DeleteSlot->GetPosition().Y));
			DeleteSlot->SetSize(FVector2D(bWithCancel ? PlaceholderHalfButtonWidth : PlaceholderButtonWidth, PlaceholderButtonHeight));
		}
	}
	if (IsValid(DeleteCancelButton))
	{
		DeleteCancelButton->SetVisibility(bSettingsRows && (Step == 1 || Step == 2)
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// 삭제 확인 중에는 바꾸기를 숨긴다 — 같은 화면에 서로 다른 「확인」 이 둘 서면 무엇을 지우는지 헷갈린다.
	if (IsValid(NicknameEditButton))
	{
		NicknameEditButton->SetVisibility(bSettingsRows && Step == 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	const ESlateVisibility NicknameVisibility = bNicknameOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (IsValid(NicknameBox))
	{
		NicknameBox->SetVisibility(NicknameVisibility);
		NicknameBox->SetIsEnabled(!bNicknameSubmitting);
	}
	if (IsValid(NicknameConfirmButton))
	{
		NicknameConfirmButton->SetVisibility(NicknameVisibility);
		NicknameConfirmButton->SetIsEnabled(!bNicknameSubmitting);
		if (UTextBlock* Label = Cast<UTextBlock>(NicknameConfirmButton->GetContent()))
		{
			Label->SetText(bNicknameSubmitting ? LOCTEXT("LobbyNicknameWorking", "확인하는 중") : LOCTEXT("LobbyNicknameConfirm", "확인"));
		}
	}
	if (IsValid(NicknameErrorPill) && !bNicknameOpen)
	{
		NicknameErrorPill->SetVisibility(ESlateVisibility::Collapsed);
	}
	RefreshDisplay();
}

void ULobbyWidget::HandleDeleteAccountClicked()
{
	if (DeleteStep == 0)
	{
		SetDeleteStep(1, LOCTEXT("LobbyDeleteWarn1", "계정·아이템·코인·목장이 전부 지워진다. 되돌릴 수 없다"));
		return;
	}
	if (DeleteStep == 1)
	{
		SetDeleteStep(2, LOCTEXT("LobbyDeleteWarn2", "정말 지울까? 한 번 더 누르면 바로 지운다"));
		return;
	}
	if (DeleteStep != 2)
	{
		return;
	}

	UGameInstance* const GameInstance = GetGameInstance();
	USnareBackendSubsystem* const Backend =
		GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
	if (Backend == nullptr)
	{
		SetDeleteStep(0, LOCTEXT("LobbyDeleteNoBackend", "계정을 못 지웠다: 서버에 연결되지 않았다"));
		return;
	}

	// 결과는 서버 답이 온 뒤에만 보인다 — 캐시를 지우는 것도 서브시스템이 서버 답을 받은 뒤다 (CLAUDE.md 8).
	SetDeleteStep(3, LOCTEXT("LobbyDeleteRequesting", "계정 지우는 중..."));
	const TWeakObjectPtr<ULobbyWidget> WeakThis(this);
	Backend->DeleteAccount([WeakThis](bool bDeleted, const FString& Error)
		{
			if (ULobbyWidget* const Self = WeakThis.Get())
			{
				Self->SetDeleteStep(0, bDeleted
					? LOCTEXT("LobbyDeleteDone", "계정을 지웠다. 새 게스트로 시작한다")
					: FText::Format(LOCTEXT("LobbyDeleteFailed", "계정을 못 지웠다: {0}"), FText::FromString(Error)));
			}
		});
}

void ULobbyWidget::HandleDeleteCancelClicked()
{
	SetDeleteStep(0, FText::GetEmpty());
}

bool ULobbyWidget::NeedsNickname() const
{
	const UGameInstance* const GameInstance = GetGameInstance();
	const USnareBackendSubsystem* const Backend =
		GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
	return Backend != nullptr && Backend->NeedsNickname();
}

void ULobbyWidget::SetNicknameOpen(bool bOpen)
{
	if (bNicknameSubmitting || (!bOpen && NeedsNickname()))
	{
		return;
	}
	bNicknameOpen = bOpen;
	ShowNicknameError(FString());
	if (bOpen && IsValid(NicknameBox))
	{
		// 바꿀 때는 지금 이름에서 시작한다 — 한 글자만 고치려는 사람이 처음부터 다시 치지 않게.
		const UGameInstance* const GameInstance = GetGameInstance();
		const USnareBackendSubsystem* const Backend =
			GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
		NicknameBox->SetText(FText::FromString(Backend != nullptr ? Backend->GetDisplayName() : FString()));
	}
	SetDeleteStep(0, FText::GetEmpty());
}

void ULobbyWidget::SetNicknamePreview(bool bPreview)
{
	bNicknamePreview = bPreview;
	if (bPreview)
	{
		bNicknameOpen = true;
	}
	SetDeleteStep(0, FText::GetEmpty());
}

void ULobbyWidget::ShowNicknameError(const FString& Message)
{
	if (IsValid(NicknameErrorText))
	{
		NicknameErrorText->SetText(FText::FromString(TEXT("! ") + Message));
	}
	if (IsValid(NicknameErrorPill))
	{
		NicknameErrorPill->SetVisibility(Message.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void ULobbyWidget::HandleNicknameEditClicked()
{
	SetNicknameOpen(true);
}

void ULobbyWidget::HandleNicknameCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		HandleNicknameConfirmClicked();
	}
}

void ULobbyWidget::HandleNicknameConfirmClicked()
{
	if (!bNicknameOpen || bNicknameSubmitting || !IsValid(NicknameBox))
	{
		return;
	}
	if (bNicknamePreview)
	{
		ShowNicknameError(TEXT("미리보기라 저장하지 않는다"));
		return;
	}
	UGameInstance* const GameInstance = GetGameInstance();
	USnareBackendSubsystem* const Backend =
		GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
	if (Backend == nullptr)
	{
		ShowNicknameError(TEXT("서버에 연결되지 않았다"));
		return;
	}

	// 이름표는 서버·PlayFab 이 둘 다 받아 준 뒤에만 바뀐다 (CLAUDE.md 8) — 여기는 답을 기다렸다 비추기만 한다.
	// 미리 막힌 이름은 콜백이 바로 오므로 진행 표시를 콜백보다 먼저 켠다.
	bNicknameSubmitting = true;
	ShowNicknameError(FString());
	SetDeleteStep(0, FText::GetEmpty());
	const TWeakObjectPtr<ULobbyWidget> WeakThis(this);
	Backend->SubmitNickname(NicknameBox->GetText().ToString(),
		[WeakThis](bool bAccepted, const FString& Code, const FString& Message)
		{
			ULobbyWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bNicknameSubmitting = false;
			if (!bAccepted)
			{
				Self->SetDeleteStep(0, FText::GetEmpty());
				Self->ShowNicknameError(Message);
				return;
			}
			const UGameInstance* const Instance = Self->GetGameInstance();
			const USnareBackendSubsystem* const Accepted =
				Instance != nullptr ? Instance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
			Self->bNicknameOpen = false;
			Self->SetDeleteStep(0, FText::Format(LOCTEXT("LobbyNicknameDone", "닉네임: {0}"),
				FText::FromString(Accepted != nullptr ? Accepted->GetDisplayName() : FString())));
		});
}

void ULobbyWidget::HandleHostClicked()
{
	if (USnareOnlineSubsystem* Online = GetOnline())
	{
		// 콘솔 명령 `Snare.Online.CreateRoom` 과 **같은 함수**다 — 방 만드는 길을 두 군데 적지 않는다.
		Online->CreateRoom();
		RefreshDisplay();
	}
}

void ULobbyWidget::HandleFindClicked()
{
	if (USnareOnlineSubsystem* Online = GetOnline())
	{
		Online->FindRooms();
		RefreshDisplay();
	}
}

void ULobbyWidget::HandleJoinClicked()
{
	if (USnareOnlineSubsystem* Online = GetOnline())
	{
		// 〔가정: 찾은 방 중 첫 번째에 들어간다 — 방 목록 UI 는 아직 없다. `-SnareRoom=join` 과 같은 규칙〕
		Online->JoinFoundRoom(0);
		RefreshDisplay();
	}
}

void ULobbyWidget::HandleSoloClicked()
{
	if (bNicknameOpen && NeedsNickname())
	{
		return;
	}
	// 처음 켠 사람에게 그림 카드 3장을 한 번만 보이고, 끝나면 사냥 맵으로 간다 (정체성 ②).
	if (USnareIntroCardsWidget::ShouldShowIntro())
	{
		if (USnareIntroCardsWidget* const Intro = CreateWidget<USnareIntroCardsWidget>(GetOwningPlayer(), USnareIntroCardsWidget::StaticClass()))
		{
			Intro->OnFinished.AddUObject(this, &ULobbyWidget::StartHunt);
			Intro->AddToViewport(50);
			UE_LOG(LogSnarehaven, Log, TEXT("[Lobby] 첫 실행 그림 카드 %d장을 띄웠다"), USnareIntroCardsWidget::GetCardCount());
			return;
		}
	}
	StartHunt();
}

void ULobbyWidget::StartHunt()
{
	UE_LOG(LogSnarehaven, Log, TEXT("[Lobby] 시작 — %s 로 간다"), *ASnarehavenGameMode::HuntLevelName.ToString());
	UGameplayStatics::OpenLevel(this, ASnarehavenGameMode::HuntLevelName);
}

#undef LOCTEXT_NAMESPACE
