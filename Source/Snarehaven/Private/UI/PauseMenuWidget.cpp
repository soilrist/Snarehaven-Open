#include "UI/PauseMenuWidget.h"
#include "Snarehaven.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareNickname.h"
#include "Online/SnareOnlineSubsystem.h"
#include "SnarehavenCharacter.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/SnareCredits.h"
#include "UI/SnareGuideBookWidget.h"
#include "UI/PlayerSafetyWidget.h"
#include "UI/SnareUiStyle.h"
#include "Player/SnareHaptics.h"
#include "Ranch/SnareLocalNotify.h"
#include "World/SnareSound.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 자리표시 UI 배치 (Autonomy.md 🟡 — 게임 규칙 숫자가 아니라 "화면 어디에 얼마나 크게").
	// 진짜 디자인이 WBP_PauseMenu 에 얹히면 아래 트리는 아예 만들어지지 않는다.
	constexpr float PlaceholderPanelTop = 100.f;
	constexpr float PlaceholderPanelWidth = 820.f;
	constexpr float PlaceholderPanelHeight = 916.f;   // 버튼 여섯(10-16 「플레이어」) — 200 + 5×131 + 121 = 976, 아래 여백 40
	constexpr float PlaceholderTitleWidth = 520.f;
	constexpr float PlaceholderButtonWidth = 420.f;
	constexpr float PlaceholderButtonHeight = SnareUiStyle::MenuTouchMin;   // 10-16 96 → 121, 애플 44pt (DESIGN 2)
	constexpr float PlaceholderButtonGap = 10.f;
	constexpr float PlaceholderFirstButtonTop = 200.f;
	// 옵션 카드 (DESIGN 2.2 「설정」 🟡) — 화면 가운데 넓은 카드, 왼쪽 소리 막대 셋 · 오른쪽 진동·알림 스위치 (감독 UI 검수 10-16).
	// 폰(616px 높이)에서 디자인 1단위 ≈ 0.57px — 단추 121 ≈ 69px, 막대 손잡이 77 ≈ 44px (애플 최소 누름 44pt 의 px 환산).
	constexpr float OptionsCardWidth = 1300.f;   // 4:3(디자인 폭 1440)에서도 양옆 70 씩 남는다
	constexpr float OptionsLabelWidth = 170.f;
	constexpr float OptionsPercentWidth = 110.f;
	constexpr float OptionsRowGap = 20.f;
	constexpr float OptionsColumnGap = 56.f;
	constexpr float OptionsTouchHeight = 121.f;
	constexpr float OptionsThumbSize = 77.f;
	constexpr float OptionsCloseWidth = 220.f;
	constexpr float OptionsFontSize = 30.f;
	constexpr float OptionsHintFontSize = 24.f;
	constexpr float CreditsFontSize = 22.f;
	constexpr float CreditsScrollHeight = 380.f;   // 폰 가로 화면에서 닫기 단추가 밀려나지 않을 만큼
	// 스위치 — 켬=고른 탭 노랑(손잡이 오른쪽) · 끔=나무(손잡이 왼쪽). 색은 팔레트(`MarkSelectedTab`) 그대로(10-16 팔레트 시험).
	constexpr float OptionsSwitchWidth = 230.f;
	constexpr float OptionsSwitchKnob = 85.f;
	constexpr float OptionsSwitchPadding = 14.f;
	// 닉네임 줄 (닉네임 b3) — 칸 글꼴은 로비 입력 줄(24)보다 카드 글자(30)에 맞춘다. 단추는 「확인하는 중」 이 들어갈 폭.
	constexpr float OptionsNicknameButtonWidth = 200.f;
	constexpr float OptionsNicknameFieldRadius = 10.f;
	constexpr float OptionsNicknameFieldBorder = 3.f;
	constexpr float OptionsNicknamePillHeight = 34.f;
	constexpr float OptionsNicknamePillFontSize = 20.f;
	constexpr float OptionsNicknamePillPaddingX = 16.f;

	/** 스위치 단추 = [왼 손잡이 · 글 · 오른 손잡이] — 켬이면 오른쪽 손잡이만, 끔이면 왼쪽만 보인다. */
	void ApplySwitchLook(UTextBlock* Text, bool bOn)
	{
		UHorizontalBox* const Row = Text != nullptr ? Cast<UHorizontalBox>(Text->GetParent()) : nullptr;
		UButton* const Button = Row != nullptr ? Cast<UButton>(Row->GetParent()) : nullptr;
		if (Button == nullptr || Row->GetChildrenCount() != 3)
		{
			return;
		}
		SnareUiStyle::MarkSelectedTab(Button, bOn);
		// 글은 단추 바로 아래가 아니라 줄 안이라 `MarkSelectedTab` 이 못 칠한다 — 같은 규칙으로 여기서.
		Text->SetColorAndOpacity(FSlateColor(bOn ? SnareUiStyle::UiTextOnButton() : SnareUiStyle::UiPanelCream()));
		Row->GetChildAt(0)->SetVisibility(bOn ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		Row->GetChildAt(2)->SetVisibility(bOn ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

const TArray<FName>& UPauseMenuWidget::GetButtonOrder()
{
	// 감독이 정한 순서 그대로 — 이어하기 · 옵션 · 로비로 · 게임 종료 (지시 2026-09-20 12:05).
	// 안내서(M28-06b2)는 옵션 바로 아래 — 게임을 떠나는 두 버튼(로비로·게임 종료)은 맨 아래에 그대로 둔다(🟡).
	// 「플레이어」(신고·차단, 10-16)는 안내서 아래 — 떠나는 두 버튼 위.
	static const TArray<FName> ButtonOrder = {
		TEXT("ResumeButton"), TEXT("OptionsButton"), TEXT("GuideButton"), TEXT("PlayersButton"), TEXT("LobbyButton"), TEXT("QuitButton")
	};
	return ButtonOrder;
}

bool UPauseMenuWidget::ShouldAllowPauseMenu(const FString& CurrentMapName)
{
	// 로비 판정은 `ASnarehavenGameMode::IsLobbyMap` 과 같은 자리(`USnareOnlineSubsystem::LobbyLevelName`)를
	// 본다 — 맵 이름을 여기 새로 적으면 로비 맵을 바꿀 때 한쪽만 바뀐다.
	return !CurrentMapName.Contains(USnareOnlineSubsystem::LobbyLevelName.ToString());
}

void UPauseMenuWidget::ShowMenu()
{
	if (!IsInViewport())
	{
		// 전체 화면 판과 같은 Z — HUD(0)와 같은 Z 면 나중에 뜬 상인 「상점 열기」 카드(Z 0)가 메뉴 버튼 위에 겹쳤다(루프#429 캡처).
		AddToViewport(SnareUiStyle::ScreenPanelViewportZOrder);
	}
	SetOptionsOpen(false);   // 다시 열면 늘 메뉴 첫 화면부터

	APlayerController* const PlayerController = GetOwningPlayer();
	if (!IsValid(PlayerController))
	{
		return;
	}

	// 월드는 멈추지 않는다 (SetPause 를 부르지 않는다) — 멀티에서 한 사람 메뉴가 남의 사냥을 세우면 안 된다.
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PlayerController->SetInputMode(InputMode);
	PlayerController->SetShowMouseCursor(true);
	UE_LOG(LogSnarehaven, Log, TEXT("[UI] 일시정지 메뉴를 열었다 (월드는 계속 돈다)"));
}

void UPauseMenuWidget::CloseMenu()
{
	if (APlayerController* const PlayerController = GetOwningPlayer())
	{
		// `FInputModeGameOnly` 를 넣으면 마우스가 뷰포트에 영구 캡처되고(`CapturePermanently`) 커서가 숨어
		// 그 뒤 HUD 의 공격·포획 버튼을 마우스로 못 누른다 (감독 지시 2026-09-20 12:45 (b)).
		// 메뉴를 닫고 돌아가는 화면은 HUD 가 떠 있는 화면이다 — 설정을 여기 한 번 더 적지 않고
		// HUD 를 띄울 때 쓰는 함수를 그대로 쓴다 (M1-10e).
		ASnarehavenCharacter::ApplyHudInputMode(PlayerController);
	}

	RemoveFromParent();
	UE_LOG(LogSnarehaven, Log, TEXT("[UI] 일시정지 메뉴를 닫았다"));
}

TSharedRef<SWidget> UPauseMenuWidget::RebuildWidget()
{
	if (NeedsPlaceholderTree())
	{
		BuildPlaceholderTree();
	}

	if (IsValid(ResumeButton) && !ResumeButton->OnClicked.IsAlreadyBound(this, &UPauseMenuWidget::HandleResumeClicked))
	{
		ResumeButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleResumeClicked);
	}
	if (IsValid(GuideButton) && !GuideButton->OnClicked.IsAlreadyBound(this, &UPauseMenuWidget::HandleGuideClicked))
	{
		GuideButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleGuideClicked);
	}
	if (IsValid(PlayersButton) && !PlayersButton->OnClicked.IsAlreadyBound(this, &UPauseMenuWidget::HandlePlayersClicked))
	{
		PlayersButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandlePlayersClicked);
	}
	if (IsValid(LobbyButton) && !LobbyButton->OnClicked.IsAlreadyBound(this, &UPauseMenuWidget::HandleLobbyClicked))
	{
		LobbyButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleLobbyClicked);
	}
	if (IsValid(QuitButton) && !QuitButton->OnClicked.IsAlreadyBound(this, &UPauseMenuWidget::HandleQuitClicked))
	{
		QuitButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleQuitClicked);
	}

	// 옵션 카드는 자리표시 트리에만 있다 — WBP 에 디자인이 얹혀 카드가 없으면 누를 거리가 없어 꺼 둔다.
	if (IsValid(OptionsButton))
	{
		OptionsButton->SetIsEnabled(IsValid(OptionsCard));
		if (!OptionsButton->OnClicked.IsAlreadyBound(this, &UPauseMenuWidget::HandleOptionsClicked))
		{
			OptionsButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleOptionsClicked);
		}
	}

	// 글자·버튼 모양은 화면마다 따로 정하지 않는다 — 네 화면이 같은 글꼴·색을 쓴다 (M5-05a·b).
	// `Super` **뒤**라야 한다 — Slate 위젯이 생긴 뒤에 칠해야 화면까지 바뀐다.
	TSharedRef<SWidget> Built = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	// 스위치·로그인 전 회색은 나무색 일괄 칠하기 **뒤**에 다시 — 안 그러면 켬/끔이 같은 나무 단추로 보인다.
	ApplySwitchLook(VibrationText, SnareHaptics::IsEnabled());
	ApplySwitchLook(NotifyText, SnareLocalNotify::IsEnabled());
	ApplyNicknameLook();
	return Built;
}

bool UPauseMenuWidget::NeedsPlaceholderTree() const
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

UButton* UPauseMenuWidget::AddPlaceholderButton(UCanvasPanel* Root, FName Name, const FText& Label, int32 Row)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	if (UCanvasPanelSlot* ButtonSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Button)))
	{
		// 세로 한 줄로 화면 가운데 — 가로·세로 어느 쪽 화면에서도 엄지가 닿는 곳에 모인다 (로비와 같은 배치).
		ButtonSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		ButtonSlot->SetAlignment(FVector2D(0.5f, 0.f));
		ButtonSlot->SetPosition(FVector2D(0.f, PlaceholderFirstButtonTop + Row * (PlaceholderButtonHeight + PlaceholderButtonGap)));
		ButtonSlot->SetSize(FVector2D(PlaceholderButtonWidth, PlaceholderButtonHeight));
	}

	UTextBlock* ButtonLabel =
		WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name.ToString() + TEXT("Text")));
	ButtonLabel->SetText(Label);
	ButtonLabel->SetJustification(ETextJustify::Center);
	Button->AddChild(ButtonLabel);

	return Button;
}

void UPauseMenuWidget::BuildPlaceholderTree()
{
	// WBP 가 만들어 둔 빈 캔버스가 있으면 그걸 쓴다 (루트를 갈아 끼우면 WBP 쪽 설정이 날아간다).
	UCanvasPanel* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (Root == nullptr)
	{
		Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("PauseMenuRoot"));
		WidgetTree->RootWidget = Root;
	}

	// 크림 판 + 위로 걸친 제목 탭 (M15-24 — 〔시트 없음 — hud_ingame 규격 준용〕).
	// 월드가 계속 돈다는 걸 제목이 먼저 말해 준다 — 멀티에서 "멈춘 줄 알았다" 가 제일 위험하다.
	const SnareUiStyle::FMenuPanelParts Parts = SnareUiStyle::AddMenuPanel(*WidgetTree, *Root,
		LOCTEXT("PauseMenuTitle", "메뉴 (게임은 계속 진행 중)"), PlaceholderPanelTop, PlaceholderPanelWidth,
		PlaceholderPanelHeight, PlaceholderTitleWidth);
	MenuTitle = Parts.TitleText;
	MenuTitlePill = Parts.TitlePill;
	MenuPanel = Parts.Panel;

	const TArray<FText> Labels = {
		LOCTEXT("PauseMenuResume", "이어하기"),
		LOCTEXT("PauseMenuOptions", "옵션"),
		LOCTEXT("PauseMenuGuide", "안내서"),
		LOCTEXT("PauseMenuPlayers", "플레이어"),
		LOCTEXT("PauseMenuLobby", "로비로"),
		LOCTEXT("PauseMenuQuit", "게임 종료"),
	};

	const TArray<FName>& ButtonOrder = GetButtonOrder();
	for (int32 Row = 0; Row < ButtonOrder.Num(); ++Row)
	{
		UButton* const Button = AddPlaceholderButton(Root, ButtonOrder[Row], Labels[Row], Row);
		switch (Row)
		{
		case 0:
			ResumeButton = Button;
			break;
		case 1:
			OptionsButton = Button;
			break;
		case 2:
			GuideButton = Button;
			break;
		case 3:
			PlayersButton = Button;
			break;
		case 4:
			LobbyButton = Button;
			break;
		default:
			QuitButton = Button;
			break;
		}
	}
	BuildOptionsCard(Root);
}

void UPauseMenuWidget::BuildOptionsCard(UCanvasPanel* Root)
{
	OptionsCard = SnareUiStyle::MakeCreamCard(*WidgetTree, TEXT("OptionsCard"));
	OptionsCard->SetPadding(FMargin(36.f, 28.f));
	if (UCanvasPanelSlot* const CardSlot = Cast<UCanvasPanelSlot>(Root->AddChild(OptionsCard)))
	{
		// 화면 한가운데 — 위에 붙이면 폰 가로(디자인 높이 1080)에서 아래 단추가 화면 밖으로 밀렸다.
		CardSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		CardSlot->SetPosition(FVector2D::ZeroVector);
		CardSlot->SetAutoSize(true);
	}
	UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OptionsColumn"));
	OptionsCard->SetContent(Column);

	auto AddText = [this](const FText& Text, const TCHAR* Name, float Size) {
		UTextBlock* const Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Label->SetText(Text);
		Label->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		SnareUiStyle::SetFontSize(*Label, Size);
		return Label;
	};

	UTextBlock* const Title = AddText(LOCTEXT("OptionsTitle", "설정"), TEXT("OptionsTitle"), OptionsFontSize + 8.f);
	Title->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* const TitleSlot = Column->AddChildToVerticalBox(Title))
	{
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, OptionsRowGap));
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
	}

	// 본문 두 칸 — 왼쪽 소리 막대 셋, 오른쪽 켬/끔 스위치 둘. 한 줄로 쌓으면 폰 화면 높이를 넘는다.
	UHorizontalBox* const Body = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("OptionsBody"));
	if (UVerticalBoxSlot* const BodySlot = Column->AddChildToVerticalBox(Body))
	{
		BodySlot->SetPadding(FMargin(0.f, 0.f, 0.f, OptionsRowGap));
	}
	UVerticalBox* const SoundColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OptionsSoundColumn"));
	if (UHorizontalBoxSlot* const SoundSlot = Body->AddChildToHorizontalBox(SoundColumn))
	{
		SoundSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		SoundSlot->SetVerticalAlignment(VAlign_Center);
	}
	UVerticalBox* const ToggleColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OptionsToggleColumn"));
	if (UHorizontalBoxSlot* const ToggleSlot = Body->AddChildToHorizontalBox(ToggleColumn))
	{
		ToggleSlot->SetPadding(FMargin(OptionsColumnGap, 0.f, 0.f, 0.f));
		ToggleSlot->SetVerticalAlignment(VAlign_Center);
	}

	const SnareSound::FVolumes& Volumes = SnareSound::GetVolumes();
	const TArray<TPair<FText, float>> Rows = {
		{ LOCTEXT("OptionsMaster", "전체 소리"), Volumes.Master },
		{ LOCTEXT("OptionsEffects", "효과음"), Volumes.Effects },
		{ LOCTEXT("OptionsAmbience", "주변 소리"), Volumes.Ambience },
	};
	// 손잡이는 지름 77 동그라미 — 기본 그림(작은 네모)은 엄지 밑에 숨어 어디를 잡았는지 안 보였다.
	const FSlateRoundedBoxBrush Thumb(FLinearColor::White, OptionsThumbSize * 0.5f);
	VolumeSliders.Reset();
	VolumeTexts.Reset();
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		UHorizontalBox* const Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),
			*FString::Printf(TEXT("OptionsRow%d"), Index));
		if (UVerticalBoxSlot* const RowSlot = SoundColumn->AddChildToVerticalBox(Row))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, Index + 1 < Rows.Num() ? OptionsRowGap : 0.f));
		}
		UTextBlock* const Label = AddText(Rows[Index].Key, *FString::Printf(TEXT("OptionsLabel%d"), Index), OptionsFontSize);
		Label->SetMinDesiredWidth(OptionsLabelWidth);
		if (UHorizontalBoxSlot* const LabelSlot = Row->AddChildToHorizontalBox(Label))
		{
			LabelSlot->SetVerticalAlignment(VAlign_Center);
		}

		USlider* const Slider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(),
			*FString::Printf(TEXT("OptionsSlider%d"), Index));
		Slider->SetValue(Rows[Index].Value);
		Slider->SetStepSize(0.05f);
		Slider->SetSliderBarColor(SnareUiStyle::UiButtonPressed());
		Slider->SetSliderHandleColor(SnareUiStyle::UiTitleTab());
		FSliderStyle Style = Slider->GetWidgetStyle();
		Style.BarThickness = 12.f;
		Style.NormalThumbImage = Thumb;
		Style.HoveredThumbImage = Thumb;
		Style.DisabledThumbImage = Thumb;
		Style.NormalThumbImage.ImageSize = FVector2D(OptionsThumbSize);
		Style.HoveredThumbImage.ImageSize = FVector2D(OptionsThumbSize);
		Style.DisabledThumbImage.ImageSize = FVector2D(OptionsThumbSize);
		Slider->SetWidgetStyle(Style);
		Slider->OnValueChanged.AddDynamic(this, &UPauseMenuWidget::HandleVolumeChanged);
		if (UHorizontalBoxSlot* const SliderSlot = Row->AddChildToHorizontalBox(Slider))
		{
			SliderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			SliderSlot->SetVerticalAlignment(VAlign_Center);
			SliderSlot->SetPadding(FMargin(12.f, 0.f));
		}
		VolumeSliders.Add(Slider);

		// 「5%」 와 「100%」 의 끝자리가 같은 세로줄에 서게 고정 폭 + 오른쪽 맞춤.
		UTextBlock* const Percent = AddText(FText::GetEmpty(), *FString::Printf(TEXT("OptionsPercent%d"), Index), OptionsFontSize);
		Percent->SetMinDesiredWidth(OptionsPercentWidth);
		Percent->SetJustification(ETextJustify::Right);
		if (UHorizontalBoxSlot* const PercentSlot = Row->AddChildToHorizontalBox(Percent))
		{
			PercentSlot->SetVerticalAlignment(VAlign_Center);
			PercentSlot->SetHorizontalAlignment(HAlign_Right);
		}
		VolumeTexts.Add(Percent);
	}

	// 진동 줄 (M21-03b) · 알림 줄 (M21-04b2 · DESIGN 3.2 「앱 밖 알림」) — 켬/끔 둘뿐이라 막대 대신 스위치.
	auto AddSwitchRow = [&](const TCHAR* RowName, const FText& LabelText, const TCHAR* LabelName, const TCHAR* ButtonName,
		const TCHAR* TextName, bool bOn, bool bLast) -> UTextBlock*
	{
		UHorizontalBox* const Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), RowName);
		if (UVerticalBoxSlot* const RowSlot = ToggleColumn->AddChildToVerticalBox(Row))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, bLast ? 0.f : OptionsRowGap));
		}
		UTextBlock* const Label = AddText(LabelText, LabelName, OptionsFontSize);
		Label->SetMinDesiredWidth(100.f);
		if (UHorizontalBoxSlot* const LabelSlot = Row->AddChildToHorizontalBox(Label))
		{
			LabelSlot->SetVerticalAlignment(VAlign_Center);
		}
		UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), ButtonName);
		SnareUiStyle::MakeRoundButton(*Button, OptionsTouchHeight * 0.5f);
		FButtonStyle Padded = Button->GetStyle();
		Padded.NormalPadding = FMargin(OptionsSwitchPadding);
		Padded.PressedPadding = FMargin(OptionsSwitchPadding);
		Button->SetStyle(Padded);
		UHorizontalBox* const Track = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),
			*FString::Printf(TEXT("%sTrack"), ButtonName));
		// 단추 칸 기본은 가운데 — 채워야 손잡이가 양 끝으로 간다.
		if (UButtonSlot* const TrackSlot = Cast<UButtonSlot>(Button->AddChild(Track)))
		{
			TrackSlot->SetHorizontalAlignment(HAlign_Fill);
			TrackSlot->SetVerticalAlignment(VAlign_Fill);
		}
		auto AddKnob = [&](const TCHAR* Side) {
			UBorder* const Knob = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
				*FString::Printf(TEXT("%sKnob%s"), ButtonName, Side));
			Knob->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiPanelCream(), OptionsSwitchKnob * 0.5f));
			USizeBox* const KnobBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
				*FString::Printf(TEXT("%sKnobBox%s"), ButtonName, Side));
			KnobBox->SetWidthOverride(OptionsSwitchKnob);
			KnobBox->SetHeightOverride(OptionsSwitchKnob);
			KnobBox->AddChild(Knob);
			if (UHorizontalBoxSlot* const KnobSlot = Track->AddChildToHorizontalBox(KnobBox))
			{
				KnobSlot->SetVerticalAlignment(VAlign_Center);
			}
		};
		AddKnob(TEXT("Left"));
		UTextBlock* const Text = AddText(FormatVibrationLabel(bOn), TextName, OptionsFontSize);
		Text->SetJustification(ETextJustify::Center);
		if (UHorizontalBoxSlot* const TextSlot = Track->AddChildToHorizontalBox(Text))
		{
			TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			TextSlot->SetVerticalAlignment(VAlign_Center);
			TextSlot->SetHorizontalAlignment(HAlign_Center);
		}
		AddKnob(TEXT("Right"));
		USizeBox* const SwitchBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
			*FString::Printf(TEXT("%sBox"), ButtonName));
		SwitchBox->SetWidthOverride(OptionsSwitchWidth);
		SwitchBox->SetHeightOverride(OptionsTouchHeight);
		SwitchBox->AddChild(Button);
		if (UHorizontalBoxSlot* const BoxSlot = Row->AddChildToHorizontalBox(SwitchBox))
		{
			BoxSlot->SetVerticalAlignment(VAlign_Center);
			BoxSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
		}
		ApplySwitchLook(Text, bOn);
		return Text;
	};
	VibrationText = AddSwitchRow(TEXT("OptionsVibrationRow"), LOCTEXT("OptionsVibration", "진동"), TEXT("OptionsVibrationLabel"),
		TEXT("OptionsVibrationButton"), TEXT("OptionsVibrationText"), SnareHaptics::IsEnabled(), false);
	NotifyText = AddSwitchRow(TEXT("OptionsNotifyRow"), LOCTEXT("OptionsNotify", "알림"), TEXT("OptionsNotifyLabel"),
		TEXT("OptionsNotifyButton"), TEXT("OptionsNotifyText"), SnareLocalNotify::IsEnabled(), true);
	if (UButton* const VibrationButton = WidgetTree->FindWidget<UButton>(TEXT("OptionsVibrationButton")))
	{
		VibrationButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleVibrationClicked);
	}
	if (UButton* const NotifyButton = WidgetTree->FindWidget<UButton>(TEXT("OptionsNotifyButton")))
	{
		NotifyButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleNotifyClicked);
	}

	BuildNicknameRow(Column);
	BuildCreditsRow(Column);

	// 카드는 내용 크기를 따른다 — 막대가 엄지로 끌 만큼 길도록 빈 줄에 최소 폭을 준다.
	UTextBlock* const WidthSpacer = AddText(FText::GetEmpty(), TEXT("OptionsWidth"), 1.f);
	WidthSpacer->SetMinDesiredWidth(OptionsCardWidth - 72.f);
	Column->AddChildToVerticalBox(WidthSpacer);

	// 아래 단추 줄 — 「만든 사람·라이선스」 와 「닫기」 를 한 줄에 (둘 다 높이 121).
	UHorizontalBox* const Footer = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("OptionsFooterRow"));
	if (UVerticalBoxSlot* const FooterSlot = Column->AddChildToVerticalBox(Footer))
	{
		FooterSlot->SetHorizontalAlignment(HAlign_Center);
	}
	auto AddFooterButton = [&](const TCHAR* ButtonName, const TCHAR* TextName, const FText& Label, float Width) -> UButton*
	{
		UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), ButtonName);
		SnareUiStyle::MakeRoundButton(*Button, SnareUiStyle::HudCornerRadius);
		UTextBlock* const Text = AddText(Label, TextName, OptionsFontSize);
		Text->SetJustification(ETextJustify::Center);
		Text->SetMinDesiredWidth(Width);
		Button->AddChild(Text);
		if (UButtonSlot* const TextSlot = Cast<UButtonSlot>(Text->Slot))
		{
			TextSlot->SetVerticalAlignment(VAlign_Center);
		}
		USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
			*FString::Printf(TEXT("%sBox"), ButtonName));
		Box->SetHeightOverride(OptionsTouchHeight);
		Box->AddChild(Button);
		if (UHorizontalBoxSlot* const BoxSlot = Footer->AddChildToHorizontalBox(Box))
		{
			BoxSlot->SetPadding(FMargin(OptionsRowGap, 0.f));
		}
		return Button;
	};
	UButton* const Credits = AddFooterButton(TEXT("OptionsCreditsButton"), TEXT("OptionsCreditsButtonText"),
		LOCTEXT("OptionsCredits", "만든 사람·라이선스"), 0.f);
	Credits->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleCreditsClicked);
	UButton* const Close = AddFooterButton(TEXT("OptionsCloseButton"), TEXT("OptionsCloseText"), LOCTEXT("OptionsClose", "닫기"),
		OptionsCloseWidth);
	Close->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleOptionsClose);
	RefreshVolumeTexts();
}

void UPauseMenuWidget::BuildCreditsRow(UVerticalBox* Column)
{
	// 만든 사람·라이선스 (M98-g · docs/ASSET_LICENSES.md) — 아래 줄 단추를 누르면 이 스크롤 칸이 펴진다.
	CreditsScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("OptionsCreditsScroll"));
	USizeBox* const ScrollLimit = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("OptionsCreditsLimit"));
	ScrollLimit->SetHeightOverride(CreditsScrollHeight);
	ScrollLimit->SetVisibility(ESlateVisibility::Collapsed);
	ScrollLimit->AddChild(CreditsScroll);
	CreditsLimit = ScrollLimit;
	UTextBlock* const Body = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("OptionsCreditsBody"));
	Body->SetText(FText::FromString(SnareCredits::GetText()));
	Body->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	Body->SetAutoWrapText(true);
	SnareUiStyle::SetFontSize(*Body, CreditsFontSize);
	CreditsScroll->AddChild(Body);
	if (UVerticalBoxSlot* const ScrollSlot = Column->AddChildToVerticalBox(ScrollLimit))
	{
		ScrollSlot->SetPadding(FMargin(0.f, 0.f, 0.f, OptionsRowGap));
	}
	CreditsScroll->SetConsumeMouseWheel(EConsumeMouseWheel::Always);
}

void UPauseMenuWidget::HandleCreditsClicked()
{
	SetCreditsOpen(!IsCreditsOpen());
}

void UPauseMenuWidget::SetCreditsOpen(bool bOpen)
{
	if (CreditsLimit == nullptr)
	{
		return;
	}
	CreditsLimit->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	// 펴면 카드가 1080 높이 화면을 넘어 아래 단추 줄이 밀려나므로 소리·스위치·닉네임 줄은 접어 둔다.
	if (const UVerticalBox* const Column = OptionsCard != nullptr ? Cast<UVerticalBox>(OptionsCard->GetContent()) : nullptr)
	{
		static const TArray<FName> KeepNames = { TEXT("OptionsTitle"), TEXT("OptionsCreditsLimit"), TEXT("OptionsWidth"),
			TEXT("OptionsFooterRow") };
		for (UWidget* const Child : Column->GetAllChildren())
		{
			if (Child != nullptr && !KeepNames.Contains(Child->GetFName()))
			{
				Child->SetVisibility(bOpen ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
			}
		}
		// 닉네임 알약은 알릴 글자가 있을 때만 보인다 — 일괄 펴기가 빈 알약(까만 점)을 되살리지 않게.
		if (!bOpen && IsValid(NicknamePill) && IsValid(NicknamePillText) && NicknamePillText->GetText().IsEmpty())
		{
			NicknamePill->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

bool UPauseMenuWidget::IsCreditsOpen() const
{
	return CreditsLimit != nullptr && CreditsLimit->GetVisibility() != ESlateVisibility::Collapsed;
}

void UPauseMenuWidget::BuildNicknameRow(UVerticalBox* Column)
{
	// 닉네임 줄 (DESIGN 2.2 「닉네임」 — 설정(ESC)에서 언제든 바꿈). 진동·알림 줄과 같은 「이름표 + 단추」 모양에
	// 가운데 입력 칸만 끼운다. 처음 켤 때 막는 입력 줄은 로비 쪽(`ULobbyWidget`)이고, 여기는 바꾸기만 한다.
	UHorizontalBox* const Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("OptionsNicknameRow"));
	if (UVerticalBoxSlot* const RowSlot = Column->AddChildToVerticalBox(Row))
	{
		RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, OptionsRowGap * 0.5f));
	}
	UTextBlock* const Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("OptionsNicknameLabel"));
	Label->SetText(LOCTEXT("OptionsNickname", "닉네임"));
	Label->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	SnareUiStyle::SetFontSize(*Label, OptionsFontSize);
	Label->SetMinDesiredWidth(OptionsLabelWidth);
	if (UHorizontalBoxSlot* const LabelSlot = Row->AddChildToHorizontalBox(Label))
	{
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}

	NicknameBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("OptionsNicknameBox"));
	NicknameBox->KeyboardType = EVirtualKeyboardType::Default;
	// 흰 칸 + 진갈색 테 + 진갈색 글자 — 엔진 기본 칸은 크림 카드 위에서 테가 안 보이고 글자가 흐렸다(감독 검수 10-16).
	// 규칙 안내는 칸 안 흐린 힌트 대신 칸 아래 한 줄(`OptionsNicknameHint`) — 이름을 지우지 않아도 늘 보인다.
	FEditableTextBoxStyle& Field = NicknameBox->WidgetStyle;
	const FSlateRoundedBoxBrush FieldBrush(FLinearColor::White, OptionsNicknameFieldRadius, SnareUiStyle::UiTitleTab(),
		OptionsNicknameFieldBorder);
	Field.BackgroundImageNormal = FieldBrush;
	Field.BackgroundImageHovered = FieldBrush;
	Field.BackgroundImageFocused = FSlateRoundedBoxBrush(FLinearColor::White, OptionsNicknameFieldRadius,
		SnareUiStyle::UiShopPriceGreen(), OptionsNicknameFieldBorder);
	Field.BackgroundImageReadOnly = FieldBrush;
	Field.BackgroundColor = FSlateColor(FLinearColor::White);
	Field.ForegroundColor = FSlateColor(SnareUiStyle::UiTextOnButton());
	Field.FocusedForegroundColor = FSlateColor(SnareUiStyle::UiTextOnButton());
	Field.ReadOnlyForegroundColor = FSlateColor(SnareUiStyle::UiTextOnButton());
	Field.Padding = FMargin(20.f, 0.f);
	// `ApplyUiStyle` 은 글자 블록만 칠한다 — 칸도 한글 글꼴(Jua)이어야 친 글자가 이름표와 같은 모양이다 (로비 입력 줄과 같은 이유).
	if (UFont* const UiFont = SnareUiStyle::GetUiFont())
	{
		Field.TextStyle.Font = FSlateFontInfo(UiFont, OptionsFontSize);
	}
	NicknameBox->OnTextCommitted.AddDynamic(this, &UPauseMenuWidget::HandleNicknameCommitted);
	USizeBox* const FieldBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("OptionsNicknameBoxSize"));
	FieldBox->SetHeightOverride(OptionsTouchHeight);
	FieldBox->AddChild(NicknameBox);
	if (USizeBoxSlot* const InnerSlot = Cast<USizeBoxSlot>(NicknameBox->Slot))
	{
		InnerSlot->SetVerticalAlignment(VAlign_Fill);
	}
	if (UHorizontalBoxSlot* const BoxSlot = Row->AddChildToHorizontalBox(FieldBox))
	{
		BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		BoxSlot->SetVerticalAlignment(VAlign_Center);
		BoxSlot->SetPadding(FMargin(12.f, 0.f));
	}

	NicknameButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("OptionsNicknameButton"));
	SnareUiStyle::MakeRoundButton(*NicknameButton, SnareUiStyle::HudCornerRadius);
	NicknameButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("OptionsNicknameButtonText"));
	NicknameButtonText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	SnareUiStyle::SetFontSize(*NicknameButtonText, OptionsFontSize);
	NicknameButtonText->SetJustification(ETextJustify::Center);
	NicknameButtonText->SetMinDesiredWidth(OptionsNicknameButtonWidth);
	NicknameButton->AddChild(NicknameButtonText);
	if (UButtonSlot* const TextSlot = Cast<UButtonSlot>(NicknameButtonText->Slot))
	{
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}
	NicknameButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleNicknameSubmitClicked);
	USizeBox* const ButtonBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("OptionsNicknameButtonBox"));
	ButtonBox->SetHeightOverride(OptionsTouchHeight);
	ButtonBox->AddChild(NicknameButton);
	if (UHorizontalBoxSlot* const ButtonSlot = Row->AddChildToHorizontalBox(ButtonBox))
	{
		ButtonSlot->SetVerticalAlignment(VAlign_Center);
	}

	// 칸 아래 안내 한 줄 — 칸 왼쪽 끝(이름표 폭 + 틈)에 맞춰 칸에 딸린 글로 읽히게.
	NicknameHint = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("OptionsNicknameHint"));
	NicknameHint->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	SnareUiStyle::SetFontSize(*NicknameHint, OptionsHintFontSize);
	if (UVerticalBoxSlot* const HintSlot = Column->AddChildToVerticalBox(NicknameHint))
	{
		HintSlot->SetPadding(FMargin(OptionsLabelWidth + 24.f, 0.f, 0.f, OptionsRowGap * 0.5f));
	}

	// 칸 아래 알약 — 로비 오류 알약과 같은 모양(높이 34·글자 20·앞 `!`). 접히면 자리를 안 먹는다.
	NicknamePill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("OptionsNicknamePill"));
	NicknamePill->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, OptionsNicknamePillHeight * 0.5f));
	NicknamePill->SetPadding(FMargin(OptionsNicknamePillPaddingX, 0.f));
	NicknamePill->SetHorizontalAlignment(HAlign_Center);
	NicknamePill->SetVerticalAlignment(VAlign_Center);
	NicknamePill->SetVisibility(ESlateVisibility::Collapsed);
	NicknamePillText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("OptionsNicknamePillText"));
	NicknamePillText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiPanelCream()));
	SnareUiStyle::SetFontSize(*NicknamePillText, OptionsNicknamePillFontSize);
	NicknamePillText->SetMinDesiredWidth(1.f);
	NicknamePill->SetContent(NicknamePillText);
	if (UVerticalBoxSlot* const PillSlot = Column->AddChildToVerticalBox(NicknamePill))
	{
		PillSlot->SetHorizontalAlignment(HAlign_Center);
		PillSlot->SetPadding(FMargin(0.f, 0.f, 0.f, OptionsRowGap * 0.5f));
	}
	RefreshNicknameRow();
}

void UPauseMenuWidget::RefreshNicknameRow()
{
	const UGameInstance* const GameInstance = GetGameInstance();
	const USnareBackendSubsystem* const Backend =
		GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
	// 로그인 전(오프라인·로그인 실패)엔 올릴 곳이 없다 — 눌러서 빨간 알약을 보느니 처음부터 꺼 둔다.
	const bool bLoggedIn = Backend != nullptr && Backend->GetLoginState() == ESnareBackendLoginState::LoggedIn;
	if (IsValid(NicknameBox))
	{
		if (!bNicknameSubmitting)
		{
			NicknameBox->SetText(FText::FromString(Backend != nullptr ? Backend->GetDisplayName() : FString()));
		}
		NicknameBox->SetIsEnabled(bLoggedIn && !bNicknameSubmitting);
	}
	if (IsValid(NicknameButton))
	{
		NicknameButton->SetIsEnabled(bLoggedIn && !bNicknameSubmitting);
	}
	if (IsValid(NicknameButtonText))
	{
		NicknameButtonText->SetText(bNicknameSubmitting
			? LOCTEXT("OptionsNicknameWorking", "확인하는 중")
			: (bLoggedIn ? LOCTEXT("OptionsNicknameChange", "바꾸기") : LOCTEXT("OptionsNicknameOffline", "로그인 전")));
	}
	if (IsValid(NicknameHint))
	{
		NicknameHint->SetText(bLoggedIn
			? FText::Format(LOCTEXT("OptionsNicknameHint", "{0}~{1}자, 한글·영문·숫자"),
				FText::AsNumber(SnareNickname::MinLength), FText::AsNumber(SnareNickname::MaxLength))
			: LOCTEXT("OptionsNicknameOfflineHint", "서버에 로그인하면 바꿀 수 있다"));
	}
	ApplyNicknameLook();
}

void UPauseMenuWidget::ApplyNicknameLook()
{
	// 꺼진 단추는 `ApplyUiStyle` 의 비활성 회갈색(`UiDisabledGray`)으로 저절로 바뀐다(감독 검수 10-16 「로그인 전」) — 따로 칠하지 않는다.
	if (IsValid(NicknameButtonText))
	{
		NicknameButtonText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiPanelCream()));
	}
	if (IsValid(NicknameBox))
	{
		NicknameBox->SetRenderOpacity(NicknameBox->GetIsEnabled() ? 1.f : 0.6f);
	}
}

void UPauseMenuWidget::ShowNicknameNotice(const FString& Message, bool bError)
{
	if (IsValid(NicknamePillText))
	{
		NicknamePillText->SetText(FText::FromString(bError ? TEXT("! ") + Message : Message));
	}
	if (IsValid(NicknamePill))
	{
		NicknamePill->SetBrushColor(bError ? SnareUiStyle::UiAccentRed() : SnareUiStyle::UiTitleTab());
		NicknamePill->SetVisibility(Message.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void UPauseMenuWidget::HandleNicknameCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		HandleNicknameSubmitClicked();
	}
}

void UPauseMenuWidget::HandleNicknameSubmitClicked()
{
	if (bNicknameSubmitting || !IsValid(NicknameBox))
	{
		return;
	}
	UGameInstance* const GameInstance = GetGameInstance();
	USnareBackendSubsystem* const Backend =
		GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
	if (Backend == nullptr)
	{
		ShowNicknameNotice(TEXT("서버에 연결되지 않았다"), true);
		return;
	}
	const FString Typed = SnareNickname::Trim(NicknameBox->GetText().ToString());
	if (!Typed.IsEmpty() && Typed == Backend->GetDisplayName())
	{
		// 지금 이름 그대로면 왕복하지 않는다 — 카드를 열자마자 「바꾸기」 를 눌러도 서버가 바쁘지 않게.
		ShowNicknameNotice(FString(), false);
		return;
	}

	// 이름표는 서버·PlayFab 이 둘 다 받아 준 뒤에만 바뀐다 (CLAUDE.md 8) — 여기는 답을 기다렸다 비추기만 한다.
	// 미리 막힌 이름은 콜백이 바로 오므로 진행 표시를 콜백보다 먼저 켠다.
	bNicknameSubmitting = true;
	ShowNicknameNotice(FString(), false);
	RefreshNicknameRow();
	const TWeakObjectPtr<UPauseMenuWidget> WeakThis(this);
	Backend->SubmitNickname(Typed,
		[WeakThis](bool bAccepted, const FString& Code, const FString& Message)
		{
			UPauseMenuWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bNicknameSubmitting = false;
			if (!bAccepted)
			{
				// 거절이면 친 글자를 남긴다 — 한 글자만 고쳐 다시 누를 수 있게.
				const FText Kept = IsValid(Self->NicknameBox) ? Self->NicknameBox->GetText() : FText::GetEmpty();
				Self->RefreshNicknameRow();
				if (IsValid(Self->NicknameBox))
				{
					Self->NicknameBox->SetText(Kept);
				}
				Self->ShowNicknameNotice(Message, true);
				return;
			}
			Self->RefreshNicknameRow();
			const UGameInstance* const Instance = Self->GetGameInstance();
			const USnareBackendSubsystem* const Accepted =
				Instance != nullptr ? Instance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
			Self->ShowNicknameNotice(FText::Format(LOCTEXT("OptionsNicknameDone", "닉네임을 {0}(으)로 바꿨다"),
				FText::FromString(Accepted != nullptr ? Accepted->GetDisplayName() : FString())).ToString(), false);
			UE_LOG(LogSnarehaven, Log, TEXT("[UI] 일시정지 옵션 — 닉네임을 바꿨다"));
		});
}

void UPauseMenuWidget::SetOptionsOpen(bool bOpen)
{
	if (!IsValid(OptionsCard))
	{
		return;
	}
	OptionsCard->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (bOpen)
	{
		// 펼 때마다 지금 이름으로 — 로비에서 바꾼 이름·지난번에 쳐 두고 안 올린 글자가 남지 않게.
		ShowNicknameNotice(FString(), false);
		RefreshNicknameRow();
	}
	const ESlateVisibility MenuVisibility = bOpen ? ESlateVisibility::Collapsed : ESlateVisibility::Visible;
	for (UWidget* const Widget : TArray<UWidget*>{ ResumeButton, OptionsButton, GuideButton, PlayersButton, LobbyButton, QuitButton, MenuTitlePill, MenuPanel })
	{
		if (IsValid(Widget))
		{
			Widget->SetVisibility(MenuVisibility);
		}
	}
}

bool UPauseMenuWidget::IsOptionsOpen() const
{
	return IsValid(OptionsCard) && OptionsCard->GetVisibility() == ESlateVisibility::Visible;
}

void UPauseMenuWidget::HandleOptionsClicked()
{
	SetOptionsOpen(true);
}

void UPauseMenuWidget::HandleOptionsClose()
{
	SetOptionsOpen(false);
}

USnareGuideBookWidget* UPauseMenuWidget::OpenGuideBook()
{
	// 주인·월드는 닫기 전에 잡는다 — 닫은 뒤에도 위젯은 살아 있지만 순서를 읽기 쉽게.
	APlayerController* const Owner = GetOwningPlayer();
	UWorld* const World = GetWorld();
	CloseMenu();
	USnareGuideBookWidget* const Guide = USnareGuideBookWidget::OpenOnViewport(Owner, World);
	OpenedGuideBook = Guide;
	UE_LOG(LogSnarehaven, Log, TEXT("[UI] 일시정지 메뉴 — 안내서를 열었다 (쪽 %d)"),
		Guide != nullptr ? Guide->GetPageKeys().Num() : 0);
	return Guide;
}

void UPauseMenuWidget::HandleGuideClicked()
{
	OpenGuideBook();
}

UPlayerSafetyWidget* UPauseMenuWidget::OpenPlayerSafety()
{
	APlayerController* const Owner = GetOwningPlayer();
	UWorld* const World = GetWorld();
	CloseMenu();
	UPlayerSafetyWidget* const Screen = UPlayerSafetyWidget::OpenOnViewport(Owner, World);
	UE_LOG(LogSnarehaven, Log, TEXT("[UI] 일시정지 메뉴 — 신고·차단 판을 열었다 (사람 %d)"),
		Screen != nullptr ? Screen->GetPeers().Num() : 0);
	return Screen;
}

void UPauseMenuWidget::HandlePlayersClicked()
{
	OpenPlayerSafety();
}

void UPauseMenuWidget::HandleVolumeChanged(float Value)
{
	if (VolumeSliders.Num() < 3)
	{
		return;
	}
	SnareSound::FVolumes Volumes;
	Volumes.Master = VolumeSliders[0]->GetValue();
	Volumes.Effects = VolumeSliders[1]->GetValue();
	Volumes.Ambience = VolumeSliders[2]->GetValue();
	SnareSound::SetVolumes(Volumes);
	RefreshVolumeTexts();
}

FText UPauseMenuWidget::FormatVibrationLabel(bool bEnabled)
{
	return bEnabled ? LOCTEXT("OptionsVibrationOn", "켬") : LOCTEXT("OptionsVibrationOff", "끔");
}

void UPauseMenuWidget::ToggleVibration()
{
	SnareHaptics::SetEnabled(!SnareHaptics::IsEnabled());
	if (IsValid(VibrationText))
	{
		VibrationText->SetText(FormatVibrationLabel(SnareHaptics::IsEnabled()));
	}
	ApplySwitchLook(VibrationText, SnareHaptics::IsEnabled());
	UE_LOG(LogSnarehaven, Log, TEXT("[UI] 진동 %s"), SnareHaptics::IsEnabled() ? TEXT("켬") : TEXT("끔"));
}

FText UPauseMenuWidget::GetVibrationLabel() const
{
	return IsValid(VibrationText) ? VibrationText->GetText() : FText::GetEmpty();
}

void UPauseMenuWidget::HandleVibrationClicked()
{
	ToggleVibration();
}

void UPauseMenuWidget::ToggleNotify()
{
	SnareLocalNotify::ApplySetting(!SnareLocalNotify::IsEnabled());
	if (IsValid(NotifyText))
	{
		NotifyText->SetText(FormatVibrationLabel(SnareLocalNotify::IsEnabled()));
	}
	ApplySwitchLook(NotifyText, SnareLocalNotify::IsEnabled());
	UE_LOG(LogSnarehaven, Log, TEXT("[UI] 알림 %s"), SnareLocalNotify::IsEnabled() ? TEXT("켬") : TEXT("끔"));
}

FText UPauseMenuWidget::GetNotifyLabel() const
{
	return IsValid(NotifyText) ? NotifyText->GetText() : FText::GetEmpty();
}

void UPauseMenuWidget::HandleNotifyClicked()
{
	ToggleNotify();
}

void UPauseMenuWidget::RefreshVolumeTexts()
{
	for (int32 Index = 0; Index < VolumeTexts.Num() && Index < VolumeSliders.Num(); ++Index)
	{
		VolumeTexts[Index]->SetText(FText::FromString(FString::Printf(TEXT("%d%%"),
			FMath::RoundToInt(VolumeSliders[Index]->GetValue() * 100.f))));
	}
}

void UPauseMenuWidget::HandleResumeClicked()
{
	CloseMenu();
}

void UPauseMenuWidget::HandleLobbyClicked()
{
	// 방에서 먼저 나온다 — 호스트면 `LeaveRoom` 이 방을 해산한다(DESIGN 3.4, 호스트 마이그레이션 없음).
	// 방을 안 치우고 맵만 옮기면 아무도 없는 방이 남의 검색 결과에 계속 뜬다.
	if (UGameInstance* const GameInstance = GetGameInstance())
	{
		if (USnareOnlineSubsystem* const Online = GameInstance->GetSubsystem<USnareOnlineSubsystem>())
		{
			Online->LeaveRoom();
		}
	}

	CloseMenu();
	UE_LOG(LogSnarehaven, Log, TEXT("[UI] 일시정지 메뉴 — 로비(%s)로 간다"),
		*USnareOnlineSubsystem::LobbyLevelName.ToString());
	UGameplayStatics::OpenLevel(this, USnareOnlineSubsystem::LobbyLevelName);
}

void UPauseMenuWidget::HandleQuitClicked()
{
	UE_LOG(LogSnarehaven, Log, TEXT("[UI] 일시정지 메뉴 — 게임을 끝낸다"));
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, /*bIgnorePlatformRestrictions=*/false);
}

#undef LOCTEXT_NAMESPACE
