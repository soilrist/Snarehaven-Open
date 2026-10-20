#pragma once

#include "CoreMinimal.h"
#include "Backend/SnareStory.h"
#include "Components/SlateWrapperTypes.h"
#include "Data/ItemAction.h"
#include "GameFramework/Character.h"
#include "Player/SnareBodyHaptic.h"
#include "SnarehavenCharacter.generated.h"

struct FItemRow;
class AAnimalCharacter;
class AGatherableActor;
class UAnimSequence;
class UCameraComponent;
class UCaptureComponent;
class UCraftingScreenWidget;
class UDataTable;
class UEquipmentComponent;
class UPlayerFaceBlinkComponent;
class UGatherComponent;
class UInputAction;
class UInputMappingContext;
class UHuntHudWidget;
class UUserWidget;
class USnareFirstRunGuideWidget;
class USnareNextGoalWidget;
class USnareStoryGoalWidget;
class USnareStoryDialogueWidget;
class USnareStoryChapterCardWidget;
class ACameraActor;
class UQuestScreenWidget;
class UPauseMenuWidget;
class URanchHudWidget;
class UAttendanceScreenWidget;
class UGiftScreenWidget;
class UBackpackScreenWidget;
class UInventoryComponent;
class UMeleeAttackComponent;
class UPlayerBodyAnimInstance;
class UPlayerVitalsComponent;
class UTapFishingComponent;
class UWaterTravelComponent;
class UWidgetComponent;
class UMountComponent;
class UZoneEffectComponent;
class URanchComponent;
class URanchInteractComponent;
class USkeletalMesh;
class USpringArmComponent;
class UStaticMeshComponent;
class UZoneTravelComponent;
struct FInputActionValue;

/**
 * 상호작용(「공격」) 버튼을 지금 누르면 무엇이 되는가 — 버튼 글자가 이걸 따른다 (INBOX 18:10 ①, DESIGN 3.3).
 * 처음 하는 사람은 버튼 하나가 채집·목장·작업대까지 맡는다는 걸 모른다 — 글자가 「베기·캐기·수확·열기」로 바뀌어야 안다.
 */
UENUM(BlueprintType)
enum class ESnareInteractVerb : uint8
{
	Attack,   // 동물이 있거나 대상이 없다 — 휘두르기
	PickUp,   // 떨어진 가방
	Chop,     // 도구로 나무 베기
	Mine,     // 곡괭이로 광석 캐기
	Harvest,  // 목장 찬 칸 수확
	Place,    // 목장 빈 칸에 펫·씨앗 넣기
	Open,     // 놓인 작업대·불·보관함·상인·의뢰인 화면 열기
	Gather,   // 맨손 채집물(열매·가지·돌)·바구니 채집 (INBOX 18:20 ②)
	Capture,  // 포획 가능한 동물·올가미 링 도는 중
	Fish,     // 낚싯대 들고 던질 거리 안 낚시 포인트·연타 중
	NeedHatchet,  // 나무 앞인데 벨 도구가 가방에도 없다 — 누르면 알림만, 휘두르지 않음 (INBOX 22:00 ②)
	NeedPickaxe,  // 광맥 앞인데 캘 도구가 가방에도 없다
	NeedRod,      // 낚시 포인트 앞인데 낚싯대가 가방에도 없다
	ShakeTree,    // 목장 다 자란 과일나무 열매 따기 (DESIGN 8.18)
	PlantFruit,   // 목장 빈 나무 자리에 과일 심기 (DESIGN 8.18)
	ChopFruitTree, // 목장 열매 딴 나무를 도끼로 베어 자리 비우기 (DESIGN 8.18)
	QuitFishing,  // 던져 입질을 기다리는 중 — 누르면 거둔다 (물가 판 「그만두기」 와 같은 함수)
	LightBeacon,  // 스토리 25단계 등대 탑 앞 — 누르면 불을 켠 것으로 대사·보고 (STORY.md 4절 가기 규칙 표)
};

// ROADMAP M0-03/M0-04: 가상 조이스틱(Enhanced Input IMC_Default/IA_Move) 으로 움직이고
// SpringArm 에 매달린 고정 각 카메라가 따라다니는 탑다운 캐릭터.
// 이동 속도 등 숫자는 엔진 기본값 그대로 두고 코드에 박지 않는다
// (docs/conventions/DataDriven.md — 숫자는 사람이 정한다).
UCLASS()
class SNAREHAVEN_API ASnarehavenCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ASnarehavenCharacter();

	/**
	 * 쏜 사람 본인 기기만 손에 반동을 준다 (M62-02 · DESIGN 2.2 「총 반동 진동」). 세기·길이·간격은 `SnareBodyHaptic::CueFor(EGun, …)` 표 그대로.
	 * 남의 기기·설정 끔·간격 안이면 안 튼다. 피해·탄창 계산은 안 건드린다.
	 * @return 실제로 틀었으면 true
	 */
	bool PlayShotHaptic(SnareBodyHaptic::EGun Gun);

	/**
	 * 때리거나 벤 사람 본인 기기만 손에 타격을 준다 (M63-02 · DESIGN 2.2 「타격 진동」). 세기·길이·간격은 `SnareBodyHaptic::CueFor(EStrike, …)` 표 그대로.
	 * 남의 기기·설정 끔·간격 안이면 안 튼다. 피해·채집 판정은 안 건드린다.
	 * @return 실제로 틀었으면 true
	 */
	bool PlayStrikeHaptic(SnareBodyHaptic::EStrike Strike);

	/**
	 * 먹은 사람 본인 기기만 손에 톡 준다 (M64-02 · DESIGN 2.2 「먹기 진동」). 세기·길이·간격은 `SnareBodyHaptic::CueFor(EEat, …)` 표 그대로.
	 * 남의 기기·설정 끔·간격 안이면 안 튼다. 회복·인벤토리 계산은 안 건드린다.
	 * @return 실제로 틀었으면 true
	 */
	bool PlayEatHaptic(SnareBodyHaptic::EEat Eat);

	/**
	 * 이동 진입점. Axis 는 조이스틱 축 그대로 (X = 화면 오른쪽, Y = 화면 위).
	 * Enhanced Input 바인딩과 2층 검사가 같은 길을 쓰도록 공개한다.
	 * @param Axis 조이스틱 입력 (-1..1, Axis2D)
	 */
	UFUNCTION(BlueprintCallable, Category = "Move")
	void MoveByAxis(const FVector2D& Axis);

	/**
	 * 카메라 거리·각을 SpringArm 에, 화각(CameraFieldOfViewDegrees)을 카메라에 반영한다.
	 * BeginPlay 가 CameraDistance/CameraPitchDegrees 로 부르고,
	 * 2층 검사는 테스트 전용 값으로 직접 부른다 (Verification.md 6).
	 * 아직 안 정한 숫자(-1)면 엔진 기본값을 그대로 두고 경고만 남긴다 (Verification.md 7).
	 * @param Distance     카메라 거리(cm), 음수면 미정
	 * @param PitchDegrees 내려다보는 각(도, 양수 = 아래로), 음수면 미정
	 */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void ApplyCameraSettings(float Distance, float PitchDegrees);

	/**
	 * 옷장 화면용 카메라 — 몸이 화면 높이의 `BodyScreenHeight` 몫, 가로로는 `ScreenX`(0~1) 지점에 오게 당긴다.
	 * `false` 면 부르기 전 거리·옆 이동으로 되돌린다.
	 */
	void SetWardrobeCamera(bool bOn, float ScreenX, float BodyScreenHeight);

	/**
	 * 도구 칸 카메라 배율(DESIGN 13.5 망원경 ×1.3)이 바뀌었으면 `CameraDistance × 배율` 로 다시 건다. Tick 이 부른다.
	 * 줌(휠·핀치, DESIGN 3.3 줌 표)도 여기서 곱한다 — `DeltaSeconds` 동안 목표 줌으로 부드럽게 다가간다.
	 * 배율이 그대로면 아무것도 안 한다 — 망원경을 안 들면 검사용 탑다운 카메라(-SnareTopDown)를 덮지 않는다.
	 */
	void UpdateToolCamera(float DeltaSeconds = 0.f);

	/** 휠·핀치가 정한 줌 목표. [CameraZoomMin, CameraZoomMax] 로 자른다. */
	void SetCameraZoomTarget(float Zoom);
	float GetCameraZoomTarget() const { return CameraZoomTarget; }
	float GetCameraZoomCurrent() const { return CameraZoomCurrent; }

	/**
	 * 집 실내 카메라 (M18-02) — 안에 들면 `CameraDistance × InteriorCameraMul` 로 다가가고, 나오면 1 로 돌아간다.
	 * `USnareHouseRoofSubsystem` 이 안팎이 바뀔 때 부른다. 다가가는 건 `UpdateToolCamera` 가 프레임마다 부드럽게.
	 */
	void SetInteriorCamera(bool bInside) { bInteriorCamera = bInside; }
	float GetInteriorCameraCurrent() const { return InteriorCameraCurrent; }

	/**
	 * 게임이 실제로 카메라에 거는 설계서 숫자 (DESIGN 3.3 표).
	 * 구도 검사가 손으로 넣은 값이 아니라 **게임 값 그대로** 재도록 꺼내 본다 — 손으로 넣으면
	 * 설계서와 코드가 어긋나도 검사가 통과한다.
	 */
	UFUNCTION(BlueprintPure, Category = "Camera")
	float GetCameraDistance() const { return CameraDistance; }

	/** 게임이 실제로 거는 내려다보는 각(도). 위 `GetCameraDistance` 와 같은 까닭으로 꺼내 본다. */
	UFUNCTION(BlueprintPure, Category = "Camera")
	float GetCameraPitchDegrees() const { return CameraPitchDegrees; }

	/** 탑다운 카메라를 매단 팔. 배선 검사용으로만 꺼내 본다. */
	UFUNCTION(BlueprintPure, Category = "Camera")
	USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** 탑다운 카메라. 배선 검사용으로만 꺼내 본다. */
	UFUNCTION(BlueprintPure, Category = "Camera")
	UCameraComponent* GetTopDownCamera() const { return TopDownCamera; }

	/** 자리표시 몸통(보이는 캡슐 대역). 3층 캡처에 무엇이 찍히는지 2층에서 확인하려고 꺼내 본다. */
	UFUNCTION(BlueprintPure, Category = "Placeholder")
	UStaticMeshComponent* GetPlaceholderBody() const { return PlaceholderBody; }

	/**
	 * 보이는 몸을 자리표시 실린더에서 사람 스켈레탈 메시(`SK_Player_Body`)로 갈아 끼운다 (M5-01c).
	 * 동물(`AAnimalCharacter::ApplyBodyFromRow`)과 같은 방식이다 — 크기·높이는 숫자를 새로 만들지 않고
	 * 콜리전 캡슐과 메시 바운드에서 계산한다 (DataDriven.md 3).
	 * @return 메시가 실제로 올라갔으면 true. 에셋이 없으면 자리표시를 그대로 두고 false.
	 */
	UFUNCTION(BlueprintCallable, Category = "Body")
	bool ApplyPlayerBody();

	/**
	 * 속도·공격 상태를 보고 지금 돌릴 클립을 고른다 (M5-01c). Tick 과 2층 검사가 같은 길을 쓴다.
	 * 고른 클립은 AnimBP 에셋 없이 코드 애님 인스턴스(`UPlayerBodyAnimInstance`, M18-10)가 섞는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Body")
	void UpdateBodyAnimation();

	/**
	 * 가끔 나오는 대기 변주 (INBOX 결함 12:40 · DESIGN 3.3 「대기 변주」). 대기 클립만 보인 채 정해진 시간이 지나면
	 * `IdleVariantAnims` 중 하나를 골라 다음 `UpdateBodyAnimation` 이 한 번 튼다. Tick 과 2층 검사가 같은 길을 쓴다.
	 * 화면만이다 — 복제하지 않고 기계마다 따로 고른다(대기 몸짓은 판정에 안 쓴다).
	 */
	void UpdateIdleVariant(float DeltaSeconds);

	/** 기다리는 시간 범위를 바꾸고 지금까지 센 대기 시간을 잊는다 — 2층 검사가 검사 전용 값을 넣는 자리. */
	void SetIdleVariantDelay(float MinSeconds, float MaxSeconds);

	/** 틀려고 고른(또는 도는 중인) 대기 변주. 없으면 nullptr. */
	UAnimSequence* GetIdleVariantAnim() const { return IdleVariantAnim; }

	/** 대기 변주 후보 (`A_Player_IdleLook`·`A_Player_IdleStretch`). */
	const TArray<TObjectPtr<UAnimSequence>>& GetIdleVariantAnims() const { return IdleVariantAnims; }

	/**
	 * 조이스틱을 민 크기를 보고 최고 이동 속도를 걷기·달리기 클립이 밟는 속도로 맞춘다 (DESIGN 3.3).
	 * 엔진 기본 `MaxWalkSpeed` 를 쓰면 클립과 속도가 안 맞아 발이 미끄러진다 (사용자 지적 2026-09-21).
	 */
	UFUNCTION(BlueprintCallable, Category = "Move")
	void UpdateMoveSpeed();

	/** 지금 몸에서 돌고 있는 클립. 2층 검사가 Idle↔Walk 전환을 이걸로 본다. */
	UFUNCTION(BlueprintPure, Category = "Body")
	UAnimSequence* GetCurrentBodyAnim() const { return CurrentBodyAnim; }

	/** 지금 클립에 걸린 재생 속도. 1 이면 클립 원래 빠르기다 — 2층 검사가 미끄러짐 보정을 이걸로 본다. */
	UFUNCTION(BlueprintPure, Category = "Body")
	float GetBodyAnimPlayRate() const { return CurrentBodyPlayRate; }

	/** 몸을 섞는 애님 인스턴스 (M18-10). 몸이 아직 없거나 시험이 SingleNode 로 자세를 잡아 둔 동안은 nullptr. */
	UPlayerBodyAnimInstance* GetBodyAnimInstance() const;

	/** 머리 젖힘 뼈·각도 — 같은 몸을 쓰는 NPC(`AShopVendorActor`)가 플레이어와 같은 고개 들기를 건다. */
	FName GetHeadBoneName() const { return HeadBoneName; }
	float GetHeadLiftDegrees() const { return HeadLiftDegrees; }

	/**
	 * 마지막으로 시작한 아이템 사용 동작과 그 횟수 (M15-12). 클립만 보면 구별이 안 되는 행동이 있어서다 —
	 * 광맥(`Mine`)과 맨주먹 공격은 둘 다 `A_Player_Attack` 을 튼다. "어느 버튼이 어느 동작을 골랐나" 는
	 * 2층 검사가 이 두 값으로 본다(클립 이름만으로 판정하지 않는다 — PROMPT ③-12).
	 */
	UFUNCTION(BlueprintPure, Category = "Body")
	EItemAction GetLastItemAction() const { return LastItemAction; }

	/** 아이템 사용 동작을 시작한 횟수. 안 늘면 그 행동은 몸 클립이 없다는 뜻이다(제작·제련·요리). */
	UFUNCTION(BlueprintPure, Category = "Body")
	int32 GetItemActionCount() const { return ItemActionCount; }

	/** 지금(마지막) 아이템 동작이 고른 클립과 남은 초 — 클립 길이를 2층 검사가 잰다. */
	const UAnimSequence* GetItemActionAnim() const { return ItemActionAnim; }
	float GetItemActionSecondsLeft() const { return ItemActionSecondsLeft; }

	/**
	 * 손에 든 행이 「정해진 길이」(`bUseAnimFixedLength`)면 그 쓰기 클립 길이(초), 아니면 0 — 전기톱·고급/전기 곡괭이는
	 * 한 번 누르면 이 길이만큼 내밀어 떨다 돌아오고 그동안 누름을 버린다 (사용자 2026-10-04). 공격 컴포넌트가 간격으로 쓴다.
	 */
	float GetHeldFixedUseSeconds() const;

	/** 공격(쓰기) 버튼을 누르고 있나 — 누른 순간 켜고 뗀 순간 끈다. 전기톱 버팀이 이걸 본다 (감독 보정 2026-10-06 02:15). */
	bool IsUseInputHeld() const { return bUseInputHeld; }

	/**
	 * 쓰러져 누워 있는 동안인가 (사망 ~ 부활, DESIGN 3.1 「쓰러짐 → 부활」 · M5-01e2).
	 * 이 동안 이동·공격 입력을 받지 않고, 몸 클립은 쓰러짐이 공격·걷기보다 세다.
	 */
	UFUNCTION(BlueprintPure, Category = "Body")
	bool IsDowned() const { return bDowned; }

	/** 검사용 — 쓰러짐부터 부활까지 걸리는 시간(초). 클립 길이 + 표의 누운 시간이다. 못 재면 음수. */
	UFUNCTION(BlueprintPure, Category = "Body")
	float GetDownedDurationSeconds() const;

	/** 달리기로 갈리는 조이스틱 크기. 2층 검사가 이 값으로 경계 바로 아래·위를 만든다. */
	UFUNCTION(BlueprintPure, Category = "Move")
	float GetRunInputThreshold() const { return RunInputThreshold; }

	/** 검사용 — Idle/걷기 히스테리시스의 진입·이탈 문턱(cm/s). 검사가 숫자를 베끼면 두 곳이 갈린다. */
	float GetMoveAnimEnterSpeed() const { return MoveAnimEnterSpeedCmPerSecond; }
	float GetMoveAnimExitSpeed() const { return MoveAnimExitSpeedCmPerSecond; }

	/** 근접 공격(자동 상호작용). 공격 버튼·하이라이트는 전부 이 컴포넌트가 판정한다 (M1-03). */
	UFUNCTION(BlueprintPure, Category = "Combat")
	UMeleeAttackComponent* GetMeleeAttack() const { return MeleeAttack; }

	/** 포획 액션. 대상은 근접 공격 컴포넌트가 하이라이트한 동물이다 (M1-05). */
	UFUNCTION(BlueprintPure, Category = "Capture")
	UCaptureComponent* GetCapture() const { return Capture; }

	/** 채집(터치 즉시 획득). 사거리는 근접 공격 컴포넌트에서 빌려 쓴다 (M1-06). */
	UFUNCTION(BlueprintPure, Category = "Gather")
	UGatherComponent* GetGather() const { return Gather; }

	/** 장비 겉모습 (M5-06b). 손 소켓에 무기를 붙인다 — 스탯은 없다 (DESIGN 3.5). */
	UFUNCTION(BlueprintPure, Category = "Equipment")
	UEquipmentComponent* GetEquipment() const { return Equipment; }
	UPlayerFaceBlinkComponent* GetFaceBlink() const { return FaceBlink; }

	/** 인벤토리 **캐시**. 보유 원본은 백엔드다 (M1-04 · DESIGN 4.5 · CLAUDE.md 8). */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	UInventoryComponent* GetInventory() const { return Inventory; }

	/** 목장 슬롯 **캐시**. 칸 내용은 백엔드 스냅샷만 바꾼다 (M2-04a · CLAUDE.md 8). */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	URanchComponent* GetRanch() const { return Ranch; }

	/** 낚시터 슬롯 **캐시**. 목장과 같은 클래스·다른 거점이다 (M10-02 · DESIGN 8.4). */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	URanchComponent* GetFishing() const { return Fishing; }

	/** 목장 슬롯 상호작용. 사거리는 근접 공격 컴포넌트에서 빌려 쓴다 (M2-04c). */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	URanchInteractComponent* GetRanchInteract() const { return RanchInteract; }

	/** 존 포탈 상호작용. 사거리는 근접 공격 컴포넌트에서 빌려 쓴다 (M6-02b). */
	UFUNCTION(BlueprintPure, Category = "Zone")
	UZoneTravelComponent* GetZoneTravel() const { return ZoneTravel; }

	/** 플레이어 체력·스태미나 (M11-01). 숫자는 `DT_PlayerVitalsTable` 행에서 온다 (DESIGN 3.1). */
	UFUNCTION(BlueprintPure, Category = "Vitals")
	UPlayerVitalsComponent* GetPlayerVitals() const { return PlayerVitals; }

	/** 화면에 올라간 사냥 HUD (M1-08). 로컬 조종이 아니거나 목장이면 만들지 않으므로 nullptr 일 수 있다. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	UHuntHudWidget* GetHuntHud() const { return HuntHud; }

	/** 화면에 올라간 목장 HUD (M2-06a). 목장 게임모드가 아닌 레벨에서는 nullptr. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	URanchHudWidget* GetRanchHud() const { return RanchHud; }

	/**
	 * 거점 요청 결과 글(`USnareBaseSubsystem::OnBaseMessage` — 놓기·텐트·집 올리기의 성공 글·거절 글)을 HUD 에 띄운다.
	 * 제작 판이 열려 있으면 판 토스트가 이미 보이니 안 띄운다(한 글이 두 번 뜨지 않게). 사냥 HUD 가 있으면
	 * 알림/오류 알약, 없으면(거점) 목장 HUD 거절 줄. 배낭에서 놓아 판이 닫힌 채면 글이 하나도 안 보였다 (#0520 빈 칸).
	 * 띄웠으면 true.
	 */
	static bool RouteBaseMessage(const FText& Message, bool bError, const UCraftingScreenWidget* CraftScreen,
		UHuntHudWidget* Hunt, URanchHudWidget* Ranch);

	/**
	 * 화면에 올라간 배낭 화면 (M13-21). 로컬 조종이 아니면 nullptr.
	 * 들고 있는 이유(M15-18): 상점 판이 열릴 때 **배낭도 오른쪽에 같이 열려야** 한다
	 * (시트 `docs/reference/shop_vendor.md` — 사고팔 때 내 칸이 보여야 한다). 상인 액터가 이 길로 찾는다.
	 */
	UFUNCTION(BlueprintPure, Category = "HUD")
	UBackpackScreenWidget* GetBackpackScreen() const { return BackpackScreen; }

	/** 일시정지 메뉴 (M1-10b). 한 번도 연 적이 없거나 로비 맵이면 nullptr. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	UPauseMenuWidget* GetPauseMenu() const { return PauseMenu; }

	/**
	 * ESC 로 일시정지 메뉴를 열거나 닫는다. 입력 바인딩과 검사용 스위치(`-SnarePauseMenu`)가 같은 길을 쓴다.
	 * 로비 맵에서는 아무것도 하지 않는다 (`UPauseMenuWidget::ShouldAllowPauseMenu`).
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void TogglePauseMenu();

	/**
	 * 공격을 시작한다 (M1-10c). 입력 액션(`IA_Attack` Started)과 화면 공격 버튼이 **같은 함수**를 부른다 —
	 * 동물이 없으면 채집·목장 슬롯으로 넘어가는 순서(DESIGN 3.3)를 두 군데 적지 않으려는 것이다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void BeginAttack();

	/** 공격을 멈춘다. 손가락을 떼거나 키를 놓았을 때 (`IA_Attack` Completed/Canceled). */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void EndAttack();

	/**
	 * `-SnareAutoAttack` 검사 손이 공격 간격마다 한 번 다시 누른다 (한 번 누름 = 한 번, 2026-10-04).
	 * 타이머 람다 밖에 둔 이유: 2층 시험이 스위치 없이 같은 손을 부르려고.
	 */
	void RepressHeldAttackOnce();

	/**
	 * 지금 `BeginAttack()` 을 부르면 무엇이 되는지 — **같은 순서**(낚시 연타 → 동물 → 가방 → 채집 → 목장 칸 → 낚시 → 놓인 것)로
	 * 보되 조회만 한다(하이라이트·요청을 건드리지 않는다). 사냥 HUD 의 버튼 글자가 이걸 읽는다 (INBOX 18:10 ①·18:20 ②).
	 */
	UFUNCTION(BlueprintPure, Category = "Combat")
	ESnareInteractVerb GetInteractVerb() const;

	/** 상황 버튼이 지금 「불 켜기」 인가 — 이 클라가 아는 단계가 25(등대 불 켜기)이고 아직 안 눌렀고 탑 반경 안. */
	bool CanLightLighthouse() const;

	/** 이 실행에서 「불 켜기」 를 눌렀나 — 25단계 도착(대사·보고)은 이 값을 본다. */
	bool HasPressedLighthouseLight() const { return bLighthouseLightPressed; }

	/**
	 * 눈앞 대상에 맞는 **가진** 도구의 표 행 이름 (없으면 None) — 도구를 요구하는 채집물이면 그걸 벨·캘 도구
	 * (덤 도구 → 요구 도구 → 그 밖에 맞는 것), 아니고 던질 거리에 낚시 포인트가 있으면 가장 빨리 무는 낚싯대.
	 * 든 것도 가진 것으로 센다. HUD 도구 버튼과 상황 버튼의 자동 들기(INBOX 22:00 ①)가 같은 답을 보려고 여기 하나다.
	 */
	FName FindContextToolRow() const;

	/**
	 * `IA_Attack` 의 누름이 들어오는 길 (M1-10e). **잔류 입력 규칙은 여기서만 본다** — 화면 공격
	 * 버튼은 `BeginAttack()` 을 바로 부른다(사람이 이 레벨에서 실제로 누른 것이라 막을 이유가 없다).
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void HandleAttackInputStarted();

	/** `IA_Attack` 의 뗌(Completed/Canceled). 잔류 입력 잠금도 여기서 풀린다 (M1-10e). */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void HandleAttackInputCompleted();

	/** 지금 `IA_Attack` 의 누름을 받아들이는가 — 레벨 진입 때 눌린 채 넘어온 키면 false (M1-10e). */
	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsAttackInputArmed() const { return bAttackInputArmed; }

	/**
	 * 키를 뗄 때까지 `IA_Attack` 의 누름을 무시한다 (M1-10e). 레벨 진입 직후 공격 키가 **이미 눌려
	 * 있을 때만** 건다 — 로비 `혼자 사냥` 버튼의 왼쪽 클릭이 레벨 이동을 넘어 새 폰까지 따라와
	 * 아무도 안 눌렀는데 반복 공격이 돌았다 (감독 재현 2026-09-20 13:00).
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void DisarmAttackInputUntilRelease();

	/**
	 * 사냥·목장 HUD 를 띄운 뒤 거는 입력 모드 (M1-10e). `GameAndUI`(DoNotLock·커서 유지)여야 UMG
	 * 버튼이 삼킨 클릭이 뷰포트의 `IA_Attack` 으로 새지 않는다 — 일시정지 메뉴를 닫을 때
	 * (`UPauseMenuWidget::CloseMenu`)와 **같은 설정**이라 여기 한 함수로 묶어 둘 다 쓴다.
	 */
	static void ApplyHudInputMode(APlayerController* PlayerController);

	// M3-06: 드롭·펫·채집을 판정하는 건 호스트지만(DESIGN 4.1 권위 표) **지급 요청은 잡은 사람의
	// 클라이언트에 쌓인다.** 아래 셋은 그래서 Client RPC 다 — 언리얼이 이 폰을 가진 연결로만 보내므로
	// 호스트 프로세스에 있는 게스트 복제본에는 아무것도 안 쌓이고, 호스트 자기 폰이면 그 자리에서 실행된다.
	// 왜 나눠서 쌓나: 대기열은 나중에 "누구 계정에 넣어 달라" 를 백엔드에 보낼 줄이고(M2-01),
	// 호스트에 몰아 두면 그 줄이 누구 것인지 알 수 없다. 보유 수량은 여전히 백엔드만 늘린다 (CLAUDE.md 8).

	/**
	 * 사냥 처치 한 마리를 잡은 클라이언트 대기열에 `animal.<종>` 줄로 쌓는다. 무엇이 나올지는 서버 `ClaimLoot {kills}` 가
	 * 그 종의 드롭 표로 굴린다 (지시 감독 10-20 19:30 ① — 호스트가 굴린 아이템 목록을 믿지 않는다).
	 */
	UFUNCTION(Client, Reliable)
	void Client_QueueKillGrant(FName AnimalID);

	/** 포획한 펫 등록 요청을 잡은 클라이언트에 쌓는다. */
	UFUNCTION(Client, Reliable)
	void Client_QueuePetGrant(FName PetItemID);

	/** 채집 지급 요청을 집은 클라이언트에 쌓는다. */
	UFUNCTION(Client, Reliable)
	void Client_QueueGatherGrant(FName GatherableID, FName ItemID, int32 Count, int32 ToolUses);

	/**
	 * 동물 한 대를 맞은 사람의 기기에서 체력을 깎고 `A_Player_Hit` 을 한 번 튼다 (M5-07d2 · DESIGN 3.1).
	 * 맞았는지는 호스트가 `AAnimalCharacter::TryAttackOnAuthority` 에서 이미 판정했다 — 체력(`UPlayerVitalsComponent`)은
	 * 복제되지 않는 기기별 값이라, 드롭 지급처럼 그 사람 쪽에서 적용한다. 이동·공격 입력은 막지 않는다.
	 */
	UFUNCTION(Client, Reliable)
	void Client_ReceiveAnimalHit(float Damage);

	/**
	 * 동물에게 맞은 순간 — 모든 기기가 그 사람 자리에서 `PlayerHurt` 를 낸다 (M44-02 · DESIGN 2.2 「플레이어 몸 소리」).
	 * 호스트가 `Client_ReceiveAnimalHit` 와 같은 순간에 부른다. 소리뿐이라 Unreliable, 권위와 무관한 연출.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayHurtVoice();

	/** 이 사람이 지금까지 낸 몸 소리 요청 수 (소리 이름별). 2층 검사가 "한 대 = 신음 한 번" 을 이 값으로 본다. */
	int32 GetBodyVoiceRequestCount(FName Cue) const { return BodyVoiceRequestCounts.FindRef(Cue); }

	/** 맞음 소리 이름 (`SFX_PlayerHurt_*`). */
	static FName GetHurtCue();

	/** 쓰러짐 소리 이름 (`SFX_PlayerDown_*`). */
	static FName GetDownCue();

	/** 먹기 소리 이름 (`SFX_Eat_*`) · 독 먹기 소리 이름 (`SFX_EatPoison_*`). */
	static FName GetEatCue();
	static FName GetEatPoisonCue();

	/**
	 * 서버 `ConsumeItem` 이 먹기를 받아 준 뒤 내 기기에서만 씹는 소리를 낸다 (M44-04 · DESIGN 2.2 「플레이어 몸 소리」).
	 * 회복 ≥ 0 = `Eat` · 음수(독) = `EatPoison`, 크기 0.6 · `Play2D`. 응답 전·거절이면 부르지 않는다 (CLAUDE.md 8).
	 */
	void PlayEatVoice(float Heal);

	/**
	 * 내 몸에 아이템 사용 동작을 한 번 틀고 남의 화면에도 보낸다 (DESIGN 8.8). 먹기는 백엔드가 먹기를 받아들인 뒤
	 * 내 기기(`UBackpackComponent::RequestConsume`)에서 부른다 — 동작은 그림일 뿐 지급·회복과 무관하다 (CLAUDE.md 8).
	 */
	void PlayItemAction(EItemAction Action);

	/** 움직일 때까지 되풀이하는 동작인가 (춤·앉기). 나머지는 클립 한 번 길이로 끝난다. */
	static bool IsLoopingItemAction(EItemAction Action);

	/**
	 * 조종이 붙거나 바뀐 순간 (엔진이 부른다 — 부모와 같이 public 이라 2층 검사도 같은 길을 탄다).
	 * 여기서 맵을 건너온 장비를 되건다 (M15-10) — 게스트 기기에서는 폰이 먼저 서고 조종이 나중에 붙어서,
	 * `BeginPlay` 만으로는 "내 폰인가"를 알 수 없다.
	 */
	virtual void NotifyControllerChanged() override;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostInitializeComponents() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// 아래 입력·카메라 값은 플레이 중 바뀌지 않는 설정이라 인스턴스별 수정을 막으려고
	// EditAnywhere 가 아니라 EditDefaultsOnly 로 둔다 (engines/unreal.md 코드 규칙 1 — 이유 없이 섞지 않기).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	/** 공격(자동 상호작용) 버튼. 한 번 누름 = 동작 한 번, 누르고 있어도 반복 없음 — 다음 누름까지의 간격은 UMeleeAttackComponent 가 정한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> AttackAction;

	/** 포획 버튼. 대상이 Capturable 일 때만 실제로 포획된다 (DESIGN 3.1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> CaptureAction;

	/** 일시정지 메뉴 여닫기(ESC · 패드 Start). 월드는 멈추지 않는다 (M1-10b · DESIGN 3.4). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> PauseAction;

	/**
	 * 화면에 올릴 사냥 HUD 위젯 클래스. 기본값은 `WBP_HUD_Hunt` 이고, 그 에셋이 없으면
	 * C++ 클래스(`UHuntHudWidget`)를 그대로 쓴다 — 디자인이 없어도 HUD 는 떠야 한다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HUD")
	TSubclassOf<UHuntHudWidget> HuntHudClass;

	/** 목장 HUD 위젯 클래스. 사냥 HUD 와 같은 규칙 — `WBP_HUD_Ranch` 가 없으면 C++ 클래스를 그대로 쓴다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HUD")
	TSubclassOf<URanchHudWidget> RanchHudClass;

	/** 일시정지 메뉴 위젯 클래스. 위 둘과 같은 규칙 — `WBP_PauseMenu` 가 없으면 C++ 클래스를 그대로 쓴다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HUD")
	TSubclassOf<UPauseMenuWidget> PauseMenuClass;

	/** 보이는 몸. 기본값은 M5-01a 가 임포트한 `SK_Player_Body` 이고, 없으면 자리표시 실린더가 남는다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<USkeletalMesh> PlayerBodyMesh;

	/** 가만히 서 있을 때 도는 클립 (`A_Player_Idle`, M5-01b). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> IdleAnim;

	/**
	 * 가끔 나오는 대기 변주 둘 (`A_Player_IdleLook` 두리번 · `A_Player_IdleStretch` 기지개, 레퍼런스 `idleVariation1·2`).
	 * 첫 장·끝 장이 대기 첫 장과 같고 한 번만 돈다. 비면 변주 없이 대기만.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TArray<TObjectPtr<UAnimSequence>> IdleVariantAnims;

	/** 대기만 보인 채 이만큼(초, 범위 안에서 매번 고름) 지나면 변주 하나. DESIGN 3.3 「대기 변주」 표. 보이는 값이다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	float IdleVariantDelayMinSeconds = 8.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	float IdleVariantDelayMaxSeconds = 15.f;

	/** 움직일 때 도는 클립 (`A_Player_Walk`). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> WalkAnim;

	/** 달릴 때 도는 클립 (`A_Player_Run`). 조이스틱을 `RunInputThreshold` 이상 밀면 여기로 갈린다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> RunAnim;

	/** 때리는 동안 도는 클립 (`A_Player_Attack`). 공격 간격은 UMeleeAttackComponent 가 정한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> AttackAnim;

	/**
	 * 맞았을 때 한 번 도는 클립 (`A_Player_Hit`, DESIGN 8.8 필수 7종 · M5-01e2).
	 * **동물 한 대에만 튼다**(M5-07d2 `Client_ReceiveAnimalHit`) — 탈진·존 특성은 초마다 조금씩이라
	 * 거기에 걸면 매초 움찔한다(감독 지시 2026-09-23 16:10 ④). 이동·공격 입력은 막지 않는다 (DESIGN 3.1).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> HitAnim;

	/**
	 * 쓰러질 때 **되풀이 없이 한 번** 도는 클립 (`A_Player_Death`, DESIGN 8.8 · M5-01e2).
	 * 부활까지의 시간은 이 클립 길이 + 표의 누운 시간이다 (DESIGN 3.1 「쓰러짐 → 부활」) —
	 * 길이를 코드에 적지 않고 클립에서 읽는다 (DataDriven.md 3).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> DeathAnim;

	/** 물속에서 거의 멈춰 있을 때 도는 클립 (`A_Player_SwimIdle`, 제자리 헤엄 · DESIGN 8.8). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> SwimIdleAnim;

	/** 물속에서 움직일 때 도는 클립 (`A_Player_SwimMove`, 몸이 수평인 자유형). 문턱은 걷기와 같은 두 문턱이다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> SwimMoveAnim;

	/** 배에 앉아 있을 때 도는 클립 (`A_Player_BoatSit`, 바닥에 무릎 세워 앉기). 운전석·승객석이 같다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> BoatSitAnim;

	/** 탈것에 타 있을 때 도는 클립 (`A_Player_Ride`, 등을 끼고 앉기 · DESIGN 8.3). 원점 = 엉덩이 밑면(안장 윗면). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> RideAnim;

	/** 낚싯대를 던질 때 **한 번** 도는 클립 (`A_Player_FishCast`). 입질 대기가 시작된 순간 클립 길이만큼 튼다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> FishCastAnim;

	/** 입질을 기다릴 때 도는 클립 (`A_Player_FishIdle`). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> FishIdleAnim;

	/** 입질 뒤 연타·잡힘 응답 대기 동안 도는 클립 (`A_Player_FishReel`). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> FishReelAnim;

	/** `EItemAction::Eat` 클립 (`A_Player_Eat`). 먹기 자체는 몸 동작 없이 머리 위 「+N」 만 띄운다(`ShowHealPop`, DESIGN 8.8) — 이 클립을 트는 길은 없다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> EatAnim;

	/** 맨손으로 채집할 때 **한 번** 도는 클립 (`A_Player_Gather`, 0.6초 — 서서 오른팔만 앞으로 뻗었다 돌아옴 · DESIGN 8.8). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> GatherAnim;

	/** 손도끼로 나무를 벨 때 한 번 도는 클립 (`A_Player_Chop`, 0.6초 — 허리 높이 수평 휘두르기 · `chop_make.py`). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> ChopAnim;

	/** 이모트 클립 (DESIGN 2.2 「이모트」) — 인사·만세는 한 번, 춤은 되풀이. `emote_make.py` 로 구웠다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> WaveAnim;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> CheerAnim;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UAnimSequence> DanceAnim;

	/**
	 * 수영 대기 중 수면이 몸 원점(발밑) 위 몇 cm 에 오나 — **클립 공간** 값이라 `BodyMeshScale` 을 곱해 쓴다.
	 * 값·근거는 DESIGN 8.9 「수영 몸 높이」 행 (glb 스키닝 측정: 목 0.56 · 어깨 위 0.56~0.59 · 턱 0.45).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	float SwimIdleSinkCm = 50.f;

	/** 수영 이동 중 같은 값. 몸이 수평이라 등 윗면(0.40~0.42)이 수면에 살짝 나오게 — DESIGN 8.9 같은 행. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	float SwimMoveSinkCm = 37.f;

	/** 물에 들고 날 때 몸 높이가 목표로 따라가는 빠르기(`FInterpTo` 속도). 0.5초쯤에 붙는 보이는 값이다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	float BodyPlacementBlendSpeed = 6.f;

	/** 감기 버튼을 누른 직후 감기 클립 재생 속도와 그 유지 시간(초) — 연타가 손에 보이게. 보이는 값이다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	float FishReelTapPlayRate = 1.6f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	float FishReelTapBoostSeconds = 0.35f;

	/**
	 * 기본 클립(대기·걷기·달리기·물·탈것·배·낚시·쓰러짐)이 바뀔 때 교차 섞는 시간(초). DESIGN 3.3 몸 섞기 표 (M18-10).
	 * 걷기 한 걸음(약 0.5초)의 절반보다 짧아야 출발이 굼떠 보이지 않고, 0.15초 아래면 다시 툭 끊겨 보인다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	float BodyCrossfadeSeconds = 0.2f;

	/**
	 * 행동(맞음·아이템 행동·휘두르기)이 들어오고 빠질 때 섞는 시간(초). DESIGN 3.3 같은 표.
	 * 휘두르기 한 바퀴(공격 간격 0.6초)의 1/6 — 더 길면 타격 순간 팔이 덜 올라와 맞는 박자가 흐려진다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	float BodyActionBlendSeconds = 0.1f;

	/**
	 * 움직이며 행동할 때 행동 클립이 먹는 상체의 시작 뼈. 이 뼈와 그 자식(가슴·목·머리·양팔)만 행동을 받고,
	 * 골반·다리는 걷기·달리기를 그대로 밟는다 — `SK_Player_Body` 의 척추 첫 마디다 (DESIGN 3.3).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	FName UpperBodyBoneName = TEXT("Spine");

	/**
	 * 모든 클립 위에 머리 뼈(`HeadBoneName`)를 턱 드는 쪽으로 젖히는 각도(도). DESIGN 3.3 몸 섞기 표.
	 * 55° 로 내려다보는 게임 카메라에서 정수리 대신 얼굴(두 눈)이 보이게 — 레퍼런스는 무엇을 하든 위에서 눈이 보인다(INBOX 12:40 ③).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	float HeadLiftDegrees = 15.f;

	/** 머리를 젖히는 뼈 — `SK_Player_Body` 의 머리 뼈다(바르코 몸, 대기 원본 `idle_v2_make.py` 의 `Head`). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Body")
	FName HeadBoneName = TEXT("Head");

	/**
	 * 걷기 클립(`A_Player_Walk`)이 **클립 공간에서** 밟는 속도(cm/s). **지어낸 숫자가 아니라 클립에서 잰 값**이다 —
	 * `tools/unreal/measure_player_anim_speed.py` 가 디딘 발끝(`LeftToeBase`/`RightToeBase`)이 몸 기준으로
	 * 뒤로 흐르는 속도를 읽는다 (바르코 클립 기준, 로그 `logs/lap85_speed3.log` — 걷기 30.3).
	 * DESIGN 3.3 은 "루트 이동 거리 ÷ 재생 시간" 이라고 적었지만 이 클립들에는 루트 모션이 없어
	 * (`enable_root_motion=False`) 재는 자리만 발로 옮겼다 — 재는 대상은 같은 "클립이 밟는 속도" 다.
	 *
	 * 루프#139 에서 클립을 한 바퀴로 다듬으면서(`tools/unreal/trim_player_anim_loops.py`) 앞머리 정지
	 * 프레임이 빠져 값이 조금 올라갔다 — 30.3 -> 32.2, 114.3 -> 114.6 (로그 `logs/measure_anim139.log`).
	 * 루프#352 걷기 v3(레퍼런스 종종걸음, 주기 0.933 -> 0.667초)는 보폭만 줄이고 속도를 맞춰 만들어 32.2 -> 32.3
	 * 루프#1308 걷기 v5 는 걷기 최고 100cm/s 결정에 맞춰 보폭을 키워(22 -> 39cm) 32.3 -> 57.8 (로그 `logs/lap1308_measure_player_anim_speed.log`)
	 * (로그 `logs/lap352_measure_player_anim_speed.log`) — 게임 속 걷기 속도는 사실상 그대로다.
	 *
	 * **클립 공간**이라 쓸 때는 `BodyMeshScale` 을 곱한다 — 몸을 캡슐(176cm)에 맞춰 키운 만큼
	 * 걸음도 커진다. 곱하는 자리는 `UpdateMoveSpeed`·`UpdateBodyAnimation` 둘 다다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Move")
	float WalkClipSpeedCmPerSecond = 57.8f;

	/** 조이스틱을 살짝만 밀어도 나아가는 최소 속도(cm/s) — 엔진 `MinAnalogWalkSpeed`. 0(엔진 기본)이면 반 밀기가 최고의 절반이라 기어간다.
	 *  루프#1308 감독 결정: 80 (걷기 최고의 약 80%·달리기의 40%, DESIGN 3.3). 최고 속도보다 낮은 상황(물·탈것·쓰러짐)에선 최고 속도로 눌러 둔다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Move")
	float MinAnalogWalkCmPerSecond = 80.f;

	/** 달리기 클립(`A_Player_Run`)이 클립 공간에서 밟는 속도(cm/s). 같은 도구·같은 로그에서 114.6.
	 *  루프#353 달리기 v3(주기 0.533 -> 0.4초, 보폭만 줄임)를 이 값에 맞춰 구워 실측도 114.6
	 *  (로그 `logs/lap353_measure_player_anim_speed.log`, v2 는 실측 111.5 라 발이 3% 미끄러졌다). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Move")
	float RunClipSpeedCmPerSecond = 114.6f;

	/**
	 * 조이스틱을 이만큼 이상 밀면 달리기 (0~1). 감독 결정 0.9 (DESIGN 3.3) — 달리기 버튼을 새로
	 * 만들지 않고 `MoveByAxis` 가 받은 입력 크기로 가른다 (모바일 버튼을 안 늘리는 3.3 의 선).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Move")
	float RunInputThreshold = 0.9f;

	/**
	 * Idle 에서 걷기 클립으로 **들어가는** 속도 문턱(cm/s). 아래 `MoveAnimExitSpeedCmPerSecond` 와
	 * 짝이다 — 문턱 하나로 가르면 `CharacterMovementComponent` 의 가속·감속·마찰 때문에 출발·정지·
	 * 방향 전환 순간 속도가 그 하나를 여러 번 넘나들고, 그때마다 `PlayBodyAnimation` 이 클립을
	 * 프레임 0 으로 되감아 걸음이 멈칫한다 (사용자 실사용 지적 · 감독 재현 2026-09-22 14:10).
	 *
	 * 플레이어가 나아가는 속도·거리는 하나도 안 바꾸고(`UpdateMoveSpeed` 는 그대로), 어느 클립을
	 * 보여 줄지만 정한다. 값·근거는 DESIGN 3.3 「몸 섞기」 표 — 조이스틱을 반쯤 민 걷기가 약 25cm/s 라
	 * 그보다 낮아야 살살 걸어도 다리가 걷는다(옛 30 은 반쯤 민 걷기를 대기로 보여 다리가 미끄러졌다, M18-10).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Move")
	float MoveAnimEnterSpeedCmPerSecond = 15.f;

	/**
	 * 걷기 클립에서 Idle 로 **나오는** 속도 문턱(cm/s). 들어가는 문턱보다 낮아야 히스테리시스가 된다 —
	 * 두 값 사이에서는 지금 상태를 그대로 두는 것이 이 고침의 전부다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Move")
	float MoveAnimExitSpeedCmPerSecond = 8.f;

	/** 카메라 거리(cm). DESIGN 3.3 표 (M15-01 레퍼런스 구도) — 숫자를 바꾸려면 설계서부터 (DataDriven.md 3). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera")
	float CameraDistance = 4500.f;

	/** 내려다보는 각(도, 양수 = 아래로 내려다봄). DESIGN 3.3 표. 음수면 미정 취급. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera")
	float CameraPitchDegrees = 55.f;

	/**
	 * 세로 화각(도, 화면 비율과 무관하게 세로 고정). DESIGN 3.3 표 — 좁은 화각으로 멀리서 찍어야 원근 왜곡 없이 레퍼런스처럼 납작한 3/4 구도가 된다.
	 * `ApplyCameraSettings` 가 거리·각과 같이 건다. 0 이하면 미정 취급(엔진 기본 90 유지).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera")
	float CameraFieldOfViewDegrees = 30.f;

	/** 줌 배율 가장 가까이 (DESIGN 3.3 줌 표). 1 = 기본 거리 `CameraDistance`. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera")
	float CameraZoomMin = 0.6f;

	/** 줌 배율 가장 멀리 (DESIGN 3.3 줌 표). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera")
	float CameraZoomMax = 1.3f;

	/** 마우스 휠 한 칸의 줌 배율 변화 (DESIGN 3.3 줌 표). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera")
	float CameraZoomWheelStep = 0.1f;

	/** 줌이 목표 배율을 따라가는 빠르기 (`FMath::FInterpTo` 속도, DESIGN 3.3 줌 표). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera")
	float CameraZoomInterpSpeed = 10.f;

	/** 집 안 카메라 거리 배율 (DESIGN 8.11 집 실내 표, M18-02). 1 = 바깥과 같음. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera")
	float InteriorCameraMul = 0.45f;

	/** 실내 배율이 목표를 따라가는 빠르기 (`FMath::FInterpTo` 속도, DESIGN 집 실내 표). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera")
	float InteriorCameraInterpSpeed = 6.f;

	/** 지금 카메라에 걸린 배율 = 도구 배율 × 줌 × 실내 (1 = 없음). `UpdateToolCamera` 가 바뀔 때만 다시 건다. */
	float AppliedToolCameraMul = 1.f;

private:
	void OnMove(const FInputActionValue& Value);
	void OnAttackStarted(const FInputActionValue& Value);
	void OnCaptureStarted(const FInputActionValue& Value);
	void OnAttackCompleted(const FInputActionValue& Value);
	void OnPauseStarted(const FInputActionValue& Value);

	/** 월드 탭·클릭을 뗌 (M13-20) — 그 화면 자리의 내 가구를 칠하기로 연다 (`USnareBaseBuildComponent::TryTapFurniture`). */
	void OnWorldTouchReleased(ETouchIndex::Type FingerIndex, FVector Location);
	void OnWorldClickReleased();

	/** 줌 입력 — 휠 한 칸, 두 손가락 핀치 (DESIGN 3.3 줌). 조이스틱·버튼 위 손가락은 Slate 가 먹어 핀치에 안 섞인다. */
	void OnZoomWheelIn();
	void OnZoomWheelOut();
	void OnWorldTouchPressed(ETouchIndex::Type FingerIndex, FVector Location);
	void OnWorldTouchMoved(ETouchIndex::Type FingerIndex, FVector Location);

	/** 마지막 발소리 뒤로 땅 위를 걸은 거리(cm) — `SnareSound::StrideCm` 마다 한 번 (DESIGN 2.2 소리). */
	float StepDistanceCm = 0.f;

	float CameraZoomTarget = 1.f;
	float CameraZoomCurrent = 1.f;
	bool bInteriorCamera = false;
	float InteriorCameraCurrent = 1.f;
	/** 월드에 닿아 있는 손가락 화면 자리 — 둘일 때만 핀치. */
	TMap<int32, FVector2D> WorldTouches;
	/** 직전 프레임 두 손가락 거리(px). 0 = 핀치 아님. */
	float PinchLastDistancePx = 0.f;
	/** 이번 누름에 핀치가 있었으면 뗄 때 가구 탭으로 치지 않는다. */
	bool bPinchedSinceTouch = false;

	/**
	 * 3층 검사용 — 명령줄이 준 키(`-SnareMoveKey=W`)를 누른 채로 시작한다 (헤드리스에는 키보드가 없다).
	 * 키를 `APlayerController::InputKey` 로 **그대로** 넣어 `IMC_Default` 의 키 매핑(M1-10a 의
	 * Swizzle/Negate)을 거치게 한다 — 매핑을 건너뛰면 "키보드로 움직인다" 를 본 것이 아니다.
	 */
	void StartCheckMove(const FString& MoveKeyName);

	/**
	 * 검사용 이동이 켜져 있으면 `Tick` 이 **프레임마다** 속도·재생 속도·클립 시각을 남긴다.
	 * 1초 간격 위치 로그로는 "5걸음마다 멈칫" 같은 프레임 단위 증상을 못 본다 (결함 ②, 루프#139).
	 */
	bool bCheckMoveTracing = false;

	void LogMoveTrace(float DeltaSeconds);

	/** 발 흐름을 재는 본 — 클립 속도를 재는 `tools/unreal/measure_player_anim_speed.py` 와 같은 발이다. */
	static constexpr const TCHAR* CheckTraceFootBone = TEXT("LeftToeBase");

	/** 직전 프레임의 몸 기준 발 위치 — 한 프레임 이동량을 내려고 들고 있는다. */
	FVector LastCheckFootLocal = FVector::ZeroVector;

	/**
	 * 3층 검사용 — `-SnareMoveScale=0.5` 처럼 **조이스틱을 그만큼만 민 상태**로 시작한다.
	 * 키보드 키는 눌림·뗌 둘뿐이라 `-SnareMoveKey=` 로는 항상 꽉 민 것(1.0)이 된다 — 살짝 민
	 * 조이스틱에서 발이 미끄러지지 않는지 보려면 이 길이 필요하다 (감독 검증 요구 2026-09-21).
	 * `MoveByAxis` 를 직접 불러 `IMC_Default` 매핑은 건너뛴다 — 매핑 자체는 `-SnareMoveKey=` 가 본다.
	 * @param Scale 조이스틱 크기 (0~1). 앞이 아니라 `S` 와 같은 -X 방향으로 민다(사냥터 바닥이 넓다).
	 */
	void StartCheckMoveAxis(float Scale);

	/**
	 * 3층 검사용 — 검사용 이동 로그 옆에 **게임 안 머리 방향**을 한 줄 찍는다 (INBOX 결함 12:40, 루프#399).
	 * 2층 `Snarehaven.Player.Body.HeadLiftShowsFace` 와 같은 식(참조 자세 몸 앞 +Y 를 머리 뼈와 함께 돌림)으로
	 * 얼굴 앞 방향을 월드로 옮겨, 높이각·실제 카메라 쪽과의 사이각·몸과 카메라의 수평 차를 남긴다.
	 * 2층은 「몸이 카메라를 마주 볼 때」 만 재므로, 게임 화면과 숫자가 어긋나는 까닭을 여기서 가린다.
	 */
	void LogCheckHeadFacing() const;

	/**
	 * 3층 검사용 — `-SnareTouch=stick|attack|capture|equip[:부위]` 로 화면에 **진짜 터치 이벤트**를 넣는다 (M4-04).
	 * 모바일에는 키보드가 없다. 함수를 직접 부르거나 키를 넣으면 "모바일 입력 경로가 산다" 를 본 것이
	 * 아니라 우리 함수가 도는지만 본 것이다 — `FSlateApplication::OnTouch*` 로 넣어야 가상 조이스틱·
	 * 화면 버튼이 **자기 히트테스트로** 받는다. 데스크톱에서는 터치 인터페이스가 안 뜨므로
	 * 실행에 `-faketouches` 를 같이 붙인다 (`docs/checks/unreal.md`).
	 * @param TouchTargetName `stick`(이동 조이스틱을 끝까지 민다) · `attack`(공격 버튼을 누른 채로 둔다)
	 *                        · `capture`(포획 버튼이 켜지는 순간 한 번 누른다 — `-SnareAutoAttack` 과 함께)
	 *                        · `equip`·`equip:tool`·`equip:top`·`equip:bottom`(그 **부위**의 장비 버튼을
	 *                        한 번 누른다 — M9-03 이 버튼을 부위로 나눴다. 부위를 안 적으면 무기다)
	 */
	void StartCheckTouch(const FString& TouchTargetName);

	/**
	 * 3층 검사용 — `-SnareChase` 는 **가장 가까운 동물 쪽으로** 조이스틱을 꽉 민 채로 둔다.
	 * `-SnareMoveKey=`·`-SnareMoveScale=` 은 방향이 고정이라, 맞고 도망가는 동물을 따라가는 화면을
	 * 만들 수가 없었다(루프#131 이 "대각선으로 가는 스위치가 없다" 로 미뤄 둔 것과 같은 벽).
	 * 1초마다 **거리와 스태미나를 같이** 남긴다 — 밸런스 패치(DESIGN 3.1 도주 140cm/s)가 노린 것이
	 * "스태미나 0 으로 걸어도 따라붙는다" 라서, 캡처 한 장이 아니라 줄어드는 거리로 봐야 한다.
	 */
	void StartCheckChase();

	/**
	 * `-SnareChase` 의 이동 입력 — **매 프레임** 넣는다. 0.01초 타이머로 넣던 때는 프레임을 건너뛰는
	 * 틈에서 `MoveByAxis` 가 안 불려, 그 프레임은 손을 뗀 것으로 처리됐다(Tick 의 `bMoveInputThisFrame`).
	 * 그래서 속도가 걷기 최고속에 못 미치고(관측 26~57cm/s) 스태미나도 설계 5/초 대신 4/초로 줄었다 —
	 * 검사 쪽 인공물이 패치 수치를 가리던 것이라 Tick 으로 옮겼다.
	 */
	void TickCheckChase();

	/** `-SnareMoveScale=` 이 민 크기(0 이면 꺼짐). Tick 이 매 프레임 `MoveByAxis` 로 넣는다. */
	float CheckMoveAxisScale = 0.f;

	/** `-SnareChase` 가 켜졌나. 켜져 있으면 Tick 이 매 프레임 `TickCheckChase` 를 부른다. */
	bool bCheckChasing = false;

	/**
	 * `-SnareChase` 가 쫓을 동물. **한 번 고른 뒤에는 그 동물만 쫓는다** — 죽거나 포획돼야 다음으로 넘어간다.
	 * 고정하지 않으면 도망자가 멀어지는 순간 옆에 서 있던 동물이 최근접이 돼 대상이 갈아타고,
	 * 로그의 거리가 **쫓아가 좁힌 것처럼** 보인다(루프#154 첫 측정이 그래서 239cm -> 51cm 를 1초에 찍었다).
	 */
	class AAnimalCharacter* FindNearestAnimal() const;

	/** 위에서 고른 동물. `FindNearestAnimal` 이 const 라 `mutable` 이다 — 검사 전용이라 게임 로직은 안 본다. */
	mutable TWeakObjectPtr<class AAnimalCharacter> CheckChaseTarget;

	/** 검사 터치를 한 박자 뒤부터 되묻으며 넣는다 — 위젯이 한 번 그려져야 화면 좌표가 잡힌다. */
	void TickCheckTouch();

	/**
	 * 터치 인터페이스 에셋이 말하는 **이동 스틱의 화면 자리**. 못 찾으면 false.
	 * @param OutCenter 스틱 중심(화면 좌표) — 손가락을 내려놓을 자리
	 * @param OutFullTilt 끝까지 민 자리(화면 좌표) — 중심에서 스틱 반지름만큼 아래
	 */
	bool GetMoveStickOnScreen(FVector2D& OutCenter, FVector2D& OutFullTilt) const;

	/** 사냥 HUD 버튼(`AttackButton`·`CaptureButton`)의 화면 한가운데. 안 그려졌으면 false. */
	bool GetHudButtonOnScreen(FName ButtonName, FVector2D& OutCenter) const;

	/**
	 * 3층 검사용 — `-SnareAutoCapture` 가 붙으면 포획이 가능해지는 순간 한 번만 포획한다.
	 * 헤드리스에는 마우스가 없어 `포획` 버튼을 누를 길이 없다 (M1-10e2 의 캡처).
	 * 화면 버튼·`IA_Capture` 와 **같은 함수**(`UCaptureComponent::TryCaptureTarget`)를 탄다.
	 */
	void TickCheckCapture();

	/**
	 * 3층 검사용 — `-SnareTravel` 이 붙으면 포탈이 사거리에 들어오는 순간 한 번만 누른다.
	 * 헤드리스에는 포탈 버튼을 누를 손가락이 없다 (M6-03b 의 캡처).
	 * 화면 버튼과 **같은 함수**(`UZoneTravelComponent::TryTravelToNearest`)를 탄다.
	 */
	void TickCheckTravel();

	/** 3층 검사용 — `-SnareKillPlayer=<초>` 가 예약한 시각에 한 번, 내 체력을 0 으로 내린다 (M5-01e2). */
	void TickCheckKill();

	/** 레벨 진입 직후 남은 입력을 끊는다 (M1-10e). 로컬 조종 폰만 — 남의 폰엔 누를 손가락이 없다. */
	void FlushLevelEntryInput();

	/** `IMC_Default` 에서 `IA_Attack` 에 걸린 키 중 지금 눌려 있는 게 있는가 (M1-10e). */
	bool IsAnyAttackKeyDown(const APlayerController* PlayerController) const;

	/**
	 * 같은 클립을 다시 걸지 않고 바뀔 때만 건다 — 매 프레임 PlayAnimation 을 부르면 포즈가 처음으로 되감긴다.
	 * 재생 속도는 클립이 그대로여도 매 프레임 따라가야 한다 (살짝 민 조이스틱은 천천히 걷는다).
	 * @param bRestart 같은 클립이어도 **이번 한 프레임만** 처음으로 되감는다. 휘두르기가 새로 시작될 때만 쓴다
	 *                 (되풀이 재생에 기대면 반복 타이머와 클립 위치가 어긋난다 — 결함 2026-09-23 16:40 ②).
	 */
	void PlayBodyAnimation(UAnimSequence* Anim, bool bLoop, float PlayRate, bool bRestart = false);

	/**
	 * 행동 클립(맞음·아이템 행동·휘두르기)을 기본 클립 위에 얹는다 (M18-10). 기본 층은 지금 이동 상태 클립으로 두고,
	 * 서 있으면 몸 전체·움직이면 상체만 행동을 받는다. 인자 뜻은 `PlayBodyAnimation` 과 같다.
	 */
	void PlayBodyAction(UAnimSequence* Anim, bool bLoop, float PlayRate, bool bRestart = false);

	/** 행동·낚시·쓰러짐이 없을 때 도는 이동 상태 클립(배·물·탈것·대기·걷기/달리기)과 그 재생 속도. */
	UAnimSequence* SelectBaseBodyClip(float& OutPlayRate) const;

	/**
	 * 지금 손에 든 것의 든 자세·쓰기 클립 (M18-11). 휘두르는 도구(쓰기 클립 있음) → 무기 → 다른 도구(바구니: 비움) →
	 * 맨손(`DT_PlayerCombatTable`) 순으로 고른다. 든 자세만 있는 도구(낚싯대 `HoldRod`)는 든 자세를 덮고 쓰기는 무기 것 그대로.
	 * 칸이 비면 nullptr — 부르는 쪽이 옛 공용 클립을 쓴다.
	 */
	void ResolveHeldBodyClips(UAnimSequence*& OutHold, UAnimSequence*& OutUse, UAnimSequence*& OutUseAlt) const;

	/** 휘두르는 손의 행 — 휘두르는 도구가 무기 손을 가리면 도구 행, 아니면 무기 행(없으면 nullptr). */
	const FItemRow* GetSwingHandRow() const;

	/**
	 * 누르는 동안 쓰기 클립 버팀 (행 `UseHoldStartSeconds`·`UseHoldEndSeconds` — 전기톱). 그 클립이 도는 중에 버튼이 눌려 있으면
	 * 구간을 걸고, 떼면 놓아 돌아오기 끝 구간을 튼다. 몸 클립을 이번 틱에 정했으면 true.
	 */
	bool UpdateHeldUseHold();

	/** 몸에 섞기 애님 인스턴스를 건다. 이미 걸려 있으면 그대로 돌려준다. 메시가 없으면 nullptr. */
	UPlayerBodyAnimInstance* EnsureBodyAnimInstance();

	/** 전환 로그를 찍고 `CurrentBodyAnim`·`CurrentBodyPlayRate` 를 맞춘다 — 2층 시험이 읽는 값이다. */
	void NoteShownBodyClip(UAnimSequence* Anim, float PlayRate);

	/** 지금 몸에서 도는 클립. 아직 몸이 없거나 클립을 못 찾았으면 nullptr. */
	UPROPERTY()
	TObjectPtr<UAnimSequence> CurrentBodyAnim;

	/** 지금 클립에 걸어 둔 재생 속도. 같은 값을 다시 걸지 않으려고 들고 있는다. */
	float CurrentBodyPlayRate = 1.f;

	/** 몸 클립에 마지막으로 반영한 휘두르기 번호. 컴포넌트의 `GetSwingCount` 와 다르면 새 휘두르기다. */
	int32 LastBodySwingCount = 0;

	/** 몸 클립에 마지막으로 반영한 재장전 동작 번호 (M39-06b2). 다르면 재장전 클립을 처음으로 되감는다. */
	int32 LastBodyReloadCount = 0;

	/** 채집 동작을 마지막으로 뿌린 프레임 — 덤 지급(`OnGathered` 두 번)에 클립을 두 번 되감지 않게 한다. */
	uint64 LastGatherActionFrame = 0;

	/** 누른 순간 채집 동작을 튼 시각 — 막타 알림이 같은 동작을 또 틀지 않게 (`PlayGatherSwingOnPress`). */
	double LastGatherPressSwingTime = -100.0;

	/**
	 * 쓰러짐 클립을 처음부터 틀고, 다 돈 뒤 **표의 누운 시간**이 지나면 부활한다 (M5-01e2).
	 * 시간을 코드에 적지 않는다 — 클립 길이는 클립에서, 누운 시간은 `UPlayerVitalsComponent` 가
	 * 표에서 읽는다. 둘 중 하나라도 미정이면 옛날처럼 **그 자리에서 바로** 부활시키고 경고를 남긴다
	 * (조용히 아무 숫자나 쓰지 않는다 — Verification.md 7).
	 */
	void BeginDownedSequence();

	/** 누운 시간이 끝난 순간 — 시작 자리로 옮기고 표의 부활 값으로 되돌린다 (M11-03 의 ③). */
	void FinishDownedSequence();

	/** 쓰러져 누워 있는 동안인가. 입력 차단과 클립 우선순위가 이 값을 본다. */
	bool bDowned = false;

	/** 피격 클립이 남은 시간(초). 길이는 `HitAnim` 에서 읽는다 — 0 이면 피격 반응이 끝났다 (M5-07d2). */
	float HitReactSecondsLeft = 0.f;

	/** 이번 틱에 피격 클립을 처음부터 다시 틀어야 하나 — 반응 중에 또 맞았을 때. */
	bool bHitReactRestart = false;

	/**
	 * 내 자리에서 몸 소리를 낸다 — 같은 소리를 최소 간격(0.6초) 안에 또 부르면 안 낸다 (DESIGN 2.2).
	 * @return 실제로 소리를 요청했으면 true
	 */
	bool PlayBodyVoice(FName Cue, float Volume);

	/**
	 * 맞은·쓰러진 사람 본인 기기만 손바닥을 떤다 (M61-03 · DESIGN 2.2 「몸 진동」). 세기·길이·간격은 `SnareBodyHaptic::CueFor` 표 그대로,
	 * 종류별 마지막 진동 시각을 따로 센다. 남의 기기(`IsLocallyControlled` 아님)·설정 끔·간격 안이면 안 튼다. 체력 계산은 안 건드린다.
	 * @return 실제로 틀었으면 true
	 */
	bool PlayBodyHaptic(SnareBodyHaptic::EEvent Event);

	/** 진동 종류(`SnareBodyHaptic::EEvent` 값) → 마지막으로 튼 월드 시각(초). */
	TMap<uint8, double> LastBodyHapticSeconds;

	/** 마지막으로 쏘기 진동을 튼 월드 시각(초). 아직 없으면 음수. */
	double LastShotHapticSeconds = -1.0;

	/** 마지막으로 타격 진동을 튼 월드 시각(초). 아직 없으면 음수. */
	double LastStrikeHapticSeconds = -1.0;

	/** 마지막으로 먹기 진동을 튼 월드 시각(초). 아직 없으면 음수. */
	double LastEatHapticSeconds = -1.0;

	/**
	 * 쓰러진 기기(체력이 0 이 된 곳)가 호스트에 알려 모든 기기에 쓰러짐 소리를 퍼뜨린다 (M44-03 · DESIGN 2.2).
	 * 쓰러짐 판정은 맞은 사람 기기 몫이라 호스트를 한 번 거친다. 소리뿐이라 Unreliable.
	 */
	UFUNCTION(Server, Unreliable)
	void Server_PlayDownVoice();

	/** 모든 기기가 그 사람 자리에서 `PlayerDown`(크기 1.0)을 낸다. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayDownVoice();

	/** 몸 소리 이름 → 마지막으로 낸 월드 시각(초). 늑대 여럿에게 동시에 물려도 한 번으로 묶는다. */
	TMap<FName, double> LastBodyVoiceSeconds;
	TMap<FName, int32> BodyVoiceRequestCounts;

	/** 아이템 사용 동작 클립과 남은 시간(초). 0 이면 끝났다. 걷기 시작·물·탈것이면 바로 끊는다. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ItemActionAnim;
	float ItemActionSecondsLeft = 0.f;
	/** 아이템 동작 재생 속도 — 손 행 `UseAnim` 은 공격 간격 한 바퀴에 맞춰 돌린다(M18-11). 나머지는 1. */
	float ItemActionPlayRate = 1.f;
	bool bItemActionRestart = false;
	/** 춤·앉기 — 시간으로 끝나지 않고 움직이면 끊긴다. */
	bool bItemActionLoop = false;

	/** 공격(쓰기) 버튼을 누르고 있나 (`BeginAttack` 켬 · `EndAttack` 끔). 이 기계 손가락만 — 남의 화면엔 복제하지 않는다. */
	bool bUseInputHeld = false;
	/** 버팀을 놓은 뒤 돌아오기(구간 끝 → 클립 끝)가 남은 초. 0 이면 끝났다. */
	float HeldUseTailSecondsLeft = 0.f;

	/** 마지막으로 시작한 아이템 동작과 시작 횟수 — `GetLastItemAction`·`GetItemActionCount` 가 읽는다 (M15-12). */
	EItemAction LastItemAction = EItemAction::Eat;
	int32 ItemActionCount = 0;
	int32 SoundRequestCount = 0;
	float GaspSecondsSinceLast = 1000.f;
	int32 GaspPlayCount = 0;
	float ShiverSecondsSinceLast = 1000.f;
	int32 ShiverPlayCount = 0;
	float HeartbeatSecondsSinceLast = 1000.f;
	int32 HeartbeatPlayCount = 0;
	float WarmthSecondsSinceLast = 1000.f;
	int32 WarmthPlayCount = 0;
	int32 RevivePlayCount = 0;
	int32 RelayedSoundCount = 0;
	FName LastRelayedSoundCue;
	float LastRelayedSoundVolume = 0.f;
	int32 CaptureFeedbackCount = 0;
	bool bLastCaptureVibrated = false;

	/** 머리 위 말풍선 번호(`SnareEmoteBubble`)와 사라지는 월드 시각 — 비었으면 `INDEX_NONE` (M20-07). */
	int32 EmoteBubbleIndex = INDEX_NONE;
	double EmoteBubbleEndSeconds = 0.0;
	int32 EmoteBubbleRequestCount = 0;
	int32 RelayedEmoteBubbleCount = 0;
	/** 말풍선 소리를 마지막으로 낸 월드 시각 / 낸 횟수 (M54-03, 연타 간격 `CueFor.MinGapSeconds`). */
	double LastEmoteSoundSeconds = -1000.0;
	int32 EmoteSoundCount = 0;

	/** 머리 위 「+N」 — 값·뜬 월드 시각(음수면 없음)·띄운 횟수 (DESIGN 8.8 「먹기」). */
	int32 HealPopAmount = 0;
	double HealPopStartSeconds = -1.0;
	int32 HealPopCount = 0;

	/** 누운 시간을 재는 타이머. 폰이 사라지면 `EndPlay` 가 지운다. */
	FTimerHandle DownedTimerHandle;

	/**
	 * 지금 걷기·달리기 클립 쪽인가. 두 문턱(`MoveAnim*SpeedCmPerSecond`) 사이 속도에서는 이 값이
	 * 그대로 유지돼 Idle 과 걷기가 한 프레임 안에서 왔다갔다하는 것을 막는다.
	 */
	bool bBodyMoving = false;

	/**
	 * `ApplyPlayerBody` 가 몸을 캡슐에 맞추며 잰 배율(캡슐 높이 ÷ 메시 높이). 몸이 아직 없으면 1.
	 * 클립 공간 속도(`*ClipSpeedCmPerSecond`)를 화면 속도로 옮기는 데 쓴다 — 자리표시 실린더일 때는
	 * 1 이라 옛 동작 그대로다.
	 */
	float BodyMeshScale = 1.f;

	/**
	 * 물·배에 맞춰 몸(메시) 자리를 옮긴다 — 땅: 캡슐 바닥 · 물: 수면 기준 `Swim*SinkCm` · 배: 좌석(Hull 흔들림을 탄다).
	 * 매 틱 `UpdateBodyAnimation` 다음에 돈다(수영 대기/이동 높이가 고른 클립을 따르므로).
	 */
	void UpdateBodyPlacement(float DeltaSeconds);

	/** 메시 상대 자리를 바꾸고 복제본 스무딩 기준(`CacheInitialMeshOffset`)도 같이 옮긴다 — 안 하면 남의 화면에서 되돌아간다. */
	void SetBodyPlacement(const FVector& RelativeLocation, const FRotator& RelativeRotation);

	enum class EBodyPlacement : uint8 { Ground, Swim, Boat };
	EBodyPlacement BodyPlacement = EBodyPlacement::Ground;

	/**
	 * 물·배에 있는 동안 몸 원점의 **월드** 높이. 캡슐 기준으로 섞으면 해저 계단(40cm)을 밟을 때마다
	 * 캡슐이 튀어 몸이 출렁이므로 월드에서 수면을 향해 섞는다.
	 */
	float BodyWorldZ = 0.f;

	/** 높이 로그를 마지막으로 남긴 수영 클립 (검사 증거 — 클립이 바뀌어 자리가 붙을 때마다 한 번). */
	const UAnimSequence* PlacementLoggedFor = nullptr;

	/** 던지기 클립이 남은 시간(초). 길이는 `FishCastAnim` 에서 읽는다. */
	float FishCastSecondsLeft = 0.f;

	/** 대기 변주 (`UpdateIdleVariant`): 고른 클립·남은 시간(초)·대기만 보인 시간(초)·이번에 기다릴 시간(초). */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> IdleVariantAnim;
	float IdleVariantSecondsLeft = 0.f;
	float IdleStillSeconds = 0.f;
	float NextIdleVariantSeconds = 0.f;

	/** 지난 틱에 낚시 중이었나 — 쉬다가 낚시로 들어선 순간 던지기를 튼다. */
	bool bWasFishing = false;

	/** 마지막으로 본 감기 횟수와 빠른 감기 남은 시간(초). 누른 사람 기계에서만 보인다. */
	int32 LastFishPressCount = 0;
	float FishReelBoostSecondsLeft = 0.f;

	/**
	 * `MoveByAxis` 가 마지막으로 받은 조이스틱 크기(0~1). 걷기·달리기를 이걸로 가른다.
	 * 조이스틱을 놓으면 `Triggered` 가 더 안 와서 값이 남지만, 그때는 속도가 0 이라 Idle 로 가므로
	 * 남은 값이 화면에 보이지 않는다 (다음 입력이 같은 프레임에 덮는다).
	 */
	float LastMoveInputSize = 0.f;

	/**
	 * 지금 달리는 중인가. 조이스틱을 `RunInputThreshold` 이상 밀었고 **스태미나가 남아 있을 때만** 참이다
	 * (DESIGN 3.1 "스태미나 0 처벌 = 달리기 불가(걷기로 강제)"). 최고 속도와 클립 고르기가 같은 답을
	 * 봐야 해서 한 함수로 둔다 — 두 군데서 따로 세면 발이 미끄러지는 조합이 생긴다.
	 */
	bool IsRunningNow() const;

	/**
	 * 이번 프레임에 `MoveByAxis` 가 불렸는가. 조이스틱을 놓으면 `Triggered` 가 더 오지 않으므로
	 * "놓았다" 를 알 길이 이것뿐이다 — 이게 없으면 스태미나가 손을 뗀 뒤에도 계속 닳는다 (M11-02).
	 */
	bool bMoveInputThisFrame = false;

	/** false 면 `IA_Attack` 의 누름을 무시한다 — 뗌(Completed)이 오면 다시 true (M1-10e). */
	bool bAttackInputArmed = true;

	/** `-SnareAutoCapture` 가 포획할 수 있을 때까지 되묻는 타이머. 검사 전용이다. */
	FTimerHandle CheckCaptureTimer;

	/** `-SnareAutoAttack` 이 공격 간격마다 다시 누르는 타이머. 검사 전용이다 (한 번 누름 = 한 번, 2026-10-04). */
	FTimerHandle AutoAttackTapHandle;

	/** `-SnareTravel` 이 포탈에 닿을 때까지 되묻는 타이머. 검사 전용이다. */
	FTimerHandle CheckTravelTimer;

	/** `-SnareKillPlayer=<초>` 가 그 시각에 한 번 나를 죽이는 타이머. 검사 전용이다 (M5-01e2). */
	FTimerHandle CheckKillTimer;

	/** `-SnareTouch=` 가 준 대상 이름(소문자). 비어 있으면 검사 터치를 넣지 않는다. */
	FString CheckTouchTarget;

	/** 검사 터치를 넣고 손가락을 붙잡고 있는 타이머. 검사 전용이다. */
	FTimerHandle CheckTouchTimer;

	/** 검사 터치의 손가락이 이미 내려가 있는가. 뗄 때 같은 자리로 떼려고 자리도 들고 있는다. */
	bool bCheckTouchDown = false;
	FVector2D CheckTouchScreenPos = FVector2D::ZeroVector;

	/** 내가 죽인 동물의 드롭을 잡은 사람의 클라이언트로 넘긴다 (M3-06). 여기선 쌓지 않는다. */
	UFUNCTION()
	void HandleAnimalKilled(AAnimalCharacter* Target, const TArray<FName>& DroppedItemIDs);

	/** 내가 포획한 동물을 펫 등록 요청으로 넘긴다 (목장 배치는 백엔드가 기록한다 — DESIGN 4.1). */
	UFUNCTION()
	void HandleAnimalCaptured(AAnimalCharacter* Target, FName PetItemID);

	/** 포획을 누른 순간 — 올가미를 대상 쪽으로 던지는 몸짓과 소리 (M28-09c). 판정은 안 기다린다(겉모습뿐). */
	UFUNCTION()
	void HandleCapturePressed(AAnimalCharacter* Target);

	/** 내가 집은 채집물을 지급 요청으로 넘긴다 (보유 수량은 안 건드린다 — DESIGN 4.5). */
	UFUNCTION()
	void HandleGathered(FName GatherableID, FName ItemID, int32 Count, int32 ToolUses);

	/** 채집 버튼을 눌러 요청이 나간 즉시 몸 동작을 튼다 (매 타, 호스트 판정을 기다리지 않음). */
	void PlayGatherSwingOnPress(EItemAction Action);

	/** 누르기 전에 채집 동작을 고른다 — 채집이 끝나면 대상이 사라져 못 고른다. */
	EItemAction PickGatherSwingBeforePress();

	/** 호스트가 내 채집 부탁을 물렸을 때 (M15-15) — 까닭 한 줄을 사냥 HUD 에 그대로 띄운다. */
	UFUNCTION()
	void HandleGatherRejected(const FText& Message);

	/**
	 * 체력이 0 이 된 순간 (M11-03 — `UPlayerVitalsComponent::OnDied`).
	 * DESIGN 3.1 「사망 시 잃는 것」 표대로 셋만 한다:
	 *  ① **아직 백엔드가 확정하지 않은 지급 대기열을 버린다** — 이번 출격에서 딴 것뿐이고,
	 *     대기열은 아직 누구의 보유도 아니라 재화·인벤토리 숫자를 만지지 않는다 (CLAUDE.md 8)
	 *  ② 죽었다는 **사실 한 줄**을 백엔드로 보낸다 (M8-02 통로 — 값은 안 바꾼다)
	 *  ③ 시작 자리로 옮기고 체력·스태미나를 표의 부활 값으로 되돌린다
	 * 보유 인벤토리·재화는 한 칸도 건드리지 않는다 — 그건 백엔드 스냅샷으로만 바뀐다 (DESIGN 4.8).
	 */
	UFUNCTION()
	void HandlePlayerDied();

	/**
	 * 죽은 폰을 그 레벨의 `PlayerStart` 로 옮긴다 (DESIGN 3.1 「부활 자리」).
	 * **Server RPC 인 이유**: 자리를 옮기는 권한은 호스트에 있다 — 게스트가 제 화면에서만 옮기면
	 * 서버 이동 보정이 되돌려 놓는다. 호스트 자기 폰이면 이 함수가 그 자리에서 바로 돈다.
	 * 체력·스태미나 되돌리기는 여기가 아니다 — 생존 값은 복제되지 않아 각자 자기 것을 되돌린다.
	 */
	UFUNCTION(Server, Reliable)
	void Server_RespawnAtPlayerStart();

	/**
	 * PlayFab 이 받아 준 내 닉네임을 호스트에 알린다 (DESIGN 2.2 「이름표」 닉네임 줄). 이름표 이름은 복제되는
	 * `PlayerState` 값이라 바꾸는 권한이 호스트에 있다 — 호스트가 같은 규칙으로 다시 거르고
	 * (`ASnarehavenGameMode::ApplyPlayerNickname`), 안 맞거나 방에서 겹치면 「플레이어 N」 을 그대로 둔다.
	 */
	UFUNCTION(Server, Reliable)
	void Server_ReportNickname(const FString& Nickname);

	/**
	 * 내 PlayFab ID 를 호스트에 알린다 — 부활 때 호스트가 「누구 집이 내 집인가」 를 가리는 데만 쓴다(재화·인벤토리 판정 아님).
	 * 집 액터(`ASnarePlacedStructureActor`)가 주인 ID 를 복제로 들고 있어 그것과 맞춘다.
	 */
	UFUNCTION(Server, Reliable)
	void Server_ReportPlayFabId(const FString& PlayFabId);

	/** 호스트가 이 폰을 내 집/텐트 문 앞으로 옮긴다. 집이 없거나 막혔으면 false (부르는 쪽이 시작 자리로 보낸다). */
	bool TryRespawnAtOwnHome();

	/** 집 앞 빈 땅으로 옮긴다 (`TryRespawnAtOwnHome` 과 검사 스위치 `-SnareHomeAt=` 가 같이 쓴다). */
	bool TeleportToHomeFront(const FVector& HomeLocation, float HomeYaw, const FIntPoint& Footprint, float CellCm);

	/** 호스트가 아는 이 폰 주인의 PlayFab ID (`Server_ReportPlayFabId`). 호스트 쪽에서만 채워진다. */
	FString KnownPlayFabId;

	/** 내가 조종하는 폰이고 백엔드에 표시 이름이 있으면, 지난번에 안 보낸 이름일 때만 호스트에 알린다. */
	void ReportNicknameToServer();

	/** 옵션 카드에서 닉네임을 바꿔 PlayFab 이 받아 줬을 때 — 이름표도 따라 바뀌게 다시 알린다. */
	UFUNCTION()
	void HandleDisplayNameChanged();

	/** 이 폰이 호스트에 마지막으로 알린 닉네임 — 조종이 붙을 때마다 같은 이름을 다시 보내지 않게. */
	FString ReportedNickname;

public:
	// 아래 둘은 2층 검사가 받는 기기 쪽(`_Implementation`)을 직접 부르려고 공개한다 (이모트 말풍선과 같은 길).
	/** 게스트가 제 몸에서 튼 동작을 호스트에 알린다 — 호스트가 모두에게 뿌린다. */
	UFUNCTION(Server, Unreliable)
	void Server_PlayItemAction(EItemAction Action);

	/**
	 * 모두의 화면에서 그 몸에 동작을 튼다. 그림뿐이라 Unreliable — 빠져도 게임 상태는 같다.
	 * @param bOwnerAlreadyPlayed 폰 주인 기기가 이미 틀었나(먹기) — 그러면 주인 기기는 다시 되감지 않는다.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayItemAction(EItemAction Action, bool bOwnerAlreadyPlayed);
	/**
	 * 도구·무기 소리를 **내 기기에서 바로 틀고**, 같은 소리를 다른 기기에도 그 사람 자리에서 낸다 (M16-08).
	 * 회선에는 소리 **이름**과 자리만 싣는다 — 에셋 참조는 싣지 않고 각 기기가 자기 표로 골라 튼다.
	 */
	void PlayActionSound(FName Cue, const FVector& Location, float Volume);

	/** 이 기기가 소리 중계를 부탁한 횟수 / 호스트가 중계한 횟수와 마지막 소리 이름 — 2층 검사용 (M16-08). */
	int32 GetSoundRequestCount() const { return SoundRequestCount; }
	int32 GetRelayedSoundCount() const { return RelayedSoundCount; }
	FName GetLastRelayedSoundCue() const { return LastRelayedSoundCue; }
	float GetLastRelayedSoundVolume() const { return LastRelayedSoundVolume; }

	/** 게스트가 낸 소리를 호스트에 알린다 — 호스트가 나머지 기기에 뿌린다. */
	UFUNCTION(Server, Unreliable)
	void Server_PlayActionSound(FName Cue, FVector_NetQuantize Location, float Volume);

	/** 소리뿐이라 Unreliable. 폰 주인 기기는 이미 틀었으니 다시 안 튼다. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayActionSound(FName Cue, FVector_NetQuantize Location, float Volume);

	/** 숨참 소리 (M56-03) — 서버 한 곳이 `SnareGasp::CueFor` 로 정해 모든 기기(주인 포함)에 뿌린다. 소리뿐이라 Unreliable. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayGasp(float Volume);

	/** 숨참 소리를 낸 횟수 (시험·검사용). */
	int32 GetGaspPlayCount() const { return GaspPlayCount; }

	/** 숨참 틱 — 서버만, 물속·스태미나 0 일 때 간격마다 `Multicast_PlayGasp`. 쓰러진 뒤엔 건너뛴다. */
	void TickGaspSound(float DeltaSeconds);

	/** 추위 떨림 소리 (M57-03) — 서버 한 곳이 `SnareShiver::CueFor` 로 정해 모든 기기(주인 포함)에 뿌린다. 소리뿐이라 Unreliable. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayShiver(float Volume);

	/** 추위 떨림 소리를 낸 횟수 (시험·검사용). */
	int32 GetShiverPlayCount() const { return ShiverPlayCount; }

	/** 추위 떨림 틱 — 서버만, 설원 칸 피해가 실제로 있을 때 간격마다 `Multicast_PlayShiver`. 쓰러진 뒤엔 건너뛴다. */
	void TickShiverSound(float DeltaSeconds);

	/** 심장 소리 (M60-04) — 내 기기만 `Play2D`. `SnareLowHealth::StateFor` 가 0 보다 클 때 `BeatInterval` 마다. 체력은 읽기만 한다. */
	void TickHeartbeatSound(float DeltaSeconds);

	/** 심장 소리를 낸 횟수 (시험·검사용). */
	int32 GetHeartbeatPlayCount() const { return HeartbeatPlayCount; }

	/** 불 옆 온기 소리 (M58-03) — 불 옆 회복 틱이 체력을 실제로 올릴 때 부른다. `SnareWarmth::CueFor` 가 정하고 `PlayActionSound` 로 모든 기기에 뿌린다. 체력 값은 건드리지 않는다. */
	void PlayWarmthIfDue(float HealPerSecond, bool bHealthFull);

	/** 불 옆 온기 소리를 낸 횟수 (시험·검사용). */
	int32 GetWarmthPlayCount() const { return WarmthPlayCount; }

	/** 불 옆 온기 소리 시계 — 지난 소리 뒤 시간을 쌓는다. 시험은 이걸로 시간을 보낸다. */
	void TickWarmthClock(float DeltaSeconds) { WarmthSecondsSinceLast += DeltaSeconds; }

	/** 부활 소리 (M59-03) — 서버 `Server_RespawnAtPlayerStart_Implementation` 이 자리를 옮긴 **뒤** 부른다. `SnareRevive::CueFor` 가 정하고 `Multicast_PlayReviveSound` 로 모든 기기(본인 포함)가 한 번씩 낸다. 부활 자리·생존 값은 건드리지 않는다. */
	void PlayReviveSound(bool bWasDowned);

	/** 모든 기기가 새 자리에서 부활 소리를 한 번 낸다 (서버에서만 부른다 — `PlayActionSound` 의 본인 거름이 없다). */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayReviveSound(FName Cue, FVector_NetQuantize Location, float Volume);

	/** 부활 소리를 낸 횟수 (시험·검사용). */
	int32 GetRevivePlayCount() const { return RevivePlayCount; }

	/** 포획 순간 조이는 소리 이름 (`SFX_Cinch_*`, M21-03b). */
	static FName GetCaptureCue();

	/**
	 * 호스트가 포획을 확정한 순간 — 모든 기기가 동물 자리에서 조이는 소리를 내고, 잡은 사람 기기만 가볍게 떤다 (M21-03b).
	 * 지급이 아니라 포획 판정(호스트 권위, DESIGN 4.1)의 연출이라 서버 응답을 기다리지 않는다 — 펫 지급 글은 안 띄운다 (CLAUDE.md 8).
	 * 반짝 조각과 펫이 잡은 사람 배낭으로 0.4초 날아가는 모습은 각 기기가 `ASnareCaptureBurstActor` 로 띄운다 (M21-03b2b).
	 * 동물은 포획 즉시 숨어서 액터 대신 메시 에셋·크기를 싣는다.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayCaptureFeedback(FVector_NetQuantize Location, USkeletalMesh* PetMesh, FVector_NetQuantize100 PetScale);

	/** 이 기기가 포획 순간 연출을 받은 횟수와 그때 진동을 틀었는지 — 2층 검사용 (M21-03b). */
	int32 GetCaptureFeedbackCount() const { return CaptureFeedbackCount; }
	bool DidLastCaptureVibrate() const { return bLastCaptureVibrated; }

	/**
	 * 감정표현 말풍선을 내 머리 위에 바로 띄우고 같은 방 다른 기기에도 보낸다 (M20-07 · DESIGN 2.2 「감정표현 말풍선」).
	 * 회선에는 번호만 싣는다 — 글자는 각 기기가 `SnareEmoteBubble` 표로 고른다. 그림뿐이라 재화·판정과 무관하다.
	 * 범위 밖 번호는 아무것도 안 한다.
	 */
	void ShowEmoteBubble(int32 Index);

	/** 그 시각에 떠 있는 말풍선 번호, 없거나 `SnareEmoteBubble::ShowSeconds` 가 지났으면 `INDEX_NONE`. */
	int32 GetEmoteBubbleAt(double NowSeconds) const;

	/** 지금(월드 시각) 떠 있는 말풍선 번호 — 이름표가 매 틱 읽는다. */
	int32 GetEmoteBubble() const;

	/** 먹은 뒤 머리 위 「+N」 이 떠올라 사라지는 시간(초) — DESIGN 8.8 「먹기」. */
	static constexpr float HealPopSeconds = 1.2f;

	/**
	 * 먹기를 서버가 받아들인 뒤 머리 위에 「+N」 을 띄운다 (INBOX 15:30 ⑤ · DESIGN 8.8 「먹기」 — 원본은 먹는 동작 없이
	 * 선 채 초록 「+15」 만 떠오른다). N 은 서버 `ConsumeItem` 의 `heal` 그대로, 음수(독)는 「-N」. 이 기기 화면만 — 그림뿐이다.
	 * 0 이면 아무것도 안 한다.
	 */
	void ShowHealPop(int32 Amount);

	/** 그 시각의 떠오름 몫 0(막 뜸)~1(사라짐), 떠 있지 않으면 음수. */
	float GetHealPopProgressAt(double NowSeconds) const;

	/** 지금(월드 시각) 떠오름 몫 — 이름표가 매 틱 읽는다. */
	float GetHealPopProgress() const;

	/** 마지막으로 띄운 「+N」 의 N 과 띄운 횟수 — 이름표 글자·2층 검사용. */
	int32 GetHealPopAmount() const { return HealPopAmount; }
	int32 GetHealPopCount() const { return HealPopCount; }

	/** 이 기기가 말풍선 중계를 부탁한 횟수 / 호스트가 중계한 횟수 — 2층 검사용 (M20-07). */
	int32 GetEmoteBubbleRequestCount() const { return EmoteBubbleRequestCount; }
	int32 GetRelayedEmoteBubbleCount() const { return RelayedEmoteBubbleCount; }
	/** 말풍선이 낸 소리 횟수 — 2층 검사용 (M54-03). */
	int32 GetEmoteSoundCount() const { return EmoteSoundCount; }

	/** 게스트가 고른 말풍선 번호를 호스트에 알린다 — 호스트가 범위를 보고 모두에게 뿌린다. */
	UFUNCTION(Server, Unreliable)
	void Server_ShowEmoteBubble(uint8 Index);

	/** 모든 기기에서 그 몸 위에 말풍선을 3초 띄운다. 그림뿐이라 Unreliable. 주인 기기는 시간만 다시 잰다. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_ShowEmoteBubble(uint8 Index);

private:

	/** 도구를 요구하는데 **지금 든 것으로는** 못 베는 가장 가까운 채집물 (없으면 nullptr) — 버튼 글자와 누름이 같이 본다. */
	AGatherableActor* FindGatherableNeedingOtherTool() const;

	/** 그 채집물을 벨·캘 수 있는 가진 도구 행 (없으면 None). `FindContextToolRow` ① 과 같은 고르기. */
	FName FindOwnedToolRowFor(const AGatherableActor* Need) const;

	/** 가진 낚싯대 중 가장 빨리 무는 것의 행 (없으면 None). `FindContextToolRow` ② 와 같은 고르기. */
	FName FindOwnedRodRow() const;

	/** 이 기기에서 동작 클립과 남은 시간을 건다. 길이는 클립에서 읽는다 (DataDriven.md 3). */
	void StartItemAction(EItemAction Action);

	/**
	 * 백엔드 캐시가 바뀌면 내 인벤토리 칸을 그대로 덮어쓴다 (M2-05, DESIGN 4.5 — 충돌 시 백엔드 우선).
	 * 폰이 아니라 게임 인스턴스가 상태를 들고 있어서, 맵을 옮겨 새 폰이 생겨도 같은 값이 따라온다.
	 */
	UFUNCTION()
	void HandleBackendSnapshotChanged();

	/**
	 * 시작 장비를 방금 받았을 때 — 받은 것을 손에 건다 (M9-07b · DESIGN 3.5 「받은 뒤」).
	 * **지급 판정도 개수도 여기서 하지 않는다** — 백엔드가 준 아이템 ID 로 표에서 행을 찾아 걸 뿐이다.
	 */
	UFUNCTION()
	void HandleStarterKitGranted();

	/** 지금 백엔드 캐시에 있는 값을 인벤토리에 한 번 밀어 넣는다. 로컬 조종 폰만 한다. */
	void PullInventoryFromBackend();

	/**
	 * 맵을 건너오기 전에 들고 있던 도구·배낭을 다시 건다 (M15-10 · INBOX 2026-09-25 ④).
	 * 몸이 서고(`ApplyPlayerBody`) 내가 조종하는 폰이 됐을 때 **한 번만** — 그래서 `BeginPlay` 와
	 * `NotifyControllerChanged` 두 곳에서 부른다(어느 쪽이 먼저 오는지는 호스트·게스트에 따라 다르다).
	 * 모자는 서버가 기억하므로 여기서 안 건다 (`USnareGearMemorySubsystem`).
	 */
	void RestoreRememberedGear();

	/** 이 폰에서 기억한 장비를 이미 되걸었는가 (두 번 걸지 않으려고). */
	bool bGearRestored = false;

	/** 로컬 조종이고 게임모드가 띄우라고 할 때만 사냥 HUD 를 만든다 (M1-08 · M2-04c). */
	void CreateHuntHud();

	/** 같은 규칙으로 목장 HUD 를 만든다 — 목장 게임모드가 켰을 때만 (M2-06a). */
	void CreateRanchHud();

	/** HUD 를 띄운 내 폰만 거점 결과 글을 듣는다 — 두 번 걸지 않는다. `EndPlay` 가 푼다 (#0520). */
	void BindBaseMessages();

	/** `OnBaseMessage` 를 받아 `RouteBaseMessage` 로. */
	void HandleBaseMessage(const FText& Message, bool bError);

	FDelegateHandle BaseMessageHandle;

	// 컴포넌트는 생성자가 만들고 사람이 바꿀 값이 아니라 VisibleAnywhere (보기만) 로 둔다
	// — 거리·각도·화각은 위 CameraDistance/CameraPitchDegrees/CameraFieldOfViewDegrees 로 조정한다 (engines/unreal.md 코드 규칙 1).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** `SetWardrobeCamera` 가 켜기 전 값을 적어 둔다. */
	float WardrobeCameraSavedArmLength = 0.f;
	FVector WardrobeCameraSavedSocketOffset = FVector::ZeroVector;
	FRotator WardrobeCameraSavedRotation = FRotator::ZeroRotator;
	bool bWardrobeCameraOn = false;
	/** 옷장 카메라와 몸 사이를 가려 이 화면에서만 숨긴 것(가로등·빛기둥 등) — 끌 때 다시 보인다. 복제 안 되는 부품 보임만 바꾼다. */
	TArray<TWeakObjectPtr<UPrimitiveComponent>> WardrobeHiddenOccluders;
	void HideWardrobeOccluders();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> TopDownCamera;

	// 머리 위 이름표(DESIGN 2.2 「이름표」) — 화면 공간이라 카메라 줌과 상관없이 같은 크기다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWidgetComponent> NameTag;

	// 발밑 행동 진행바(M15-19 · 시트 `docs/reference/hud_ingame.md` 「월드 위 오버레이」) — 이름표와 같은
	// 화면 공간 위젯이라 줌과 상관없이 시트 px 그대로다. 진행이 없으면 막대가 접힌다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWidgetComponent> FootProgress;

	// 아트가 들어오기 전까지 화면에 보이는 몸 역할을 하는 엔진 기본 실린더.
	// ACharacter 의 콜리전 캡슐은 렌더되지 않아서, 이게 없으면 3층 캡처에 캐릭터가 아예 안 찍힌다
	// (M0-06 에서 실제로 그랬다). M1-07 에서 돼지 등 진짜 메시가 들어오면 이 자리를 대체한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Placeholder", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	// 공격 판정은 캐릭터가 아니라 컴포넌트가 한다 — 나중에 NPC·동료가 같은 공격을 쓰려면 붙이기만 하면 된다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMeleeAttackComponent> MeleeAttack;

	// 포획도 컴포넌트다 — 대상 선택은 근접 공격 컴포넌트에서 빌려 쓴다 (같은 하이라이트 한 마리).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCaptureComponent> Capture;

	// 채집도 컴포넌트다 — 사거리·갱신 주기를 근접 공격 컴포넌트에서 빌려 쓴다 (같은 자동 상호작용 거리).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gather", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UGatherComponent> Gather;

	// 장비 겉모습도 컴포넌트다 (M5-06b) — 나중에 NPC·다른 플레이어 몸이 같은 방식으로 무기를 든다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UEquipmentComponent> Equipment;

	// 얼굴 그림 눈 깜빡임 (INBOX 12:40) — 몸 머티리얼의 바탕색 그림을 잠깐 눈 감은 짝으로 바꾼다. 화면만.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Face", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPlayerFaceBlinkComponent> FaceBlink;

	// 인벤토리도 컴포넌트다 — 목장 NPC·상자가 나중에 같은 캐시를 쓸 수 있게.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInventoryComponent> Inventory;

	// 지급 대기열을 `ClaimLoot` 로 비우는 주기 전송기 (DESIGN 8.12). 로직은 전부 컴포넌트 안에 있다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class ULootClaimComponent> LootClaim;

	// 배낭 먹기·버리기·사망 가방·줍기를 서버에 묻는 자리 (DESIGN 8.12). 로직은 전부 컴포넌트 안에 있다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UBackpackComponent> Backpack;

	// 목장 캐시도 인벤토리와 같은 이유로 플레이어에 붙는다 — 목장은 개인 소유라 폰이 들고 다닌다
	// (DESIGN 3.2·3.4). 사냥 맵에서도 붙어 있지만 슬롯 액터가 없어 아무 일도 하지 않는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ranch", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<URanchComponent> Ranch;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ranch", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<URanchInteractComponent> RanchInteract;

	// 낚시터도 개인 소유라 목장과 같은 이유로 폰이 들고 다닌다 (DESIGN 8.4 · 3.2). 거점 슬롯 액터가
	// 없는 맵에서는 아무 일도 하지 않는 것도 목장과 같다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ranch", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<URanchComponent> Fishing;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ranch", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class URanchSyncComponent> RanchSync;

	// 포탈 상호작용도 컴포넌트다 (M6-02b) — 채집·목장 슬롯과 같은 짜임이라 사거리·주기를 같은 곳에서
	// 빌린다. 포탈이 없는 맵에서도 붙어 있지만 고를 액터가 없어 아무 일도 하지 않는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Zone", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UZoneTravelComponent> ZoneTravel;

	// 체력·스태미나도 컴포넌트다 (M11-01) — 나중에 NPC·동료가 같은 소모 규칙을 쓰려면 붙이기만 하면 된다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vitals", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPlayerVitalsComponent> PlayerVitals;

	// 수영·배(8.9)와 두드리기 낚시(8.13) — 규칙은 두 컴포넌트에 있다. 여기선 속도 배율만 읽는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Water", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWaterTravelComponent> WaterTravel;

	// 탈것(8.3·M6-06) — 규칙은 컴포넌트에 있다. 여기선 속도 배율만 읽는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMountComponent> Mount;

	// 존 특성(DESIGN 1.3 — 설원 체력 감소·섬 입장료). 규칙은 컴포넌트에 있다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Zone", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UZoneEffectComponent> ZoneEffect;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Water", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTapFishingComponent> TapFishing;

	/** 체력·스태미나 숫자가 오는 표. 동물이 `DT_AnimalTable` 을 보는 것과 같은 짜임 (DataDriven.md 1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vitals", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDataTable> PlayerVitalsTable;

	/** 그 표에서 읽을 행. 플레이어가 한 종류뿐이라 행도 하나다 — 종족·직업이 생기면 값만 갈린다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vitals", meta = (AllowPrivateAccess = "true"))
	FName PlayerVitalsRowName = TEXT("Player");

	/**
	 * 근접 공격 네 숫자가 오는 표 (M11-05 · 감독 지시 2026-09-23 17:25). 체력·스태미나 표와 **따로 둔다** —
	 * 원본이 DESIGN 3.1 의 다른 표이고(확정 수치 ↔ 플레이어 체력·스태미나), 무기가 늘면 이쪽만 행이 는다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDataTable> PlayerCombatTable;

	/** 그 표에서 읽을 행. 맨손 기준 한 줄이다 (무기별 값은 `DT_ItemTable` 무기 칸, DESIGN 8.6-b). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FName PlayerCombatRowName = TEXT("Player");

	// HUD 는 컴포넌트가 아니라 화면 위젯이라 로컬 플레이어 한 명에게만 만든다
	// (서버·다른 클라이언트의 폰에도 만들면 남의 동물 HP 가 내 화면에 뜬다).
	UPROPERTY()
	TObjectPtr<UHuntHudWidget> HuntHud;

	/** 첫 실행 안내(M17-01) — 처음 켠 기기에서만 만들어진다. */
	UPROPERTY(Transient)
	TObjectPtr<USnareFirstRunGuideWidget> FirstRunGuide;

	/** 안내가 끝난 뒤 위 가운데 다음 목표 한 줄(INBOX M22 ⑤) — 안내가 떠 있는 동안엔 안 만든다. */
	UPROPERTY(Transient)
	TObjectPtr<USnareNextGoalWidget> NextGoalHud;

	/** 왼쪽 위 메인 스토리 「지금 할 일」 한 줄(INBOX 메인 스토리 ②) — 다음 목표 줄과 같은 때 만든다. */
	UPROPERTY(Transient)
	TObjectPtr<USnareStoryGoalWidget> StoryGoalHud;

	/** 새 스토리 단계를 받을 때 아래에 뜨는 대사 판 — 끝나면 비운다. */
	UPROPERTY(Transient)
	TObjectPtr<USnareStoryDialogueWidget> StoryDialogueHud;

	/** 장을 끝냈을 때 뜨는 장 끝 카드 — 닫으면 비운다. */
	UPROPERTY(Transient)
	TObjectPtr<USnareStoryChapterCardWidget> StoryChapterCardHud;

	/** 끝낸 장을 이 기기에서 아직 안 봤으면 카메라가 등대를 한 번 비춘 뒤 카드를 띄운다. */
	void ShowChapterCardIfNew(int32 CompletedChapter);

	/** 장 끝 카드를 바로 띄운다. bPersistSeen 이 false 면 본 장을 저장하지 않는다(검사 스위치). */
	void OpenChapterCard(int32 Chapter, bool bPersistSeen);

	/** 장 끝 연출 (STORY.md 4절) — 카메라가 등대로 갔다가 돌아온 뒤 OnDone. 5장(마지막 장)이면 비추는 동안 탑에 불이 붙는다(점화).
	 *  등대가 없는 맵이거나 이미 비추는 중이면 false 이고 OnDone 을 안 부른다. */
	bool PlayLighthouseGlance(int32 Chapter, TFunction<void()> OnDone);

	/** 장 끝 연출 동안만 있는 카메라 — 돌아오면 지운다. */
	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> LighthouseGlanceCamera;

	/** 지금 단계 대사를 이 기기에서 아직 안 봤으면 띄운다. */
	void ShowStoryDialogueIfNew(int32 CurrentStep);

	/** 장 끝과 대사 판을 한 화면에 하나씩 (감독 보정 10-20 17:15) — 대사 판이 열려 있나 · 장 끝(비추기 카메라·카드)이 열려 있나. */
	bool IsStoryDialogueOpen() const;
	bool IsChapterEndOpen() const;
	/** 장 끝·대사 판 차례 — 미룬 것만 쥔다. */
	SnareStory::FPopupQueue StoryPopupQueue;
	/** 장 끝을 차례대로 — 대사가 열려 있으면 미루고, 아니면 등대 비추기 → 카드 (등대 없는 맵은 바로 카드). */
	void StartChapterEnd(int32 Chapter, bool bPersistSeen);
	/** 미룬 장 끝을 지금 시작할 수 있으면 시작한다. */
	void StartWaitingChapterEnd();
	/** 장 끝 카드가 닫혔을 때 (또는 카드를 못 띄웠을 때) — 미룬 대사를 잇는다. */
	void HandleChapterEndClosed();
	/** 대사 판이 닫혔을 때 — 미룬 장 끝을 잇는다. */
	void HandleStoryDialogueClosed();
	/** 도착 조건이 있는 단계의 대사를 기다리는 중인 단계 (0 = 없음). 0.5초마다 도착을 본다. */
	int32 PendingStoryArrivalStep = 0;

	/** 25단계 「불 켜기」 를 눌렀나 (서버 판정 아님 — 대사를 띄울 때만 쓰고, 단계 끝은 대사 뒤 보고를 서버가 정한다). */
	bool bLighthouseLightPressed = false;

	/** 가기 단계 「가 보게」 대사를 이미 띄운 단계 (도착 때 발견 대사를 따로 띄우려고). */
	int32 StoryGoShownStep = 0;
	FTimerHandle StoryArrivalTimer;
	bool IsStoryArrived(int32 CurrentStep) const;

	/** 「지금 할 일」 줄을 누르면 여는 의뢰 판 — 의뢰인 없이 어디서든 줄기 탭을 본다. */
	UPROPERTY(Transient)
	TObjectPtr<UQuestScreenWidget> StoryQuestScreen;

	void OpenStoryQuestScreen();

	void CreateNextGoalHud();

public:
	/** 창이 열려 있는 동안 HUD 뿌리 위젯을 접는 칸 하나 — 접기 전 보임 상태를 기억해 닫으면 그대로 되돌린다. */
	struct FHudHideSlot
	{
		ESlateVisibility Saved = ESlateVisibility::Visible;
		bool bHidden = false;
	};

	/** 창(뽑기·상자·옷장·상점·가방…) 열림 하나로 목표 띠·상황 카드·행동 버튼을 한 곳에서 접고 편다 (INBOX 결함 2026-10-11 05:00). */
	static void ApplyHudHidden(UUserWidget* Widget, bool bHide, FHudHideSlot& Slot);

	/** 검사 스위치 전용 — `-SnareStandNear=animal.<행>` 가 옮겨 세운 동물을 `-SnareChase` 가 쫓게 한다(순간이동 전에 딴 동물로 굳지 않게). */
	void SetCheckChaseTargetForTest(class AAnimalCharacter* Target);

private:
	void UpdateHudForOpenScreens(float DeltaSeconds);

	FHudHideSlot HuntHudHide;
	FHudHideSlot RanchHudHide;
	FHudHideSlot NextGoalHide;
	FHudHideSlot StoryGoalHide;
	float HudHideWait = 0.f;

	UPROPERTY()
	TObjectPtr<URanchHudWidget> RanchHud;

	/** 7일 출석 달력 (M21-05b) — 목장 HUD 와 같이 띄우고 같이 내린다. */
	UPROPERTY()
	TObjectPtr<UAttendanceScreenWidget> AttendanceScreen;

	/** 건네기 화면(M104-06) — 오른쪽 아래 「선물」 버튼. 사냥·목장 어디서나 뜬다. */
	UPROPERTY(Transient)
	TObjectPtr<UGiftScreenWidget> GiftScreen;

	UPROPERTY()
	TObjectPtr<UBackpackScreenWidget> BackpackScreen;

	// 메뉴는 HUD 와 달리 필요할 때 처음 만들고 그 뒤로는 화면에 올렸다 내렸다만 한다 —
	// 매번 새로 만들면 나중에 옵션 값이 들어왔을 때 열 때마다 초기화된다.
	UPROPERTY()
	TObjectPtr<UPauseMenuWidget> PauseMenu;
};
