#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inventory/InventoryComponent.h"

#include "CrateScreenWidget.generated.h"

class UBackpackComponent;
class UBorder;
class UButton;
class UDataTable;
class USnareBackendSubsystem;
class UTextBlock;
class UVerticalBox;
class FJsonObject;
struct FCrateRow;

// 상점 개편 B: 꾸미기 상자 여는 화면 (DESIGN 10.8 · 10.4 확률 고지).
//
// **이 화면은 아무것도 굴리지도 주지도 않는다.** "열기" 를 누르면 배낭 컴포넌트가 서버(`OpenCrate`)에 묻고,
// 큰 결과(이름·등급)는 **서버 답이 온 뒤에만** 그 답(`itemId`)으로 그린다. 상자·열쇠 칸은 이어서 받는 스냅샷이 바꾼다 (CLAUDE.md 8).
// 고지 표는 `DT_CrateTable` 값 그대로 — 등급별 합 + 품목별 %.
// 배낭에서 상자를 누르면(열쇠가 있을 때) `OpenFor` 로 뜬다.
UCLASS()
class SNAREHAVEN_API UCrateScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UCrateScreenWidget(const FObjectInitializer& ObjectInitializer);

	/** 상자 화면을 띄운다. 열쇠 확인은 부르는 쪽(배낭) 몫. */
	static UCrateScreenWidget* OpenFor(APlayerController* Controller, USnareBackendSubsystem* InBackend,
		UBackpackComponent* InBackpack, FName InCrateID);

	void BindToCrate(USnareBackendSubsystem* InBackend, UBackpackComponent* InBackpack, FName InCrateID);

	/** 이 아이템이 `DT_CrateTable` 에 있는 상자인가. */
	static bool IsCrateItem(const UDataTable* InCrateTable, FName ItemID);

	/** 보유 칸에서 이 아이템 개수. */
	static int32 CountOwned(const TArray<FInventorySlot>& Owned, FName ItemID);

	/** 기본 객체가 읽어 둔 `DT_CrateTable` — 배낭이 상자인지 볼 때 쓴다. */
	const UDataTable* GetCrateTable() const { return CrateTable; }

	/**
	 * "열쇠로 열기" — 서버에 묻는다.
	 * @return 요청을 띄웠으면 true. 기다리는 중·연출 중·열쇠 없음·배낭 없음이면 false
	 */
	UFUNCTION(BlueprintCallable, Category = "Crate")
	bool PressOpen();

	/** "젬으로 열쇠 사기" — `BuyItem{currency:GM}`. @return 서버에 물었으면 true */
	UFUNCTION(BlueprintCallable, Category = "Crate")
	bool PressBuyKey();

	UFUNCTION(BlueprintCallable, Category = "Crate")
	void SetDisclosureShown(bool bInShown);

	bool IsDisclosureShown() const { return bDisclosureShown; }
	bool IsRequestPending() const { return bPending; }
	bool IsRevealing() const { return bRevealing; }

	/** 지금 보유한 열쇠 수 (서버 스냅샷). */
	int32 GetKeyCount() const;

	/** 결과 큰 글자 — 답 전에는 빈 글자다. */
	const FText& GetResultText() const { return ResultText; }

	/** 아래 한 줄 알림(거절 문구·열쇠 없음). */
	const FText& GetStatusText() const { return StatusText; }

	/** 고지 줄 — 등급 줄(`커먼 75%` …) 뒤에 품목 줄(`작업 셔츠 (커먼) 25%` …). 값은 표 그대로. */
	TArray<FText> BuildDisclosureTexts() const;

	/** 이 상자의 표 줄들. */
	TArray<const FCrateRow*> FindCrateRows() const;

	/** 서버 답을 결과로 바꾼다. 요청 콜백이 부르고, 2층 검사도 직접 부른다. */
	void ApplyReply(bool bAccepted, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Response);

	/** 상자 열기를 서버가 받아 준 뒤 내 기기에서 한 번 — 자물쇠 딸깍 + 뚜껑 + 반짝 `CrateOpen` (M45-04). 뽑기도 빌린다. */
	static FName GetConfirmCue();

	/** 그 소리 크기 — 0.8 = 젬 쓴 결과라 코인 거래 확정과 같게 (DESIGN 2.2 「거점·상자 소리」 크기 줄). */
	static float GetConfirmVolume();

	/** 서버 성공 답 뒤 소리를 낸 횟수·마지막 이름 (2층 검사가 본다). */
	int32 GetConfirmSoundCount() const { return ConfirmSoundCount; }
	FName GetLastConfirmSoundCue() const { return LastConfirmSoundCue; }

	/** 연출을 끝내고 결과를 보인다 — 타이머가 부른다(검사는 직접). */
	void FinishReveal();

	UFUNCTION(BlueprintCallable, Category = "Crate")
	void RefreshDisplay();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UFUNCTION()
	void HandleOpenClicked();

	UFUNCTION()
	void HandleBuyKeyClicked();

	UFUNCTION()
	void HandleDisclosureClicked();

	UFUNCTION()
	void HandleCloseClicked();

	UPROPERTY(BlueprintReadOnly, Category = "Crate", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, Category = "Crate", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultBody;

	UPROPERTY(BlueprintReadOnly, Category = "Crate", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusBody;

	UPROPERTY(BlueprintReadOnly, Category = "Crate", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> DisclosureList;

	UPROPERTY(BlueprintReadOnly, Category = "Crate", meta = (BindWidgetOptional))
	TObjectPtr<UButton> OpenButton;

	UPROPERTY(BlueprintReadOnly, Category = "Crate", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> OpenButtonText;

	UPROPERTY(BlueprintReadOnly, Category = "Crate", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BuyKeyButton;

	UPROPERTY(BlueprintReadOnly, Category = "Crate", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BuyKeyButtonText;

	UPROPERTY(BlueprintReadOnly, Category = "Crate", meta = (BindWidgetOptional))
	TObjectPtr<UButton> DisclosureButton;

	UPROPERTY(BlueprintReadOnly, Category = "Crate", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DisclosureButtonText;

	UPROPERTY(BlueprintReadOnly, Category = "Crate", meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crate")
	TObjectPtr<UDataTable> CrateTable;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crate")
	TObjectPtr<UDataTable> ItemTable;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crate")
	TObjectPtr<UDataTable> FailureTable;

	UPROPERTY(Transient)
	TObjectPtr<UBackpackComponent> Backpack;

	UPROPERTY(Transient)
	TObjectPtr<USnareBackendSubsystem> Backend;

private:
	FText FindItemDisplayName(FName ItemID) const;
	int32 FindKeyGemPrice() const;
	bool NeedsPlaceholderTree() const;
	void BuildPlaceholderTree();
	void RebuildDisclosureList();

	FName CrateID;
	bool bDisclosureShown = false;
	bool bPending = false;
	bool bRevealing = false;
	FText ResultText;
	FText StatusText;
	FTimerHandle RevealTimer;
	int32 ConfirmSoundCount = 0;
	FName LastConfirmSoundCue;
};
