#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/QuestScreenWidget.h"

#include "SnareNextGoalWidget.generated.h"

class UBorder;
class UDataTable;
class UTextBlock;
class USnareBackendSubsystem;

// 첫 실행 안내(5장)가 끝난 뒤 사냥 HUD 위 가운데에 남는 **다음 목표 한 줄** (INBOX M22 ⑤, DESIGN 8.14 「다음 목표 줄」).
// 줄은 서버 `GetQuests` 의 오늘 의뢰 중 **아직 안 끝낸 첫 줄**이다 — 가진 개수는 스냅샷 배낭에서 읽기만 한다.
// 누르는 것 없음(정보만) · 코인·배낭·의뢰 완료를 바꾸지 않는다 (CLAUDE.md 8).
UCLASS()
class SNAREHAVEN_API USnareNextGoalWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	USnareNextGoalWidget(const FObjectInitializer& ObjectInitializer);

	/** 백엔드를 묶고 의뢰 목록을 한 번 묻는다. 그 뒤엔 보유 상태가 바뀔 때마다 다시 묻는다(간격 `MinRefreshSeconds`). */
	void BindToBackend(USnareBackendSubsystem* Backend);

	/** 서버 `GetQuests` 를 묻는다. @return 요청을 띄웠으면 true */
	bool RequestQuests();

	/** `GetQuests` 응답을 줄로 옮긴다. @return `quests` 배열이 있었으면 true */
	bool ApplyQuestsReply(const TSharedPtr<FJsonObject>& Response);

	/**
	 * 보여 줄 한 줄 — 규칙 전부가 여기 있다 (2층 검사가 이 함수를 그대로 부른다).
	 * 답 전: `의뢰인 솔방울에게 오늘 의뢰를 받아요` · 모으는 중: `당근 3개 모으기 (2/3)` ·
	 * 다 모음: `의뢰인 솔방울에게 당근 3개 건네기 > 5코인` · 전부 끝: `오늘 의뢰 끝! 내일 새 의뢰가 와요`.
	 * 하루 의뢰가 전부 끝났고 줄기 단계(`InChainStep` = 끝낸 단계 수, 모르면 -1)를 알면 줄기 글로 바뀐다
	 * (`땔감 부탁: 나무 30개 모으기 (12/30)` / `의뢰인 솔방울에게 땔감 부탁 건네기 > 10코인`).
	 * 줄기도 끝이면 빈 글 — 줄이 접힌다 (DESIGN 8.14 「다음 목표 줄」, M105-05).
	 */
	static FText FormatGoal(bool bInHasReply, const TArray<FQuestLineView>& InLines, TFunctionRef<int32(FName)> CountOwned,
		TFunctionRef<FText(FName)> DisplayName, int32 InChainStep = -1);

	/** `GetQuestChain` 응답의 `step` 을 읽는다. @return step 이 있었으면 true */
	bool ApplyChainReply(const TSharedPtr<FJsonObject>& Response);

	FText GetGoalText() const;
	bool IsRequestPending() const { return bPending; }
	bool HasReply() const { return bHasReply; }

	/** 보유 상태 변화로 다시 물을 때 최소 간격(초) — 🟡 서버 호출 아끼기. 2층 검사는 0 으로 둔다. */
	float MinRefreshSeconds = 10.f;

	/** 메인 스토리 「지금 할 일」 줄이 떠 있으면 두 줄이 서로 다른 말을 하므로 이 줄을 접는다. */
	void SetSuppressed(bool bInSuppressed);
	bool IsSuppressed() const { return bSuppressed; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandleBackendSnapshotChanged();

	FText FindItemDisplayName(FName ItemID) const;
	int32 CountOwned(FName ItemID) const;
	void Refresh();

	UPROPERTY(EditDefaultsOnly, Category = "Quest")
	TObjectPtr<UDataTable> ItemTable;

	UPROPERTY(Transient)
	TObjectPtr<USnareBackendSubsystem> WatchedBackend;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Card;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> GoalText;

	/** 하루 의뢰가 다 끝났을 때만 줄기 단계를 묻는다. */
	bool RequestChain();

	TArray<FQuestLineView> Lines;
	int32 ChainStep = -1;
	bool bChainPending = false;
	bool bHasReply = false;
	bool bPending = false;
	/** 묻는 중에 보유 상태가 또 바뀌었다 — 답이 오면 한 번 더 묻는다. */
	bool bRefreshQueued = false;
	bool bSuppressed = false;
	double LastRequestSeconds = -1.0e9;
	FTimerHandle RefreshTimer;
};
