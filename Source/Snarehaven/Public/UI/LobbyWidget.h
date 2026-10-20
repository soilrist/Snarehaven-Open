#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Online/SnareOnlineSubsystem.h"

#include "LobbyWidget.generated.h"

class UBorder;
class UButton;
class UEditableTextBox;
class UTextBlock;

// ROADMAP M3-04: 로비 화면 — 방 만들기·방 찾기·참가·혼자 사냥.
// 지금까지 방은 콘솔 명령(`Snare.Online.*`)이나 명령줄 스위치(`-SnareRoom=`)로만 열렸다. 모바일에는
// 콘솔이 없으므로 같은 길을 화면 버튼으로 낸다 — **판정은 하나도 여기로 옮기지 않는다.**
// 버튼은 `USnareOnlineSubsystem` 의 같은 함수를 부르고, 이 위젯은 그 상태를 비추기만 한다.
// 보이는 모양은 자리표시다 (`UHuntHudWidget` 과 같은 규칙) — WBP 트리가 비어 있으면 C++ 이 세운다.
UCLASS()
class SNAREHAVEN_API ULobbyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 상태 줄 글자. 로그인·방이 어디까지 갔는지 사람 말로. */
	UFUNCTION(BlueprintPure, Category = "Lobby")
	FText GetStatusText() const;

	/** 방 만들기·찾기 버튼이 눌리는 상태인지. */
	UFUNCTION(BlueprintPure, Category = "Lobby")
	bool IsOpenRoomEnabled() const;

	/** 참가 버튼이 눌리는 상태인지. */
	UFUNCTION(BlueprintPure, Category = "Lobby")
	bool IsJoinEnabled() const;

	/** 화면에 보이는 위젯을 실제 상태에 맞춘다. */
	UFUNCTION(BlueprintCallable, Category = "Lobby")
	void RefreshDisplay();

	/**
	 * 방을 새로 열거나 찾아도 되는 상태인가. 네트워크 없이 2층이 보게 따로 뺐다
	 * (`MakeCreateRoomParams` 와 같은 이유).
	 * 로그인이 끝나야 EOS 를 부를 수 있고, 이미 방에 들어갔거나 진행 중이면 두 번 부르면 안 된다.
	 */
	static bool CanOpenRoom(ESnareLoginState LoginState, ESnareRoomState RoomState);

	/** 찾아 둔 방이 있고 아직 들어가지 않았을 때만 참가할 수 있다. */
	static bool CanJoinRoom(ESnareRoomState RoomState, int32 FoundRoomCount);

	/**
	 * 상태 줄 글자를 만든다. 위 둘과 같은 이유로 static — 2층이 월드 없이 본다.
	 * @param bReturnedFromRoom 방이 해산돼 돌아온 길인가 (M3-03). 참이면 왜 로비에 와 있는지 먼저 알려 준다 —
	 *        이 표시가 화면에 나오지 않으면 게스트는 방이 터진 건지 자기가 잘못 누른 건지 알 수 없다.
	 * @param RejectMessage 입장 거절 문구(`USnareOnlineSubsystem::GetLastRejectCode` → `DT_EntryRejectTable`).
	 *        방 실패·해산 복귀 자리에서 일반 문구 대신 보인다 — "가득 찼다" 와 "버전이 다르다" 는 할 일이 다르다.
	 */
	static FText GetTagline();

	/** 타이틀 아래쪽 세계관 팁 (docs/STORY.md 2절). 인덱스는 개수로 돌려 쓴다. */
	static int32 GetLoadingTipCount();
	static FText GetLoadingTip(int32 Index);

	/**
	 * 참가 버튼이 꺼진 이유 한 줄. 켜져 있거나 방이 이미 움직이는 중이면(상태 줄이 말해 준다) 빈 글자.
	 * 꺼진 단추는 말이 없으므로 처음 하는 사람이 왜 안 눌리는지 모른다 — 상태 줄 끝에 붙인다.
	 */
	static FText MakeJoinDisabledReason(bool bHasOnline, ESnareRoomState RoomState, int32 FoundRoomCount);

	static FText MakeStatusText(ESnareLoginState LoginState, ESnareRoomState RoomState, int32 FoundRoomCount,
		bool bReturnedFromRoom, const FText& RejectMessage = FText::GetEmpty());

	/**
	 * 설정 화면을 펴거나 접는다 (M22 ⑧ — 계정 삭제는 첫 화면이 아니라 설정 안에 있다).
	 * 펴면 시작·방 버튼을 숨기고 계정 삭제 줄을 보이며, 접으면 삭제 확인을 처음으로 돌린다.
	 * 삭제 요청 중(서버 답 전)에는 접지 않는다 — 결과 알림을 놓친다.
	 */
	void SetSettingsOpen(bool bOpen);

	bool IsSettingsOpen() const { return bSettingsOpen; }

	/**
	 * 닉네임 입력 줄을 펴거나 접는다 (DESIGN 2.2 「닉네임」). 펴면 시작·방·계정 삭제 줄을 숨기고 입력 칸·확인 버튼을 보인다.
	 * 로그인했는데 표시 이름이 없으면(`USnareBackendSubsystem::NeedsNickname`) 받아들여질 때까지 접히지 않는다 —
	 * 그동안은 설정 버튼도 숨겨 「시작」 으로 갈 길이 없다. 올리는 중(서버 답 전)에도 접지 않는다.
	 */
	void SetNicknameOpen(bool bOpen);

	bool IsNicknameOpen() const { return bNicknameOpen; }

	/**
	 * 3층 검사용 (M106-01) — 서버·새 계정 없이 닉네임 입력 줄을 펴서 캡처한다. 미리보기 중 「확인」 은 서버를 부르지 않고
	 * 아무것도 저장하지 않는다 (CLAUDE.md 8). 줄을 편 채로 두고, 설정 쪽 「뒤로」 로 접을 수 있다.
	 */
	void SetNicknamePreview(bool bPreview);

	bool IsNicknamePreview() const { return bNicknamePreview; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 아래는 WBP 에서 같은 이름으로 만들면 그걸 쓰고, 없으면 RebuildWidget 이 자리표시로 만들어 넣는다.
	UPROPERTY(BlueprintReadOnly, Category = "Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, Category = "Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	/** PlayFab 접속이 실패했을 때만 상태 줄 옆에 보인다 (`USnareBackendSubsystem::RetryNow`). */
	UPROPERTY(BlueprintReadOnly, Category = "Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UButton> RetryButton;

	UPROPERTY(BlueprintReadOnly, Category = "Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UButton> HostButton;

	UPROPERTY(BlueprintReadOnly, Category = "Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UButton> FindButton;

	UPROPERTY(BlueprintReadOnly, Category = "Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UButton> JoinButton;

	UPROPERTY(BlueprintReadOnly, Category = "Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UButton> SoloButton;

	/** 설정 화면을 연다/닫는다. 열려 있으면 글자가 「뒤로」 다. */
	UPROPERTY(BlueprintReadOnly, Category = "Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UButton> SettingsButton;

	/** 계정 삭제 (DESIGN 4.4 「계정 삭제」). 설정 화면에서만 보인다. 두 번 더 눌러 확인해야 요청이 나간다. */
	UPROPERTY(BlueprintReadOnly, Category = "Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UButton> DeleteAccountButton;

	/** 삭제 확인 중에만 보인다. 누르면 처음으로 돌아간다. */
	UPROPERTY(BlueprintReadOnly, Category = "Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UButton> DeleteCancelButton;

	/** 설정 화면의 「닉네임 바꾸기」. 누르면 지금 이름을 채운 입력 줄이 펴진다. */
	UPROPERTY(BlueprintReadOnly, Category = "Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UButton> NicknameEditButton;

	/** 닉네임 입력 칸. 입력 줄이 펴졌을 때만 보인다. */
	UPROPERTY(BlueprintReadOnly, Category = "Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> NicknameBox;

	/** 닉네임 올리기. 서버 답 전엔 꺼진다. */
	UPROPERTY(BlueprintReadOnly, Category = "Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UButton> NicknameConfirmButton;

	/** 거절 문구 빨간 알약 (DESIGN 2.1 「오류 알약」 모양). 문구가 있을 때만 펴진다. */
	UPROPERTY(BlueprintReadOnly, Category = "Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> NicknameErrorPill;

	UPROPERTY(BlueprintReadOnly, Category = "Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NicknameErrorText;

private:
	/** 상태 줄 + 참가가 꺼진 이유 (RefreshDisplay 가 쓴다). */
	FText GetStatusWithJoinReason() const;

	/** 게임 인스턴스의 온라인 서브시스템. 2층에서 월드 없이 만들면 널이다. */
	USnareOnlineSubsystem* GetOnline() const;

	UFUNCTION()
	void HandleHostClicked();

	/** 백엔드 재시도 대기를 건너뛰고 지금 다시 붙는다. */
	UFUNCTION()
	void HandleRetryClicked();

	/** PlayFab 로그인·보유 상태 조회가 실패해 멈춰 있나 — 백엔드가 없으면(2층) 아니다. */
	bool HasBackendProblem() const;

	/** 틱이 백엔드 실패가 생기거나 풀린 순간을 잡으려고. */
	bool bLastBackendProblem = false;

	UFUNCTION()
	void HandleFindClicked();

	UFUNCTION()
	void HandleJoinClicked();

	/** 혼자 사냥하러 간다 — 로비가 흐름을 멈춰 세웠으니 여기서만 사냥 맵이 열린다. */
	UFUNCTION()
	void HandleSoloClicked();

	/** 0 → 1차 확인 → 2차 확인 → 요청. 확인 단계마다 상태 줄에 되돌릴 수 없다고 알린다. */
	UFUNCTION()
	void HandleSettingsClicked();

	UFUNCTION()
	void HandleDeleteAccountClicked();

	UFUNCTION()
	void HandleDeleteCancelClicked();

	UFUNCTION()
	void HandleNicknameEditClicked();

	/** 입력 칸 글자를 `USnareBackendSubsystem::SubmitNickname` 에 넘긴다 — 거름·저장 판정은 전부 그쪽(서버)이다. */
	UFUNCTION()
	void HandleNicknameConfirmClicked();

	/** 자판의 「완료」(엔터)도 확인과 같다 — 폰에서 자판을 닫고 버튼을 또 찾지 않게. */
	UFUNCTION()
	void HandleNicknameCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	/** 거절 문구를 알약에 띄운다. 빈 글자면 알약을 접는다. */
	void ShowNicknameError(const FString& Message);

	/** 로그인했는데 표시 이름이 없는가 — 백엔드가 없으면(오프라인) 막지 않는다. */
	bool NeedsNickname() const;

	/** 닉네임 입력 줄이 펴져 있는가. */
	bool bNicknameOpen = false;

	/** 검사용 미리보기 — 확인이 서버를 부르지 않는다 (`SetNicknamePreview`). */
	bool bNicknamePreview = false;

	/** 서버 답을 기다리는 중 — 확인 버튼을 끄고 줄을 접지 않는다. */
	bool bNicknameSubmitting = false;

	/** 틱이 「표시 이름 없음」 이 새로 생긴 순간을 잡으려고 (로그인이 로비가 뜬 뒤에 끝난다). */
	bool bLastNeedsNickname = false;

	/** 검사용 그림 카드 미리보기를 이미 띄웠는가 (`-SnareIntroPreview`). */
	bool bIntroPreviewShown = false;

	/** 사냥 맵을 연다 — 그림 카드가 끝났거나 안 뜰 때. */
	void StartHunt();

	/** 단계·설정 화면에 맞춰 버튼 보임·삭제 버튼 글자·상태 줄 알림을 바꾼다. */
	void SetDeleteStep(int32 Step, const FText& Notice);

	/** 설정 화면이 열려 있는가. 시작 화면은 닫힘. */
	bool bSettingsOpen = false;

	/** 0 평소 · 1 1차 확인 · 2 2차 확인 · 3 요청 중. */
	int32 DeleteStep = 0;

	/** 비어 있지 않으면 상태 줄에 방 상태 대신 이걸 보인다(삭제 확인·결과). */
	FText DeleteNotice;

	/** WBP 트리가 아직 비었는지 (루트가 없거나, 자식 없는 빈 패널 하나뿐인지). */
	bool NeedsPlaceholderTree() const;

	/** WBP 에 디자인이 없을 때만 부른다. 엔진 기본 위젯으로 최소 트리를 세운다. */
	void BuildPlaceholderTree();

	/** 버튼 하나 + 그 안의 글자를 만들어 캔버스에 붙인다. 네 번 같은 코드를 쓰지 않으려고. */
	UButton* AddPlaceholderButton(class UCanvasPanel* Root, const TCHAR* Name, const FText& Label, int32 Row);

	// 서브시스템에는 상태가 바뀌었다는 알림이 없다(M3-01 은 로그로만 확인했다). 타이머를 쓰면
	// 주기라는 설계에 없는 숫자를 하나 만들어야 하므로(DataDriven.md 3) 틱에서 **바뀌었을 때만** 고친다.
	ESnareLoginState LastLoginState = ESnareLoginState::NotStarted;
	ESnareRoomState LastRoomState = ESnareRoomState::None;
	int32 LastFoundRoomCount = 0;
};
