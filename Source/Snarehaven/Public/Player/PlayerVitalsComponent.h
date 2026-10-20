#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlayerVitalsComponent.generated.h"

struct FPlayerVitalsRow;
enum class EBuffKind : uint8;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerHealthChangedSignature, float, NewHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerStaminaChangedSignature, float, NewStamina);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerDiedSignature);

// ROADMAP M11-01: 플레이어의 체력·스태미나 두 값을 한 곳에서 판정한다.
// 숫자는 여기 없다 — `DT_PlayerVitalsTable` 의 행(DESIGN 3.1 표의 사본)을 받아 채운다 (DataDriven.md 3).
// 짜임은 `UAnimalStateComponent` 와 같다: 컴포넌트가 상태를 갖고, 바뀌면 **알림으로** 던진다
// (HUD 게이지는 M11-02 가 이 알림을 듣는다 — 주기 갱신 없음, DESIGN 3.1 HUD 규칙).
//
// DESIGN 3.1 이 정한 것만 한다:
//  - 달리는 동안 스태미나가 닳고, 공격 1타가 정액을 먹는다. 걷기·정지는 0 이다.
//  - 달리기·공격을 멈추고 `StaminaRegenDelaySeconds` 가 지나면 스태미나가 찬다.
//  - 스태미나 0 의 벌은 **달리기 불가 + 공격 불가**까지다 (기절·즉사 없음).
//  - 그 상태로 계속 이동하면 **탈진** — 체력이 초당 깎인다. 멈추면 풀린다.
//  - 체력은 탈진이 아니고 스태미나가 문턱 위일 때만 천천히 찬다.
//
// 부착은 `ASnarehavenCharacter` 가 한다 (M11-02, 루프#144).
// **사망 뒤 처리는 여기서 끝나지 않는다** — 이 컴포넌트는 `OnDied` 를 한 번 던지고 값만 되돌릴 줄 알고,
// 시작 자리로 옮기는 것과 지급 대기열을 버리는 것은 캐릭터 몫이다 (M11-03).
// 재화·인벤토리는 이 컴포넌트가 절대 건드리지 않는다 (CLAUDE.md 8 · DESIGN 4.8).
// 복제도 아직 없다 — 자기 체력·스태미나는 자기 화면에만 뜨고, 남의 값을 보는 화면이 아직 없다
// (게스트에게 보일 필요가 생기면 `UAnimalStateComponent` 처럼 두 값만 실으면 된다).
UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API UPlayerVitalsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerVitalsComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * 시간이 흐른 만큼 소모·회복·탈진을 굴린다. 틱이 부르는 알맹이고, 2층 검사는 이쪽을 직접 부른다 —
	 * 등록 안 된 컴포넌트로 `TickComponent` 를 부르면 엔진이 `bRegistered` 단언에서 죽는다(월드가 필요하다).
	 */
	UFUNCTION(BlueprintCallable, Category = "Vitals")
	void AdvanceTime(float DeltaSeconds);

	/**
	 * 표 한 줄로 최대치·속도를 채우고 체력·스태미나를 최대로 되돌린다.
	 * 행에 미정(음수)이 하나라도 있으면 채우지 않고 경고만 남긴다 (Verification.md 7).
	 * @return 숫자가 성해서 실제로 초기화됐으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Vitals")
	bool InitializeFromRow(const FPlayerVitalsRow& Row);

	/** 달리기 입력이 들어오고 있는지 (조이스틱이 달리기 문턱을 넘은 동안 true). */
	UFUNCTION(BlueprintCallable, Category = "Vitals")
	void SetRunInput(bool bInRunInput);

	/** 이동 입력이 들어오고 있는지 (걷기 포함). 탈진 판정이 이 값을 본다. */
	UFUNCTION(BlueprintCallable, Category = "Vitals")
	void SetMoveInput(bool bInMoveInput);

	/**
	 * 물속인지와 수영 숫자를 준다 (DESIGN 8.9 — `UWaterTravelComponent` 가 매 틱 부른다).
	 * 물속이면 `AdvanceTime` 이 스태미나를 초당 `StaminaPerSecond` 씩 빼고, 0 이면 체력을 초당
	 * `DrownHealthPerSecond` 씩 뺀다. 물속에선 회복이 없다. 물 밖(false)이면 3.1 규칙 그대로.
	 */
	void SetSwimState(bool bNowInWater, float StaminaPerSecond, float DrownHealthPerSecond);

	/**
	 * 지금 선 존의 체력 감소(초당)를 준다 (DESIGN 1.3 설원 — `UZoneEffectComponent` 가 매 틱 부른다).
	 * 0 이면 안 깎는다. 값은 `FZoneRow::HealthDrainPerSecond` 에서 온다 — 여기엔 숫자가 없다.
	 */
	void SetZoneHealthDrain(float HealthPerSecond);

	/** 마지막 `SetZoneHealthDrain` 값. */
	float GetZoneHealthDrain() const { return ZoneHealthDrainPerSecond; }

	/**
	 * 쓰고 있는 장비의 체력 회복(초당)을 준다 (DESIGN 13.5 딸기 모자 — 캐릭터가 매 틱 머리 칸 행에서 부른다).
	 * 상시라 물속·존 감소 중에도 더한다. 0 이면 없다. 값은 `FItemRow::HealthRegenPerSecond`.
	 */
	void SetEquipmentHealthRegen(float HealthPerSecond);

	/**
	 * 입은 장비의 수영 스태미나 소모 배율 (DESIGN 13.8 구명조끼 — 캐릭터가 매 틱 상의 칸 행에서 부른다).
	 * 0 이하 = 상관없다(×1). SwimSave 버프와 곱한다. 값은 `FItemRow::SwimStaminaMul`.
	 */
	void SetEquipmentSwimStaminaMul(float Mul);

	/**
	 * 먹은 음식의 버프를 건다 (DESIGN 13.6). 같은 종류는 새 것이 덮어쓰고, 다른 종류는 같이 간다.
	 * 값·시간은 부르는 쪽이 `FItemRow::BuffValue`·`BuffSeconds` 에서 가져온다 — 여기엔 숫자가 없다.
	 * @return None 이거나 시간이 0 이하라 안 걸었으면 false
	 */
	bool ApplyBuff(EBuffKind Kind, float Value, float Seconds);

	/** 지금 걸린 그 종류의 버프 값(비율). 없거나 끝났으면 0. */
	float GetBuffValue(EBuffKind Kind) const;

	/** 그 종류의 버프가 끝나기까지 남은 초. 없으면 0. */
	float GetBuffSecondsLeft(EBuffKind Kind) const;

	/** 지금 물속으로 치는지 (마지막 `SetSwimState`). */
	bool IsSwimming() const { return bInWater; }

	/**
	 * 공격 1타 값을 스태미나에서 뺀다. 모자라면 아무것도 빼지 않고 false —
	 * 부르는 쪽(M11-02 뒤 `UMeleeAttackComponent`)은 그때 타격을 내보내지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vitals")
	bool TryConsumeAttackStamina();

	/** 거점존 진입 시 체력·스태미나 전회복 (DESIGN 3.1). 존 배선은 M12-01. */
	UFUNCTION(BlueprintCallable, Category = "Vitals")
	void RestoreToFull();

	/**
	 * 먹어서 체력을 채운다 (DESIGN 8.10). 양은 **서버 `ConsumeItem` 응답의 `heal`** 이다 — 클라이언트가 정하지 않는다.
	 * 죽었거나 0 이하면 아무것도 안 한다. 최대치를 넘지 않는다.
	 * @return 실제로 체력이 올랐으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Vitals")
	bool ApplyHeal(float Amount);

	/**
	 * 사망 상태를 풀고 체력·스태미나를 표의 부활 값(DESIGN 3.1 = 50/50)으로 되돌린다 (M11-03).
	 * **전회복과 다른 함수인 이유**: 전회복이면 죽는 것이 곧 회복 수단이 된다 (DESIGN 3.1 근거 칸).
	 * 재화·인벤토리는 여기서도 안 건드린다 — 잃는 것의 판정은 부르는 쪽(캐릭터)에 있다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vitals")
	void ReviveAtRespawnValues();

	/**
	 * **3층 검사 전용** — 체력을 한 번에 0 으로 내려 사망 알림을 낸다 (`-SnareKillPlayer=<초>`).
	 * 게임 규칙의 길이 아니다: 지금 체력이 닳는 길은 탈진뿐인데 달리기 소모가 0 이라(DESIGN 3.1,
	 * 스태미나 = 수영 전용) 헤드리스에서는 죽을 방법이 없다. 동물이 때려서 죽는 진짜 길은
	 * `ApplyDamage` (M5-07d2) 이고, 이 함수는 검사 스위치 쪽에만 남는다.
	 */
	void KillForCheck();

	/**
	 * 동물 한 대만큼 체력을 깎는다 (M5-07d2, DESIGN 3.1 "동물 공격"). 판정은 호스트가 이미 끝냈다 —
	 * 이 함수는 소유 기기에서 그 결과를 적용할 뿐이다. 초기화 전·사망·0 이하 값이면 false.
	 */
	bool ApplyDamage(float Amount);

	/**
	 * 체력을 채운다 (최대에서 멈춘다). 쓰러진 뒤에는 안 찬다 — 부활은 `ReviveAtRespawnValues` 길이다.
	 * 모닥불·화로 옆 회복(DESIGN 8.10)이 부른다. 양은 부르는 쪽이 표(`DT_BaseRuleTable`)에서 가져온다.
	 */
	void Heal(float Amount);

	/** 지금 달릴 수 있는지. 스태미나가 0 이면 걷기로 강제된다 (DESIGN 3.1). */
	UFUNCTION(BlueprintPure, Category = "Vitals")
	bool CanRun() const;

	/** 지금 공격할 수 있는지 (1타 값만큼 스태미나가 남았는지). */
	UFUNCTION(BlueprintPure, Category = "Vitals")
	bool CanAttack() const;

	/** 스태미나가 0 인 채로 이동 입력을 계속하는 상태 — 체력이 깎인다. 멈추면 풀린다. */
	UFUNCTION(BlueprintPure, Category = "Vitals")
	bool IsExhausted() const { return bExhausted; }

	UFUNCTION(BlueprintPure, Category = "Vitals")
	bool IsDead() const { return bDead; }

	UFUNCTION(BlueprintPure, Category = "Vitals")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "Vitals")
	float GetStamina() const { return Stamina; }

	UFUNCTION(BlueprintPure, Category = "Vitals")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Vitals")
	float GetMaxStamina() const { return MaxStamina; }

	/**
	 * 쓰러진 뒤 **누운 채로 있는 시간**(초) — DESIGN 3.1 「쓰러짐 → 부활」의 표 값이다 (M5-01e2).
	 * 부활까지의 전체 시간은 여기에 쓰러짐 클립 길이를 더한 것이고, 그 덧셈은 캐릭터가 한다
	 * (클립을 아는 쪽이 캐릭터다). 표를 못 읽었으면 음수 그대로라 부르는 쪽이 미정을 알아챈다.
	 */
	UFUNCTION(BlueprintPure, Category = "Vitals")
	float GetDeathLingerSeconds() const { return DeathLingerSeconds; }

	/** 체력이 바뀔 때. HUD 체력 게이지(M11-02)가 이걸 듣는다. */
	UPROPERTY(BlueprintAssignable, Category = "Vitals")
	FOnPlayerHealthChangedSignature OnHealthChanged;

	/** 스태미나가 바뀔 때. HUD 스태미나 게이지(M11-02)가 이걸 듣는다. */
	UPROPERTY(BlueprintAssignable, Category = "Vitals")
	FOnPlayerStaminaChangedSignature OnStaminaChanged;

	/** 체력이 0 이 된 순간 한 번. 부활·잃는 것 판정은 `ASnarehavenCharacter::HandlePlayerDied` 가 듣는다 (M11-03). */
	UPROPERTY(BlueprintAssignable, Category = "Vitals")
	FOnPlayerDiedSignature OnDied;

private:
	/** 체력을 더하거나 빼고, 실제로 값이 움직였을 때만 알린다. 0 이 되면 사망. */
	void ApplyHealthDelta(float Delta);

	/** 스태미나를 더하거나 빼고, 실제로 값이 움직였을 때만 알린다. */
	void ApplyStaminaDelta(float Delta);

	// 아래 숫자는 전부 `DT_PlayerVitalsTable` 행에서 온다 — 기본값 -1 은 "아직 안 채움" 이다.
	float MaxHealth = -1.0f;
	float MaxStamina = -1.0f;
	float RunStaminaPerSecond = -1.0f;
	float AttackStaminaCost = -1.0f;
	float StaminaRegenPerSecond = -1.0f;
	float StaminaRegenDelaySeconds = -1.0f;
	float ExhaustionHealthPerSecond = -1.0f;
	float HealthRegenPerSecond = -1.0f;
	float HealthRegenStaminaThreshold = -1.0f;
	float RespawnHealth = -1.0f;
	float RespawnStamina = -1.0f;
	float DeathLingerSeconds = -1.0f;

	float Health = 0.0f;
	float Stamina = 0.0f;

	/** 달리기·공격을 마지막으로 한 뒤 흐른 시간(초). 월드 시계가 아니라 틱 누적이라 2층이 그대로 부를 수 있다. */
	float TimeSinceStaminaUseSeconds = 0.0f;

	bool bRunInput = false;
	bool bMoveInput = false;
	bool bInWater = false;
	float SwimStaminaPerSecond = 0.0f;
	float SwimDrownHealthPerSecond = 0.0f;
	float ZoneHealthDrainPerSecond = 0.0f;
	float EquipmentHealthRegenPerSecond = 0.0f;
	float EquipmentSwimStaminaMul = 1.0f;
	/** 버프 종류(`EBuffKind` 순번)마다 한 칸 — 같은 종류는 한 칸을 덮어쓴다. 0 칸(None)은 안 쓴다. */
	static constexpr int32 BuffKindCount = 5;
	float BuffValues[BuffKindCount] = {};
	float BuffSecondsLeft[BuffKindCount] = {};
	bool bExhausted = false;
	bool bDead = false;
	bool bInitialized = false;
};
