#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/ItemRow.h"

#include "WardrobeWidget.generated.h"

class UBackpackComponent;
class UButton;
class UDataTable;
class UImage;
class UWidget;
class UEquipmentComponent;
class UTextBlock;
class UUniformGridPanel;
class USnareBackendSubsystem;
class USnareClickProxy;
class UWardrobeShopWidget;
struct FInventorySlot;

/** 옷장 격자 원판 하나 — 표의 꾸미기 행 하나. */
USTRUCT()
struct FWardrobeTile
{
	GENERATED_BODY()

	UPROPERTY()
	FName RowName;

	UPROPERTY()
	FName ItemID;

	UPROPERTY()
	EItemGrade Grade = EItemGrade::Common;

	UPROPERTY()
	FText DisplayName;

	/** 서버 스냅샷 배낭에 있나. 없는 것은 눌러도 서버에 안 묻는다(상점에서 산다). */
	UPROPERTY()
	bool bOwned = false;

	/** 표의 아이콘(`IconRef`). 비면 아이콘이 없는 행 — 2층이 꾸미기 전 행에 아이콘이 있음을 단정한다. */
	UPROPERTY()
	TSoftObjectPtr<UTexture2D> Icon;
};

/**
 * 옷장 화면 (`docs/specs/skin-parts.md` 5장 5 · 6장 e, 레퍼런스 옷장 `steam_06.jpg` 과 배치는 다르게, M97-c-3) — 배낭 「장비」 탭의 「옷장」 이 띄운다.
 * 화면 아래 가로 나무 표찰 줄(칸 7 + 회전) · 왼쪽 세로 판(밧줄 테두리) `내 꾸미기`/`전체` 탭 + 8열 원판 격자(원판 바탕 = 등급 색)
 * + 아래 `상점 열기`·`나가기`. 원판 누름은 `UBackpackComponent::RequestSetOutfit` 로 서버에 묻기만 한다 —
 * 갈아입기는 서버가 받아들인 뒤 배낭 컴포넌트가 한다 (CLAUDE.md 8).
 */
UCLASS()
class SNAREHAVEN_API UWardrobeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UWardrobeWidget(const FObjectInitializer& ObjectInitializer);

	/** 화면 판 Z 로 뷰포트에 띄운다. */
	static UWardrobeWidget* OpenOnViewport(APlayerController* Owner, UWorld* World, USnareBackendSubsystem* Backend,
		UBackpackComponent* Backpack, UEquipmentComponent* Equipment);

	void BindTargets(USnareBackendSubsystem* InBackend, UBackpackComponent* InBackpack, UEquipmentComponent* InEquipment);

	/** 칸 버튼 순서 (명세 4장 「칸 수 7」) — 모자·얼굴 장식·상의·하의·신발·장갑·배낭. */
	static const TArray<EEquipSlot>& GetSlotOrder();

	/**
	 * 표에서 그 칸 꾸미기(`EItemType::Cosmetic`) 행을 등급·ItemID 순으로 모은다. 보유는 서버 스냅샷 배낭에서만 읽는다.
	 * @param bOwnedOnly true 면 `Owned` 에 1개 이상 있는 것만 (`내 꾸미기` 탭)
	 */
	static TArray<FWardrobeTile> CollectTiles(const UDataTable* ItemTable, EEquipSlot InSlot, bool bOwnedOnly,
		const TArray<FInventorySlot>& Owned);

	void SelectSlot(EEquipSlot InSlot);
	void SelectOwnedTab(bool bInOwnedOnly);

	/** 그 원판을 누른다 — 가진 것이면 서버에 입기(입은 것이면 벗기)를 묻고 true. 안 가진 것·답 대기 중이면 false. */
	bool PressTile(int32 Index);

	/** 몸 메시만 180° 돌려 앞·뒤를 본다(내 화면에서만). 닫으면 되돌린다. */
	UFUNCTION()
	void PressRotate();

	UFUNCTION()
	void PressOwnedTab();

	UFUNCTION()
	void PressAllTab();

	UFUNCTION()
	void PressOpenShop();

	UFUNCTION()
	void Close();

	/** 서버에 닿지 못한 갈아입기를 같은 칸·같은 옷으로 다시 보낸다 (의뢰 화면과 같은 꼴). */
	UFUNCTION()
	void HandleRetryClicked();

	/** 화면이 거둬질 때(나가기·배낭 닫힘 등 어느 길이든). 배낭 판이 숨김을 되돌리는 데 쓴다. */
	FSimpleMulticastDelegate OnClosed;

	EEquipSlot GetSelectedSlot() const { return SelectedSlot; }
	bool IsOwnedOnly() const { return bOwnedOnly; }
	bool IsPending() const { return bPending; }
	bool IsRotated() const { return bRotated; }
	const TArray<FWardrobeTile>& GetShownTiles() const { return ShownTiles; }
	const FText& GetMessage() const { return Message; }
	/** 알림 줄에 지금 보이는 글(빈 칸 안내 포함). */
	FString GetMessageShown() const;
	UWardrobeShopWidget* GetWardrobeShop() const { return WardrobeShop; }

	/** 그 원판 버튼의 바탕색(등급 색). 없는 칸이면 투명. 2층이 화면에 실제로 칠해진 색을 본다. */
	FLinearColor GetTileColor(int32 Index) const;

	/** 그 원판 버튼 (`GetShownTiles` 와 같은 순서). 2층이 사람 누름 길(`OnClicked`)을 그대로 태운다. */
	UButton* GetTileButton(int32 Index) const;

	/** 그 원판에 실제로 붙은 아이콘 그림. 아이콘이 안 보이는 칸이면 null. */
	UTexture2D* GetTileIconTexture(int32 Index) const;

	/** 가진 꾸미기가 어느 칸에도 없나 (서버 스냅샷 배낭 기준). 그러면 옷장은 `전체` 탭으로 열린다. */
	bool HasNoOwnedCosmetic() const;

	/** 격자 열 수 (판은 고정 폭 — 손가락 칸 3열, 1334폭에서 칸 120px 이상). */
	static constexpr int32 GridColumns = 3;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandleBackendSnapshotChanged();

	UFUNCTION()
	void HandleBackpackReply(bool bAccepted, const FString& Code, const FString& InMessage);

	void BuildTree();
	void RefreshDisplay();
	void RebuildGrid();
	FName GetWornItemID(EEquipSlot InSlot) const;
	void SetRotated(bool bInRotated);
	void NotifyGone();

	/** 옷장이 열려 있는 동안 HUD 오른쪽 세로 단추(표정·메뉴·지도·배낭)를 숨긴다 — 캐릭터 옆을 가린다. 닫히면 옛 보임 상태로 되살린다. */
	void HideSideButtons();
	void RestoreSideButtons();

	UPROPERTY()
	TObjectPtr<UDataTable> ItemTable;

	UPROPERTY(Transient)
	TObjectPtr<USnareBackendSubsystem> Backend;

	UPROPERTY(Transient)
	TObjectPtr<UBackpackComponent> Backpack;

	UPROPERTY(Transient)
	TObjectPtr<UEquipmentComponent> Equipment;

	UPROPERTY(Transient)
	TObjectPtr<UWardrobeShopWidget> WardrobeShop;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> SlotButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USnareClickProxy>> SlotProxies;

	UPROPERTY(Transient)
	TObjectPtr<UButton> OwnedTabButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> AllTabButton;

	UPROPERTY(Transient)
	TObjectPtr<UUniformGridPanel> Grid;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> Tiles;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> TileIcons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USnareClickProxy>> TileProxies;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MessageText;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> EmptyPanel;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> RetryBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> EmptyHint;

	UPROPERTY(Transient)
	TArray<FWardrobeTile> ShownTiles;

	EEquipSlot SelectedSlot = EEquipSlot::Head;
	bool bOwnedOnly = true;
	bool bPending = false;
	bool bRotated = false;
	bool bCameraMoved = false;
	bool bGoneNotified = false;
	TMap<TWeakObjectPtr<UUserWidget>, ESlateVisibility> HiddenSideWidgets;
	FRotator MeshRotationBefore = FRotator::ZeroRotator;
	FText Message;
	/** 보낸 갈아입기 / 서버에 닿지 못해 다시 보낼 것 — 비었으면 「다시 시도」 를 숨긴다. */
	TFunction<void()> SentRequest;
	TFunction<void()> RetryRequest;
};
