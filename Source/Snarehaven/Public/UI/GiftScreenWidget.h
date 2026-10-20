#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "GiftScreenWidget.generated.h"

class FJsonObject;
class UBorder;
class UPanelWidget;
class UButton;
class UDataTable;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class USnareBackendSubsystem;
class USnareClickProxy;

/** 서버 `GetGifts` 한 줄 — 받는 사람이 수락을 기다리는 선물. */
struct FGiftView
{
	FString GiftId;
	FString FromId;
	FName ItemID;
	int32 Count = 0;
};

/** 같은 방 안의 다른 사람 한 명 — 보낼 사람 후보. */
struct FGiftPeer
{
	FString PlayFabId;
	FText Name;
	float DistanceCm = 0.f;
};

// DESIGN 3.3 「건네기」 — 근처 사람에게 아이템 선물 + 받는 쪽 수락 카드.
//
// **이 화면은 배낭을 바꾸지 않는다.** 보내기는 서버 `GiftItem`, 수락·거절은 `AcceptGift`·`DeclineGift` 를 묻고,
// 성공 답이 오면 보유 상태와 목록을 **다시 받는다** (CLAUDE.md 8). 줄에 보이는 제외 아이템 거름은 힌트일 뿐 —
// 판정은 서버(`excluded_item`)가 하고, 그 사유 문장을 그대로 보여 준다.
UCLASS()
class SNAREHAVEN_API UGiftScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 보낼 사람 후보 거리 한도(cm) — DESIGN 3.3 「건네기」 표 `거리`. */
	static constexpr float MaxGiftDistanceCm = 1000.f;
	/** 한 번에 보이는 보낼 사람 줄 수 — 4~8인 방에서 내 몸 빼고 가까운 순 4명. */
	static constexpr int32 MaxPeerRows = 4;
	/** 한 번에 보내는 개수 상한 — DESIGN 3.3 「건네기」 표 `한 번에`. */
	static constexpr int32 MaxSendCount = 99;

	UGiftScreenWidget(const FObjectInitializer& ObjectInitializer);

	/** 내 몸에 화면을 붙인다(내 컨트롤러가 아니면 null). 열기 버튼은 오른쪽 아래 기둥 맨 위. */
	static UGiftScreenWidget* AttachTo(APawn* Pawn);

	void BindToBackend(USnareBackendSubsystem* Backend);

	/** 펴면 내 대기 선물을 서버에 묻고 근처 사람을 다시 센다. */
	void SetOpen(bool bInOpen);
	bool IsOpen() const { return bOpen; }

	/** 근처 사람 목록을 대는 곳. 안 정하면 월드의 다른 몸에서 모은다. */
	TFunction<TArray<FGiftPeer>()> PeerSource;

	// --- 서버 호출 (화면은 답을 받아 보여 주기만) ---
	bool RequestGifts();
	bool RequestSend();
	bool RequestAccept(const FString& GiftId);
	bool RequestDecline(const FString& GiftId);

	// --- 고르기 ---
	void SelectPeer(const FString& PlayFabId);
	void SelectItem(FName ItemID);
	void ChangeCount(int32 Delta);

	// --- 읽기 (시험·표시) ---
	const TArray<FGiftView>& GetGifts() const { return Gifts; }
	const TArray<FGiftPeer>& GetPeers() const { return Peers; }
	const FString& GetSelectedPeer() const { return SelectedPeer; }
	FName GetSelectedItem() const { return SelectedItem; }
	int32 GetSendCount() const { return SendCount; }
	bool IsRequestPending() const { return bPending; }
	const FText& GetMessage() const { return Message; }

	/** 보낼 수 있는 상태인가 — 사람·아이템 골랐고 개수가 가진 만큼 안쪽. */
	bool CanSend() const;

	/** 스냅샷 배낭에서 그 아이템 개수(칸이 나뉘어도 합). 백엔드가 없으면 0. */
	int32 CountOwned(FName ItemID) const;

	/** 보낼 거리 안의 사람만 가까운 순으로 `MaxPeerRows` 명. 순수 함수. */
	static TArray<FGiftPeer> PickNearby(const TArray<FGiftPeer>& All, float MaxCm = MaxGiftDistanceCm);

	/** `GetGifts` 응답 `gifts` 를 읽기만 한다. @return 배열이 있었으면 true */
	static bool ParseGifts(const TSharedPtr<FJsonObject>& Response, TArray<FGiftView>& OutGifts);

	/** 서버 `GiftItem` 인자 `{targetPlayerId, itemId, count}`. */
	static TSharedRef<FJsonObject> MakeSendArgs(const FString& TargetId, FName ItemID, int32 Count);

	/** 화면에 보일 아이템 힌트 — 서버가 어차피 거르지만 눈에 뻔한 제외(가챠·펫·`item.` 아닌 것)는 줄에서 뺀다. */
	static bool IsOfferableItem(FName ItemID);

	/** 같은 방의 다른 사람들(내 몸 제외, PlayFabId 를 올린 몸만). */
	static TArray<FGiftPeer> GatherPeers(const APawn* Me);

	FText FindItemDisplayName(FName ItemID) const;
	void RefreshDisplay();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void HandleBackendSnapshotChanged();
	UFUNCTION()
	void HandleOpenClicked();
	UFUNCTION()
	void HandleCloseClicked();
	UFUNCTION()
	void HandleSendClicked();
	UFUNCTION()
	void HandleCountMinus();
	UFUNCTION()
	void HandleCountPlus();
	UFUNCTION()
	void HandleRetryClicked();

	UPROPERTY(Transient)
	TObjectPtr<UButton> OpenButton;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> Panel;
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> TitlePillBox;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MessageText;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> BodyList;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CountText;
	UPROPERTY(Transient)
	TObjectPtr<UButton> SendButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> CloseButton;
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> RetryBox;

	UPROPERTY(EditDefaultsOnly, Category = "Gift")
	TObjectPtr<UDataTable> ItemTable;

	UPROPERTY()
	TObjectPtr<USnareBackendSubsystem> WatchedBackend;
	TWeakObjectPtr<USnareBackendSubsystem> BackendToRebind;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USnareClickProxy>> RowProxies;

private:
	void BuildTree();
	void AddSectionTitle(const FText& Title, int32& Index);
	UButton* AddRowButton(UPanelWidget& Parent, const FText& Label, bool bEnabled, bool bSelected, int32& Index);
	void BindRow(UButton& Button, FName Key, TFunction<void(FName)> OnClicked);
	/** 서버 함수 하나를 묻고, 거절이면 사유 문장을, 성공이면 `SuccessText` 를 보이고 보유 상태·목록을 다시 받는다. */
	bool CallGiftFunction(const TCHAR* Function, const TSharedRef<FJsonObject>& Args, const FText& SuccessText, bool bClearSelection);

	TArray<FGiftView> Gifts;
	TArray<FGiftPeer> Peers;
	FString SelectedPeer;
	FName SelectedItem;
	int32 SendCount = 1;
	bool bOpen = false;
	bool bPending = false;
	FText Message;
	/** 서버에 닿지 못한 마지막 요청 — 비었으면 「다시 시도」 를 숨긴다 (의뢰 화면과 같은 꼴). */
	TFunction<void()> RetryRequest;
	mutable TSet<FName> WarnedUnknownIDs;
};
