#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inventory/InventoryComponent.h"
#include "BackpackScreenWidget.generated.h"

class APawn;
class UBackpackComponent;
class UBorder;
class UButton;
class UCanvasPanel;
class UCodexScreenWidget;
class UGachaScreenWidget;
class USnareGuideBookWidget;
class UWardrobeShopWidget;
class UWardrobeWidget;
class UDataTable;
class UHuntHudWidget;
class URanchComponent;
class URanchHudWidget;
class URanchSyncComponent;
class UScrollBox;
class USizeBox;
class UEquipmentComponent;
class USnareBackendSubsystem;
class USnareBaseBuildComponent;
class UWaterTravelComponent;
class UTextBlock;
class UUniformGridPanel;
class UVerticalBox;
struct FGachaPoolRow;
struct FItemRow;

// 배낭 화면 (DESIGN 8.12 · 시트 `docs/reference/inventory_backpack.md`) — 오른쪽 가장자리 크림 패널, 4열 격자,
// 위로 걸친 제목 탭, 왼쪽 세로 탭(배낭·장비·도감·제작), 아래 재화 바. 칸을 누르면 카드 + 사용/장착/놓기/버리기.
//
// **그리기만 한다.** 칸의 원본은 백엔드 스냅샷(`GetPlayerSnapshot`)이고, 다시 그리는 때는 `OnPlayerSnapshotChanged`
// 하나뿐이다 — 버튼을 눌러도 답이 와서 스냅샷이 바뀌기 전까지 칸은 그대로다 (CLAUDE.md 8).
// 먹기·버리기는 `UBackpackComponent` 가 서버에 묻는다.
UCLASS()
class SNAREHAVEN_API UBackpackScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UBackpackScreenWidget(const FObjectInitializer& ObjectInitializer);

	/** 로컬 플레이어 폰에 붙여 화면에 올린다. 탭은 늘 보이고 패널은 닫힌 채 — `-SnareBackpackScreen` 이면 연 채. */
	static UBackpackScreenWidget* AttachTo(APawn* Pawn);

	/** 보이는 칸 목록 — 펫·가챠 빼고(서버 `isBackpackItem`), 스택 상한으로 나누고, 종류 → 등급 → ID 로 줄 세운다. */
	static TArray<FInventorySlot> BuildVisibleSlots(const TArray<FInventorySlot>& Owned, const UDataTable* ItemTable,
		int32 StackMax);

	/**
	 * 가챠 경품 칸 목록 (M35-06) — 배낭 칸을 **안 차지하는**(서버 `isBackpackItem`) 가챠 아이템을 보여만 준다.
	 * 코인 환급(`gacha.refund_coin_*`)은 돈으로 나가 칸 0. 스택 상한으로 나누고 등급(높은 것 먼저) → ID 로 줄 세운다.
	 * 등급은 `DT_GachaPool`(공개 문서 표)에서 — 소재 5종은 아이템 표 행이 없다.
	 */
	static TArray<FInventorySlot> BuildGachaSlots(const TArray<FInventorySlot>& Owned, const UDataTable* GachaPoolTable,
		int32 StackMax);

	/** 배낭에 보이는 가챠 경품인가 — `gacha.` 이면서 코인 환급이 아닌 것. */
	static bool IsShownGachaPrize(FName ItemID);

	/** `DT_GachaPool` 에서 아이템 ID 로 한 줄 (없으면 nullptr). */
	static const FGachaPoolRow* FindGachaPoolRow(const UDataTable* GachaPoolTable, FName ItemID);

	/**
	 * 하단 바의 펫 한 줄 — 펫은 칸을 안 차지해 격자에 안 보이니(서버 `isBackpackItem`) 여기서 센다.
	 * `Owned` 의 `item.pet_*` 합계 + 아직 백엔드가 못 받은 지급 대기 펫(`PendingPets`). 둘 다 0 이면 빈 글.
	 */
	static FText FormatPetSummary(const TArray<FInventorySlot>& Owned, int32 PendingPets);

	/**
	 * `풀어놓기` 가 목장 배치로 고를 펫 — 지금 고른 것이 가진 펫이면 그대로, 아니면(씨앗이거나 없음) 가진 펫 첫 마리.
	 * 가진 펫이 없으면 NAME_None. 고르기는 표시 상태일 뿐이다(CLAUDE.md 8).
	 */
	static FName PickReleasePet(const TArray<FName>& OwnedPets, FName CurrentSelection);

	/** 칸 수 = 기본 칸 + 가진 배낭 장비(`EquipSlot == Backpack`) 중 가장 큰 `BonusSlots` (서버 `capacityFor` 거울). 보트 컨테이너는 배 보관함 칸이라 안 더한다(M32-02 보정). */
	static int32 ComputeCapacity(const TArray<FInventorySlot>& Owned, const UDataTable* ItemTable, int32 BaseSlots);

	/**
	 * 열린 배낭 판 **왼쪽 변**이 화면 오른쪽 끝에서 몇 px 떨어져 앉는가 (1.6배로 키운 뒤의 값, M15-17).
	 * 판이 화면 오른쪽 변에 붙어 있어 이 값은 화면 폭과 무관하다 — 옆에 같이 뜨는 창(상점, M15-18)이
	 * 배낭을 덮지 않을 자리를 이 한 곳에서 읽는다. 두 위젯이 각자 숫자를 적으면 한쪽만 옮겼을 때 겹친다.
	 */
	static float GetPanelLeftInsetFromRight();

	/** 배낭 판 몸통이 지금 보이나 — 옷장이 위에 열려 있으면 가려진다(2층이 겹침 없음을 본다). */
	bool IsPanelShown() const;

	/** 왼쪽 세로 탭 단추 열의 **왼쪽 끝**이 화면 오른쪽 끝에서 몇 px 떨어지는가 (1.6배 뒤의 값) — 옆 창이 탭을 덮지 않을 자리. */
	static float GetTabsLeftInsetFromRight();

	/** 검사·폰 연결용. 스냅샷 바뀜을 듣기 시작하고 한 번 그린다. */
	void BindTargets(USnareBackendSubsystem* InBackend, UBackpackComponent* InBackpack, UEquipmentComponent* InEquipment);

	void SetItemTable(UDataTable* Table);

	/** 결과 글을 띄울 HUD 를 정한다. 안 정하면 폰 주인의 HUD 를 쓴다 — 폰 없이 위젯만 세우는 2층 검사가 쓴다. */
	void SetNoticeHud(UHuntHudWidget* Hud);

	/** 사냥 HUD 가 없을 때(거점) 결과 글을 띄울 목장 HUD. 안 정하면 폰 주인의 목장 HUD 를 쓴다. */
	void SetNoticeRanchHud(URanchHudWidget* Hud);

	/**
	 * 목장 데코 「목장 적용/적용 해제」 가 부를 목장 전송기 (M35-05d). 안 정하면 배낭 주인에게서 찾는다 —
	 * 폰 없이 위젯만 세우는 2층 검사가 쓴다.
	 */
	void SetRanchSync(URanchSyncComponent* Sync);

	UFUNCTION(BlueprintCallable, Category = "Backpack")
	void SetPanelOpen(bool bOpen);

	bool IsPanelOpen() const { return bPanelOpen; }

	/** 상점 판이 열린 동안 배낭 판 머리 코인 칩을 접는다 — 상점 판 머리에 같은 코인 알약이 있어 값이 둘로 보인다 (M106-09). */
	void SetBalanceChipHidden(bool bHidden);

	bool IsBalanceChipHidden() const;

	/** 상점 판이 열린 동안 배낭 판 「닫기」 를 접는다 — 상점 탭 줄에도 「닫기」 가 있어 둘로 보이고, 상점 쪽이 둘 다 닫는다 (출시점검 ③). */
	void SetCloseHidden(bool bHidden);

	bool IsCloseHidden() const;

	/**
	 * 지금 있어야 할 뷰포트 Z — 연 판은 전체 화면 판 Z(HUD Z 로 붙는 상인 「상점 열기」 카드·출석 버튼 위), 닫히면 HUD Z.
	 * 지도(`UWorldMapScreenWidget::GetViewportZOrder`)와 같은 꼴.
	 */
	int32 GetViewportZOrder() const;

	/** 칸 하나를 고른다(누른 것과 같다). 같은 칸을 다시 고르면 카드를 닫는다. 감독 PIE 파이썬 입구(M13-25). */
	UFUNCTION(BlueprintCallable, Category = "Backpack")
	void SelectSlot(int32 Index);

	/**
	 * 칸 누름·뗌 (M13-20) — 누를 땐 자리만 기억하고, 뗄 때 움직임이 `GetTapSlopPx` 이하면 그 칸을 고른다.
	 * 문턱을 넘겼으면 스크롤(`SlotScroll`)을 한 것이라 고르지 않는다. 뗌이 칸을 골랐으면 true.
	 */
	void BeginSlotPress(int32 Index, const FVector2D& ScreenPos);
	bool EndSlotPress(const FVector2D& ScreenPos);

	/** 탭 문턱 px (DESIGN 8.12) = 엔진 드래그 문턱 — `SScrollBox` 가 터치 스크롤을 시작하는 거리와 같다. */
	static float GetTapSlopPx();

	/** 카드 버튼 셋. 칸은 여기서 안 바뀐다 — 답이 와서 스냅샷이 바뀔 때만. */
	UFUNCTION()
	void PressUse();
	UFUNCTION()
	void PressEquip();
	UFUNCTION()
	void PressDrop();
	UFUNCTION()
	void PressPlace();
	/** 서버에 닿지 못한 카드 요청(사용·모자·버리기)을 같은 인자로 다시 보낸다 (의뢰 화면과 같은 꼴). */
	UFUNCTION()
	void PressRetry();

	/**
	 * 목장 데코 카드의 사용 칸 = 「목장 적용」(이미 적용 중이면 「적용 해제」) — `SetRanchDeco` 만 묻는다 (M35-05d).
	 * 버튼 글자·목장 겉모습은 서버 답 뒤 다시 받은 `GetBaseState.ranchDeco` 로만 바뀐다 (CLAUDE.md 8).
	 */
	void PressRanchDeco();

	/** 고른 칸이 목장 데코이고, 다시 받은 목장 데코 캐시에 그 종류 칸으로 적혀 있나. */
	bool IsSelectedDecoApplied() const;

	/** 펫 줄 `풀어놓기` — 목장 배치로 펫을 고르고 배낭을 닫는다. 실제 넣기는 목장 칸 앞 상호작용이 백엔드에 묻는다. */
	UFUNCTION()
	void PressReleasePet();

	/**
	 * 세로 탭 (M13-29 · DESIGN 8.12) — 새 화면을 만들지 않고 있는 화면에 잇는다.
	 * 장비 = 이 판을 장착할 수 있는 것만 보기 · 도감 = `UCodexScreenWidget` · 제작 = 배낭 제작 목록(8.10 어디서나)
	 * · 안내서 = `USnareGuideBookWidget`(M28-06b2b, 배낭 판은 밑에 그대로).
	 * 같은 탭을 다시 누르면 판을 닫는다(배낭·장비).
	 */
	UFUNCTION()
	void PressGearTab();
	UFUNCTION()
	void PressCodexTab();
	UFUNCTION()
	void PressCraftTab();
	UFUNCTION()
	void PressGuideTab();

	/** 옷장 꾸미기 상점(DESIGN 10.7)을 띄운다. 떠 있으면 닫는다. 사람은 옷장 화면의 「상점 열기」 로 연다. */
	UFUNCTION()
	void PressWardrobeShop();
	UWardrobeShopWidget* GetWardrobeShop() const { return WardrobeShop; }

	/** 장비 보기 아래 「옷장」 (`docs/specs/skin-parts.md` 5장 5) — 옷장 화면을 띄운다. 떠 있으면 닫는다. */
	UFUNCTION()
	void PressWardrobe();

	/** 장비 보기 아래 「올가미 기계」 — 뽑기 판의 사람 입구(M107-05 ⑥). 떠 있으면 다시 만들지 않는다. */
	UFUNCTION()
	void PressGacha();

	UGachaScreenWidget* GetGachaScreen() const { return GachaScreen; }

	/** 옷장이 거둬지면 가려 둔 배낭 판을 되살린다. */
	void HandleWardrobeClosed();

	/** 옷장이 위에 열려 배낭 판을 가리는 중. */
	bool bWardrobeCovers = false;
	UWardrobeWidget* GetWardrobe() const { return Wardrobe; }

	bool IsGearView() const { return bGearOnly; }

	/** 노란 테를 두를 세로 탭 번호 (M24-01) — 판이 배낭 보기면 0 · 장비 보기면 1 · 닫혔으면 `INDEX_NONE`. */
	int32 GetCurrentTabIndex() const;
	UCodexScreenWidget* GetCodexScreen() const { return CodexScreen; }
	USnareGuideBookWidget* GetGuideBook() const { return GuideBook; }

	const TArray<FInventorySlot>& GetShownSlots() const { return ShownSlots; }
	int32 GetCapacity() const { return Capacity; }
	int32 GetCellCount() const;
	int32 GetSelectedIndex() const { return SelectedIndex; }
	UFUNCTION(BlueprintPure, Category = "Backpack")
	FText GetStatusText() const;

	/** 카드 아래 까닭 줄 — 사용·장착·놓기가 다 꺼졌을 때만 글이 있다 (M15-09 · INBOX ③). 빈 글이면 안 보인다. */
	UFUNCTION(BlueprintPure, Category = "Backpack")
	FText GetCardHintText() const;

	/**
	 * 아무 버튼도 못 누르는 칸의 까닭 한 줄 (M15-09 · INBOX ③ — 폰에는 툴팁이 안 보인다).
	 * 아는 것만 말한다: 종류·놓기 칸 수·장착 부위는 아이템 표가, "제작에 쓴다" 는 레시피 표가 근거다
	 * (`RecipeTable` 이 없으면 제작 얘기를 빼고 말한다). 표 밖 아이템이면 그 사실을 말한다.
	 */
	static FText MakeDisabledHint(const FItemRow* Row, const UDataTable* RecipeTable);

	/** 이 아이템이 레시피 어딘가의 재료 칸(ID 또는 태그)에 드는가 (DESIGN 8.10). 표가 없으면 거짓. */
	static bool IsCraftIngredient(const UDataTable* RecipeTable, const FItemRow& Row);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** 아이템 표 — 이름·종류·등급·그림·회복량·추가 칸. */
	UPROPERTY(EditDefaultsOnly, Category = "Backpack")
	TObjectPtr<UDataTable> ItemTable;

	/** 거절 code → 화면 문구 (M26-02, 목장과 같은 `DT_RanchFailureTable`). */
	UPROPERTY(EditDefaultsOnly, Category = "Backpack")
	TObjectPtr<UDataTable> FailureTable;

	/** 가챠 풀 표 — 가챠 경품 칸의 이름·등급 (M35-06, 공개 문서 `docs/GACHA_DISCLOSURE.md` 그대로). */
	UPROPERTY(EditDefaultsOnly, Category = "Backpack")
	TObjectPtr<UDataTable> GachaPoolTable;

	/** 한 칸 스택 상한. DESIGN 8.12 "칸" = 서버 `STACK_MAX`. */
	UPROPERTY(EditDefaultsOnly, Category = "Backpack")
	int32 StackMax = 99;

private:
	UFUNCTION()
	void HandleSnapshotChanged();

	UFUNCTION()
	void HandleReply(bool bAccepted, const FString& Code, const FString& Message);

	UFUNCTION()
	void HandleEquipmentChanged();

	/** 포획·채집으로 지급 요청이 대기열에 쌓이면 하단 바의 펫 줄을 다시 그린다 (오프라인이면 스냅샷이 안 와서 이 신호가 유일하다). */
	UFUNCTION()
	void HandlePendingGrantQueued(FName ItemID, int32 Count);

	/**
	 * 결과 한 줄을 남기고, 판 밖에 남는 글자 대신 HUD 왼쪽 위 알림 카드(M15-02)로 몇 초만 띄운다.
	 * 전에는 판 위쪽에 글자를 따로 띄워서 배낭을 닫아도 남았다 (INBOX 2026-09-29 ②).
	 * @param bError 거절·실패면 크림 카드 대신 빨간 오류 알약으로 (M26-04).
	 */
	void PostStatus(const FText& Message, bool bError = false);

	/** 이 아이템이 지금 몸에 든 것인가 — 모자는 서버 기록, 도구·옷·배낭은 장비 컴포넌트가 안다. */
	bool IsItemWorn(FName ItemID) const;

	UFUNCTION()
	void ToggleBackpackTab();

	/** 판 「닫기」 단추 — 판만 닫는다(여는 버튼은 그대로). */
	UFUNCTION()
	void PressClose();

	void Redraw();
	void RedrawCard();

	/** 판을 배낭 보기(`bGear` 거짓)·장비 보기로 연다. 이미 그 보기로 열려 있으면 닫는다. */
	void ShowTab(bool bGear);

	/** 세로 탭 테 색을 지금 탭(노랑)·나머지(크림)로 다시 칠한다 (M24-01). */
	void RedrawTabs();

	/** 고른 칸이 지금 머리에 쓴 모자인가 — 그러면 장착 버튼이 `벗기` 가 된다 (#0198). */
	bool IsSelectedWornHat() const;

	/** 고른 칸이 지금 손·몸에 붙어 있는 장비인가(모자 말고) — 그러면 버튼이 `내리기` 가 된다 (M15-08 · INBOX ②). */
	bool IsSelectedEquippedGear() const;

	/** 폰 주인(가방·장비가 붙은 폰)의 컴포넌트 — 없으면 nullptr. */
	USnareBaseBuildComponent* FindBuild() const;
	UWaterTravelComponent* FindWater() const;

	/** 레시피 표는 거점 서브시스템이 들고 있다 — 까닭 줄의 "제작에 쓴다" 근거 (M15-09). 없으면 nullptr. */
	UFUNCTION()
	void HandleRanchAnswered(const FString& FunctionName, bool bAccepted, const FString& FailureCode, const FText& Message);

	UFUNCTION()
	void HandleRanchCacheChanged();

	/** 목장 전송기·목장 캐시 알림을 (다시) 잡는다. 전송기가 목장 칸을 늦게 묶어도 카드를 그릴 때 따라잡는다. */
	void WatchRanchSync();

	const UDataTable* FindRecipeTable() const;
	void BuildTree();
	const FItemRow* FindRow(FName ItemID) const;
	UTextBlock* MakeText(FName Name, const FText& Text, int32 Size, const FLinearColor& Color);
	UButton* MakeButton(FName Name, const FText& Label, int32 Size);

	UPROPERTY()
	TObjectPtr<USnareBackendSubsystem> Backend;

	UPROPERTY()
	TObjectPtr<UBackpackComponent> Backpack;

	UPROPERTY()
	TObjectPtr<UEquipmentComponent> Equipment;

	/** 지급 대기열 주인 — 배낭 소유자에게서 찾는다. */
	UPROPERTY()
	TObjectPtr<UInventoryComponent> PendingSource;

	UPROPERTY()
	TObjectPtr<UHuntHudWidget> NoticeHud;

	UPROPERTY()
	TObjectPtr<URanchHudWidget> NoticeRanchHud;

	/** 목장 데코 적용 요청을 보낼 곳 (M35-05d). */
	UPROPERTY()
	TObjectPtr<URanchSyncComponent> RanchSync;

	/** 데코 캐시가 바뀌면 카드 버튼 글자를 다시 그리려고 듣는 목장 칸 캐시. */
	UPROPERTY()
	TObjectPtr<URanchComponent> WatchedRanch;

	/** 데코 요청이 받아들여지면 띄울 한 줄 — 누른 때 정해 두고 답이 와서야 띄운다. */
	FText PendingDecoNotice;

	UPROPERTY()
	TObjectPtr<UCanvasPanel> Root;

	UPROPERTY()
	TObjectPtr<UBorder> Panel;

	UPROPERTY()
	TObjectPtr<UBorder> TitlePill;

	/** 판 오른쪽 위 「닫기」 상자 — 판이 열린 동안만 보인다. */
	UPROPERTY()
	TObjectPtr<USizeBox> CloseBox;

	/** 카드 버튼 줄 아래 「다시 시도」 — 서버에 닿지 못한 요청이 있을 때만 보인다. */
	UPROPERTY()
	TObjectPtr<USizeBox> RetryBox;

	/** 왼쪽 세로 탭 기둥 — 패널이 열린 동안만 보인다(M15-02, 레퍼런스는 닫힌 배낭에 탭이 없다). */
	UPROPERTY()
	TObjectPtr<UVerticalBox> TabColumn;

	/** 세로 탭마다 테 — 지금 탭만 노랑 (M24-01, 시트 「선택 시 노란 테두리」). 순서 = 배낭·장비·도감·제작·안내서. */
	UPROPERTY()
	TArray<TObjectPtr<UBorder>> TabFrames;

	UPROPERTY()
	TObjectPtr<UUniformGridPanel> Grid;

	/** 칸 격자를 담는 세로 스크롤 (#0203) — 보이는 줄보다 많으면 스크롤로 본다. */
	UPROPERTY()
	TObjectPtr<UScrollBox> SlotScroll;

	/** 스크롤 높이 = 보이는 줄 수만큼. */
	UPROPERTY()
	TObjectPtr<USizeBox> SlotView;

	UPROPERTY()
	TObjectPtr<UTextBlock> BalanceText;

	/** 판 머리 오른쪽 코인 칩 통째(아이콘·숫자). */
	UPROPERTY()
	TObjectPtr<USizeBox> BalanceChip;

	/** 포획한 펫 수·등록 대기 한 줄 — 바(260px)에 안 들어가 격자 아래 따로 두고 없으면 접는다. */
	UPROPERTY()
	TObjectPtr<UTextBlock> PetText;

	UPROPERTY()
	TObjectPtr<UBorder> Card;

	UPROPERTY()
	TObjectPtr<UBorder> CardTail;

	UPROPERTY()
	TObjectPtr<UTextBlock> CardNameText;

	UPROPERTY()
	TObjectPtr<UTextBlock> CardInfoText;

	/** 꺼진 버튼의 까닭 줄 (M15-09). 누를 것이 하나라도 있으면 접힌다. */
	UPROPERTY()
	TObjectPtr<UTextBlock> CardHintText;

	UPROPERTY()
	TObjectPtr<UButton> UseButton;

	UPROPERTY()
	TObjectPtr<UButton> EquipButton;

	UPROPERTY()
	TObjectPtr<UButton> PlaceButton;

	UPROPERTY()
	TObjectPtr<UButton> DropButton;

	/** 가진 펫이 있을 때만 보이는 `풀어놓기` (격자 밖 펫을 목장으로 잇는다). */
	UPROPERTY()
	TObjectPtr<UButton> ReleasePetButton;

	/** 도감 탭이 띄운 도감 — 떠 있는 동안(업적으로 접힌 때 포함) 다시 만들지 않는다. */
	UPROPERTY()
	TObjectPtr<UCodexScreenWidget> CodexScreen;

	/** 「올가미 기계」 가 띄운 뽑기 판. */
	UPROPERTY()
	TObjectPtr<UGachaScreenWidget> GachaScreen;

	/** 안내서 탭이 띄운 안내서 (M28-06b2b) — 떠 있는 동안 다시 만들지 않는다. */
	UPROPERTY()
	TObjectPtr<USnareGuideBookWidget> GuideBook;

	/** 장비 보기에서만 보이는 「옷장」. */
	UPROPERTY()
	TObjectPtr<UButton> WardrobeButton;

	/** 장비 보기에서만 보이는 「올가미 기계」. */
	UPROPERTY()
	TObjectPtr<UButton> GachaButton;

	/** `PressWardrobeShop` 이 띄운 옷장 상점. 배낭 판이 닫히면 같이 닫는다. */
	UPROPERTY()
	TObjectPtr<UWardrobeShopWidget> WardrobeShop;

	/** 「옷장」 이 띄운 옷장 화면. 배낭 판이 닫히면 같이 닫는다. */
	UPROPERTY()
	TObjectPtr<UWardrobeWidget> Wardrobe;

	/** 격자 칸 위젯 — 누른 자리 찾기용(칸 순서 = `ShownSlots` 순서, 뒤는 빈 칸). */
	UPROPERTY()
	TArray<TObjectPtr<UBorder>> Cells;

	TArray<FInventorySlot> ShownSlots;

	/** `ShownSlots` 중 가챠 경품 칸이 시작하는 자리·개수 — 배낭 칸 수에 안 들어가 그만큼 칸을 더 그린다 (M35-06). */
	int32 GachaSlotStart = 0;
	int32 GachaSlotCount = 0;

	/** 등록 대기 펫 — `ShownSlots` 바로 뒤 칸에 그린다(눌러도 고르지 않고 안내만). */
	TArray<FInventorySlot> PendingPetSlots;
	FText Status;
	/** 배낭 컴포넌트에 보낸 마지막 요청 / 서버에 닿지 못해 다시 보낼 것. */
	TFunction<void(UBackpackComponent&)> SentRequest;
	TFunction<void(UBackpackComponent&)> RetryRequest;
	void SendToBackpack(TFunction<void(UBackpackComponent&)> Send);
	int32 Capacity = 0;
	int32 SelectedIndex = INDEX_NONE;
	int32 PressedIndex = INDEX_NONE;
	FVector2D PressedAt = FVector2D::ZeroVector;
	bool bPanelOpen = false;
	/** 상점이 열려 있어 배낭 「닫기」 를 접어 두는가 — `SetPanelOpen` 이 판을 다시 펼 때도 이 값을 따른다. */
	bool bCloseHidden = false;
	bool bGearOnly = false;
	/** 뷰포트에 붙인 Z — `SetPanelOpen` 이 달라졌을 때만 떼었다 다시 붙인다. */
	int32 AddedViewportZOrder = 0;
};
