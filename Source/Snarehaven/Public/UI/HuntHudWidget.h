#pragma once

#include "CoreMinimal.h"
#include "Animals/AnimalStateComponent.h"
#include "Blueprint/UserWidget.h"
#include "Data/ItemRow.h"
#include "UI/SnareAchievementNotice.h"
#include "UI/SnareOutpostBanner.h"
#include "HuntHudWidget.generated.h"

class AAnimalCharacter;
class ASnarehavenCharacter;
class AZonePortalActor;
class UBackpackComponent;
class UBorder;
class UButton;
class UCanvasPanel;
class UCaptureComponent;
class UDataTable;
class UEquipmentComponent;
class UHorizontalBox;
class UImage;
class UInventoryComponent;
class UMeleeAttackComponent;
class UPlayerVitalsComponent;
class UProgressBar;
class USnareBackendSubsystem;
class USnareCaptureRingWidget;
class USnareNoticeCards;
class USnareNoticeText;
class UTextBlock;
class UZoneTravelComponent;
struct FZoneRow;
enum class ESnareInteractVerb : uint8;
enum class EPlaceableFunction : uint8;

namespace SnareHuntHud
{
	/** 동물 칸(이름·HP 막대)을 펼칠지 — 하이라이트 동물이 있고, 포획 성공 줄이 없고, 스토어 촬영(`-SnareStoreShot=`, 검사 전용)이 아닐 때만 */
	SNAREHAVEN_API bool WantAnimalPanel(bool bHasAnimal, bool bCaptureBanner, bool bStoreShot);
}

// ROADMAP M1-08: 사냥 화면 HUD — 지금 하이라이트된 동물의 이름·HP 바와 포획 버튼.
// 갱신을 타이머로 돌리지 않는 이유: 주기를 정하려면 설계서에 없는 숫자를 하나 만들어야 한다
// (DataDriven.md 3). 필요한 알림은 이미 다 있으므로(대상 바뀜·체력 바뀜·상태 바뀜) 이벤트로만 고친다.
// 보이는 모양은 자리표시다 — WBP 쪽 위젯 트리가 비어 있으면 C++ 이 최소 트리를 만들어 넣는다.
UCLASS()
class SNAREHAVEN_API UHuntHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UHuntHudWidget(const FObjectInitializer& ObjectInitializer);

	/**
	 * 어떤 플레이어의 사냥 상태를 보여줄지 정한다. 뷰포트에 올린 쪽(`ASnarehavenCharacter`)이 부르고,
	 * 2층 검사는 월드를 띄우지 않고 직접 부른다 (Verification.md 6).
	 * 이전에 듣던 플레이어·동물의 알림은 모두 끊는다.
	 * @param Character 볼 플레이어. nullptr 이면 HUD 를 비운다.
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void BindToCharacter(ASnarehavenCharacter* Character);

	/** 지금 HUD 가 보여주고 있는 동물. 아무도 하이라이트되지 않았으면 nullptr. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	AAnimalCharacter* GetShownAnimal() const { return ShownAnimal; }

	/** 동물 정보(이름·HP 바)를 보여줄 상태인지. 하이라이트가 없으면 false 라 접어 둔다. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool IsAnimalPanelVisible() const;

	/** 포획 버튼이 눌리는 상태인지. 판정은 `UCaptureComponent::CanCapture()` 하나뿐이다 (DESIGN 3.1). */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool IsCaptureButtonEnabled() const;

	/** HP 바 채움 비율(0~1). 보여줄 동물이 없거나 숫자가 미정이면 0 (Verification.md 7). */
	UFUNCTION(BlueprintPure, Category = "HUD")
	float GetHealthPercent() const;

	/**
	 * 플레이어 체력 게이지 채움 비율(0~1). 체력·스태미나가 아직 안 붙었거나 숫자가 미정이면 0
	 * (Verification.md 7 — 모르면서 꽉 찬 게이지를 보여 주면 거짓말이다).
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	float GetPlayerHealthPercent() const;

	/** 플레이어 스태미나 게이지 채움 비율(0~1). 위와 같은 규칙. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	float GetPlayerStaminaPercent() const;

	/** 이름 칸에 들어가는 글자. 보여줄 동물이 없으면 빈 FText. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetAnimalNameText() const;

	/**
	 * 이름 아래 작은 줄 — 지금은 우두머리의 확률 고지 하나다 (M21-07g, DESIGN 10.6 「우두머리 모자」):
	 * `우두머리 모자 10% (주 1회)`. 값은 `DT_DangerZoneTable` 우두머리 행 `BossHatChance`(굴림은 서버 `ClaimBoss`).
	 * 우두머리가 아니면 빈 FText.
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetAnimalNoteText() const;

	/** 우두머리 고지 글자 — 2층이 표 값을 넣어 화면 글자를 맞댄다. */
	static FText FormatBossDisclosure(float HatChance);

	/** 위험구역 표에서 우두머리를 세우는 행(`BossHPMultiplier` > 0)의 모자 확률. 없으면 0. */
	static float FindBossHatChance(const UDataTable* DangerZoneTable);

	/** 포획 성공 줄이 떠 있는지 (M1-10e2). 포획한 순간부터 2초 동안만 참 (DESIGN 3.1). */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool IsCaptureBannerVisible() const;

	/** 포획 성공 줄 글자 — `포획 성공 — <동물 이름>`. 떠 있지 않으면 빈 FText. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetCaptureBannerText() const;

	/** 포획 성공 줄을 내린다. 2초 타이머가 부르고, 2층 검사도 직접 부른다 (타이머를 못 돌리므로). */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void ClearCaptureBanner();

	/** 포획 성공 줄과 같은 자리·같은 2초로 짧은 알림을 띄운다 (M12-07 월드 이벤트 등장 알림). */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void ShowNoticeBanner(const FText& Message);

	/** 업적·도감 알림 카드 두 장 (M48-03c) — 자리표시 트리가 세운다. WBP 디자인이면 nullptr. */
	USnareNoticeCards* GetAchievementCards() const { return AchievementCards; }

	/**
	 * 거절·실패 한 줄을 왼쪽 위 빨간 알약으로 3초 띄운다 (M26-04, 시트 `hud_ingame.md` 「왼쪽 위(오류)」).
	 * 같은 자리의 크림 알림 카드와 둘 중 **나중 것 하나**만 선다 — 오류가 뜨면 카드는 내려간다.
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void ShowErrorBanner(const FText& Message);

	/** 오류 줄이 뜰 때의 짧은 막힘 소리 (M55-03, DESIGN 2.2 막힘 소리 표). 규칙은 `SnareSound::DenyCue`.
	 *  소리를 냈으면 그 이름, 무음(빈 문구·0.4초 안)이면 NAME_None. `NowSeconds` 는 시험이 가짜 시각을 넣는 자리. */
	FName PlayDenySound(bool bMessageEmpty, double NowSeconds);

	/** 막힘 소리를 낸 횟수 (2층 시험용). */
	int32 GetDenySoundCount() const { return DenySoundCount; }

	/** 오류 알약이 떠 있는지 (M26-04). */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool IsErrorBannerVisible() const;

	/** 오류 알약 글자. 떠 있지 않으면 빈 FText. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetErrorBannerText() const;

	/** 오류 알약을 내린다. 3초 타이머가 부르고, 2층 검사도 직접 부른다. */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void ClearErrorBanner();

	/** 링 실패 줄 글자 — `놓쳤다!` (M21-09d, 명세 2). 2층이 맞댄다. */
	static FText GetCaptureMissText();

	/** 공격 버튼 글자 한 칸 — 「공격·줍기·베기·캐기·수확·넣기·열기」 (INBOX 18:10 ①, DESIGN 3.3). 2층이 맞댄다. */
	static FText GetInteractVerbText(ESnareInteractVerb Verb);

	/** 원 버튼에 실제로 찍는 글자 — 두 낱말이면 두 줄(「손도끼\n필요」). 2층이 원 폭에 맞댄다. */
	static FText GetVerbDiscText(const FText& Verb);

	/** 원 버튼 글자 크기 — 기본 26, 글자 네모 모서리가 글자 원(`GetVerbDiscTextRadius`) 밖이면 그만큼 줄인다. */
	static float GetVerbDiscFontSize(const FText& DiscText);

	/** 원 버튼 글자 원 반지름 (지름/2 - 테두리 여백 8). */
	static float GetVerbDiscTextRadius();

	/** 지금 공격 버튼에 쓸 글자 — 캐릭터가 「지금 누르면 무엇이 되나」(`GetInteractVerb`)를 정한다. 캐릭터가 없으면 「공격」. */
	FText GetAttackButtonText() const;

	/**
	 * 공격 버튼 위 도구 안내 한 줄 (INBOX 18:10 ②, DESIGN 3.3) — 버튼이 「손도끼 필요」「곡괭이 필요」일 때
	 * 「손도끼가 필요 — 잡화점」. 가방에 있으면 버튼이 누를 때 알아서 들어(INBOX 22:00) 안내가 없다. 띄울 일이 없으면 빈 FText.
	 */
	FText GetNeedToolHintText() const;

	/** 도구 안내 줄이 펴져 있는지 (2층이 본다). 자리표시 트리가 없으면 false. */
	bool IsNeedToolHintVisible() const;

	/** 도구 안내 줄을 지금 대상에 맞춰 펴거나 접는다 — `UpdateAttackButtonText` 가 같은 간격으로 부르고, 2층도 직접 부른다. */
	void UpdateNeedToolHint();

	/**
	 * 처음 들기 안내 한 줄 (M28-05b, `docs/specs/onboarding-usage-hints.md`) — 손에 든 도구·무기의 표 `DT_UsageHints`
	 * 줄을 이 기기에서 처음일 때만 상황 버튼 위(도구 안내 알약과 같은 자리·모양)에 줄의 `ShowSeconds` 동안 띄운다.
	 * 지금 안내 중인 키. 없으면 NAME_None.
	 */
	FName GetUsageHintKey() const { return UsageHintKey; }

	/** 지금 안내 줄 글자 — 안내 중이 아니면 빈 FText (도구 안내 알약에 밀려 잠깐 접힌 동안에도 글은 남는다). */
	FText GetUsageHintText() const { return UsageHintLine; }

	/** 안내 알약이 펴져 있는지 (자리표시 트리가 없으면 false). */
	bool IsUsageHintVisible() const;

	/** 안내 줄 시간을 흘린다 — 틱이 부르고, 2층도 직접 부른다. 막혔거나 도구 안내 알약이 떠 있으면 시간이 안 간다. */
	void TickUsageHint(float DeltaSeconds);

	/**
	 * 처음 다가가기 (M28-05c) — 상황 버튼 동사가 목장 칸(「수확」·「넣기」)이거나 잡화점 상인 대화 거리 안이면 그 키
	 * (`ranch.slot`·`vendor.shop`). 키가 **바뀐 순간**만 띄워 본다 — 같은 대상 앞에 서 있는 동안 다시 묻지 않는다.
	 * `UpdateAttackButtonText` 가 같은 간격으로 부르고, 2층도 직접 부른다.
	 */
	void CheckUsageHintForApproach();

	/** 동사·곁의 기능·상인 근접 → 다가가기 안내 키. 목장 칸 > 놓인 것·배 > 상인 순(버튼이 가리키는 쪽). 해당 없으면 NAME_None. */
	static FName PickApproachHintKey(ESnareInteractVerb Verb, bool bNearGeneralShop, EPlaceableFunction NearFunction);

	/** 본 키를 적을 ini 섹션 — 2층은 `SnareUsageHints::TestSection` 을 넘겨 이 기기의 진짜 기록을 건드리지 않는다. */
	void SetUsageHintSection(const TCHAR* Section) { UsageHintSection = Section; }

	/**
	 * 올가미 링 화면이 떠 있는지 (M21-09d). 링이 도는 동안(또는 검사 스위치 `-SnareCaptureRingHold` 로 대상이
	 * 포획 가능할 때)만 참이다.
	 */
	bool IsCaptureRingVisible() const;

	/** 링 화면 위젯 (2층이 반지름·색을 본다). 아직 안 만들었으면 nullptr. */
	USnareCaptureRingWidget* GetCaptureRingWidget() const { return CaptureRing; }

	/**
	 * 링 화면을 지금 상태로 맞춘다 — 링 경과로 원을 다시 그리고 동물 발밑 화면 자리로 옮긴다.
	 * 틱이 부르고 2층 검사도 직접 부른다 (헤드리스는 틱이 안 돈다).
	 */
	void UpdateCaptureRing();

	/**
	 * 위험 체력 붉은 테두리 (M60-03) — `SnareLowHealth::StateFor` 세기로 화면 가장자리를 칠하고 심장 간격으로 펄스한다.
	 * 틱이 부르고 2층 검사도 직접 부른다. 체력은 읽기만 한다. `TimeSeconds` 는 펄스 위상.
	 */
	void UpdateLowHealthEdge(double TimeSeconds);

	/**
	 * 마을 이름 띠 — 걸어서 마을(`DT_OutpostTable` 반지름) 안으로 들어서는 순간 3초, 같은 마을은 60초 안엔 다시 안 띄운다.
	 * 틱이 부르고 2층 검사도 직접 부른다. @return 이번 부름에 띠를 새로 띄웠나
	 */
	bool UpdateOutpostBanner(double NowSeconds);

	/**
	 * 수영 기운 경고 (M107-05 ③ 익사 결함, DESIGN 8.9) — 처음 물에 들면 안내 한 번, 수영 중 기운이
	 * `SwimWarnStaminaRatio` 이하로 떨어지면 가운데 위 빨간 알약 + 가벼운 진동을 잠수당 한 번.
	 * 틱이 부르고 2층 검사도 직접 부른다. @return 이번 부름에 경고를 새로 띄웠나
	 */
	bool UpdateSwimWarn();

	/** 경고를 띄울 때인가 — 수영 중, 기운 비율 ≤ 기준, 이번 잠수에 아직 안 띄움. */
	static bool ShouldWarnSwimStamina(bool bSwimming, float Stamina, float MaxStamina, bool bWarnedThisDive);

	/** 경고 글자 위젯(검사용). */
	USnareNoticeText* GetSwimWarnText() const { return SwimWarnText; }

	/** 이름 띠 글자 위젯(검사용). */
	USnareNoticeText* GetOutpostBannerText() const { return OutpostBannerText; }

	/** 테두리 레이어가 지금 보이는지. */
	bool IsLowHealthEdgeVisible() const;

	/** 마지막으로 칠한 테두리 불투명도(펄스 반영 전 기본값 아님 — 실제 칠한 값). */
	float GetLowHealthEdgeOpacity() const { return LowHealthEdgeOpacity; }

	/** 테두리 레이어 위젯(검사용). */
	UCanvasPanel* GetLowHealthEdgeLayer() const { return LowHealthEdgeLayer; }

	/**
	 * **그 부위**의 장비 버튼이 가리키는 `DT_ItemTable` **행 이름**. 들 것도 든 것도 없으면 NAME_None.
	 * M5-06c 는 버튼을 한 칸으로 뒀는데(그때 장비 행이 `Item_Club` 하나였다) M9-03 이 부위로 나눴다 —
	 * 무기와 도구는 **동시에** 들리므로(M9-01) 한 칸으로는 둘 중 하나를 고를 수가 없다.
	 * 고르는 순서: ① 지금 그 부위에 붙어 있는 장비 ② 인벤토리 캐시에 있는 **그 부위** 장비. 든 것을
	 * 먼저 보는 이유는 인벤토리가 비어도 **벗을 길은 남아야** 하기 때문이다 (캐시는 M2-03 전까지 빈다).
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FName GetEquipButtonItem(EEquipSlot EquipSlot) const;

	/**
	 * 지금 가까이 있는 대상이 요구하는 **도구** 행 이름 (나무=손도끼/전기톱 · 광맥=곡괭이 · 낚시 포인트=낚싯대).
	 * 대상이 없거나 맞는 도구를 안 가졌으면 NAME_None — 그때는 도구 버튼이 예전 순서(든 것 → 가진 것)로 돌아간다.
	 * M15-08: 표 순서상 낚싯대가 앞이라 나무 앞에서도 낚싯대를 들던 것을 이게 대신한다 (INBOX ②).
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FName GetContextToolItem() const;

	/** 그 부위 버튼을 띄울 상태인지. 가리킬 장비가 없으면 그 부위 버튼만 접는다. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool IsEquipButtonVisible(EEquipSlot EquipSlot) const;

	/** 지금 그 부위에 그 장비를 들고 있는지 — 버튼 글자가 `해제`/`장착` 중 무엇인지를 이게 정한다. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool IsEquipButtonItemEquipped(EEquipSlot EquipSlot) const;

	/** 그 부위 버튼 글자 — `<아이템 이름> 장착` / `<아이템 이름> 해제`. 가리킬 장비가 없으면 빈 FText. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetEquipButtonText(EEquipSlot EquipSlot) const;

	/**
	 * 그 부위 버튼을 탭한 것과 같은 일을 한다. 화면 버튼도 2층 검사도 이 길 하나만 쓴다.
	 * @return 장착·해제가 실제로 일어났으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	bool ToggleEquipButtonItem(EEquipSlot EquipSlot);

	/**
	 * 화면에 세우는 장비 버튼의 부위들 — 왼쪽 위에서부터 이 순서로 쌓는다.
	 * `EEquipSlot::None` 은 장비가 아닌 행의 값이라 버튼이 없다.
	 */
	static TArray<EEquipSlot> GetEquipButtonSlots();

	/**
	 * 그 부위 버튼의 **위젯 이름** (`EquipButton_Weapon`). 3층 검사 스위치 `-SnareTouch=equip:<부위>` 와
	 * 2층 트리 검사가 이 함수로 이름을 얻는다 — 이름을 두 군데 적으면 한쪽이 바뀔 때 손가락이 허공을 짚는다.
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	static FName GetEquipButtonWidgetName(EEquipSlot EquipSlot);

	/**
	 * 포탈 버튼을 띄울 상태인지 (M6-02b). 판정은 `UZoneTravelComponent::CanTravel()` 하나뿐이다 —
	 * 갈 곳이 확정된 포탈이 사거리 안에 있을 때만 참이다.
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	bool IsZoneTravelButtonVisible() const;

	/** 포탈 버튼 글자 — 갈 곳 이름은 표(`DT_ZoneTable`)가 정한다 (DataDriven.md 1). 없으면 빈 FText. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetZoneTravelButtonText() const;

	/**
	 * 포탈 버튼을 탭한 것과 같은 일을 한다. 화면 버튼도 2층 검사도 이 길 하나만 쓴다.
	 * @return 호스트에서 실제로 떠났으면 true (게스트는 요청만 보내고 false)
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	bool TryZoneTravel();

	/**
	 * 재화 잔액 줄이 볼 백엔드 캐시를 정한다 (M7-03). `BindToCharacter` 가 게임 인스턴스에서 찾아
	 * 부르고, 2층 검사는 게임 인스턴스가 없는 월드에서 도니 직접 부른다 (Verification.md 6).
	 * **읽기만 한다** — 이 위젯에 잔액을 바꾸는 길은 없다 (CLAUDE.md 8).
	 * @param Backend 볼 백엔드. nullptr 이면 잔액 줄이 `-` 로 돌아간다.
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void BindToBackend(USnareBackendSubsystem* Backend);

	/** 잔액 줄 글자 — `코인 120` / `젬 3` 두 줄. 아직 백엔드에게 못 들었으면 숫자 대신 `-`. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetBalanceText() const;

	/** 화면에 보이는 위젯을 실제 상태에 맞춘다. 알림을 받을 때마다 부른다. */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void RefreshDisplay();

	/**
	 * 버프 칸(M13-16, DESIGN 13.6)을 소유 기기 바이탈에 맞춘다 — 켜진 버프만 칸이 보이고 남은 초를 적는다.
	 * 남은 시간은 초마다 줄어드는데 알림이 없어 그리기 틱이 부른다(미니맵과 같은 이유). 2층 검사도 직접 부른다.
	 */
	void UpdateBuffSlots();

	/** 지금 보이는 버프 칸 수. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	int32 GetVisibleBuffSlotCount() const;

	/** 그 버프 칸 글자 — `이동 180초`. 칸이 없거나 접혀 있으면 빈 FText. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetBuffSlotText(EBuffKind Kind) const;

	/**
	 * 도구 칸에 **든 동안만** 듣는 장비 효과 한 줄 (M15-11, DESIGN 13.5 · INBOX 2026-09-25 ⑤).
	 * 망원경·위성 통신기는 먹는 것도 쓰는 것도 아니라 들고 있는 것 말고는 "쓰고 있다" 는 표시가 없었다.
	 * 효과는 **아이템 표 칸**에서 그대로 읽는다 — 어느 도구가 무엇을 하는지 HUD 에 또 적지 않는다
	 * (DataDriven.md 1). 새 효과 칸이 표에 생기면 여기 한 줄만 는다.
	 * @return `망원경 · 시야 ×1.3`. 효과 칸이 빈 도구(손도끼)나 빈손이면 빈 FText
	 */
	static FText MakeHeldGearText(const FItemRow* ToolRow, const TArray<const FItemRow*>& CarriedRows = TArray<const FItemRow*>());

	/** 도구 행 하나의 효과 줄 (`이름 · 효과`). 효과가 없으면 빈 FText. */
	static FText MakeGearLine(const FItemRow* ToolRow);

	/** 배낭에 든 「소지만으로 효과」 행들 (`bPassiveWhileCarried`) — 장착과 상관없이 효과가 켜진다. */
	static TArray<const FItemRow*> FindCarriedPassiveRows(const UDataTable* Table, const UInventoryComponent* Inventory);

	/** 지금 보이는 든 장비 줄 글자. 줄이 없거나 접혀 있으면 빈 FText. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	FText GetHeldGearText() const;

	/** 든 장비 줄을 도구 칸에 맞춘다. 장비가 바뀔 때마다 `RefreshDisplay` 가 부른다(2층 검사도 직접 부른다). */
	void UpdateHeldGearLine();

	/**
	 * 보물 자리 다시받기(M13-15 `GetTreasure`)를 배낭마다 **성공할 때까지** 잇는다 (M13-16, DESIGN 13.8).
	 * 물은 시각부터 재물음 간격이 지나기 전엔 다시 묻지 않고, 답이 오기 전에도 겹쳐 묻지 않는다.
	 * 실패해도 아는 표식은 배낭이 그대로 둔다. 로그인 판정은 부르는 쪽(틱) 몫이다 — 2층 검사가 시각을 넣어 직접 부른다.
	 */
	void UpdateTreasureRestore(UBackpackComponent* Backpack, double NowSeconds);

	/** 보물 재물음 간격(초, DESIGN 13.8). 2층 검사가 같은 값으로 시각을 민다. */
	static double GetTreasureRestoreRetrySeconds();

	// ── 사냥 총 탄 HUD (M39-04, 명세 `docs/specs/hunting-gun.md` 2절) — 총을 들었을 때만 보인다 ──

	/** 탄 글 `탄창/가방 탄약`(`7/25`). 총을 안 들었으면 빈 FText. 가방 수는 백엔드 캐시 그대로다. */
	FText GetGunAmmoText() const;

	/** 탄 글 왼쪽 그림을 든 총의 탄 행 `IconRef` 로 맞춘다. */
	void UpdateGunAmmoIcon();

	/** 탄 글·재장전 버튼이 펴져 있는지. */
	bool IsGunHudVisible() const;

	/** 탄 글·재장전 버튼을 든 것·탄창·가방에 맞춘다. 틱이 부르고 2층도 직접 부른다. */
	void UpdateGunHud();

	/** 재장전 버튼을 누른 것과 같은 일 — 화면 버튼도 2층도 이 길 하나. @return 서버에 물었으면 true */
	bool TryReloadGun();

	/** 가방에 탄약이 없어 서버가 거절했을 때 알약 문구. 2층이 맞댄다. */
	static FText GetNoAmmoText();

protected:
	virtual void NativeDestruct() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/**
	 * 미니맵 점(M12-03)만은 알림이 없다 — 걷는 동안 위치가 계속 바뀌므로 그리는 프레임마다 옮긴다.
	 * 주기 타이머가 아니라 그리기 틱이라 설계서에 없는 주기 숫자가 생기지 않는다.
	 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 아래 넷은 WBP 에서 같은 이름으로 만들면 그걸 쓰고, 없으면 RebuildWidget 이 자리표시로 만들어 넣는다.
	// BindWidgetOptional 인 이유 — 디자인이 들어오기 전에도 HUD 가 화면에 나와야 3층 캡처를 찍는다.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AnimalNameText;

	/** 이름 아래 고지 줄 (M21-07g). 우두머리일 때만 보인다. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AnimalNoteText;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> AnimalHealthBar;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> CaptureButton;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CaptureButtonText;

	// 공격 버튼은 **모바일의 유일한 공격 수단**이다 (감독 지시 2026-09-20 12:05 (e)) — 포획 버튼 왼쪽.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> AttackButton;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AttackButtonText;

	// 재장전 버튼(M39-04)은 포획 버튼 **바로 위** — 공격 버튼 위는 포탈·안내 알약 자리라 한 칸 오른쪽에 둔다.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> GunReloadButton;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GunReloadButtonText;

	/** 탄 글 `7/25` — 재장전 버튼 바로 위. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GunAmmoText;

	// 알림 카드 글자 — 포획 성공·이벤트 알림이 떠 있는 동안만 왼쪽 위 크림 카드에 뜬다(M15-02).
	// 이름이 `GuideText` 인 것은 WBP 바인딩 호환 때문이다. 조작 안내 문장은 더 띄우지 않는다(레퍼런스엔 없다).
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GuideText;

	// 버프 칸 줄(M13-22). WBP 에 없으면 체력 막대 위에 만든다. 칸은 줄 안이든 밖이든 `BuffSlot_<종>` 이름으로 찾는다.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> BuffRow;

	// 든 장비 효과 줄(M15-11) — 버프 칸과 **같은 줄 맨 앞**이다. 둘 다 "지금 내게 걸려 있는 것" 이라
	// 한자리에 모아 둔다. WBP 에 `HeldGearSlot`·`HeldGearSlotText` 가 있으면 그것을 쓴다.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> HeldGearSlot;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HeldGearSlotText;

	// 장비 버튼(M5-06c)은 왼쪽 위에 둔다 — 오른쪽 아래는 공격·포획, 왼쪽 아래는 이동 조이스틱 자리다.
	// M9-03 부터 **부위마다 하나**다. 칸을 배열로 두지 않고 이름을 붙여 둔 이유는 디자이너가 WBP 에서
	// 부위별로 다른 자리·모양을 줄 수 있어야 하기 때문이다 (`BindWidgetOptional` 은 이름으로 붙는다).
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> EquipButton_Weapon;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EquipButton_WeaponText;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> EquipButton_Tool;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EquipButton_ToolText;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> EquipButton_OutfitTop;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EquipButton_OutfitTopText;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> EquipButton_OutfitBottom;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EquipButton_OutfitBottomText;

	// 포탈 버튼(M6-02b)은 왼쪽 위 장비 버튼 **아래** — 오른쪽 아래는 공격·포획, 오른쪽 위는 잔액 줄이다.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UButton> ZoneTravelButton;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ZoneTravelButtonText;

	// 플레이어 체력·스태미나 게이지(M11-02)는 **오른쪽 아래** — 레퍼런스(레퍼런스)가 기력 줄을 두는
	// 자리다 (`docs/reference/hud_ingame.md`). 공격·포획 버튼 **위**에 쌓아 엄지를 가리지 않는다.
	// 동물 HP 바(위 가운데)와 헷갈리지 않게 화면 반대쪽 구석이다.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> PlayerHealthBar;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> PlayerStaminaBar;

	// 재화 잔액(M7-03)은 **오른쪽 위** — 자리와 문구는 `SnareBalanceHud` 가 정한다(목장 HUD 와 한 벌).
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BalanceText;

	// 미니맵(M12-03)은 **왼쪽 위**, 기본 숨김(M15-02) — 좌표 도구·보물·이벤트가 있을 때만 `MinimapLayer` 가 펼친다.
	UPROPERTY()
	TObjectPtr<UCanvasPanel> MinimapLayer;

	/** 미니맵 전체를 덮는 투명 누름 칸 — 누르면 큰 지도(지도 버튼과 같은 판)가 열린다. */
	UPROPERTY(Transient)
	TObjectPtr<UButton> MinimapTouchButton;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> MinimapPlayerDot;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MinimapZoneText;

	// 월드 이벤트(M12-07) 표식과 남은 시간 줄 — 이벤트가 떠 있는 동안만 보인다.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> MinimapEventDot;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MinimapEventText;

	// 위성 통신기(DESIGN 13.5)를 도구 칸에 들고 있으면 미니맵 아래 좌표 줄.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MinimapCoordText;

	// 메인 스토리 지금 단계의 상대 자리 표식 (STORY.md 「지도에 목표 표식」).
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> MinimapStoryDot;

	// 보물 지도(DESIGN 13.8)를 쓰면 보물 표식과 남은 시간 줄 — 찾는 보물이 있는 동안만 보인다.
	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> MinimapTreasureDot;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MinimapTreasureText;

	/** 미니맵 칸의 자리·크기·색과 지금 존 이름은 이 표가 안다 (`SnareZones::TablePath`). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HUD")
	TObjectPtr<UDataTable> ZoneTable;

	/** 어떤 행이 장비이고 이름이 무엇인지는 코드가 아니라 표가 안다 (DataDriven.md 1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HUD")
	TObjectPtr<UDataTable> ItemTable;

	/** 우두머리 고지 줄의 모자 확률은 이 표의 우두머리 행이 안다 (M21-07g). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HUD")
	TObjectPtr<UDataTable> DangerZoneTable;

private:
	/** 하이라이트가 바뀌었다 — 듣던 동물을 갈아 끼운다. */
	UFUNCTION()
	void HandleTargetChanged(AAnimalCharacter* NewTarget);

	UFUNCTION()
	void HandleAnimalHealthChanged(float NewHealth);

	// 플레이어 게이지가 움직이는 **유일한** 계기다 — 주기 갱신을 두지 않는다 (DESIGN 3.1 HUD 규칙).
	UFUNCTION()
	void HandlePlayerHealthChanged(float NewHealth);

	UFUNCTION()
	void HandlePlayerStaminaChanged(float NewStamina);

	UFUNCTION()
	void HandleAnimalStateChanged(EAnimalState NewState);

	/** 올가미 링이 섰다 — 실패 줄을 띄울 대상으로 기억한다 (M21-09d). */
	UFUNCTION()
	void HandleCaptureRingStarted(AAnimalCharacter* Target, float RingSeconds, float WindowSeconds);

	/** 알림 줄을 주어진 초만큼 띄운다 — `ShowNoticeBanner` 는 2초, 링 실패 줄은 도주 시간. */
	void ShowNoticeBannerFor(const FText& Message, float Seconds);

	/** 링 화면 위젯을 루트 캔버스에 세운다 (WBP 디자인이 들어와도 따로 챙긴다 — 버프 칸과 같은 길). */
	void BuildCaptureRing();

	/** 위험 체력 붉은 테두리 레이어를 루트에 깐다 (M60-03). */
	void BuildLowHealthEdge(UCanvasPanel& Root);

	/** 화면 포획 버튼. 입력 액션(`IA_Capture`)과 같은 길을 쓴다 — 판정을 두 군데 두지 않는다. */
	UFUNCTION()
	void HandleCaptureClicked();

	UFUNCTION()
	void HandleMinimapClicked();

	// 공격은 누르고 있는 동안 계속된다 (`UMeleeAttackComponent` 가 간격 타이머를 돈다) — 그래서
	// OnClicked(뗄 때 한 번)가 아니라 누름/뗌 두 개를 듣는다. 포획은 한 번이라 OnClicked 그대로다.
	UFUNCTION()
	void HandleAttackPressed();

	UFUNCTION()
	void HandleAttackReleased();

	// 화면 장비 버튼 넷. 전부 2층 검사가 쓰는 `ToggleEquipButtonItem(부위)` 로 들어간다 — 판단은 한 곳이다.
	// 부위마다 함수가 따로인 이유는 UMG 의 `OnClicked` 가 **인자를 못 싣는** 동적 델리게이트라서다.
	UFUNCTION()
	void HandleEquipWeaponClicked();

	UFUNCTION()
	void HandleEquipToolClicked();

	UFUNCTION()
	void HandleEquipOutfitTopClicked();

	UFUNCTION()
	void HandleEquipOutfitBottomClicked();

	/** 그 부위의 버튼·글자 칸. 위 여덟 칸 중 짝을 돌려준다 (`None` 이면 nullptr). */
	UButton* GetEquipButton(EEquipSlot EquipSlot) const;
	UTextBlock* GetEquipButtonTextBlock(EEquipSlot EquipSlot) const;

	/** 자리표시 트리가 방금 지은 버튼·글자를 그 부위 칸에 꽂는다 (WBP 가 있으면 여기 오지 않는다). */
	void SetEquipButtonWidgets(EEquipSlot EquipSlot, UButton* Button, UTextBlock* Text);

	/** 화면 재장전 버튼. 2층이 쓰는 `TryReloadGun` 과 같은 길이다. */
	UFUNCTION()
	void HandleGunReloadClicked();

	/** 재장전 답 — 탄약이 없어 거절되면 오류 알약 「탄약이 없다 — 잡화점」. */
	UFUNCTION()
	void HandleGunReloadReply(bool bAccepted, const FString& Code);

	/** 화면 포탈 버튼. 2층 검사가 쓰는 `TryZoneTravel` 과 같은 길이다. */
	UFUNCTION()
	void HandleZoneTravelClicked();

	/** 사거리 안 포탈이 바뀌었다 — 버튼을 폈다 접는 유일한 계기다 (타이머를 따로 돌리지 않는다). */
	UFUNCTION()
	void HandleZonePortalTargetChanged(AZonePortalActor* NewTarget);

	/** 백엔드 스냅샷이 캐시를 덮으면 들 수 있는 장비가 달라진다 (DESIGN 4.5). */
	UFUNCTION()
	void HandleInventoryCacheChanged();

	/**
	 * 걸친 것이 달라졌다 — 버튼을 누른 사람이 없어도(시작 지급 자동 장착·복제) 버튼을 다시 그린다 (M9-07b).
	 */
	UFUNCTION()
	void HandleEquipmentChanged();

	/** 업적 달성·도감 새 칸 알림 카드를 왼쪽 위 알림 카드에 띄운다 (M48-03). */
	void HandleAchievementNotice(const TArray<SnareAchievementNotice::FCard>& Cards);

	/** 백엔드가 새 잔액을 보내 줬다 — 화면 숫자가 바뀌는 **유일한** 계기다 (CLAUDE.md 8). */
	UFUNCTION()
	void HandleBackendSnapshotChanged();

	/** 플레이어의 장비·인벤토리 컴포넌트. 없으면 장비 버튼이 뜨지 않는다. */
	UEquipmentComponent* GetEquipment() const;
	UInventoryComponent* GetInventory() const;

	/** 포획 성공 줄을 띄우고 2초 타이머를 건다. 이름은 이때 박아 둔다 — 곧 하이라이트에서 빠진다. */
	void ShowCaptureBanner(const FText& AnimalName);

	/** 서버가 지급 대기열을 받아 준 뒤 획득 글을 알림 카드에 띄운다 (DESIGN 3.3 「획득 팝업」 · CLAUDE.md 8). */
	void HandleLootGrantPopup(const FText& Popup);

	void WatchAnimal(AAnimalCharacter* NewAnimal);
	void UnwatchAnimal();

	/** WBP 트리가 아직 비었는지 (루트가 없거나, 자식 없는 빈 패널 하나뿐인지). */
	bool NeedsPlaceholderTree() const;

	/** WBP 에 디자인이 없을 때만 부른다. 엔진 기본 위젯으로 최소 트리를 세운다. */
	void BuildPlaceholderTree();

	/** 버프·든 장비 칸이 설 가로 줄. WBP 가 안 준 경우에만 만든다 — 둘이 같은 줄을 쓰므로 만드는 자리는 하나다. */
	UHorizontalBox* EnsureBuffRow();

	/** 버프 칸을 이름(`BuffSlot_<종>`·`BuffSlotText_<종>`)으로 트리에서 찾고, 없는 칸만 `BuffRow` 에 만든다 (M13-22). */
	void BindOrBuildBuffSlots();

	/** 기력 번개 칸 수를 스태미나에 맞춘다 — 켜진 칸 = 칸 단위로 올린 비율 × 10. */
	void UpdateStaminaBolts();

	// 자리표시 트리가 세우는 겉모양 칸 (M15-02). WBP 디자인이 들어오면 비어 있다.
	UPROPERTY()
	TObjectPtr<UBorder> NoticeCard;

	/** 업적·도감 알림 카드 두 장 (M48-03c) — 알림 카드 바로 아래 같은 줄. */
	UPROPERTY()
	TObjectPtr<USnareNoticeCards> AchievementCards;

	/** 왼쪽 위 오류 알약 (M26-04) — 알림 카드와 같은 자리, 오류가 떠 있을 때만 펴진다. */
	UPROPERTY()
	TObjectPtr<UBorder> ErrorPill;

	UPROPERTY()
	TObjectPtr<UTextBlock> ErrorText;

	/** 가운데 위 수영 기운 경고 — 빨간 알약(`SwimWarnText` 의 짝)과 글자. */
	UPROPERTY()
	TObjectPtr<UBorder> SwimWarnPill;

	UPROPERTY()
	TObjectPtr<USnareNoticeText> SwimWarnText;

	bool bSwimWarnedThisDive = false;
	bool bSwimIntroShown = false;

	/** 공격 버튼 위 도구 안내 알약 (INBOX 18:10 ②) — 맞는 도구 없이 나무·광맥 앞일 때만 펴진다. */
	UPROPERTY()
	TObjectPtr<UBorder> NeedToolHint;

	UPROPERTY()
	TObjectPtr<UTextBlock> NeedToolHintText;

	/** 처음 들기 안내 알약 (M28-05b) — 도구 안내 알약과 같은 자리, 안내 중이고 그 알약이 없을 때만 펴진다. */
	UPROPERTY()
	TObjectPtr<UBorder> UsageHint;

	UPROPERTY()
	TObjectPtr<UTextBlock> UsageHintText;

	/** 탄 글 크림 카드 (M39-04) — 총을 들었을 때만 펴진다. WBP 디자인이 들어오면 비어 있다. */
	UPROPERTY()
	TObjectPtr<UBorder> GunAmmoCard;

	/** 탄 글 왼쪽 탄 아이템 그림 — 표 `IconRef` (든 총의 `AmmoItemID`). 그림이 없으면 접는다. */
	UPROPERTY()
	TObjectPtr<UImage> GunAmmoIcon;

	/** `GunAmmoIcon` 에 지금 그려 둔 탄 행 — 같으면 다시 안 읽는다. */
	FName ShownAmmoIconRow;

	/** `DT_UsageHints` — 처음 장비가 바뀔 때 읽는다. */
	UPROPERTY()
	TObjectPtr<const UDataTable> UsageHintTable;

	FName UsageHintKey;
	FText UsageHintLine;
	float UsageHintSecondsLeft = 0.f;
	const TCHAR* UsageHintSection = nullptr;   // nullptr = 게임 섹션(`SnareUsageHints::DefaultSection`)

	/** 손(도구·무기) 칸에 지난번 본 행 — 바뀐 칸만 「처음 들기」 계기다. */
	FName UsageHintLastTool;
	FName UsageHintLastWeapon;

	/** 지난번 다가가기 키 — 바뀐 순간만 계기다 (M28-05c). */
	FName UsageHintLastApproach;

	/** 손 칸이 바뀌었으면 그 줄을 띄울지 본다 (`HandleEquipmentChanged` 가 부른다). */
	void CheckUsageHintForHeldItems();

	/** 안 본 키면 띄우고 본 것으로 적는다. 막혔거나 다른 줄이 떠 있으면 버린다(본 것 아님). 띄웠으면 true. */
	bool TryShowUsageHint(FName Key);

	/** 첫 실행 안내가 아직 안 끝났거나 판(배낭·상점·제작 등)이 열려 있는가. */
	bool IsUsageHintBlocked() const;

	/** 안내 알약을 지금 상태(글·막힘·도구 안내 알약)에 맞춰 펴거나 접는다. */
	void UpdateUsageHintPill();

	/** 크림 알약을 공격 버튼 바로 위에 세운다 — 포탈 버튼이 떠 있으면 한 칸 위 (도구 안내·처음 들기 안내가 같이 쓴다). */
	void PlaceAboveAttackButton(UBorder* Pill) const;

	UPROPERTY()
	TObjectPtr<UBorder> AnimalCard;

	UPROPERTY()
	TObjectPtr<UTextBlock> PlayerHealthText;

	// 체력 알약 왼쪽 민트 동그라미 속 = 든 무기 아이콘 (시트 "무기 아이콘 40px"). 행이 바뀔 때만 다시 칠한다.
	UPROPERTY()
	TObjectPtr<UImage> PlayerWeaponIcon;

	FName ShownWeaponIconRow;

	UPROPERTY()
	TObjectPtr<UTextBlock> PillZoneText;

	UPROPERTY()
	TArray<TObjectPtr<UImage>> StaminaBolts;

	UPROPERTY()
	TObjectPtr<ASnarehavenCharacter> OwnerCharacter;

	UPROPERTY()
	TObjectPtr<AAnimalCharacter> ShownAnimal;

	UPROPERTY()
	TObjectPtr<UMeleeAttackComponent> WatchedMelee;

	UPROPERTY()
	TObjectPtr<UCaptureComponent> WatchedCapture;

	UPROPERTY()
	TObjectPtr<UZoneTravelComponent> WatchedZoneTravel;

	UPROPERTY()
	TObjectPtr<UPlayerVitalsComponent> WatchedVitals;

	// 획득 글은 네이티브 알림이라 끊을 때 이 포인터로 `RemoveAll` 한다.
	UPROPERTY()
	TObjectPtr<class ULootClaimComponent> WatchedLootClaim;

	/** 낚시 잡은 것 글 「+1 이름」 — 채집 획득 글과 같은 알림 줄 (M28-01). */
	UPROPERTY(Transient)
	TObjectPtr<class UTapFishingComponent> WatchedFishing;

	UPROPERTY()
	TObjectPtr<USnareBackendSubsystem> WatchedBackend;

	// 비어 있으면 성공 줄이 안 떠 있다는 뜻이다 — 떠 있는지를 bool 로 따로 들면 둘이 어긋난다.
	UPROPERTY()
	FText CaptureBannerText;

	FTimerHandle CaptureBannerTimer;

	// 알림 카드 글자와 같은 방식 — 비어 있으면 오류 알약이 안 떠 있다.
	UPROPERTY()
	FText ErrorBannerText;

	FTimerHandle ErrorBannerTimer;

	/** 마지막 막힘 소리 시각(초)과 횟수 — 같은 줄 연타를 `DenyCue` 간격 규칙으로 한 번만 낸다 (M55-03). */
	double LastDenySoundSeconds = -1.0e9;
	int32 DenySoundCount = 0;

	/** 올가미 링 화면 (M21-09d). 루트 캔버스 위 동물 발밑 자리에 선다. */
	UPROPERTY(Transient)
	TObjectPtr<USnareCaptureRingWidget> CaptureRing;

	/** 마지막으로 링을 건 동물 — 이 동물이 `Escaping` 으로 바뀌면 `놓쳤다!` 를 띄운다. 링이 끝나도 남겨 둔다(게스트는 판정 전에 링을 지운다). */
	TWeakObjectPtr<AAnimalCharacter> LastRingAnimal;

	/** 위험 체력 테두리 레이어 — 루트 맨 아래에 깔려 다른 UI 를 덮지 않는다. `SelfHitTestInvisible` (M60-03). */
	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> LowHealthEdgeLayer;

	/** 테두리 띠(가장자리 4방향 × 안쪽으로 6겹) — 불투명도만 바꾼다. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> LowHealthEdgeBands;

	float LowHealthEdgeOpacity = 0.f;

	/** 마을 이름 띠 (위 가운데, 동물 카드 아래) — 3초 뒤 스스로 접힌다. */
	UPROPERTY(Transient)
	TObjectPtr<USnareNoticeText> OutpostBannerText;

	/** 거점 표 (처음 쓸 때 한 번 읽는다). */
	UPROPERTY(Transient)
	TObjectPtr<const UDataTable> OutpostTable;

	SnareOutpostBanner::FTracker OutpostBannerTracker;

	/** 검사 스위치 `-SnareCaptureRingHold` — 포획 가능한 대상에 창 한가운데 순간의 링을 멈춰 그린다(3층 캡처 한 장용). */
	bool bCaptureRingHold = false;

	/** 점을 플레이어 자리로 옮기고, 칸이 바뀌었을 때만 존 이름을 고친다. */
	void UpdateMinimap();

	// 미니맵이 그린 칸들의 월드 범위와 그 맵 이름 — 자리표시 트리를 세울 때 한 번 정한다(틱마다 표를 다시 훑지 않게).
	FBox2D MinimapBounds = FBox2D(ForceInit);
	FName MinimapLevelName;

	// 보물 자리를 다시 받는 배낭 (M13-15). 다르면(새로 선 캐릭터) 처음부터 다시 묻는다.
	TWeakObjectPtr<UBackpackComponent> TreasureRestoreFor;
	bool bTreasureRestored = false;        // 서버 답을 받았다 — 이 배낭엔 더 묻지 않는다
	bool bTreasureRestorePending = false;  // 물었고 답을 기다린다
	double TreasureRestoreNextAskSeconds = 0.0;

	// 버프 칸 — `EBuffKind` 순번 − 1 자리(None 은 칸이 없다). `BindOrBuildBuffSlots` 가 채운다.
	UPROPERTY()
	TArray<TObjectPtr<UBorder>> BuffSlots;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> BuffSlotTexts;

	// 지난 틱에 보여 준 칸. 같으면 글자를 다시 넣지 않는다 (nullptr = 칸 밖).
	const FZoneRow* ShownMinimapRow = nullptr;

	/** 공격 버튼 글자와 그 위 도구 안내 줄을 대상에 맞춘다 (INBOX 18:10 ①②). 대상 훑기가 액터를 돌아 매 틱이 아니라 간격마다. */
	void UpdateAttackButtonText(double NowSeconds);

	// 공격 원 버튼 글자를 두 줄·맞춤 크기로 찍는다.
	void SetAttackDiscText(const FText& Verb);


	// 다음에 공격 버튼 글자를 다시 볼 시각. 간격은 cpp 의 `AttackButtonTextRefreshSeconds`.
	double AttackButtonTextNextSeconds = 0.0;
};
