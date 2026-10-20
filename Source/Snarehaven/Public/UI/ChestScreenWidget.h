#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "ChestScreenWidget.generated.h"

class UBackpackComponent;
class UBorder;
class UButton;
class UDataTable;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class FJsonObject;

// ROADMAP M13-12: 상자 앞에서 뜨는 화면 — 열기·결과·확률 고지 (DESIGN 8.10 · 10.6 · 13.8).
//
// **이 화면은 아무것도 굴리지도 주지도 않는다.** "상자 열기" 를 누르면 배낭 컴포넌트가 서버(`OpenChest`)에 묻고,
// 결과 줄은 **서버 답이 온 뒤에만** 그 답(`items`·`blueprints`)으로 그린다. 가방 칸은 이어서 받는 스냅샷이 바꾼다 (CLAUDE.md 8).
// 재충전 중이면 서버가 준 `waitMs` 로 남은 시간을 보인다.
// 확률 고지 표는 10.6 그대로 — 15줄 + 설계도 한 줄 (`SnareChestLoot::GetDisclosureLines`).
//
// 닫힌 상태에서는 상자 옆 카드 둘("상자 열기"·"확률 보기")만 보인다. 상자 액터(`ALootChestActor`)가 가까이 왔을 때 올린다.
UCLASS()
class SNAREHAVEN_API UChestScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UChestScreenWidget(const FObjectInitializer& ObjectInitializer);

	/** 이 화면이 열 상자. 위험구역 상자면 `ChestIndex` 1..2, 보물 상자면 `TreasureID`. */
	void BindToChest(UBackpackComponent* InBackpack, int32 InChestIndex, const FString& InTreasureID);

	/** 잠긴 금고(DESIGN 8.1c)를 여는 화면으로 묶는다 — 폐허 번호 1..3. 열기는 `OpenVault`, 고지 표는 8.1c 표. */
	void BindToVault(UBackpackComponent* InBackpack, int32 InRuinIndex);

	bool IsVault() const { return VaultRuinIndex > 0; }

	/**
	 * "상자 열기" — 창을 펴고 서버에 묻는다.
	 * @return 요청을 띄웠으면 true. 답을 기다리는 중이거나 배낭(백엔드)이 없으면 false
	 */
	UFUNCTION(BlueprintCallable, Category = "Chest")
	bool PressOpen();

	UFUNCTION(BlueprintCallable, Category = "Chest")
	void SetOpen(bool bInOpen);

	bool IsOpen() const { return bOpen; }

	/** 결과 대신 확률 고지 표를 보인다. */
	UFUNCTION(BlueprintCallable, Category = "Chest")
	void SetDisclosureShown(bool bInShown);

	bool IsDisclosureShown() const { return bDisclosureShown; }

	bool IsRequestPending() const { return bPending; }

	/** 결과 한 덩이 — 답 전에는 빈 글자다. */
	const FText& GetResultText() const { return ResultText; }

	/** 고지 표 줄 글자 — `보물 지도 10%` … 마지막이 설계도 줄. */
	TArray<FText> BuildDisclosureTexts() const;

	/** 금고 24시간 대기 글자 — `N시간 M분`. 상자의 m:ss 는 600초짜리라 시간 단위가 없다. */
	static FText FormatVaultWait(int32 Seconds);

	/** 서버 답을 결과 글자로 바꾼다. 상자 요청 콜백이 부르고, 2층 검사도 직접 부른다. */
	void ApplyReply(bool bAccepted, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Response);

	UFUNCTION(BlueprintCallable, Category = "Chest")
	void RefreshDisplay();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UFUNCTION()
	void HandleOpenClicked();

	UFUNCTION()
	void HandleDisclosureClicked();

	UFUNCTION()
	void HandleCloseClicked();

	// WBP 에 같은 이름이 있으면 그걸 쓰고, 없으면 `RebuildWidget` 이 자리표시로 세운다.
	UPROPERTY(BlueprintReadOnly, Category = "Chest", meta = (BindWidgetOptional))
	TObjectPtr<UButton> OpenButton;

	UPROPERTY(BlueprintReadOnly, Category = "Chest", meta = (BindWidgetOptional))
	TObjectPtr<UButton> DisclosureCardButton;

	UPROPERTY(BlueprintReadOnly, Category = "Chest", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> ChestPanel;

	UPROPERTY(BlueprintReadOnly, Category = "Chest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	/** 판 왼쪽 위에 걸친 제목 알약 상자(M25-05) — 판과 같이 접힌다. */
	UPROPERTY(BlueprintReadOnly, Category = "Chest", meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> ChestTitlePillBox;

	UPROPERTY(BlueprintReadOnly, Category = "Chest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultBody;

	UPROPERTY(BlueprintReadOnly, Category = "Chest", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> DisclosureList;

	UPROPERTY(BlueprintReadOnly, Category = "Chest", meta = (BindWidgetOptional))
	TObjectPtr<UButton> DisclosureButton;

	UPROPERTY(BlueprintReadOnly, Category = "Chest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DisclosureButtonText;

	UPROPERTY(BlueprintReadOnly, Category = "Chest", meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	/** 이름만 읽는다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chest")
	TObjectPtr<UDataTable> ItemTable;

	/** 거절 code → 화면 문구 (M26-02, 목장과 같은 `DT_RanchFailureTable`). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chest")
	TObjectPtr<UDataTable> FailureTable;

	UPROPERTY(Transient)
	TObjectPtr<UBackpackComponent> Backpack;

private:
	FText FindItemDisplayName(FName ItemID) const;
	bool NeedsPlaceholderTree() const;
	void BuildPlaceholderTree();

	int32 ChestIndex = 0;
	int32 VaultRuinIndex = 0;
	FString TreasureID;
	bool bOpen = false;
	bool bDisclosureShown = false;
	bool bPending = false;
	FText ResultText;
};
