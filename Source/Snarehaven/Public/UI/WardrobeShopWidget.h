#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Dom/JsonObject.h"

#include "WardrobeShopWidget.generated.h"

class UBorder;
class UButton;
class UDataTable;
class UImage;
class UTexture2D;
class USizeBox;
class UTextBlock;
class USnareBackendSubsystem;
class USnareClickProxy;

/** 이번 주 진열 한 칸 — 서버(`GetWardrobeShop`)가 준 그대로. 값은 보여 주기만, 사는 값은 서버가 다시 정한다. */
USTRUCT()
struct FWardrobeOffer
{
	GENERATED_BODY()

	UPROPERTY()
	FName ItemID;

	UPROPERTY()
	int32 Gems = 0;
};

/**
 * 옷장 꾸미기 상점 (DESIGN 10.7, 레퍼런스 `UMG_InGameShop`) — 배낭 「장비」 탭의 「상점 열기」 가 띄운다.
 * 진열 8칸 → 칸 누름 → 구매 확인 창 → 서버 응답 뒤 「구매 완료」 창.
 * 젬·배낭은 손대지 않는다 — 성공 응답 뒤 `FetchPlayerState` 로 다시 받아 그린다 (CLAUDE.md 8).
 */
UCLASS()
class SNAREHAVEN_API UWardrobeShopWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UWardrobeShopWidget(const FObjectInitializer& ObjectInitializer);

	/** 화면 판 Z 로 뷰포트에 띄우고 진열을 서버에 묻는다. */
	static UWardrobeShopWidget* OpenOnViewport(APlayerController* Owner, UWorld* World, USnareBackendSubsystem* Backend);

	void BindToBackend(USnareBackendSubsystem* Backend);

	/** 열면 진열을 서버에 다시 묻는다(월요일이 지났을 수 있다). */
	void SetOpen(bool bInOpen);

	/** `GetWardrobeShop` 응답을 진열로 옮긴다. 응답이 이상하면 진열을 비우고 false. */
	bool ApplyCatalogReply(const TSharedPtr<FJsonObject>& Response);

	/** 그 칸을 골라 구매 확인 창을 띄운다. 빈 칸·보내는 중이면 false. */
	bool SelectOffer(int32 Index);

	/** 확인 창의 「구매」 — 서버 `BuyItem {currency:"GM"}` 로 보낸다. 보냈으면 true. */
	bool ConfirmBuy();

	UFUNCTION()
	void PressConfirm();

	UFUNCTION()
	void CancelBuy();

	UFUNCTION()
	void CloseCompleteWindow();

	UFUNCTION()
	void Close();

	/** 진열을 못 받았을 때 보이는 「다시 시도」 — 진열을 다시 묻는다 (의뢰 화면과 같은 꼴). */
	UFUNCTION()
	void HandleRetryClicked();

	const TArray<FWardrobeOffer>& GetOffers() const { return Offers; }
	bool IsPending() const { return bPending; }
	bool IsBuyWindowOpen() const { return SelectedIndex != INDEX_NONE; }
	bool IsCompleteWindowOpen() const { return !CompletedItemName.IsEmpty(); }

	/** 그 칸 아이콘 그림 — 빈 칸이거나 아이콘이 없으면 nullptr (시험이 「진열 칸 모두 아이콘 있음」 을 본다). */
	UTexture2D* GetTileIconTexture(int32 Index) const;

	/** 젬 잔액 줄 — 스냅샷에서만 읽는다. */
	FText GetGemText() const;

	/** 진열 칸 수 (DESIGN 10.7 「옷장 상점 칸 수」). 서버 `WARDROBE_SHOP_SLOTS` 와 같다. */
	static constexpr int32 TileCount = 8;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandleBackendSnapshotChanged();

	void RequestCatalog();
	void BuildTree();
	void RefreshDisplay();
	FText FindItemName(FName ItemID) const;

	UPROPERTY(Transient)
	TObjectPtr<USnareBackendSubsystem> WatchedBackend;

	TWeakObjectPtr<USnareBackendSubsystem> BackendToRebind;

	UPROPERTY()
	TObjectPtr<UDataTable> ItemTable;

	UPROPERTY(Transient)
	TArray<FWardrobeOffer> Offers;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> Tiles;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> TileIcons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> TileNames;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> TilePrices;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USnareClickProxy>> TileProxies;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> GemText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MessageText;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> RetryBox;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> BuyWindow;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BuyWindowText;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> CompleteWindow;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CompleteWindowText;

	int32 SelectedIndex = INDEX_NONE;
	bool bPending = false;
	bool bCatalogLoading = false;
	bool bCatalogFailed = false;
	FText Message;
	FText CompletedItemName;
};
