#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/ShopRow.h"

#include "ShopScreenWidget.generated.h"

class UBackpackScreenWidget;
class UBorder;
class UButton;
class UCanvasPanel;
class UDataTable;
class UHorizontalBox;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class USnareBackendSubsystem;
class USnareClickProxy;
struct FGachaPoolRow;
struct FItemRow;

/** 거래 목록 한 줄이 화면에 뜨기 직전의 모습. 원본은 표·스냅샷이라 저장하지 않는다. */
struct FShopLineView
{
	FName ItemID;
	FText DisplayName;

	/** 사기: 한 번 살 때 값(`DT_ShopTable.Price`). 팔기: 하나 팔 때 값(`DT_ItemTable.SellPrice`, 가챠 소재는 `DT_GachaPool.SellPrice`). */
	int32 Price = 0;

	/** 사기: 한 번에 들어오는 개수(`DT_ShopTable.Count`). 팔기: 지금 가진 개수(스냅샷). */
	int32 Count = 0;

	/** 탈것 줄(DESIGN 8.3): 목줄을 바꿀 종 또는 안장을 달 종. None 이면 보통 사기 줄. `ItemID` = 쓸 목줄·안장. */
	FName MountSpecies;
	FText MountSpeciesName;

	/** 고구마 장수 줄(DESIGN 8.17): 이 줄이 사는 묶음 수(1·5·10). 0 이면 보통 줄. `Price`·`Count` 는 묶음 전체의 값. */
	int32 Bundles = 0;

	/** 잡화상 팔기의 고구마 줄(DESIGN 8.17): 참이면 이 줄을 누르면 `SellSweetPotato`. `Price` = 지금 개당 값, `Count` = 가진 개수. */
	bool bPotatoSell = false;

	/** 고구마 줄의 지난 칸 대비 방향: +1 ▲ · -1 ▼ · 0 같음/모름. */
	int32 PotatoTrend = 0;

	/** 고구마 줄의 산 값(이번 주 산 평균, 반올림). 0 이면 이번 주 산 적 없음. */
	int32 PotatoPaidEach = 0;
};

// DESIGN 8.14 잡화점 — 사기·팔기 두 탭 (레퍼런스 `docs/reference/shop_vendor.md`).
//
// **이 화면은 코인도 배낭도 바꾸지 않는다.** 줄을 누르면 서버(`BuyItem`/`SellItem`)에 묻고, 성공 답이 오면
// 보유 상태를 **다시 받아**(`FetchPlayerState`) 그 스냅샷으로만 코인·팔 목록이 바뀐다 (CLAUDE.md 8).
// 답을 기다리는 동안 줄은 전부 잠긴다 — 시트의 "응답 전 행 잠금, 스피너 없음".
//
// 사기 목록은 `DT_ShopTable` 전부(재고 무한), 팔기 목록은 스냅샷 배낭 중 `DT_ItemTable.SellPrice > 0` 인 것뿐이다.
// 아이템 표 행이 없는 가챠 희귀 소재 5종은 잡화점에서만 `DT_GachaPool.SellPrice` 로 줄을 세운다(DESIGN 10.2).
// 사기 탭 끝에 탈것 줄(M13-03 · 결정 009): 목줄을 가졌으면 "목줄 > 안 가진 종" (`ExchangeCollar`), 안장을 가졌으면
// "안장 > 가진 종" (`AttachSaddle`). 답의 `mounts`·`saddles` 로만 탈것 칸이 바뀐다(`ApplyMountsReply`).
// 값은 표에서 읽어 **보여 주기만** 한다 — 실제로 얼마를 빼고 줄지는 서버가 정한다.
//
// 닫힌 상태에서는 "상점 열기" 카드 하나만 상인 머리 옆에 보인다(M23-02, `SetPromptTarget`). 상인(`AShopVendorActor`)이
// 가까이 왔을 때 뷰포트에 올린다.
UCLASS()
class SNAREHAVEN_API UShopScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UShopScreenWidget(const FObjectInitializer& ObjectInitializer);

	/** 사기 탭 줄 — `DT_ShopTable` 행 순서 그대로. */
	TArray<FShopLineView> BuildBuyLines() const;

	/** 팔기 탭 줄 — 스냅샷 배낭 중 파는 값이 있는 것만. 백엔드를 모르면 빈 목록. */
	TArray<FShopLineView> BuildSellLines() const;

	/** 코인·배낭을 읽을 백엔드. 읽기만 한다. nullptr 이면 코인이 `-`, 팔 목록이 빈다. */
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void BindToBackend(USnareBackendSubsystem* Backend);

	/** 상점 창을 펴거나 접는다. 접으면 "상점 열기" 버튼만 남는다. */
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void SetOpen(bool bInOpen);

	bool IsOpen() const { return bOpen; }

	/**
	 * 닫힌 동안 "상점 열기" 카드가 붙을 월드 점 (시트 「상호작용 프롬프트: 대상 옆 작은 크림 카드」, M23-02).
	 * 상인 액터가 자기 머리 점을 넣는다. 넣은 뒤로는 첫 투영 전까지 카드를 접어 둔다.
	 */
	void SetPromptTarget(const FVector& WorldLocation);

	/**
	 * 투영된 대상 자리로 열기 카드를 옮긴다 — 매 틱 `NativeTick` 이 부른다(2층 검사는 직접 부른다).
	 * @return 대상이 화면 안이라 카드 자리가 섰으면 true. false 면 카드를 접는다
	 */
	bool PlaceOpenButton(const FVector2D& TargetPx, const FVector2D& ViewportPx);

	/** 다른 창이 열린 동안 「열기」 카드를 접는다 (INBOX 결함 2026-10-11 05:00) — 캐릭터가 창 열림 한 곳에서 부른다. */
	void SetPromptSuppressed(bool bSuppressed);
	bool IsPromptSuppressed() const { return bPromptSuppressed; }

	UFUNCTION(BlueprintCallable, Category = "Shop")
	void SetSellTab(bool bInSellTab);

	bool IsSellTab() const { return bSellTab; }

	/** 팔기 탭 「모두 팔기」 켬/끔 — 켜 두면 팔기 줄 탭이 하나가 아니라 그 종류 전부(`RequestSellAll`)다. */
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void SetSellAllMode(bool bInSellAll);

	bool IsSellAllMode() const { return bSellAllMode; }

	/** NPC 명함의 이름. 상인 액터가 넣는다. */
	void SetVendorName(const FText& InName);

	/** NPC 명함 둘째 줄 — 시트의 좌표 자리에 **존 이름**을 적는다. 상인 액터가 자기 자리의 존을 넣는다. */
	void SetVendorZone(const FText& InZone);

	/**
	 * 같이 열 배낭 화면 (시트 「구성」 — 사고팔 때 내 칸이 보여야 한다). 없으면 상점 판만 뜬다.
	 * 상인 액터가 내 폰의 배낭 화면을 찾아 넣는다.
	 */
	void BindBackpackScreen(UBackpackScreenWidget* InBackpack);

	/** 명함 본문 — 가게마다 다른 편지체 한 문단 (우리가 새로 쓴 글이다, Reference.md 5). */
	static FText GetVendorLetter(EShopKind Kind);

	/**
	 * 시트 px(1920 기준)를 화면 px 로 키우는 배율 (감독 2026-09-25 "아이템 창이 작다" — 배낭과 같은 1.6배).
	 * 배낭은 판 전체에 렌더 변환을 걸어 레이아웃 값이 시트 px 그대로지만, 상점 판은 **폭이 늘었다 줄었다**
	 * 해야 해서 값마다 배율을 먹인다 — 그래서 시트와 대조하는 검사는 잰 값을 이 배율로 나눠야 한다.
	 */
	static float GetPhoneScale();

	/** 어느 가게의 줄을 보일지 (DESIGN 8.14 「가게 여럿」). 상인 액터가 넣는다. */
	void SetShopKind(EShopKind InKind);

	/** 사기 표의 이 물건을 어느 가게가 진열하나 — ID 모양으로 가른다. */
	static EShopKind GetSellerKind(FName ItemID);

	/**
	 * 이 물건이 탈것 **목줄**인가 (DESIGN 8.3·13.8 · 결정 009). 목줄·안장은 배낭에서 누를 것이 없고
	 * **잡화상 줄에서만** 동물에 붙는다 — 그 판정을 여기 한 곳에 둔다(배낭 카드 안내도 이것을 본다, M15-11).
	 */
	static bool IsMountCollar(FName ItemID);

	/** 이 물건이 탈것 **안장**인가 (`AttachSaddle`). 위 목줄과 같은 이유로 여기서 가른다. */
	static bool IsMountSaddle(FName ItemID);

	/** 이 가게가 이 물건을 사들이나 (팔기 탭). 파는 값 여부는 따로 본다. */
	bool DoesBuy(EShopKind Kind, const FItemRow& Row) const;

	/**
	 * 사기 줄을 누른 순간 — 서버 `BuyItem {itemId, count:1}` 을 묻는다.
	 * @return 요청을 띄웠으면 true. 답을 기다리는 중이거나 백엔드가 없으면 false
	 */
	UFUNCTION(BlueprintCallable, Category = "Shop")
	bool RequestBuy(FName ItemID);

	/** 팔기 줄을 누른 순간 — 서버 `SellItem {itemId, count:1}`. 반환은 `RequestBuy` 와 같다. */
	UFUNCTION(BlueprintCallable, Category = "Shop")
	bool RequestSell(FName ItemID);

	/**
	 * 「모두 팔기」 를 켠 팔기 줄 탭 — 그 종류를 스냅샷에 가진 만큼 한 번에 판다: 서버 `SellItem {itemId, count: 가진 개수}`
	 * (INBOX M22 ④). 개수는 화면이 본 스냅샷 값이고, 실제로 그만큼 있는지·얼마를 줄지는 서버가 정한다.
	 * @return 요청을 띄웠으면 true. 팔기 목록에 없는 것·대기 중·백엔드 없음이면 false
	 */
	UFUNCTION(BlueprintCallable, Category = "Shop")
	bool RequestSellAll(FName ItemID);

	/** 탈것 줄을 누른 순간 — 목줄이면 서버 `ExchangeCollar {collarItemId, speciesId}`, 안장이면 `AttachSaddle {speciesId, saddleItemId}`. */
	bool RequestMountTrade(FName ItemID, FName SpeciesID);

	/** 고구마 장수에게 묶음 사기 — 서버 `BuySweetPotato {bundles}`. 반환은 `RequestBuy` 와 같다. 값은 서버가 정한다. */
	bool RequestBuySweetPotato(int32 Bundles);

	/** 잡화상에서 고구마 팔기 — 서버 `SellSweetPotato {count}`. 값은 서버가 지금 칸으로 정한다. 반환은 `RequestBuy` 와 같다. */
	bool RequestSellSweetPotato(int32 Count);

	/** 서버 `GetSweetPotato` 를 물어 사기 줄(개당 값·남은 상한)을 맞춘다. 고구마 장수 화면만. */
	void FetchSweetPotato();

	/** `GetSweetPotato`·`BuySweetPotato` 답에서 개당 값·산 개수·상한을 읽는다. 검사도 쓴다. */
	void ApplySweetPotatoReply(const TSharedPtr<FJsonObject>& Response);

	/** 고구마 사기 줄 묶음 수 셋 (DESIGN 8.17 화면 줄). */
	static const TArray<int32>& GetPotatoBundleChoices();

	bool IsRequestPending() const { return bPending; }

	/** 마지막 거래의 결과 한 줄 (거절 사유 또는 성공). */
	const FText& GetMessage() const { return Message; }

	/** `코인 5` — 스냅샷에서만 읽는다 (`SnareBalanceHud::FormatBalanceText` 와 같은 글자). */
	FText GetCoinText() const;

	/** 한 줄 글자 — 사기 `3코인 > 기본 낚싯대 x1`, 팔기 `소고기 x2 > 1코인`. */
	FText GetLineText(const FShopLineView& Line, bool bSellLine) const;

	/** 화면 글자·줄을 지금 표·스냅샷에 맞춘다. */
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void RefreshDisplay();

	/** 2층 검사가 표 없는 상황을 흉내 낼 때 쓴다. 바뀌는 것은 화면 목록뿐이다. */
	void SetSourceTables(UDataTable* InShopTable, UDataTable* InItemTable);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void HandleBackendSnapshotChanged();

	UFUNCTION()
	void HandleOpenClicked();

	UFUNCTION()
	void HandleCloseClicked();

	UFUNCTION()
	void HandleBuyTabClicked();

	UFUNCTION()
	void HandleSellTabClicked();

	UFUNCTION()
	void HandleSellAllClicked();

	UFUNCTION()
	void HandleRetryClicked();

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> RetryBox;

	// WBP 에 같은 이름이 있으면 그걸 쓰고, 없으면 `RebuildWidget` 이 자리표시로 세운다.
	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UButton> OpenButton;

	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> ShopPanel;

	/** 제목 탭 — 판 왼쪽 위에 걸쳐 위로 튀어나오는 연초록 알약 (배낭의 진갈색 탭과 같은 규격, 색만 다름). */
	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> TitlePill;

	/** 두 칸을 가르는 세로 구분선 (시트 「구성」 4px). */
	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> ColumnDivider;

	/** 오른쪽 칸 — NPC 명함(이름 알약 + 존 + 편지체 본문). */
	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> VendorCard;

	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> VendorNamePill;

	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> VendorNameText;

	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> VendorZoneText;

	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> VendorLetterText;

	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CoinText;

	/** 코인 줄을 담은 남색 알약 — 판과 같이 접고 편다. */
	UPROPERTY(Transient)
	TObjectPtr<UBorder> CoinPill;

	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BuyTabButton;

	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UButton> SellTabButton;

	/** 팔기 탭에서만 보이는 「모두 팔기」 켬/끔 (INBOX M22 ④). */
	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UButton> SellAllButton;

	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> LineList;

	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MessageText;

	UPROPERTY(BlueprintReadOnly, Category = "Shop", meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	/** 사는 목록 (DESIGN 8.14 「사는 목록」). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UDataTable> ShopTable;

	/** 이름·파는 값 (`SellPrice`). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UDataTable> ItemTable;

	/** 가챠 소재의 이름·등급·파는 값 — 아이템 표에 행이 없는 경품 (`DT_GachaPool`). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UDataTable> GachaPoolTable;

	UPROPERTY()
	TObjectPtr<USnareBackendSubsystem> WatchedBackend;

	/** 화면을 내릴 때 풀었던 백엔드 — 같은 화면을 다시 올리면(`NativeConstruct`) 다시 묶는다(M22). */
	TWeakObjectPtr<USnareBackendSubsystem> BackendToRebind;

	/** 거래 줄마다 하나 — 줄 버튼이 자기 아이템 ID 를 들고 눌리게 한다. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USnareClickProxy>> LineProxies;

	/** 같이 여닫을 배낭 화면. 혼자 띄운 검사·WBP 에서는 비어 있다. */
	UPROPERTY(Transient)
	TObjectPtr<UBackpackScreenWidget> BackpackScreen;

private:
	bool SendTrade(const FString& FunctionName, FName ItemID, FName SpeciesID = NAME_None, int32 Count = 1);
	void AppendMountLines(TArray<FShopLineView>& Lines) const;
	const FItemRow* FindItemRow(FName ItemID) const;
	FText FindItemDisplayName(FName ItemID) const;
	/** 팔 수 있는 가챠 소재 줄(`SellPrice > 0`) — 아니면 nullptr. */
	const FGachaPoolRow* FindSellableGachaRow(FName ItemID) const;
	bool NeedsPlaceholderTree() const;
	void BuildPlaceholderTree();
	/** 거래 줄 하나 — 시트의 "가격 > 아이템 원판"(사기) / "아이템 원판 > 가격"(팔기). */
	UWidget* BuildTradeRow(int32 Index, const FShopLineView& Line, bool bSellLine);

	bool bOpen = false;

	/** 열기 카드가 붙을 월드 점. 없으면(검사·WBP) 카드는 옛 자리 그대로 보인다. */
	TOptional<FVector> PromptTarget;

	/** 마지막 투영에서 대상이 화면 안이었나 — 밖이면 닫힌 상태에서도 카드를 접는다. */
	bool bPromptOnScreen = true;
	bool bPromptSuppressed = false;
	bool bSellTab = false;
	bool bSellAllMode = false;
	bool bPending = false;
	FText Message;
	/** 서버에 닿지 못한 마지막 요청 — 비었으면 「다시 시도」 를 숨긴다 (의뢰 화면과 같은 꼴). */
	TFunction<void()> RetryRequest;
	FText VendorName;
	FText VendorZone;
	EShopKind ShopKind = EShopKind::General;

	/** 서버가 알려준 이번 일요일 개당 값(0 = 아직 모름/일요일 아님)·이번 주 산 개수·주 상한. 화면 표시용 사본이다. */
	int32 PotatoBuyPrice = 0;
	int32 PotatoBought = 0;
	int32 PotatoCap = 0;

	/** 월~토 `GetSweetPotato` 답: 지금 칸 값(0 = 모름/일요일)·지난 칸 값(0 = 첫 칸)·이번 주 산 값 합. 잡화상 팔기 줄용 사본. */
	int32 PotatoPrice = 0;
	int32 PotatoPrevPrice = 0;
	int32 PotatoPaid = 0;

	/** 스냅샷 배낭의 고구마 개수. */
	int32 CountHeldPotatoes() const;
};
