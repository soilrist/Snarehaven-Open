#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "AttendanceScreenWidget.generated.h"

class FJsonObject;
class UBorder;
class UButton;
class UDataTable;
class UHorizontalBox;
class USizeBox;
class UTextBlock;
class UWidget;
class USnareBackendSubsystem;
struct FItemRow;

/** 달력 한 칸의 상태 — 받은 칸 · 오늘 받을 칸 · 아직 남은 칸. */
enum class EAttendanceSlotState : uint8
{
	Claimed,
	Today,
	Later
};

/** 서버 `GetAttendance` 의 보상 한 칸 (`{coins}` 또는 `{itemId, count}`). 화면은 받은 그대로 보여 주기만 한다. */
struct FAttendanceRewardView
{
	int32 Coins = 0;
	FName ItemID;
	int32 Count = 0;
};

// DESIGN 11 「7일 출석」 · ROADMAP M21-05b — 7칸 달력. 닫힌 채엔 「출석」 버튼, 열면 7칸 + 「받기」.
//
// **이 화면은 코인도 배낭도 바꾸지 않는다.** 열 때 서버 `GetAttendance` 로 칸을 받고, 「받기」 는
// `ClaimAttendance` 를 묻고, 성공 답이 오면 보유 상태와 달력을 **다시 받는다** (CLAUDE.md 8).
// 답을 받기 전엔 칸을 모른다고 그리고 「받기」 를 끈다.
UCLASS()
class SNAREHAVEN_API UAttendanceScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UAttendanceScreenWidget(const FObjectInitializer& ObjectInitializer);

	void BindToBackend(USnareBackendSubsystem* Backend);

	/** 펴면 달력을 서버에 다시 묻는다. 접으면 「출석」 버튼만. */
	void SetOpen(bool bInOpen);
	bool IsOpen() const { return bOpen; }

	/** 서버 `GetAttendance`. @return 요청을 띄웠으면 true */
	bool RequestAttendance();

	/** 「받기」 — 서버 `ClaimAttendance`. 오늘 이미 받았거나 답을 기다리는 중이면 안 띄운다. @return 띄웠으면 true */
	bool RequestClaim();

	/** `GetAttendance` 응답을 칸으로 옮긴다. @return `nextSlot`·`rewards` 가 있어 옮겼으면 true */
	bool ApplyAttendanceReply(const TSharedPtr<FJsonObject>& Response);

	/** 보상 한 칸 JSON(`{coins}`·`{itemId, count}`)을 읽는다. 둘 다 없으면 false. */
	static bool ParseReward(const TSharedPtr<FJsonObject>& Object, FAttendanceRewardView& OutReward);

	/** 칸(1~7)의 상태. 답을 못 받았으면 전부 `Later`. */
	EAttendanceSlotState GetSlotState(int32 SlotNumber) const;

	/** 칸(1~7)에 보일 보상 — 다음에 받을 칸은 서버 `next`(두 번째 모자 → 코인)를 쓴다. */
	FAttendanceRewardView GetSlotReward(int32 SlotNumber) const;

	/** `코인 50` · `당근 씨앗 x3`. 아이템 표에 없는 ID 는 원 ID 대신 `보상` (M23-05). */
	FText GetRewardText(const FAttendanceRewardView& Reward) const;

	bool IsKnown() const { return bKnown; }
	bool IsClaimedToday() const { return bClaimedToday; }
	bool IsRequestPending() const { return bPending; }
	bool CanClaim() const { return bKnown && !bClaimedToday && !bPending; }
	const FText& GetMessage() const { return Message; }

	void RefreshDisplay();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void HandleOpenClicked();
	UFUNCTION()
	void HandleCloseClicked();
	UFUNCTION()
	void HandleClaimClicked();
	UFUNCTION()
	void HandleRetryClicked();

	UPROPERTY(Transient)
	TObjectPtr<UButton> OpenButton;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> Panel;
	/** 판 왼쪽 위에 걸친 제목 알약(M24-03) — 판과 같이 접고 편다. */
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> TitlePillBox;
	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> SlotRow;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MessageText;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ClaimButton;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ClaimButtonText;
	UPROPERTY(Transient)
	TObjectPtr<UButton> CloseButton;
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> RetryBox;

	UPROPERTY(EditDefaultsOnly, Category = "Attendance")
	TObjectPtr<UDataTable> ItemTable;

	UPROPERTY()
	TObjectPtr<USnareBackendSubsystem> WatchedBackend;

	/** 화면을 내릴 때 풀었던 백엔드 — 다시 올리면(`NativeConstruct`) 다시 묶는다 (의뢰 화면과 같은 까닭). */
	TWeakObjectPtr<USnareBackendSubsystem> BackendToRebind;

private:
	const FItemRow* FindItemRow(FName ItemID) const;
	FText FindItemDisplayName(FName ItemID) const;
	/** 칸 보상 원판 (M23-05) — 코인 = 재화 그림, 아이템 = 등급 색 원판 + 아이템 표 그림. */
	UWidget* MakeRewardDisc(const FAttendanceRewardView& Reward, int32 SlotNumber);
	void BuildTree();

	TArray<FAttendanceRewardView> Rewards;
	FAttendanceRewardView NextReward;
	int32 NextSlot = 0;
	bool bKnown = false;
	bool bClaimedToday = false;
	bool bOpen = false;
	bool bPending = false;
	/** 달력을 못 받았다 — 「다시 시도」 를 보인다 (의뢰 화면과 같은 꼴). */
	bool bListFailed = false;
	FText Message;
	/** 표에 없다고 이미 경고한 ID — 칸을 다시 그릴 때마다 같은 경고를 쌓지 않는다. */
	mutable TSet<FName> WarnedUnknownIDs;
};
