#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/SnareAchievementNotice.h"
#include "RanchHudWidget.generated.h"

class AZonePortalActor;
class UBorder;
class ARanchSlotActor;
class ASnarehavenCharacter;
class UButton;
class UDataTable;
class UInventoryComponent;
class URanchComponent;
class URanchInteractComponent;
class URanchSyncComponent;
class UCoopReportComponent;
class UHorizontalBox;
class UImage;
class USizeBox;
class UTexture2D;
class USnareBackendSubsystem;
class UTextBlock;
class USnareNoticeText;
class USnareNoticeCards;
class UVerticalBox;
class UZoneTravelComponent;

// ROADMAP M2-06a: 목장 화면 HUD — 칸마다 **미수확 개수 / 상한**과 수확 준비(Ready) 알림 아이콘
// (DESIGN 3.2 상태 머신의 `[Ready] … UI에 알림 아이콘`).
//
// `UHuntHudWidget` 과 같은 모양을 일부러 그대로 쓴다 — 갱신은 타이머가 아니라 알림으로만
// (`URanchComponent::OnRanchCacheChanged`), 보이는 모양은 WBP 가 비어 있을 때만 C++ 자리표시로.
//
// **여기서 남은 시간을 계산하지 않는다.** `now - OccupiedSinceUtc` 를 나누는 순간 기기 시계로 재화를
// 만드는 길이 열린다 (CLAUDE.md 8 · DESIGN 4.1 권위 표). 남은 시간 표시는 백엔드가 그 값을 스냅샷에
// 실어 주는 M2-01·M2-03 뒤의 일이다. 지금 화면에 나오는 것은 **백엔드가 확인해 준 `Stock`** 뿐이다.
UCLASS()
class SNAREHAVEN_API URanchHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	URanchHudWidget(const FObjectInitializer& ObjectInitializer);

	/**
	 * 어떤 플레이어의 목장을 보여줄지 정한다. 뷰포트에 올린 쪽(`ASnarehavenCharacter`)이 부르고,
	 * 2층 검사는 월드를 띄우지 않고 직접 부른다 (Verification.md 6).
	 * 이전에 듣던 목장의 알림은 끊는다.
	 * @param Character 볼 플레이어. nullptr 이면 HUD 를 비운다.
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void BindToCharacter(ASnarehavenCharacter* Character);

	/** 지금 보여주는 목장 캐시. 안 붙었으면 nullptr. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	URanchComponent* GetWatchedRanch() const { return WatchedRanch; }

	/**
	 * 그 칸이 수확 준비 상태인가 — 백엔드가 보내 준 `Stock` 이 1개 이상이면 Ready 다 (DESIGN 3.2).
	 * 경과 시간으로 판단하지 않는다.
	 * @param SlotIndex 칸 번호 (0~4)
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool IsSlotReady(int32 SlotIndex) const;

	/** 수확할 게 쌓인 칸 수. 화면 위 알림 아이콘이 이 수를 쓴다. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	int32 GetReadySlotCount() const;

	/**
	 * 그 칸에 배치된 동물의 미수확 누적 상한 (`DT_RanchProductTable.MaxStack`). 작물 칸은 상한이 없다(⑩).
	 * @return 상한. 빈 칸·작물 칸이거나 표에 행이 없으면 -1 (미정 취급 — 화면에 숫자 대신 `-` 를 쓴다)
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	int32 GetSlotMaxStack(int32 SlotIndex) const;

	/** 한 칸에 적히는 글자 (`비어 있음` / `3/5` / 작물 칸은 `자라는 중`·`다 자람` / Ready 면 앞에 아이콘). 빈 칸도 줄은 남는다. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetSlotText(int32 SlotIndex) const;

	/** 칸 카드 제목 — 든 것의 이름(변종 칸이면 `변종 ○○`, M21-07c2), 빈 칸·이름 모름이면 `n번 칸`. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetSlotCardTitle(int32 SlotIndex) const;

	/**
	 * 고르기 줄에 적히는 글자 (M2-06b · ⑩) — `배치할 것: 돼지 (1/2)`. 펫·씨앗이 없으면 `배치할 것: 없음`.
	 * 괄호 안은 "선택지 중 몇 번째" 이지 개수가 아니다 (개수는 인벤토리 화면의 몫).
	 * 씨앗을 골랐는데 라이브에 `PlantCrop` 이 없으면 끝에 `(서버 준비 중)` 이 붙는다.
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetPetPickerText() const;

	/** 고를 펫·씨앗이 있는가 — 없으면 버튼을 꺼 둔다 (숨기지 않는다, 사냥 HUD 포획 버튼과 같은 이유). */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool IsPetPickerEnabled() const;

	/** 칸 카드 「넣기/수확」 단추를 켜는가 — 빈 칸인데 넣을 동물·씨앗이 없으면 끈다 (M106-03). */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool IsSlotActionEnabled(int32 SlotIndex) const;

	/** 넣기 판에 「동물 / 씨앗」 탭을 세우는가 (M28-08) — 둘 다 가졌을 때만. 한쪽뿐이면 탭 없이 고르기 줄만. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool ArePlacementTabsShown() const;

	/** 탭 버튼과 같은 일 — 그 탭의 첫 것을 고른다(이미 그 탭이면 그대로). 칸은 건드리지 않는다 (CLAUDE.md 8). */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void SelectPlacementTab(bool bSeedTab);

	/**
	 * 재화 잔액 줄이 볼 백엔드 캐시를 정한다 (M7-03). `BindToCharacter` 가 게임 인스턴스에서 찾아
	 * 부르고, 2층 검사는 게임 인스턴스가 없는 월드에서 도니 직접 부른다 (사냥 HUD 와 같은 길).
	 * **읽기만 한다** — 이 위젯에 잔액을 바꾸는 길은 없다 (CLAUDE.md 8).
	 * @param Backend 볼 백엔드. nullptr 이면 잔액 줄이 `-` 로 돌아간다.
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void BindToBackend(USnareBackendSubsystem* Backend);

	/**
	 * 목장 유지·명성 줄 (M12-05) — `목장 유지 12시간 남음, 명성 30, 칸 5/5` 모양 (가운뎃점은 Jua 글꼴에 없을 수 있어 쉼표).
	 * **숫자는 전부 백엔드 스냅샷(`URanchComponent::GetUpkeep`) 그대로다** — 시계로 남은 시간을 줄이지 않는다.
	 * 스냅샷을 아직 못 받았으면 빈 FText.
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetUpkeepText() const;

	/**
	 * 같이 하기 줄 (M21-06b2) — 서버 `ReportCoop` 답이 `coop` 일 때만 `같이 하기 - 생산 10% 빨라짐`, 아니면 빈 FText.
	 * 유지 줄 아래 둘째 줄로 붙는다. 숫자를 세지 않는다 — 서버 답 그대로다 (CLAUDE.md 8).
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetCoopText() const;

	/** 같이 하기 줄이 볼 보고 컴포넌트. `BindToCharacter` 가 폰에서 찾아 부르고, 2층 검사는 직접 부른다. */
	void BindToCoopReport(UCoopReportComponent* Coop);

	/**
	 * 나무 넣기·칸 확장 버튼과 실패 문구가 쓸 요청 컴포넌트 (M12-05). `BindToCharacter` 가 폰에서 찾아 부르고,
	 * 2층 검사는 직접 부른다. nullptr 이면 두 버튼이 꺼진다.
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void BindToRanchSync(URanchSyncComponent* Sync);

	/** `나무 넣기 (12개)` — 괄호 안은 가방(백엔드 스냅샷)의 유지비 아이템 수. 모르면 `나무 넣기`. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetFeedUpkeepButtonText() const;

	/** 요청 중이 아니고 넣을 나무가 1개 이상이면 켠다. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool IsFeedUpkeepEnabled() const;

	/**
	 * 나무 넣기 버튼과 같은 일 — 가진 나무 **전부**를 `FeedRanchUpkeep` 에 싣는다(서버가 남은 여유만큼 자른다).
	 * @return 서버에 물었으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	bool TryFeedUpkeep();

	/** `칸 확장 - 명성 50, 코인 500` / `칸 최대` / 모르면 `칸 확장`. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetExpandSlotButtonText() const;

	/** 요청 중이 아니고 다음 비용을 알면 켠다 (명성·코인이 모자라도 켠다 — 판정은 서버, 거절 문구로 알린다). */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool IsExpandSlotEnabled() const;

	/** 칸 확장 버튼과 같은 일. @return 서버에 물었으면 true */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	bool TryExpandSlot();

	/**
	 * 펫 빼기 버튼 글자 (#0198) — `펫 빼기 (2번 칸)`. 하이라이트된 칸이 없으면 `펫 빼기`.
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetRemovePetButtonText() const;

	/** 하이라이트된 칸이 펫이 선 동물 칸이고 그 칸에 대기 요청이 없으면 켠다. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool IsRemovePetEnabled() const;

	/** 요청이 나갔으면 손 뻗는 몸짓. */
	void PlayHandActionIf(bool bSent) const;

	/**
	 * 확인 줄 「빼기」와 같은 일(#0199 부터 펫 빼기 버튼은 확인 줄만 편다) — 하이라이트된 칸에 `RemovePet` 요청을 쌓는다. 칸이 비는 것은 서버 답 뒤
	 * 다시 받은 스냅샷이다 (CLAUDE.md 8). @return 요청을 쌓았으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	bool TryRemovePet();

	/**
	 * 펫 빼기 확인 줄이 펴져 있는지 (#0199). 「펫 빼기」는 이 줄만 펴고, 줄의 「빼기」를 눌러야 `TryRemovePet` 이다.
	 * 「취소」·대상 칸이 바뀜·뺄 수 없게 됨이면 접힌다.
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool IsRemovePetConfirmOpen() const { return bRemovePetConfirmOpen; }

	/**
	 * 꾸미기 고르기 선택지 (DESIGN 10.9) — 배낭에 있는 목장 꾸미기 + 씌운 게 있으면 맨 끝에 NAME_None(벗기기).
	 * 하이라이트된 칸이 펫이 선 동물 칸이 아니면 빈 배열.
	 */
	TArray<FName> GetPetCosmeticOptions() const;

	/** 지금 고른 꾸미기 (NAME_None = 벗기기 또는 선택지 없음). */
	FName GetSelectedPetCosmetic() const;

	/** 꾸미기 고르기 버튼 글자 — `꾸미기: 동물 리본 (1/3)` · `꾸미기: 벗기기 (2/2)` · `꾸미기: 없음`. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetPetCosmeticPickerText() const;

	/** 다음 꾸미기로 돌린다 — 고르기만 바뀌고 칸은 그대로다. */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void CyclePetCosmetic();

	/** 고른 꾸미기가 지금 것과 다르고 그 칸에 대기 요청이 없으면 켠다. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool IsApplyPetCosmeticEnabled() const;

	/**
	 * 「씌우기」(벗기기) 버튼과 같은 일 — `SetPetCosmetic` 요청만 쌓는다. 몸에 보이는 것도 카드 글자도 서버 답 뒤
	 * 다시 받은 스냅샷이 바꾼다 (CLAUDE.md 8). @return 요청을 쌓았으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	bool TryApplyPetCosmetic();

	/** 지금 띄운 수확 팝업 글 (M21-03). 서버가 수확을 확정한 뒤에만 찬다. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetHarvestPopupText() const { return HarvestPopup; }

	/** 수확 팝업이 떠 있는 시간(초) — DESIGN 3.2 「수확 팝업」 줄. */
	static float GetHarvestPopupSeconds();

	/** 수확 알림 카드를 Seconds 초 띄운다 (M23-03). 서버가 확정한 수확 답의 글·아이콘만 넣는다 (CLAUDE.md 8). */
	void ShowHarvestCard(const FText& Title, const FText& CoinLine, const TSoftObjectPtr<UTexture2D>& Icon, float Seconds);

	/** 지금 띄운 실패 문구. 없거나 접혔으면 빈 FText. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetFailureNoticeText() const { return FailureNotice; }

	/** 거절 줄에 한 줄 띄운다 — 서버 거절과 같은 줄·같은 시간. 거점엔 사냥 HUD 가 없어 배낭 판 결과 글도 여기로 온다 (#0518). */
	void ShowNotice(const FText& Message);

	/** 거절·알림 줄이 지금 펴져 있나 (접히면 false) — `GetFailureNoticeText` 는 접혀도 마지막 글을 들고 있다. */
	bool IsFailureNoticeShown() const;

	/** 업적·도감 알림 카드 두 장 (M48-03c) — 자리표시 트리가 세운다. WBP 디자인이면 nullptr. */
	USnareNoticeCards* GetAchievementCards() const { return AchievementCards; }

	/** 잔액 줄 글자 — `코인 120` / `젬 3` 두 줄. 아직 백엔드에게 못 들었으면 숫자 대신 `-`. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetBalanceText() const;

	/**
	 * 포탈 버튼을 띄울 상태인지 (M6-03b). 판정·글자는 사냥 HUD 와 **같은 곳**이 정한다
	 * (`SnareZoneTravelHud`) — 목장에서 사냥터로 돌아가는 길이 이 버튼이다.
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool IsZoneTravelButtonVisible() const;

	/** 포탈 버튼 글자 — 갈 곳 이름은 표(`DT_ZoneTable`)가 정한다. 없으면 빈 FText. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetZoneTravelButtonText() const;

	/**
	 * 포탈 버튼을 탭한 것과 같은 일을 한다. 화면 버튼도 2층 검사도 이 길 하나만 쓴다.
	 * @return 호스트에서 실제로 떠났으면 true (게스트는 요청만 보내고 false)
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	bool TryZoneTravel();

	/** 화면에 보이는 위젯을 실제 캐시에 맞춘다. 알림을 받을 때마다 부른다. */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void RefreshDisplay();

	/**
	 * 처음 다가가기 안내 한 줄 (M28-05c2, `docs/specs/onboarding-usage-hints.md`) — 사냥 HUD 가 없는 `L_Ranch` 에서
	 * 목장 칸(수확·넣기)·잡화점 앞에 처음 서면 칸 카드 위에 크림 알약. 키 고르기·막힘·본 목록은 사냥 HUD 와 같은
	 * `SnareUsageHints` 라 한 기기에서 어느 화면에서 봤든 두 번 안 뜬다. 화면만 — 서버·재화와 무관 (CLAUDE.md 8).
	 */
	FName GetUsageHintKey() const { return UsageHintKey; }

	/** 지금 떠 있는 안내 글(표 `Line`). 없으면 빈 FText. */
	FText GetUsageHintText() const { return UsageHintLine; }

	/** 안내 알약이 보이는가. */
	bool IsUsageHintVisible() const;

	/** 다가가기 키를 다시 본다 — 틱이 0.2초마다 부르고, 2층은 직접 부른다. 키가 지난번과 바뀐 순간만 띄우려 한다. */
	void CheckUsageHintForApproach();

	/** 떠 있는 시간을 흘린다(막혀 있는 동안은 안 흐름). 틱이 부르고, 2층은 직접 부른다. */
	void TickUsageHint(float DeltaSeconds);

	/** 본 키를 적을 ini 섹션 — 2층은 `SnareUsageHints::TestSection` 을 넘긴다. */
	void SetUsageHintSection(const TCHAR* Section) { UsageHintSection = Section; }

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;

	// 둘 다 WBP 에서 같은 이름으로 만들면 그걸 쓰고, 없으면 RebuildWidget 이 자리표시로 만들어 넣는다
	// (`UHuntHudWidget` 과 같은 이유 — 디자인 전에도 3층 캡처가 찍혀야 한다).
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ReadyNoticeText;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> SlotList;

	// 목장 유지·명성 줄 (M12-05). 칸 목록 바로 위.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> UpkeepText;

	// 재화 잔액(M7-03)은 **오른쪽 위** — 자리와 문구는 `SnareBalanceHud` 가 정한다(사냥 HUD 와 한 벌).
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BalanceText;

	// 펫 고르기 (M2-06b). 새 입력 액션을 만들지 않고 화면 버튼 하나로 종을 돌린다 —
	// 자동 상호작용 버튼을 종류마다 늘리지 않는다는 선을 지킨다 (DESIGN 3.2).
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> PetPickerButton;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PetPickerText;

	// 넣기 판 「동물 / 씨앗」 탭 (M28-08) — 고르기 줄 바로 위. 고른 탭은 강조색, 한쪽만 가졌으면 줄째 접힘.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> PlacementTabRow;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> PlacementAnimalTabButton;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> PlacementSeedTabButton;

	// 포탈 버튼(M6-03b)은 오른쪽 아래 작은 둥근 상황 버튼 (M15-03). 버튼 한 벌 자체는 `SnareZoneTravelHud` 가 만든다.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> ZoneTravelButton;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ZoneTravelButtonText;

	// 나무 넣기·칸 확장 (M12-05) — 유지 줄 바로 아래 가로 한 줄.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> FeedUpkeepButton;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FeedUpkeepButtonText;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> ExpandSlotButton;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ExpandSlotButtonText;

	// 펫 빼기 (#0198) — 고르기 버튼 바로 아래. 하이라이트된 칸의 펫을 뺀다.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> RemovePetButton;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RemovePetButtonText;

	// 펫 빼기 확인 줄 (#0199) — 펫 빼기 버튼 바로 아래 「정말 뺄까요? [빼기] [취소]」. 평소엔 접힘.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> RemovePetConfirmRow;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> RemovePetConfirmButton;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> RemovePetCancelButton;

	// 동물 꾸미기 줄 (DESIGN 10.9) — 「꾸미기: ○○ (1/3)」 고르기 + 「씌우기」. 펫이 선 동물 칸에서만 보인다.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> PetCosmeticRow;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> PetCosmeticPickerButton;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PetCosmeticPickerText;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> PetCosmeticApplyButton;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PetCosmeticApplyText;

	// 목장 서버 함수 거절 문구 (M12-05) — 위 가운데, 수확 알림 아래. 잠깐 떴다 접힌다.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<USnareNoticeText> FailureNoticeText;

	// 거점 상태(GetBaseState)를 못 받았을 때 거절 문구 아래 「다시 시도」 (의뢰 화면과 같은 꼴).
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> RetryBox;

	// 수확 팝업 (M21-03 → M23-03) — 왼쪽 위 알림 줄의 크림 카드 제목 `+1 송로버섯`. 잠깐 떴다 카드째 접힌다.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<USnareNoticeText> HarvestPopupText;

	/** 업적·도감 알림 카드 두 장 (M48-03c) — 왼쪽 위 알림 줄 셋째 칸. */
	UPROPERTY()
	TObjectPtr<USnareNoticeCards> AchievementCards;

	// 수확 알림 카드 (M23-03) — 시트 「왼쪽 위 알림 카드 300×70, 왼쪽 아이콘 48, 제목 18 + 설명 14」.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> HarvestCard;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> HarvestIcon;

	// 카드 설명 줄 `+10 코인` — 코인이 없는 수확이면 접힌다.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HarvestCoinText;

	// 칸 카드 (M15-03) — 칸 곁에 섰을 때만 뜨는 작은 크림 카드 하나. 이름·상태·수확 버튼과
	// 넣기·빼기·유지 버튼이 전부 이 안에 있다(레퍼런스: 대상 곁 프롬프트 하나, 평소 화면은 비어 있다).
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> SlotCard;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SlotCardTitle;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SlotCardStatus;

	// 찬 칸이면 「수확」, 빈 칸이면 「넣기」 — 상호작용 키와 **같은 함수**(`TryInteractNearest`)를 부른다.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> SlotHarvestButton;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SlotHarvestButtonText;

	// 수확 알림 카드 (M15-03) — 왼쪽 위 크림 카드. 알림이 없으면 접힌다.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> ReadyNoticeCard;

	// 처음 다가가기 안내 알약 (M28-05c2) — 아래 가운데, 칸 카드가 떠 있으면 그 바로 위. 사냥 HUD `UsageHint` 와 같은 크림 알약.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> UsageHint;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> UsageHintText;

	/** 칸마다 상한이 몇인지 적힌 표 (M2-02). 상한 숫자를 코드에 적지 않기 위해서다 (DataDriven.md 3). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HUD")
	TObjectPtr<UDataTable> RanchProductTable;

private:
	/** 캐시가 바뀌었다 — 백엔드 스냅샷이 들어온 순간이다. */
	UFUNCTION()
	void HandleRanchCacheChanged();

	/** 보유가 바뀌었다 — 가진 펫 목록이 달라지므로 고르기 줄을 다시 그린다. */
	UFUNCTION()
	void HandleInventoryCacheChanged();

	/** 업적 달성·도감 새 칸 알림 카드를 왼쪽 위 알림 카드에 띄운다 (M48-03). */
	void HandleAchievementNotice(const TArray<SnareAchievementNotice::FCard>& Cards);

	/** 백엔드가 새 잔액을 보내 줬다 — 화면 숫자가 바뀌는 **유일한** 계기다 (CLAUDE.md 8). */
	UFUNCTION()
	void HandleBackendSnapshotChanged();

	/** 플레이어가 다음 펫·씨앗으로 돌렸다. */
	UFUNCTION()
	void HandleSelectedPetChanged(FName NewPetAnimalID);

	/** 빈 칸을 눌렀는데 넣을 펫·씨앗이 없다 — 거절 줄에 한 줄. */
	UFUNCTION()
	void HandleNothingToPlace();

	/** 고르기 버튼을 눌렀다 — 고르기만 바뀌고 칸은 그대로다 (CLAUDE.md 8). */
	UFUNCTION()
	void HandlePetPickerClicked();

	UFUNCTION()
	void HandlePlacementAnimalTabClicked();

	UFUNCTION()
	void HandlePlacementSeedTabClicked();

	/** 탭 줄을 폈다 접고 고른 탭을 강조한다. `ApplyUiStyle` 이 버튼을 기본색으로 칠한 **뒤에** 부른다. */
	void RefreshPlacementTabs();

	/** 하이라이트된 포탈이 바뀌었다 — 포탈 버튼을 폈다 접는다 (사냥 HUD 와 같은 배선). */
	UFUNCTION()
	void HandleZonePortalTargetChanged(AZonePortalActor* NewTarget);

	/** 포탈 버튼을 눌렀다 — 맵을 여는 것은 여기가 아니라 호스트다 (DESIGN 4.1 권위 표). */
	UFUNCTION()
	void HandleZoneTravelClicked();

	/** 목장 서버 함수 답이 왔다 — 거절이면 문구를 잠깐 띄운다. 유지·칸 숫자는 다시 받은 스냅샷이 바꾼다. */
	UFUNCTION()
	void HandleRanchRequestAnswered(const FString& FunctionName, bool bAccepted, const FString& FailureCode,
		const FText& Message);

	UFUNCTION()
	void HandleRetryClicked();

	UFUNCTION()
	void HandleRemovePetClicked();

	UFUNCTION()
	void HandleRemovePetConfirmClicked();

	UFUNCTION()
	void HandleRemovePetCancelClicked();

	UFUNCTION()
	void HandlePetCosmeticPickerClicked();

	UFUNCTION()
	void HandlePetCosmeticApplyClicked();

	/** 하이라이트된 칸이 바뀌었다 — 펫 빼기 버튼이 가리키는 칸이 달라진다. */
	UFUNCTION()
	void HandleSlotTargetChanged(ARanchSlotActor* NewTarget);

	/** 하이라이트된 칸 번호. 없으면 INDEX_NONE. */
	int32 GetTargetSlotIndex() const;

	/** 칸 카드의 수확(빈 칸이면 넣기) 버튼 — 요청만 쌓는다. 지급은 서버 답 뒤 스냅샷이 한다 (CLAUDE.md 8). */
	UFUNCTION()
	void HandleSlotHarvestClicked();

	/** 칸 카드를 대상 칸에 맞춰 채우고 폈다 접는다. */
	void RefreshSlotCard();

	/** 같이 하기 답이 바뀌었다 — 유지 줄 아래 한 줄을 다시 쓴다. */
	UFUNCTION()
	void HandleCoopStateChanged();

	UFUNCTION()
	void HandleFeedUpkeepClicked();

	UFUNCTION()
	void HandleExpandSlotClicked();

	/** 안 본 키면 알약을 띄우고 본 것으로 적는다. 떠 있는 중·막힘·이미 봄이면 버린다(본 것 아님). */
	bool TryShowUsageHint(FName Key);

	/** 알약을 지금 키·막힘·칸 카드에 맞춰 폈다 접고 자리를 잡는다. */
	void UpdateUsageHintPill();

	/** 줄 수를 목장 칸 수에 맞춘다 (`SlotCount` 가 블루프린트에서 바뀌어도 따라오게). */
	void EnsureSlotRows(int32 SlotCount);

	/** WBP 트리가 아직 비었는지 (루트가 없거나, 자식 없는 빈 패널 하나뿐인지). */
	bool NeedsPlaceholderTree() const;

	/** WBP 에 디자인이 없을 때만 부른다. 엔진 기본 위젯으로 최소 트리를 세운다. */
	void BuildPlaceholderTree();

	UPROPERTY()
	TObjectPtr<URanchComponent> WatchedRanch;

	UPROPERTY()
	TObjectPtr<URanchInteractComponent> WatchedInteract;

	UPROPERTY()
	TObjectPtr<UInventoryComponent> WatchedInventory;

	UPROPERTY()
	TObjectPtr<USnareBackendSubsystem> WatchedBackend;

	UPROPERTY()
	TObjectPtr<UZoneTravelComponent> WatchedZoneTravel;

	UPROPERTY()
	TObjectPtr<URanchSyncComponent> WatchedSync;

	UPROPERTY()
	TObjectPtr<UCoopReportComponent> WatchedCoop;

	/** 마지막 거절 문구 (성공 응답이 오면 비운다). 화면 표시 시간은 `USnareNoticeText` 가 센다. */
	FText FailureNotice;
	/** GetBaseState 실패 — 이유를 계속 띄우고 「다시 시도」 를 보인다. */
	bool bBaseStateFailed = false;

	/** 마지막 수확 팝업 글. */
	FText HarvestPopup;

	/** 펫 빼기 확인 줄이 펴졌는지 (#0199). */
	bool bRemovePetConfirmOpen = false;

	/** 꾸미기 고르기 자리 (UI 로컬 — 무엇을 보낼지만 정한다). 선택지 수로 나눈 나머지를 쓴다. */
	int32 PetCosmeticPickIndex = 0;

	/** 다가가기 키를 볼 내 캐릭터 (`BindToCharacter`). */
	UPROPERTY()
	TObjectPtr<ASnarehavenCharacter> WatchedCharacter;

	/** `DT_UsageHints` — 처음 키가 생길 때 읽는다. */
	UPROPERTY()
	TObjectPtr<const UDataTable> UsageHintTable;

	FName UsageHintKey;
	FText UsageHintLine;
	float UsageHintSecondsLeft = 0.f;
	const TCHAR* UsageHintSection = nullptr;   // nullptr = `SnareUsageHints::ResolveSection` 이 고름
	/** 지난번 다가가기 키 — 바뀐 순간만 계기(서 있는 동안 다시 묻지 않음). */
	FName UsageHintLastApproach;
	/** 다음 다가가기 확인 시각(`FPlatformTime::Seconds`). */
	double UsageHintNextCheckSeconds = 0.0;
};
