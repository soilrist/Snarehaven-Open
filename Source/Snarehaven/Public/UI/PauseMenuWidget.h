#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "PauseMenuWidget.generated.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UEditableTextBox;
class UScrollBox;
class USizeBox;
class USlider;
class USnareGuideBookWidget;
class UPlayerSafetyWidget;
class UTextBlock;
class UVerticalBox;

// ROADMAP M1-10b: ESC 일시정지 메뉴 — 이어하기 · 옵션 · 안내서 · 로비로 · 게임 종료 (감독 지시 2026-09-20 12:05, 안내서 M28-06b2).
// **월드는 멈추지 않는다.** 4~8인 PVE(DESIGN 3.4)라 한 사람이 메뉴를 열었다고 남의 사냥이 멎으면 안 된다 —
// 메뉴만 화면에 올리고 입력 모드만 UI 쪽으로 바꾼다.
// 옵션 = 소리 크기 세 줄(전체·효과음·주변음) 카드 (DESIGN 2.2 「설정」). 폰에는 ESC 가 없어 HUD 톱니 버튼이 연다.
// 보이는 모양은 자리표시다 (`ULobbyWidget` 과 같은 규칙) — WBP 트리가 비어 있으면 C++ 이 세운다.
UCLASS()
class SNAREHAVEN_API UPauseMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * 화면에 보이는 버튼 이름 — **순서 그대로**다 (감독이 정한 순서, 2층이 이 배열로 확인한다).
	 * 자리표시 트리도 이 배열로 만든다: 순서를 두 군데 적으면 한쪽만 바뀐다.
	 */
	static const TArray<FName>& GetButtonOrder();

	/**
	 * 이 맵에서 ESC 로 일시정지 메뉴를 띄워도 되는가. **로비에서는 안 띄운다** — 로비 화면에는 이미
	 * 버튼이 넷 있고(`ULobbyWidget`), 그 위에 또 메뉴가 겹치면 어느 쪽 버튼을 눌러야 할지 알 수 없다.
	 * @param CurrentMapName `UWorld::GetMapName()` 값. PIE 접두어가 붙으므로 Contains 로 본다
	 *        (`ASnarehavenGameMode::IsLobbyMap` 과 같은 규칙).
	 */
	static bool ShouldAllowPauseMenu(const FString& CurrentMapName);

	/**
	 * 메뉴를 화면에 올리고 마우스·터치가 버튼에 닿게 입력 모드를 바꾼다. **월드는 계속 돈다.**
	 * 입력 모드는 UIOnly 가 아니라 GameAndUI 다 — UIOnly 면 ESC 가 게임 쪽으로 오지 않아
	 * 감독이 정한 "ESC 재입력이면 닫힘" 이 성립하지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "PauseMenu")
	void ShowMenu();

	/** 메뉴를 화면에서 내리고 입력을 게임으로 되돌린다. 이어하기 버튼과 ESC 재입력이 같은 길을 쓴다. */
	UFUNCTION(BlueprintCallable, Category = "PauseMenu")
	void CloseMenu();

	/** 옵션 카드를 펴거나 접는다. 펴면 메뉴 버튼 넷을 숨긴다. 검사(`-SnareOptions`)도 이 길. */
	void SetOptionsOpen(bool bOpen);

	bool IsOptionsOpen() const;

	/** 진동 줄 단추 글 — 켜졌으면 `켬`, 꺼졌으면 `끔` (M21-03b). */
	static FText FormatVibrationLabel(bool bEnabled);

	/** 진동 줄 단추를 누른 것과 같다 — 켬/끔을 뒤집어 저장하고 글을 바꾼다. 2층 검사도 이 길. */
	void ToggleVibration();

	/** 지금 진동 줄 단추에 적힌 글. 카드를 아직 안 만들었으면 빈 글. */
	FText GetVibrationLabel() const;

	/** 알림 줄 단추를 누른 것과 같다 — 켬/끔을 뒤집어 저장하고, 끄면 걸어 둔 알림을 지운다 (M21-04b2). 2층 검사도 이 길. */
	void ToggleNotify();

	/** 지금 알림 줄 단추에 적힌 글(`켬`/`끔`, 진동 줄과 같은 글). 카드를 아직 안 만들었으면 빈 글. */
	FText GetNotifyLabel() const;

	/**
	 * 「안내서」 버튼을 누른 것과 같다 (M28-06b2) — 메뉴를 닫고 안내서 판(`USnareGuideBookWidget`)을 연다.
	 * 메뉴를 먼저 닫아서 안내서의 닫기·ESC 한 번이면 게임으로 돌아간다. 2층 검사도 이 길.
	 */
	USnareGuideBookWidget* OpenGuideBook();

	/** 「플레이어」 버튼을 누른 것과 같다 (10-16) — 메뉴를 닫고 신고·차단 판(`UPlayerSafetyWidget`)을 연다. */
	UPlayerSafetyWidget* OpenPlayerSafety();

	/**
	 * 옵션 카드의 닉네임 칸·「바꾸기」 를 지금 로그인·표시 이름에 맞춘다 (DESIGN 2.2 「닉네임」 — 설정(ESC)에서 언제든 바꿈).
	 * 칸은 지금 이름으로 채우고, 로그인 전이면 칸·단추를 끈다. 카드를 펼 때마다 부른다.
	 */
	void RefreshNicknameRow();

	/** 이 메뉴가 마지막으로 연 안내서 — 검사가 버튼 누름의 결과를 본다. 없으면 nullptr. */
	USnareGuideBookWidget* GetOpenedGuideBook() const { return OpenedGuideBook.Get(); }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	// 아래 여섯은 WBP 에서 같은 이름으로 만들면 그걸 쓰고, 없으면 RebuildWidget 이 자리표시로 만들어 넣는다.
	UPROPERTY(BlueprintReadOnly, Category = "PauseMenu", meta = (BindWidgetOptional))
	TObjectPtr<UButton> ResumeButton;

	UPROPERTY(BlueprintReadOnly, Category = "PauseMenu", meta = (BindWidgetOptional))
	TObjectPtr<UButton> OptionsButton;

	UPROPERTY(BlueprintReadOnly, Category = "PauseMenu", meta = (BindWidgetOptional))
	TObjectPtr<UButton> GuideButton;

	UPROPERTY(BlueprintReadOnly, Category = "PauseMenu", meta = (BindWidgetOptional))
	TObjectPtr<UButton> PlayersButton;

	UPROPERTY(BlueprintReadOnly, Category = "PauseMenu", meta = (BindWidgetOptional))
	TObjectPtr<UButton> LobbyButton;

	UPROPERTY(BlueprintReadOnly, Category = "PauseMenu", meta = (BindWidgetOptional))
	TObjectPtr<UButton> QuitButton;

private:
	UFUNCTION()
	void HandleResumeClicked();

	UFUNCTION()
	void HandleOptionsClicked();

	UFUNCTION()
	void HandleOptionsClose();

	UFUNCTION()
	void HandleGuideClicked();

	UFUNCTION()
	void HandlePlayersClicked();

	/** 슬라이더 셋을 읽어 한꺼번에 저장한다 (어느 줄이 움직였는지 몰라도 된다). */
	UFUNCTION()
	void HandleVolumeChanged(float Value);

	UFUNCTION()
	void HandleVibrationClicked();

	UFUNCTION()
	void HandleNotifyClicked();

	/** 칸 글자를 `USnareBackendSubsystem::SubmitNickname` 에 넘긴다 — 거름·저장 판정은 전부 그쪽(서버)이다. */
	UFUNCTION()
	void HandleNicknameSubmitClicked();

	/** 자판 「완료」 도 「바꾸기」 와 같다. */
	UFUNCTION()
	void HandleNicknameCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	/** 칸 아래 알약 — 빈 글이면 접는다. 거절은 빨강, 바뀜은 진갈색. */
	void ShowNicknameNotice(const FString& Message, bool bError);

	void BuildOptionsCard(UCanvasPanel* Root);
	void BuildCreditsRow(UVerticalBox* Column);

	/** 「만든 사람·라이선스」 단추 — 아래 스크롤 칸을 펴고 접는다 (M98-g). */
	UFUNCTION()
	void HandleCreditsClicked();
	void BuildNicknameRow(UVerticalBox* Column);
	void RefreshVolumeTexts();

	UPROPERTY(Transient)
	TObjectPtr<UBorder> OptionsCard;

	TWeakObjectPtr<USnareGuideBookWidget> OpenedGuideBook;

	/** 전체 · 효과음 · 주변음 순서. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USlider>> VolumeSliders;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> VolumeTexts;

	/** 진동 켬/끔 단추 글 (M21-03b). */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> VibrationText;

	/** 앱 밖 알림 켬/끔 단추 글 (M21-04b2). */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NotifyText;

	/** 만든 사람·라이선스 글을 담은 스크롤 칸 — 처음엔 접혀 있다. */
	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> CreditsScroll;

	/** 스크롤 칸 높이 한도 + 접기 대상. */
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> CreditsLimit;

public:
	/** 라이선스 칸이 펴져 있는지 (시험용). */
	bool IsCreditsOpen() const;

	/** 라이선스 칸을 펴거나 접는다. 단추와 검사(`-SnareCredits`)가 같은 길. */
	void SetCreditsOpen(bool bOpen);

private:
	/** 옵션 카드 닉네임 줄 (닉네임 b3) — 칸 · 「바꾸기」 · 그 아래 알약. */
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> NicknameBox;

	UPROPERTY(Transient)
	TObjectPtr<UButton> NicknameButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NicknameButtonText;

	/** 칸 아래 안내 한 줄 — 로그인 전이면 「로그인하면 바꿀 수 있다」. */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NicknameHint;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> NicknamePill;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NicknamePillText;

	/** 닉네임 단추·칸을 켜짐 여부에 맞춰 칠한다 — 꺼졌으면 회색. `ApplyUiStyle` 뒤에도 부른다. */
	void ApplyNicknameLook();

	/** 서버 답을 기다리는 중 — 칸·단추를 끄고 단추 글을 「확인하는 중」 으로. */
	bool bNicknameSubmitting = false;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MenuTitle;

	/** 제목 글자를 감싼 탭 — 옵션 카드가 펴지면 글자와 함께 접는다. */
	UPROPERTY(Transient)
	TObjectPtr<UBorder> MenuTitlePill;

	/** 메뉴 크림 판 — 옵션 카드가 펴지면 접는다(안 접으면 설정 카드 뒤로 빈 판이 비친다, M98-g 보정). */
	UPROPERTY(Transient)
	TObjectPtr<UBorder> MenuPanel;

	/** 방에서 나와 로비로. 호스트면 방을 해산한다 — 판정은 `USnareOnlineSubsystem::LeaveRoom` 하나뿐이다. */
	UFUNCTION()
	void HandleLobbyClicked();

	UFUNCTION()
	void HandleQuitClicked();

	/** WBP 트리가 아직 비었는지 (루트가 없거나, 자식 없는 빈 패널 하나뿐인지). */
	bool NeedsPlaceholderTree() const;

	/** WBP 에 디자인이 없을 때만 부른다. 엔진 기본 위젯으로 최소 트리를 세운다. */
	void BuildPlaceholderTree();

	/** 버튼 하나 + 그 안의 글자를 만들어 캔버스에 붙인다 (`ULobbyWidget::AddPlaceholderButton` 과 같은 이유). */
	UButton* AddPlaceholderButton(UCanvasPanel* Root, FName Name, const FText& Label, int32 Row);
};
