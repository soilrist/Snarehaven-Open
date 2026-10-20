#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Backend/SnareStory.h"

#include "SnareStoryGoalWidget.generated.h"

class UBorder;
class UImage;
class UDataTable;
class UTextBlock;
class USnareBackendSubsystem;

// 사냥 HUD 왼쪽 위에 늘 떠 있는 **메인 스토리 「지금 할 일」 한 줄** (INBOX 메인 스토리 ②, `docs/STORY.md`).
// 줄은 서버 `GetStory` 답을 `SnareStory::FormatLine` 으로 옮긴 것이다. 정보만 — 코인·배낭·단계를 바꾸지 않는다 (CLAUDE.md 8).
UCLASS()
class SNAREHAVEN_API USnareStoryGoalWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	USnareStoryGoalWidget(const FObjectInitializer& ObjectInitializer);

	/** 백엔드를 묶고 `GetStory` 를 한 번 묻는다. 그 뒤엔 보유 상태가 바뀔 때마다 다시 묻는다(간격 `MinRefreshSeconds`). */
	void BindToBackend(USnareBackendSubsystem* Backend);

	/** 서버 `GetStory` 를 묻는다. @return 요청을 띄웠으면 true */
	bool RequestStory();

	/** `GetStory` 답을 읽어 줄을 고친다. @return 읽었으면 true (오류 답이면 앞 줄 유지) */
	bool ApplyStoryReply(const TSharedPtr<FJsonObject>& Response);

	/** 보여 줄 한 줄 — 답 전이면 빈 글(줄이 접힌다). */
	FText GetGoalText() const;
	bool HasReply() const { return bHasReply; }
	bool IsRequestPending() const { return bPending; }

	/** 줄이 보이는 상태가 바뀔 때마다 불린다(true = 한 줄 떠 있음). 옛 「다음 목표」 줄을 접는 데 쓴다. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnLineShown, bool);
	FOnLineShown OnLineShown;

	/** 답을 새로 읽을 때마다 불린다 — 의뢰인 머리 위 표식이 따라 바뀐다. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnViewChanged, const SnareStory::FStoryView&);
	FOnViewChanged OnViewChanged;

	const SnareStory::FStoryView& GetView() const { return View; }

	/** 줄을 눌렀을 때 불린다 — 캐릭터가 의뢰 판(줄기 탭)을 연다. */
	DECLARE_MULTICAST_DELEGATE(FOnTapped);
	FOnTapped OnTapped;

	/** 서버 `AdvanceStory` 를 묻는다(지금 단계 번호를 같이 보내 어긋나면 서버가 거절). 답은 `GetStory` 와 같은 모양이라 줄을 고친다. @return 요청을 띄웠으면 true */
	bool RequestAdvance(bool bReport = false, bool bGive = false);

	/** 보고·가기 단계의 대사를 다 읽었을 때 — `report: true` 로 넘겨 달라고 한다. 건네기 단계면 `report` 없이 건네 달라고 한다(재료가 모자라면 서버가 거절). 단계가 어긋나면 안 부른다. @return 요청을 띄웠으면 true */
	bool RequestReport(int32 ReadStep);
	bool IsAdvancePending() const { return bAdvancePending; }

	/** 줄이 눈에 보여 누를 수 있나 (답이 와서 줄이 펴졌을 때만). */
	bool IsTappable() const;

	/** 누름 처리 — 줄이 접혀 있으면 아무 일도 없다. @return 알렸으면 true */
	bool NotifyTapped();

	/** 보유 상태 변화로 다시 물을 때 최소 간격(초) — 🟡 서버 호출 아끼기. 2층 검사는 0 으로 둔다. */
	float MinRefreshSeconds = 10.f;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;

private:
	UFUNCTION()
	void HandleBackendSnapshotChanged();

	FText FindItemDisplayName(FName ItemID) const;
	void Refresh();

	UPROPERTY(EditDefaultsOnly, Category = "Story")
	TObjectPtr<UDataTable> ItemTable;

	UPROPERTY(Transient)
	TObjectPtr<USnareBackendSubsystem> WatchedBackend;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Card;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> GoalText;

	/** 줄 아래 등대 불빛 고리 5칸 — 끝낸 장 수만큼 노랗게 켜진다(글꼴에 `●` 가 없어 색 칸으로 그린다). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> RingPips;

public:
	/** 켜진 고리 칸 수 (시험용). */
	int32 CountLitRingPips() const;
private:

	SnareStory::FStoryView View;
	bool bHasReply = false;
	bool bPending = false;
	bool bAdvancePending = false;
	int32 GiveReadStep = 0;	// 대사를 읽은 건네기 단계 번호 — 재료를 모은 뒤 다시 건넨다
	bool bRefreshQueued = false;
	double LastRequestSeconds = -1.0e9;
	FTimerHandle RefreshTimer;
};
