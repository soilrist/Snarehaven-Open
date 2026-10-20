#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Crafting/SnareCraftRules.h"
#include "CraftingScreenWidget.generated.h"

class ASnarePlacedStructureActor;
class UBorder;
class UButton;
class UEditableTextBox;
class UHorizontalBox;
class UHuntHudWidget;
class URanchHudWidget;
class UScrollBox;
class USnareBackendSubsystem;
class USnareBaseBuildComponent;
class USnareBaseSubsystem;
class USnareClickProxy;
class UTextBlock;
class UUniformGridPanel;
class UWidget;
struct FBaseRuleRow;
struct FInventorySlot;
struct FSnareTentMirror;

/** 화면 한 장 안의 쪽. 놓인 것마다 보이는 쪽이 다르다. */
UENUM(BlueprintType)
enum class ESnareBaseScreenPage : uint8
{
	/** 제작·요리 목록. */
	Craft,
	/** 가방 속 배치형 → 놓기 모드. 호스트·거점존만. */
	Place,
	/** 텐트 → 집 올리기·색·유지비. 호스트만. */
	House,
	/** 보관함 PIN·넣기·꺼내기. */
	Storage,
	/** 작업대(텐트) → 페인트로 내 가구 칠하기 (DESIGN 13.8). 호스트만. */
	Paint,
	/** 배 위 HUD 「배 보관함」 → 계정당 하나인 배 보관함 넣기·꺼내기 (DESIGN 8.9 보트 컨테이너). */
	BoatStorage,
	/** 연료 발전기 → 돌아감/멈춤·이 땅 전력·나무·석탄 덩어리 넣기 (M108-07c-2). 넣기 단추는 내 발전기만. */
	Generator,
	/** 전기 가구 → 이름·켜짐/꺼짐 까닭 한 줄·「가방에 넣기」 만 있는 작은 판. 까닭 알림이 떠 있는 동안 한 번 더 「쓰기」 하면 열린다
	 *  (INBOX 지시 10-20 15:40 — 페인트 없이도 전기 가구를 치울 수 있게). 제작 목록 없음. */
	Furniture
};

// DESIGN 8.10 제작·요리 · 8.11 건축 화면 (모바일 터치 한 장).
//
// **이 화면은 아무것도 지급하지 않는다.** 줄을 누르면 `USnareBaseSubsystem` 이 서버(`CraftItem`·`PlaceStructure`·
// `UpgradeHouse`·`FeedUpkeep`·`MoveToStorage`·`MoveFromStorage`·`PaintStructure`)에 묻고, 가방 글자는 서버 성공 뒤
// `FetchPlayerState` 가 새 스냅샷을 줬을 때만 바뀐다 (CLAUDE.md 8). 있다/모자라다·잠김 은 버튼 켜기용 안내다.
// 마감·유지 시각은 서버 응답 글자 그대로 적는다 — 클라이언트 시계로 다시 계산하지 않는다 (DESIGN 8.11 판정).
UCLASS()
class SNAREHAVEN_API UCraftingScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Setup(USnareBaseBuildComponent* InOwner, USnareBaseSubsystem* InBase);

	/** 뷰포트에 전체 화면 판 층(도감·지도·배낭 판과 같은 Z)으로 붙인다 — 「다음 목표」 띠·물 버튼 줄이 판 위로 그려지지 않게.
	 *  닫힌 판은 접혀(Collapsed) 있어 이 층에 있어도 아래 HUD 를 막지 않는다. */
	void AddToScreenPanelLayer();

	/** 배낭 제작 탭 — "어디서나" 레시피 (+ 호스트·거점존이면 놓기 쪽). */
	void OpenBackpack();

	/** 놓인 것 앞 상호작용 — 텐트·작업대는 작업대, 불은 요리, 보관함은 보관함 쪽. */
	void OpenFor(ASnarePlacedStructureActor* Target);

	/** 배 보관함 쪽 — 배에 탄 사람의 HUD 「배 보관함」 버튼이 부른다. 넣기·꺼내기는 서버 `boat: true` 길. */
	void OpenBoatStorage();

	/** 3층 캡처 전용 — 놓인 텐트 없이 작업대 자리 목록을 띄운다 (재화는 안 움직인다). */
	void OpenWorkbenchForTest();

	/** 마지막 전기 가구 까닭 알림 글 — 2층 시험·3층 검사 로그가 읽는다 (판은 안 열린다, INBOX 보정 10-20 14:40). */
	const FText& GetElectricNoticeText() const { return ElectricNoticeText; }

	/** 2층 검사 문 — 몸(캐릭터) 없는 시험 월드에서 까닭 알림이 갈 HUD 를 직접 준다. */
	void SetNoticeHudsForTest(UHuntHudWidget* InHunt, URanchHudWidget* InRanch);

	/** 2층 검사 문 — 몸 없는 시험 월드에서 「지을 수 있는 땅에 섰다」(`CanBuildHere`)를 대신한다. 「가방에 넣기」 줄이 이것을 본다. */
	void SetCanBuildForTest(bool bInCanBuild) { bCanBuildForTest = bInCanBuild; }

	/** 작은 가구 판의 까닭 줄 글 (켜짐/꺼짐 — 알림과 같은 꼴, 판을 세울 때 다시 잰 숫자). 다른 쪽이면 빈 글. */
	const FText& GetFurnitureStatusText() const { return FurnitureStatusText; }

	/** 지금 판에 「가방에 넣기」 줄이 있나 — 2층 시험·3층 검사 로그가 읽는다. */
	bool IsPickUpShown() const { return bPickUpShown; }

	/**
	 * 「가방에 넣기」 한 번 — 고른 가구를 서버 `RemoveStructure` 에 묻는다(가구는 응답 뒤에만 사라진다, CLAUDE.md 8).
	 * 줄 단추와 2층 시험이 같은 길을 탄다. @return 물었으면 true (판은 닫힌다)
	 */
	bool PressPickUp();

	/** 2층 검사 문 — 게임 인스턴스 없는 시험 월드에서 가방(페인트) 스냅샷을 읽을 창구. */
	void SetBackendForTest(USnareBackendSubsystem* InBackend);

	/** 배 보관함 칸 한 번 누름 = 고르기 (서버에 안 묻는다). 띠의 「넣기」/「꺼내기」 가 옮긴다. */
	void PickBoatCell(FName ItemID, bool bFromStore);
	/** 배 보관함에서 고른 아이템 (없으면 None). 응답 뒤 그쪽에서 사라지면 풀린다. */
	FName GetBoatPickedItem() const { return BoatPickedItem; }
	bool IsBoatPickedFromStore() const { return bBoatPickedFromStore; }

	void ShowPage(ESnareBaseScreenPage Page);
	void Close();
	bool IsOpen() const { return bOpen; }
	ESnareBaseScreenPage GetPage() const { return Page; }
	ESnareCraftPlace GetCraftPlace() const { return CraftPlace; }

	/** 지금 자리의 레시피 줄 (표 + 스냅샷). */
	TArray<FSnareRecipeView> BuildRecipes() const;

	/**
	 * 레시피 줄을 누른 것과 같다 — 불이면 요리를 올리고, 아니면 `CraftItem` 을 묻는다.
	 * 2층 검사가 버튼 없이 같은 길을 탄다. @return 서버에 물었거나 요리를 올렸으면 true
	 */
	bool PressRecipe(FName ResultItemID);

	// ── 가구 집어 칠하기 (DESIGN 13.8 🟡) ──

	/**
	 * 이 가구를 칠할 대상으로 고르고 칠하기 쪽을 연다. 내 땅의 가구(`IsOwnFurniture`)만 —
	 * 남의 가구·텐트·미리보기·빈 것은 false 이고 지금 대상은 그대로다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paint")
	bool PickPaintTarget(ASnarePlacedStructureActor* Structure);

	/** 가구를 직접 탭했다 (M13-20) — 페인트를 가졌고 `PickPaintTarget` 이 받으면 칠하기 쪽이 열린다. 아니면 아무것도 안 연다. */
	bool TapPaintTarget(ASnarePlacedStructureActor* Structure);

	/** 고른 가구 (없으면 null). 작업대 목록 쪽이면 null. */
	UFUNCTION(BlueprintCallable, Category = "Paint")
	ASnarePlacedStructureActor* GetPaintTarget() const;

	/**
	 * 「칠하기」 한 번 — 고른 가구에 서버 `PaintStructure` 를 묻는다. 색은 응답 뒤에만 입는다 (CLAUDE.md 8).
	 * @return 서버에 물었으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Paint")
	bool PressPaint(FName PaintItemID);

	// ── 글자 (2층 검사가 서버 응답 글자 그대로인지 잰다) ──

	/** `마감 <서버 upgradeDeadlineUtc>`. 집이면 빈 글. */
	static FText FormatDeadlineText(const FSnareTentMirror& Tent);
	/** `유지 <서버 upkeepUntilUtc>`. 텐트면 빈 글. */
	static FText FormatUpkeepText(const FSnareTentMirror& Tent);
	/** 집 단계 한 줄 `2층 집 · 가구 3.5/60` — 층은 서버 `floors`, 가구는 몫(벽 장식·소품 0.5), 한도는 표(`SnareBuildRules::FurnitureMaxFor`). 텐트면 빈 글. */
	static FText FormatFloorText(const FSnareTentMirror& Tent, const FBaseRuleRow& Rules);
	/** 발전기 상태 한 줄 — `돌아감 · 연료 1시간 20분 남음`(분은 올림) / `멈춤 - 연료 없음`. 남은 초는 놓인 액터의 복제 값. */
	static FText FormatGeneratorFuelText(double SecondsLeft);
	/** `이 땅 전력 공급 110 / 소모 85` (`USnarePowerSubsystem::MeasureLand`). */
	static FText FormatLandPowerText(int32 Supply, int32 Use);
	/**
	 * 전기 가구 「쓰기」 까닭 한 줄 (M108-08e) — 켜짐 `켜짐 — 이 땅 전력 공급 105 / 소모 100` ·
	 * 꺼짐 `꺼짐 — 전력이 모자랍니다 (공급 10 / 소모 45)`. 숫자는 발전기 판 줄과 같은 `MeasureLand`.
	 */
	static FText FormatElectricStatusText(bool bOn, int32 Supply, int32 Use);
	/**
	 * 발전기 연료 단추를 켤까 — 내 발전기이고 최대(`GeneratorFuelMaxHours`)까지 남은 칸이 그 연료 한 개(`Minutes`) 이상.
	 * 모자라면 서버가 `full` 로 거절할 줄이라 미리 끈다. 판정은 여전히 서버 `FeedGenerator` 다.
	 */
	static bool CanFeedGenerator(bool bOwn, double SecondsLeft, int32 FuelMinutes, int32 MaxHours);

	/** `나무 3/5` — 모자라면 `(모자람)` 이 붙는다. */
	static FText FormatIngredientText(const FSnareIngredientView& Ingredient);

	void RefreshDisplay();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void HandleBackendSnapshotChanged();

	UFUNCTION()
	void HandleCloseClicked();

private:
	void HandleBaseChanged();
	void HandleBaseMessage(const FText& Message, bool bError);
	void HandleCookTick();
	void Open(ESnareBaseScreenPage FirstPage);
	/**
	 * 전기 가구(`PowerUse` > 0)면 판을 열지 않고 HUD 짧은 알림 한 줄로 그 땅 켜짐/꺼짐 까닭 — 거점 결과 글과 같은 길
	 * (`ASnarehavenCharacter::RouteBaseMessage`, 꺼짐은 붉은 오류 알약). @return 전기 가구라 알림을 냈으면 true (M108-08e).
	 */
	bool PostElectricStatus(const ASnarePlacedStructureActor* Structure);
	/** 전기 가구의 그 땅 켜짐/꺼짐 까닭 한 줄 — 놓인 몸 켜짐(`ApplyPower`)과 같은 셈을 지금 다시. 전기 아니거나 규칙이 없으면 빈 글. */
	FText MeasureElectricStatus(const ASnarePlacedStructureActor* Structure, bool& bOutOn) const;
	/** 놓기·치우기 권한 — 시험 문이 켰거나 몸이 지을 수 있는 땅에 섰다(`CanBuildHere`). */
	bool CanBuildNow() const;
	/** 이 전기 가구의 까닭 알림이 아직 HUD 에 떠 있나 — 사냥 HUD 알림 카드·오류 알약 글, 목장 HUD 알림 줄이 펴져 있는지로 본다. */
	bool IsElectricNoticeShown(const ASnarePlacedStructureActor* Structure) const;
	/** 까닭 알림이 갈 HUD — 시험 문이 준 것이 먼저, 없으면 몸의 HUD. */
	UHuntHudWidget* ResolveNoticeHuntHud() const;
	URanchHudWidget* ResolveNoticeRanchHud() const;
	/** 테두리를 지금 고른 가구(`GetPaintTarget`, 열려 있을 때만) 하나에만 맞춘다. */
	void SyncOutline();

	/** 목록에 버튼 줄 하나 (누르면 `OnClick`). 꺼 두면 눌림색(비활성)으로 보인다. */
	UButton* AddButtonRow(const FText& Label, bool bEnabled, TFunction<void()> OnClick);
	/**
	 * 레시피 줄 하나 (M23-04) — 왼쪽 만들 것 원판(등급 색 + 아이템 표 아이콘) + 이름, 그 아래 재료 작은 원판 + `있음/필요`.
	 * 모자란 재료 글자는 빨강. 그림·글자는 버튼 **위 겹**이라 꺼진 줄에서도 회색으로 죽지 않는다.
	 * 위젯 이름 `CraftRecipeDisc_<줄>`·`CraftRecipeIcon_<줄>`·`CraftIngredientIcon_<줄>_<재료>`·`CraftIngredientText_<줄>_<재료>` 는
	 * 2층 시험이 찾는다.
	 */
	UButton* AddRecipeRow(const FSnareRecipeView& View, int32 RowIndex, bool bEnabled, TFunction<void()> OnClick);
	/** 등급 색 원판 + 아이템 표 아이콘 (아이콘이 없거나 태그 재료면 원판만). 이름 `<접두어>Disc<꼬리>`·`<접두어>Icon<꼬리>`. */
	UWidget* MakeItemDisc(FName ItemID, float Size, const FString& NamePrefix, const FString& NameSuffix);
	UTextBlock* AddTextRow(const FText& Label);
	void AddTab(const FText& Label, ESnareBaseScreenPage TabPage);
	/** 고른 탭·고른 색 줄만 강조 바탕으로 — `ApplyUiStyle` 이 버튼 색을 한 벌로 덮은 **뒤**에 부른다. */
	void MarkSelectedButtons();

	void BuildCraftRows();
	void BuildPlaceRows();
	void BuildHouseRows();
	void BuildStorageRows();
	void BuildBoatStorageRows();
	/** 배 보관함 칸 하나 — 빈 것(`ItemID` None)은 칸 모양만. 이름 `BoatBagCell_<i>`·`BoatStoreCell_<i>`(빈 칸 `…Empty_<i>`). */
	void AddBoatCell(UUniformGridPanel* Grid, int32 Index, int32 Columns, const FInventorySlot& Entry, bool bFromStore, bool bEnabled);
	void BuildPaintRows();
	void BuildGeneratorRows();
	void BuildFurnitureRows();
	/** 가방 속 페인트 → 개수 (서버 스냅샷 그대로). */
	TMap<FName, int32> CollectPaints() const;

	FText ItemName(FName ItemID) const;
	FString FireKey() const;
	USnareBackendSubsystem* GetBackend() const;

	bool NeedsPlaceholderTree() const;
	void BuildPlaceholderTree();

	/** 판 왼쪽 위에 걸쳐 위로 튀어나온 제목 탭 (M24-02, 시트 「제목 탭: 진갈색 알약」). 글자는 `TitleText`. */
	UPROPERTY(Transient)
	TObjectPtr<UBorder> TitlePill;

	/** 가운데 판 — 배 보관함 쪽만 알파 1 로 칠한다 (INBOX 결함 10-07 02:00 ①). */
	UPROPERTY(Transient)
	TObjectPtr<UBorder> PanelBorder;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> TabRow;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;

	/** 발전기 쪽 「돌아감 · 연료 … 남음」 줄 — 1초마다 글만 고친다(줄을 다시 세우지 않는다). 다른 쪽이면 null. */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> GeneratorFuelLine;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> RowList;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> PinBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ToastText;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> BottomRow;

	UPROPERTY(Transient)
	TObjectPtr<UButton> CloseButton;

	/** 줄 버튼의 받는 쪽. 줄을 다시 세울 때 같이 비운다. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USnareClickProxy>> ClickProxies;

	/** 지금 쪽 탭·고른 색·고른 페인트 버튼 — 다시 그릴 때마다 비우고 새로 모은다. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> SelectedButtons;

	/** 쪽 탭 단추와 그 고름 여부 — `ApplyUiStyle` 이 칠한 뒤 나무 표찰 꼴로 다시 칠한다 (M101-01). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> TabButtons;

	UPROPERTY(Transient)
	TObjectPtr<UButton> CurrentTabButton;

	UPROPERTY(Transient)
	TObjectPtr<USnareBaseSubsystem> Base;

	UPROPERTY(Transient)
	TObjectPtr<USnareBackendSubsystem> WatchedBackend;

	TWeakObjectPtr<USnareBaseBuildComponent> BuildOwner;
	FText ElectricNoticeText;
	TWeakObjectPtr<UHuntHudWidget> TestHuntHud;
	TWeakObjectPtr<URanchHudWidget> TestRanchHud;
	/** 마지막 까닭 알림을 낸 전기 가구 — 알림이 떠 있는 동안 같은 가구를 또 「쓰기」 하면 작은 가구 판. */
	TWeakObjectPtr<const ASnarePlacedStructureActor> ElectricNoticeTarget;
	FText FurnitureStatusText;
	bool bCanBuildForTest = false;
	bool bPickUpShown = false;
	TWeakObjectPtr<ASnarePlacedStructureActor> Target;
	/** 테두리를 켜 둔 가구 (M13-24) — 고른 가구가 바뀌거나 화면이 닫히면 이것부터 끈다. */
	TWeakObjectPtr<ASnarePlacedStructureActor> OutlinedTarget;

	ESnareBaseScreenPage Page = ESnareBaseScreenPage::Craft;
	ESnareCraftPlace CraftPlace = ESnareCraftPlace::Backpack;

	/** 불·제련소 — 누르면 바로 만들지 않고 타이머를 건다 (DESIGN 8.10·8.16). */
	bool IsTimedPlace() const { return CraftPlace == ESnareCraftPlace::Fire || CraftPlace == ESnareCraftPlace::Smelter; }
	/** 집 올리기에 쓸 색 (서버 `color`). */
	int32 ChosenColor = 0;
	/** 칠하기에 쓸 페인트 (가방에 있는 것 중 고름). */
	FName ChosenPaint;
	/** 배 보관함에서 고른 칸 — 아이템과 어느 쪽(배 보관함/가방)인지. */
	FName BoatPickedItem;
	bool bBoatPickedFromStore = false;
	bool bOpen = false;
	int32 WidgetCounter = 0;

	FDelegateHandle BaseChangedHandle;
	FDelegateHandle BaseMessageHandle;
	FTimerHandle CookTimerHandle;
};
