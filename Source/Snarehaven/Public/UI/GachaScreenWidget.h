#pragma once

#include "CoreMinimal.h"
#include "Backend/GachaPullResult.h"
#include "Blueprint/UserWidget.h"
#include "Data/GachaRateRow.h"

#include "GachaScreenWidget.generated.h"

class FJsonObject;
class UBorder;
class UButton;
class UCanvasPanel;
class UDataTable;
class UHorizontalBox;
class USizeBox;
class UTextBlock;
class USnareBackendSubsystem;
class UVerticalBox;
class APlayerController;
struct FGachaCostRow;
struct FGachaPoolRow;

// ROADMAP M7-04: 올가미 기계(뽑기) 화면 — **등급별 확률을 백분율로** 띄우고 1회·10연차 버튼을 둔다.
//
// **확률을 화면에 적는 것은 법적 의무다** (DESIGN 10.4 — 게임산업법 2024-03-22 시행, 미표시·오표시는
// 2년 이하 징역 또는 2000만원 이하 벌금). 그래서 이 화면의 숫자는 **코드에 한 글자도 적지 않는다** —
// `DT_GachaRates`(등급·이름·확률)와 `DT_GachaConfig`(비용)에서만 읽고, 그 표가 문서
// `docs/GACHA_DISCLOSURE.md` 와 어긋나면 M7-02 의 2층 검사가 운다. 세 곳(문서·표·화면)이 한 줄로 이어진다.
//
// **이 화면은 굴리지 않는다.** 확률 롤·천장 카운트·중복 환급은 전부 PlayFab CloudScript 몫이고
// (DESIGN 10.5 · 4.1 권위 2단 표 · CLAUDE.md 8), 클라이언트는 표를 **보여주는 데만** 쓴다.
// 버튼은 `RollGacha`(M7-06) 를 **부르기만** 하고, 결과 줄은 그 응답에서만 생긴다 — 누른 순간에는 화면이 안 바뀐다.
// 클라이언트가 스스로 굴려 결과를 만드는 순간 그건 치트 경로고, 표시한 확률과 다른 결과를 줄 수 있다.
UCLASS()
class SNAREHAVEN_API UGachaScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UGachaScreenWidget(const FObjectInitializer& ObjectInitializer);

	/**
	 * 한 등급 줄에 적히는 글자 — `커먼 70.0%`. 소수 한 자리는 공개 문서(`docs/GACHA_DISCLOSURE.md`)의
	 * 표기 그대로다(`70.0%`·`6.5%`) — 반올림해서 `7%` 로 적으면 그것이 곧 오표시다.
	 * @param Grade 볼 등급
	 * @return 그 줄의 글자. 표에 행이 없거나 확률이 자리표시(-1)면 숫자 대신 `-` (Verification.md 7)
	 */
	UFUNCTION(BlueprintPure, Category = "Gacha")
	FText GetRateLineText(EGachaGrade Grade) const;

	/** 젬 1회 뽑기 버튼 글자 — `1회 뽑기` + 젬 비용 (DESIGN 10.2). */
	UFUNCTION(BlueprintPure, Category = "Gacha")
	FText GetSinglePullButtonText() const;

	/** 코인 1회 뽑기 버튼 글자 — `1회 뽑기` + 코인 비용 (DESIGN 10.2 "또는 스네어 코인"). 재화마다 버튼을 갈랐다. */
	UFUNCTION(BlueprintPure, Category = "Gacha")
	FText GetCoinPullButtonText() const;

	/** 아이템별 확률 한 줄 — `펫 리본 목줄 10.0%` (M7-07 · 공개 문서 "아이템별 확률"). 미정이면 `-`. */
	FText GetPoolLineText(const FGachaPoolRow& Row) const;

	/**
	 * 서버에 뽑기를 부탁한다 — `RollGacha {currency, rollCount}`. 답이 오기 전에는 결과 칸도 잔액도 안 바뀐다.
	 * 받아들여지면 응답의 `results` 를 그대로 걸고 스냅샷을 다시 받는다(잔액은 그 길로만 바뀐다).
	 * @return 부탁을 보냈으면 true (이미 하나가 날아가는 중이거나 백엔드가 없으면 false)
	 */
	bool RequestPull(const FString& Currency, int32 RollCount);

	/**
	 * 뽑기 판을 전체 화면 판 Z 로 띄운다 — 배낭 장비 탭 「올가미 기계」(사람 입구)와 `-SnareGachaScreen`(검사)이 같이 쓴다.
	 * 도감(`UCodexScreenWidget::OpenOnViewport`)과 같은 꼴.
	 */
	static UGachaScreenWidget* OpenOnViewport(APlayerController* Owner, UWorld* World, USnareBackendSubsystem* InBackend);

	/** 「닫기」 단추·ESC 와 같은 길 — 떼어 낸다(`FSnareScreenCloser` 가 뽑기 판을 닫는 방법 그대로). */
	UFUNCTION(BlueprintCallable, Category = "Gacha")
	void Close() { RemoveFromParent(); }

	/** 검사·폰 연결용. 비우면 `RebuildWidget` 이 게임 인스턴스에서 찾는다. */
	void BindToBackend(USnareBackendSubsystem* InBackend) { Backend = InBackend; }

	/**
	 * `RollGacha` 응답의 `results` 를 결과 줄로 옮긴다. 등급 문자열은 `EGachaGrade` 이름과, 이름은 풀 표와
	 * 맞대기만 한다 — 모르는 등급이 하나라도 있으면 false (커먼으로 떨어뜨려 적으면 오표시다).
	 */
	bool ParsePullResponse(const TSharedPtr<FJsonObject>& Response, TArray<FGachaPullResult>& OutResults) const;

	/** 결과 칸 위 한 줄 — 서버가 거절한 이유(`재화 부족` 등)를 그대로. */
	FText GetStatusText() const { return Status; }

	/** 스냅샷을 다시 받은 횟수 — `FetchPlayerState` 는 로그인 전엔 조용히 끝나서 2층이 이걸로 센다. */
	int32 GetRefetchCount() const { return RefetchCount; }

	/** 서버 성공 답의 결과 줄을 건 뒤 소리를 낸 횟수·마지막 이름 — 상자 열기 `CrateOpen` 을 빌린다 (M45-04, 2층 검사가 본다). */
	int32 GetConfirmSoundCount() const { return ConfirmSoundCount; }
	FName GetLastConfirmSoundCue() const { return LastConfirmSoundCue; }

	/** 10연차 버튼 글자 — `10연차` + 젬 비용. 10연차는 코인으로 살 수 없다 (DESIGN 10.2 에 코인 값이 없다). */
	UFUNCTION(BlueprintPure, Category = "Gacha")
	FText GetTenPullButtonText() const;

	/**
	 * 명함 본문 밑 천장 줄 — `{N}회 안에 레전더리 하나` (M15-18 · 시트 오른쪽 칸 "천장·중복 환급 설명").
	 * 숫자는 `DT_GachaConfig.PityPullCount` 에서만 읽고, 없으면 `-` 다 (없는 보장을 적으면 그것이 오표시다).
	 */
	UFUNCTION(BlueprintPure, Category = "Gacha")
	FText GetPityLineText() const;

	/** 명함 편지체 본문 — 우리가 새로 쓴 글이다 (Reference.md 5). 숫자는 한 글자도 안 적는다. */
	static FText GetMachineLetter();

	/**
	 * 한 결과 줄에 적히는 글자 — `레전더리 황금 올가미`, 중복이면 `커먼 밀짚모자 (중복 코인 50 환급)`.
	 * **등급 이름은 `DT_GachaRates` 에서 읽는다** — 위 확률 줄과 같은 글자여야 한 화면으로 읽힌다.
	 * 등급과 이름 사이를 기호 없이 띄어 쓰는 것도 확률 줄(`커먼 70.0%`)과 같은 짜임이다
	 * (구분 기호로 `-` 를 쓰면 미정 표시 `-` 와 섞여 읽을 수 없다 — `SnareBalanceHud` 와 같은 이유).
	 * @param Result 백엔드가 준 결과 한 줄
	 * @return 그 줄의 글자. 모르는 칸(이름 없음·환급 양 음수)은 숫자 대신 `-` (Verification.md 7)
	 */
	UFUNCTION(BlueprintPure, Category = "Gacha")
	FText GetResultLineText(const FGachaPullResult& Result) const;

	/**
	 * 백엔드가 준 뽑기 결과를 화면에 건다. **여기서 굴리지도, 잔액을 건드리지도 않는다** —
	 * 결과도 환급도 이미 CloudScript 가 확정한 값이고 이 함수는 글자로 옮길 뿐이다
	 * (DESIGN 10.5 · CLAUDE.md 8). 잔액 표시가 따라가는 길은 백엔드 스냅샷 하나뿐이다(M7-03).
	 * @param Results 결과 줄들 (1회면 하나, 10연차면 열). 비우면 결과 칸이 사라진다
	 */
	UFUNCTION(BlueprintCallable, Category = "Gacha")
	void ShowPullResults(const TArray<FGachaPullResult>& Results);

	/** 화면 글자를 표에 맞춘다. 표는 뽑는 중에 바뀌지 않으니 화면을 세울 때 한 번 부른다. */
	UFUNCTION(BlueprintCallable, Category = "Gacha")
	void RefreshDisplay();

	/**
	 * 화면이 읽을 표를 바꾼다. 평소에는 생성자가 잡아 둔 `DT_GachaRates`·`DT_GachaConfig` 그대로고,
	 * 2층 검사가 **표가 없는 상황**(포장에서 에셋을 빠뜨린 사고)을 흉내 낼 때 쓴다.
	 * WBP 가 이미 같은 칸을 갈아 끼울 수 있으니(위 `EditDefaultsOnly`) 새로 열리는 문은 없다 —
	 * 그리고 이 문으로 바뀌는 것은 **화면 글자뿐**이다. 뽑기 판정도 잔액도 여기서 움직이지 않는다
	 * (CLAUDE.md 8 · DESIGN 10.5).
	 * @param RateTable 등급·확률 표. nullptr 이면 등급 줄이 `-` 로 돌아간다
	 * @param ConfigTable 비용 표. nullptr 이면 버튼 비용이 `-` 로 돌아간다
	 * @param PoolTable 뽑기 풀 표(M7-07). nullptr 이면 아이템별 확률이 `-` 한 줄로 돌아간다
	 */
	UFUNCTION(BlueprintCallable, Category = "Gacha")
	void SetSourceTables(UDataTable* RateTable, UDataTable* ConfigTable, UDataTable* PoolTable = nullptr);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	// 아래 넷은 WBP 에서 같은 이름으로 만들면 그걸 쓰고, 없으면 RebuildWidget 이 자리표시로 만들어 넣는다
	// (`UPauseMenuWidget` 과 같은 규칙 — 디자인이 없어도 3층 캡처가 찍혀야 한다).
	UPROPERTY(BlueprintReadOnly, Category = "Gacha", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	// 아래 다섯은 M15-18 시트(`docs/reference/shop_vendor.md`) 규격 — 두 칸 판·제목 탭·오른쪽 명함.
	UPROPERTY(BlueprintReadOnly, Category = "Gacha", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> TitlePill;

	UPROPERTY(BlueprintReadOnly, Category = "Gacha", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> ColumnDivider;

	UPROPERTY(BlueprintReadOnly, Category = "Gacha", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> MachineCard;

	UPROPERTY(BlueprintReadOnly, Category = "Gacha", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> MachineNamePill;

	UPROPERTY(BlueprintReadOnly, Category = "Gacha", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MachineNameText;

	UPROPERTY(BlueprintReadOnly, Category = "Gacha", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MachinePityText;

	UPROPERTY(BlueprintReadOnly, Category = "Gacha", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MachineLetterText;

	/** 등급 줄이 담기는 세로 상자. 줄 순서는 `EGachaGrade` 차례(커먼 → 레전더리)다. */
	UPROPERTY(BlueprintReadOnly, Category = "Gacha", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> RateList;

	UPROPERTY(BlueprintReadOnly, Category = "Gacha", meta = (BindWidgetOptional))
	TObjectPtr<UButton> SinglePullButton;

	UPROPERTY(BlueprintReadOnly, Category = "Gacha", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TenPullButton;

	UPROPERTY(BlueprintReadOnly, Category = "Gacha", meta = (BindWidgetOptional))
	TObjectPtr<UButton> CoinPullButton;

	/** 아이템별 확률 줄이 담기는 세로 상자 (M7-07). 줄 순서는 풀 표 순서 = 공개 문서 순서. */
	UPROPERTY(BlueprintReadOnly, Category = "Gacha", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> PoolList;

	/** 「닫기」 — 판 오른쪽 위(상점 판과 같은 자리), 모든 판 공용 `SnareUiStyle::MakeCloseButton` 모양. */
	UPROPERTY(BlueprintReadOnly, Category = "Gacha", meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	/** 서버가 거절한 이유 한 줄. */
	UPROPERTY(BlueprintReadOnly, Category = "Gacha", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	/**
	 * 뽑은 결과가 한 줄씩 쌓이는 세로 상자 (M7-05). 뽑기 전에는 비어 있다 —
	 * 빈 줄이라도 미리 세워 두면 뽑지 않았는데 뽑은 것처럼 보인다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Gacha", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> ResultList;

	/** 등급·이름·확률의 원본 (M7-01). 화면에 뜨는 확률은 전부 여기서 온다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gacha")
	TObjectPtr<UDataTable> GachaRateTable;

	/** 비용·천장의 원본 (M7-01). 버튼에 적히는 젬·코인 값이 여기서 온다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gacha")
	TObjectPtr<UDataTable> GachaConfigTable;

	/** 뽑기 풀 (M7-07). 아이템별 확률 줄과 결과 줄의 아이템 이름이 여기서 온다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gacha")
	TObjectPtr<UDataTable> GachaPoolTable;

	/** 거절 code → 화면 문구 (M26-02, 목장과 같은 `DT_RanchFailureTable`). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gacha")
	TObjectPtr<UDataTable> FailureTable;

private:
	UFUNCTION()
	void PressSinglePull();

	UFUNCTION()
	void PressCoinPull();

	UFUNCTION()
	void PressTenPull();

	/** 서버에 닿지 못한 뽑기를 같은 재화·횟수로 다시 보낸다 (의뢰 화면과 같은 꼴). */
	UFUNCTION()
	void HandleRetryClicked();

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> RetryBox;

	/** 서버에 닿지 못한 마지막 뽑기 — 비었으면 「다시 시도」 를 숨긴다. */
	TFunction<void()> RetryRequest;

	/** 아이템별 확률 줄을 풀 표에 맞춘다. */
	void RefreshPoolList();

	void SetStatus(const FText& Text);

	UPROPERTY()
	TObjectPtr<USnareBackendSubsystem> Backend;

	FText Status;
	int32 RefetchCount = 0;
	int32 ConfirmSoundCount = 0;
	FName LastConfirmSoundCue;
	bool bPullInFlight = false;

	/** 그 등급의 행. 없으면 nullptr — 화면은 그 줄을 `-` 로 둔다. */
	const FGachaRateRow* FindRateRow(EGachaGrade Grade) const;

	/** 비용 행(기계가 하나라 행도 하나다). 없으면 nullptr. */
	const FGachaCostRow* FindCostRow() const;

	/** 등급 줄 수를 `EGachaGrade` 항목 수에 맞춘다 (`URanchHudWidget::EnsureSlotRows` 와 같은 방식). */
	void EnsureRateRows(int32 GradeCount);

	/** WBP 트리가 아직 비었는지 (루트가 없거나, 자식 없는 빈 패널 하나뿐인지). */
	bool NeedsPlaceholderTree() const;

	/** WBP 에 디자인이 없을 때만 부른다. 엔진 기본 위젯으로 최소 트리를 세운다. */
	void BuildPlaceholderTree();

	/**
	 * 뽑기 버튼 한 줄을 세로 상자에 붙인다 — 시트의 **사는-것 규격**(`가격 > 아이템`)이라 버튼 안이
	 * 글자 하나가 아니라 `가격(초록) · > · 이름` 세 조각이다.
	 */
	UButton* AddPullRow(UVerticalBox& Column, FName Name);

	/** 그 줄의 가격·이름 글자를 지금 표 값으로 맞춘다. WBP 가 글자 하나로 만들어 두었으면 한 줄 통째로. */
	void UpdatePullRow(UButton* Button, const FText& Price, const FText& Label);
};
