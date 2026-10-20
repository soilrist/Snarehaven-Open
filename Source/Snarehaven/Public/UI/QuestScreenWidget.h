#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "QuestScreenWidget.generated.h"

class FJsonObject;
class UBorder;
class UButton;
class UDataTable;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class USnareBackendSubsystem;
class USnareClickProxy;

/** 서버 `GetQuests` 한 줄. 화면은 받은 그대로 보여 주기만 한다. */
struct FQuestLineView
{
	FName QuestID;
	FName ItemID;
	int32 Count = 0;
	int32 Reward = 0;
	bool bDone = false;
};

/** 의뢰 판 탭 — 오늘 의뢰 / 줄기(DESIGN 8.14 「의뢰 줄기」, M105-04). */
enum class EQuestTab : uint8
{
	Daily,
	Chain
};

// DESIGN 8.14 「의뢰」 — 의뢰인 앞 화면. 닫힌 채엔 "의뢰 보기" 버튼, 열면 오늘 의뢰 3줄.
//
// **이 화면은 코인도 배낭도 바꾸지 않는다.** 열 때 서버 `GetQuests` 로 목록을 받고, 줄을 누르면
// `CompleteQuest {questId}` 를 묻고, 성공 답이 오면 보유 상태와 목록을 **다시 받는다** (CLAUDE.md 8).
UCLASS()
class SNAREHAVEN_API UQuestScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UQuestScreenWidget(const FObjectInitializer& ObjectInitializer);

	void BindToBackend(USnareBackendSubsystem* Backend);

	/** 펴면 목록을 서버에 다시 묻는다. 접으면 "의뢰 보기" 카드만. */
	void SetOpen(bool bInOpen);
	bool IsOpen() const { return bOpen; }

	/** 닫힌 동안 "의뢰 보기" 카드가 붙을 월드 점 — 상점 화면 `SetPromptTarget` 과 같은 길 (M23-02). */
	void SetPromptTarget(const FVector& WorldLocation);

	/** 투영된 대상 자리로 열기 카드를 옮긴다. @return 대상이 화면 안이면 true, 아니면 카드를 접는다 */
	bool PlaceOpenButton(const FVector2D& TargetPx, const FVector2D& ViewportPx);

	/** 다른 창이 열린 동안 「열기」 카드를 접는다 (INBOX 결함 2026-10-11 05:00) — 캐릭터가 창 열림 한 곳에서 부른다. */
	void SetPromptSuppressed(bool bSuppressed);
	bool IsPromptSuppressed() const { return bPromptSuppressed; }

	void SetGiverName(const FText& InName);

	/** 서버 `GetQuests` 를 묻는다. @return 요청을 띄웠으면 true */
	bool RequestQuests();

	/** 줄을 누른 순간 — 서버 `CompleteQuest {questId}`. @return 요청을 띄웠으면 true */
	bool RequestComplete(FName QuestID);

	/** `GetQuests` 응답의 `quests` 를 목록으로 옮긴다. @return 배열이 있어 옮겼으면 true */
	bool ApplyQuestsReply(const TSharedPtr<FJsonObject>& Response);

	/** `GetQuests` 응답의 `quests` 를 줄로 읽기만 한다 — 의뢰 화면과 HUD 다음 목표 줄(M22 ⑤)이 같이 쓴다. */
	static bool ParseQuestLines(const TSharedPtr<FJsonObject>& Response, TArray<FQuestLineView>& OutLines);

	const TArray<FQuestLineView>& GetLines() const { return Lines; }
	bool IsRequestPending() const { return bPending; }
	const FText& GetMessage() const { return Message; }

	/** 스냅샷 배낭에서 그 아이템 개수. 백엔드가 없으면 0. */
	int32 CountOwned(FName ItemID) const;

	/** `당근 x3 (가진 2)  >  5코인` · 끝낸 줄은 `… 완료`. */
	FText GetLineText(const FQuestLineView& Line) const;

	void RefreshDisplay();

	/** 탭을 바꾼다. 줄기 탭이면 서버 `GetQuestChain` 을 묻는다. */
	void SetTab(EQuestTab InTab);
	EQuestTab GetTab() const { return ActiveTab; }

	/** 서버 `GetQuestChain` — 지금 단계를 받는다. @return 요청을 띄웠으면 true */
	bool RequestChain();

	/** 받기를 누른 순간 — 서버 `CompleteQuestChainStep {step}`. 답 전엔 배낭·코인·단계를 안 바꾼다. */
	bool RequestChainComplete();

	/** `GetQuestChain`/`CompleteQuestChainStep` 응답의 `step`(끝낸 단계 수)을 읽는다. @return step 이 있어 읽었으면 true */
	bool ApplyChainReply(const TSharedPtr<FJsonObject>& Response);

	/** 3층 캡처용 (`-SnareQuestChainPreview=<단계>`) — 서버 없이 표로 그 단계(1~)를 채워 줄기 탭을 연다. 받기는 서버를 안 부르고 「확인 중」만 보인다. */
	void SetChainPreview(int32 StepOneBased);
	bool IsChainPreview() const { return bChainPreview; }

	/** 3층 캡처용 (`-SnareQuestOpen=Chain`) — 판을 줄기 탭으로 연 채 시작한다. 미리보기와 달리 서버 `GetQuestChain` 을 실제로 묻는다. */
	void OpenOnChainTab();

	/** 서버가 알려 준 끝낸 단계 수. 아직 못 받았으면 -1. */
	int32 GetChainStep() const { return ChainStep; }

	/** 지금 단계 재료를 배낭이 다 채웠나 (버튼 켜짐용 — 판정은 서버). 단계를 모르거나 끝났으면 false. */
	bool CanClaimChain() const;

	/** 줄기 탭 안내 글 (지금 단계 제목 · 끝났으면 마무리 글). */
	FText GetChainTitleText() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void HandleBackendSnapshotChanged();
	UFUNCTION()
	void HandleOpenClicked();
	UFUNCTION()
	void HandleCloseClicked();
	/** 목록을 못 받았을 때만 보이는 「다시 시도」 (10-16). */
	UFUNCTION()
	void HandleRetryClicked();
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> RetryBox;

	UPROPERTY(Transient)
	TObjectPtr<UButton> OpenButton;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> Panel;
	/** 판 왼쪽 위에 걸친 제목 알약 `의뢰` (M24-05). 판이 접히면 같이 접힌다. */
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> TitlePillBox;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> GiverNameText;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> LineList;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MessageText;
	UPROPERTY(Transient)
	TObjectPtr<UButton> CloseButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> TabDailyButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> TabChainButton;

	UFUNCTION()
	void HandleTabDailyClicked();
	UFUNCTION()
	void HandleTabChainClicked();
	UFUNCTION()
	void HandleChainClaimClicked();

	UPROPERTY(EditDefaultsOnly, Category = "Quest")
	TObjectPtr<UDataTable> ItemTable;

	UPROPERTY()
	TObjectPtr<USnareBackendSubsystem> WatchedBackend;

	/** 화면을 내릴 때 풀었던 백엔드 — 같은 화면을 다시 올리면(`NativeConstruct`) 다시 묶는다(M22). */
	TWeakObjectPtr<USnareBackendSubsystem> BackendToRebind;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USnareClickProxy>> LineProxies;

public:
	/** 아이템 표의 이름. 표에 없는 ID 는 원문 대신 `물건` + ID 마다 경고 로그 한 번 (M24-05). */
	FText FindItemDisplayName(FName ItemID) const;

private:
	void BuildTree();

	TArray<FQuestLineView> Lines;
	bool bOpen = false;

	/** 열기 카드가 붙을 월드 점. 없으면(검사) 카드는 옛 자리 그대로 보인다. */
	TOptional<FVector> PromptTarget;

	/** 마지막 투영에서 대상이 화면 안이었나. */
	bool bPromptOnScreen = true;
	bool bPromptSuppressed = false;
	bool bPending = false;
	/** 마지막 의뢰 목록 요청이 실패했다 — 「다시 시도」 를 띄운다. */
	bool bListFailed = false;
	bool bChainPreview = false;
	EQuestTab ActiveTab = EQuestTab::Daily;
	/** 서버가 알려 준 끝낸 단계 수(0~총 단계). -1 = 아직 못 받음. */
	int32 ChainStep = -1;
	FText Message;
	FText GiverName;

	/** 경고 로그를 이미 남긴 표에 없는 ID — 줄을 다시 그릴 때마다 로그가 쌓이지 않게. */
	mutable TSet<FName> WarnedUnknownIDs;
};
