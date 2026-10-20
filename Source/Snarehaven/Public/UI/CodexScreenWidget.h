#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "CodexScreenWidget.generated.h"

class FJsonObject;
class UButton;
class UCanvasPanel;
class UDataTable;
class UTextBlock;
class USnareBackendSubsystem;
class USnareClickProxy;
class UHorizontalBox;
class UVerticalBox;
struct FItemRow;
struct FSnarePlayerSnapshot;
class UAchievementScreenWidget;

/**
 * 도감 여섯 갈래 — 다섯은 DESIGN 8.3 원본 맞춤, 여섯째 `Variant` 는 색 변종 펫 12칸(DESIGN 10.6 「변종 도감」, M21-07c1).
 * 서버 `GetCodexRewards` 의 `categories` 키 순서와 같다.
 */
enum class ECodexCategory : uint8
{
	Fishing,
	Hunt,
	Plant,
	Blueprint,
	Cooking,
	Variant,
	Count
};

/** 서버 `GetCodexRewards.categories` 한 갈래 — `{have, total, percent}` 그대로. 화면은 이 수를 세지 않는다 (DESIGN 8.3). */
struct FCodexCategoryProgress
{
	/** 서버가 이 갈래를 말해 줬나. 거짓이면 탭은 `-` 다. */
	bool bKnown = false;
	int32 Have = 0;
	int32 Total = 0;
	int32 Percent = 0;
};

/** 서버 `GetCodexRewards` 답 전체 — 여섯 갈래 진척 + 해금 보상 셋 (DESIGN 8.3·8.5). */
struct FCodexServerReply
{
	FCodexCategoryProgress Categories[static_cast<int32>(ECodexCategory::Count)];
	bool bFarSea = false;
	bool bDangerDock = false;
	bool bSpecialMerchant = false;
};

/**
 * 도감 한 칸이 화면에 뜨기 직전의 모습 (M10-05). 값이 아니라 **본 것**이라 저장하지 않는다 —
 * 원본은 세 데이터 표와 백엔드 스냅샷이고 이 구조체는 그것을 한 줄로 합쳐 둔 것뿐이다.
 */
struct FCodexSlotView
{
	/** `animal.pig`·`item.carrot` — 백엔드 `codex` 배열에 들어가는 것과 같은 글자 (CODEX.md 표 A). */
	FName EntryID;

	/** 다섯 탭 중 어디에 드는가. 탭 머리글의 `찬 칸 / 칸 수` 도 이것으로 센다. */
	ECodexCategory Category = ECodexCategory::Fishing;

	/** `낚시`·`사냥`·`식물`·`설계도`·`요리`·`변종` — 탭 글자. */
	FText CategoryName;

	/** 화면에 보이는 이름. 동물은 `DT_AnimalTable`, 작물·어류는 `DT_ItemTable` 이 안다. */
	FText DisplayName;

	/**
	 * 원판에 얹을 그림의 아이템 ID (M25-02). 아이템 칸은 칸 ID 그대로, 사냥 종은 그 종의 펫 아이템(`PetItemID`) —
	 * 종 행에는 그림이 없고 펫 아이템 행이 그 동물 그림을 든다. 비었거나 표에 없으면 원판만 선다(그림을 지어내지 않는다).
	 */
	FName IconItemID;

	/** 백엔드가 이 칸을 찼다고 말했나. **모를 때는 찬 칸이 0개가 아니라 "모른다" 다**(진행률이 `-`). */
	bool bCollected = false;
};

// ROADMAP M10-05: 도감 화면 — **다섯 탭**(낚시·사냥·식물·설계도·요리, DESIGN 8.3 원본 맞춤)의 진행률을 띄운다.
// 탭마다 칸은 표 행에서 센다: 낚시 = `DT_FishingTable` · 사냥 = `DT_AnimalTable` 종 + 죽을 때 떨구는 깃털·뿔(고기·가죽 제외)
// · 식물 = `DT_CropProductTable` + 나무·향나무·산딸기 태그 아이템 · 설계도 = `DT_RecipeTable.RequiredBlueprintID`
// · 요리 = `DT_RecipeTable` 불 레시피 결과물 · 변종 = `DT_AnimalTable` 기본 플레이 종의 `VariantPetItemID`(M21-07c1,
// 서버 `CODEX_CATEGORIES.variant` 와 같은 12칸 — 보상·업적 % 계산 밖은 서버 몫). 50%·100% 보상(먼바다·위험구역 선착장·특별 상인)은
// **서버 `GetCodexRewards` 의 답만** 보여 준다 — 화면이 비율을 세서 "열렸다" 고 말하지 않는다.
//
//
// **이 화면은 칸을 채우지 않는다.** 채우는 자리는 CloudScript 성공 판정 셋뿐이고(M10-04), 여기는
// 백엔드 스냅샷의 `CodexEntries` 를 **읽기만** 한다 — 인벤토리 캐시(M2-05)와 같은 규칙이다.
// 백엔드에게 아직 못 들었으면 진행률을 `-` 로 두고 칸을 0개도 1개도 채우지 않는다 (DESIGN 4.8 · CLAUDE.md 8).
// 화면이 스스로 세어 "다 찼다" 고 말할 수 있으면 그것이 곧 탈것(M10-06)을 공짜로 여는 길이다.
//
// **로스터는 코드에 적지 않는다** — `DT_AnimalTable`(사냥 3) · `DT_CropProductTable`(농사 3) ·
// `DT_FishingTable`(낚시 21) 세 표에서 읽어 잇는다 (DataDriven.md 1 — 종이 늘어도 C++ 파일이 늘지 않는다).
// 어종이 3 → 21 종이 됐을 때(2026-09-23) 이 파일에서 바뀐 것은 줄을 **세 칸으로 나눠 담는 자리**뿐이고
// 로스터 코드는 한 줄도 안 늘었다 — 그것이 표에서 읽는다는 말의 뜻이다.
// 도감 전용 표를 새로 세우지 않은 이유: 그러면 같은 로스터가 네 표에 살게 되고, 물고기 한 종을 더할 때
// 두 군데를 고쳐야 한다. 이 화면이 세 표와 같은 로스터를 보고 있는지는 2층
// `Snarehaven.Codex.Screen.RosterMatchesTables` 가 잰다.
UCLASS()
class SNAREHAVEN_API UCodexScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UCodexScreenWidget(const FObjectInitializer& ObjectInitializer);

	/**
	 * 다섯 표를 이어 만든 지금의 칸 전부(다섯 탭 순서). 표가 빠지면 그 묶음만큼 칸이 줄어든다 —
	 * 없는 칸을 지어내지 않는다 (Verification.md 7).
	 */
	TArray<FCodexSlotView> BuildSlots() const;

	/**
	 * 탈것 조건(DESIGN 8.3 "도감 100%")에 드는 칸 ID — 사냥 종(자리표시 뺌) + 작물 생산물 + 어종 생산물.
	 * 세 표의 행에서 읽는다 — 칸 수를 코드에 적지 않는다(ROADMAP M10-06, 지금 12 + 3 + 21 = 36).
	 * 트로피·설계도·요리는 탈것 조건이 아니다(DESIGN 8.3 — 탈것 조건용 카테고리는 농사·사냥·낚시 셋).
	 */
	static TArray<FName> CollectMountCodexIDs(const UDataTable* InAnimalTable, const UDataTable* InCropTable,
		const UDataTable* InFishingTable);

	/**
	 * **탈것 탑승 게이팅** (M10-06). 백엔드 스냅샷의 `CodexEntries` 하나만 읽는다(DESIGN 4.8 — 클라이언트가 세어 채우지 않는다).
	 * 위 칸 ID 가 **전부** 들어 있으면 참. 배열 길이만 맞대지 않는 이유: 서버 `addToCodex` 는 트로피·요리·설계도도
	 * 같은 배열에 넣어서, 길이만 보면 종 하나 없이도 열린다. 표가 비어 칸이 0 이면 거짓(빈 조건으로 열지 않는다).
	 */
	static bool IsMountUnlocked(const FSnarePlayerSnapshot& Snapshot, const UDataTable* InAnimalTable,
		const UDataTable* InCropTable, const UDataTable* InFishingTable);

	/**
	 * 머리글 — `도감 3 / 27`. 백엔드에게 못 들었으면 찬 칸 자리가 `-` 다
	 * (`SnareBalanceHud::FormatBalanceText` 와 같은 규칙 — 0 으로 적으면 "하나도 못 모았다" 로 읽힌다).
	 * 분모는 코드에 적힌 상수가 아니라 **표에서 읽어 낸 칸 수**다.
	 */
	UFUNCTION(BlueprintPure, Category = "Codex")
	FText GetProgressText() const;

	/**
	 * 도감 값이 실린 백엔드 캐시를 정한다. `ASnarehavenCharacter` 가 게임 인스턴스에서 찾아 부르고,
	 * 2층 검사는 게임 인스턴스가 없는 월드에서 도니 직접 부른다 (`UHuntHudWidget::BindToBackend` 와 같은 길).
	 * **읽기만 한다** — 이 위젯에 도감을 채우는 길은 없다 (CLAUDE.md 8).
	 * @param Backend 볼 백엔드. nullptr 이면 진행률이 `-` 로 돌아간다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Codex")
	void BindToBackend(USnareBackendSubsystem* Backend);

	/** 화면 글자를 지금 표·스냅샷에 맞춘다. 알림을 받을 때마다 부른다. */
	UFUNCTION(BlueprintCallable, Category = "Codex")
	void RefreshDisplay();

	/**
	 * 화면이 읽을 표를 바꾼다. 평소에는 생성자가 잡아 둔 넷 그대로고, 2층 검사가 **표가 없는 상황**
	 * (포장에서 에셋을 빠뜨리는 사고)을 흉내 낼 때 쓴다 (`UGachaScreenWidget::SetSourceTables` 와 같은 문).
	 * 이 문으로 바뀌는 것은 **화면 글자뿐**이다 — 도감 값은 백엔드 스냅샷에서만 온다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Codex")
	void SetSourceTables(UDataTable* AnimalTable, UDataTable* CropTable, UDataTable* FishingTable, UDataTable* ItemTable,
		UDataTable* RecipeTable);

	/** 보이는 탭을 고른다. 칸 줄은 고른 탭 것만 그린다 — 한 화면에 70칸을 다 늘어놓지 않는다. */
	void SetSelectedCategory(ECodexCategory Category);

	ECodexCategory GetSelectedCategory() const { return SelectedCategory; }

	/**
	 * 탭 글자 — 서버 답 그대로 `낚시\n3/21 14%`. 서버 답 전(또는 그 갈래를 못 들었으면) `낚시\n-/21` —
	 * 분모는 표 칸 수(로스터)고 분자·퍼센트는 **서버만** 채운다. 화면이 스냅샷을 세어 퍼센트를 만들지 않는다.
	 */
	FText GetTabText(ECodexCategory Category) const;

	/**
	 * 서버 `GetCodexRewards` 답을 읽는다. `categories` 키 = fishing·hunt·plant·blueprint·cooking·variant.
	 * @return `{error}` 답·`rewards` 없음·JSON 아님이면 false (OutReply 는 건드린 값이 있어도 쓰지 않는다)
	 */
	static bool ParseCodexRewards(const TSharedPtr<FJsonObject>& Response, FCodexServerReply& OutReply);

	/** 마지막으로 받은 서버 진척. 받기 전엔 `bKnown` 이 거짓이다. */
	const FCodexCategoryProgress& GetServerProgress(ECodexCategory Category) const
	{
		return ServerReply.Categories[static_cast<int32>(Category)];
	}

	/**
	 * 보상 줄 — `먼바다 열림 | 위험구역 선착장 잠김 | 특별 상인 잠김`. 서버 답 전에는 각 칸이 `-` 다.
	 * 열림·잠김은 서버 `GetCodexRewards.rewards` 그대로다. 화면에서는 답 전엔 줄째 접힌다(M27-05, 변종 탭 고지는 빼고).
	 */
	FText GetRewardText() const;

	/**
	 * 변종 탭의 확률 고지 줄 (M21-07g, DESIGN 10.6 「변종 확률」) — `확률 고지: 변종 3% (동물이 설 때마다)`.
	 * 값은 `DT_AnimalTable` 기본 플레이 종의 `VariantChance`(종 상관없이 같은 값). 변종 탭에선 보상 줄 자리에 선다 —
	 * 변종 칸은 보상 계산 밖이라(서버 `CODEX_REWARD_EXCLUDED`) 그 탭에서 보상 줄은 볼 거리가 없다.
	 */
	FText GetVariantDisclosureText() const;

	/** 기본 플레이 종의 변종 확률(0~1). 표가 없거나 기본 플레이 종이 없으면 0. */
	float GetVariantChance() const;

	/** 확률 고지 줄 글자 — 2층이 표 값을 넣어 화면 글자를 맞댄다. */
	static FText FormatVariantDisclosure(float Chance);

	/**
	 * 서버에 보상 판정을 묻는다. 붙일 때·스냅샷이 바뀔 때 부른다.
	 * **도감 칸이 지난번 물은 것과 같으면 다시 안 묻는다** — 스냅샷 알림은 재화·인벤토리·시작 장비 왕복에도
	 * 오므로, 알림마다 물으면 화면 한 번 여는 데 `GetCodexRewards` 가 여러 번 나간다(감독 3층 캡처 3번).
	 * 보상은 서버가 도감 칸만 세어 정하니 칸이 같으면 답도 같다. 답을 기다리는 중에도 다시 안 보낸다.
	 */
	void RequestRewards();

	bool AreRewardsKnown() const { return bRewardsKnown; }
	bool IsDangerDockOpen() const { return bRewardsKnown && ServerReply.bDangerDock; }

	/**
	 * 3층 캡처 전용 — 앞에서부터 `Count` 칸을 **찬 것처럼 그린다**. 백엔드는 M2-03 이 서기 전까지
	 * 늘 빈 도감을 주므로, 이게 없으면 "채운 칸 강조" 가 캡처 어디에도 안 나온다
	 * (ROADMAP M10-05 가 말한 "자리표시 codex 세트").
	 *
	 * **바뀌는 것은 화면 글자·색뿐이다.** 백엔드 스냅샷에는 손대지 않으므로 M10-06 의 탑승 판정
	 * (`IsMountUnlocked` — 스냅샷 `CodexEntries` 만 읽는다, DESIGN 4.8)은 이 문으로 열리지 않는다. 그 사실은 2층
	 * `Snarehaven.Codex.Screen.NeverFillsLocally` 가 잰다 — 이 문을 열어 두는 값은 그 검사뿐이다.
	 * 채울 ID 를 받지 않고 **개수**만 받는 이유: 로스터는 표 하나에서만 오게 두려는 것이다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Codex")
	void SetDemoFilledCount(int32 Count);

	/**
	 * 도감 화면을 만들어 백엔드를 붙이고 ZOrder 10 으로 띄운다 — 배낭 도감 탭(M13-29)과
	 * `-SnareCodexScreen` 스위치가 같이 쓴다. `Owner` 가 없으면 `World` 로 만든다(2층 시험). 둘 다 없으면 nullptr.
	 */
	static UCodexScreenWidget* OpenOnViewport(APlayerController* Owner, UWorld* World, USnareBackendSubsystem* Backend);

	/** "업적" 버튼 (M12-06) — 업적 화면을 띄우고 이 화면은 접는다. 닫으면 되살아난다. */
	UAchievementScreenWidget* OpenAchievementScreen();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;

	/** 백엔드가 새 스냅샷을 보내 줬다 — 도감 진행률이 바뀌는 **유일한** 계기다 (CLAUDE.md 8). */
	UFUNCTION()
	void HandleBackendSnapshotChanged();

	// 아래 셋은 WBP 에서 같은 이름으로 만들면 그걸 쓰고, 없으면 `RebuildWidget` 이 자리표시로 만들어 넣는다
	// (`UGachaScreenWidget` 과 같은 규칙 — 디자인이 없어도 3층 캡처가 찍혀야 한다).
	UPROPERTY(BlueprintReadOnly, Category = "Codex", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, Category = "Codex", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ProgressText;

	/**
	 * 칸 목록 자리. 안에 줄바꿈 격자(`CodexSlotGrid`, `UWrapBox`) 하나가 들어가 원판 칸이 판 폭만큼 옆으로 놓이고
	 * 넘치면 다음 줄로 간다 (M25-02 — 옛 `[O] 붕어` 글자 줄 세로 칸을 대신한다).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Codex", meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> SlotList;

	/** 여섯 탭 버튼이 담기는 가로 상자. 버튼은 `RefreshDisplay` 가 세운다. */
	UPROPERTY(BlueprintReadOnly, Category = "Codex", meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> TabBar;

	UPROPERTY(BlueprintReadOnly, Category = "Codex", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RewardText;

	/** 설계도·요리 칸의 원본 (DESIGN 8.10). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Codex")
	TObjectPtr<UDataTable> RecipeTable;

	/** 탭 버튼마다 하나 — 버튼이 자기 탭 번호를 들고 눌리게 한다. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USnareClickProxy>> TabProxies;

	/** "업적" 버튼의 심부름꾼. */
	UPROPERTY(Transient)
	TObjectPtr<USnareClickProxy> AchievementProxy;

	/** "닫기" 버튼의 심부름꾼. */
	UPROPERTY(Transient)
	TObjectPtr<USnareClickProxy> CloseProxy;

	/** 사냥 3칸의 원본 (M1-01). 종 ID 도 화면 이름도 여기서 온다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Codex")
	TObjectPtr<UDataTable> AnimalTable;

	/** 농사 3칸의 원본 (M6-05a). 칸을 세는 ID 는 행 이름이 아니라 `ProducedItemID` 다 (CODEX.md 표 A). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Codex")
	TObjectPtr<UDataTable> CropTable;

	/** 낚시 21칸의 원본 (M10-01, DESIGN 8.4). 농사와 같은 이유로 `ProducedItemID` 를 본다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Codex")
	TObjectPtr<UDataTable> FishingTable;

	/** 작물·어류의 화면 이름 (M1-05). 동물 이름은 `AnimalTable` 이 직접 들고 있다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Codex")
	TObjectPtr<UDataTable> ItemTable;

	UPROPERTY()
	TObjectPtr<USnareBackendSubsystem> WatchedBackend;

private:
	ECodexCategory SelectedCategory = ECodexCategory::Fishing;

	/** 서버 `GetCodexRewards` 답을 받았나 · 그 답(진척 + 보상). 받기 전엔 탭 분자·보상이 전부 `-` 다. */
	bool bRewardsKnown = false;
	bool bRewardsPending = false;
	FCodexServerReply ServerReply;

	/** 마지막으로 보낸(또는 보내는 중인) 요청의 도감 칸. 같은 칸으로는 다시 묻지 않는다 (`RequestRewards`). */
	TArray<FName> AskedCodexEntries;
	bool bRewardsAsked = false;

	/** 캡처용 자리표시로 찬 칸들 (`SetDemoFilledCount`). 비어 있고 꺼져 있으면 평소 동작 그대로다. */
	TArray<FName> DemoEntries;

	/** 자리표시 모드인가. 켜지면 화면은 백엔드 대신 `DemoEntries` 를 본다 — 화면만. */
	bool bDemoMode = false;

	/** 백엔드가 도감을 말해 준 적이 있나. 거짓이면 화면은 찬 칸 수를 **모른다고** 적는다. */
	bool IsCodexKnown() const;

	/** 아이템 ID 의 화면 이름. 표에 없으면 ID 를 그대로 보여 준다 — 이름을 지어내지 않는다. */
	FText FindItemDisplayName(FName ItemID) const;

	/** 아이템 표의 그 행. 표가 없거나 ID 가 없으면 nullptr. */
	const FItemRow* FindItemRow(FName ItemID) const;

	/**
	 * 도감 한 칸 — 원판(모은 칸 = 등급 색 + 그림 · 못 모은 칸 = 빈 칸 색 + 흐린 그림) 아래 이름 (M25-02, 시트 「원판 위 그림」).
	 * 위젯 이름 `CodexSlotCell_<n>`·`CodexSlotDisc_<n>`·`CodexSlotIcon_<n>`·`CodexSlotName_<n>`.
	 */
	UWidget* MakeSlotCell(const FCodexSlotView& CodexSlot, int32 Index);

	/** WBP 트리가 아직 비었는지 (루트가 없거나, 자식 없는 빈 패널 하나뿐인지). */
	bool NeedsPlaceholderTree() const;

	/** WBP 에 디자인이 없을 때만 부른다. 엔진 기본 위젯으로 최소 트리를 세운다. */
	void BuildPlaceholderTree();
};
