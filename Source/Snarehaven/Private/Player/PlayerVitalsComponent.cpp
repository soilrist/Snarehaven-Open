#include "Player/PlayerVitalsComponent.h"
#include "Snarehaven.h"
#include "Data/PlayerVitalsRow.h"
#include "Data/ItemRow.h"

UPlayerVitalsComponent::UPlayerVitalsComponent()
{
	// 동물 상태와 달리 이쪽은 시간이 흐르기만 해도 값이 움직인다 (소모·회복·탈진).
	PrimaryComponentTick.bCanEverTick = true;
}

bool UPlayerVitalsComponent::InitializeFromRow(const FPlayerVitalsRow& Row)
{
	// 미정(음수)이 하나라도 있으면 채우지 않는다 — 최대치가 0 이면 "시작하자마자 사망" 으로
	// 조용히 오작동한다 (Verification.md 7 · `UAnimalStateComponent::InitializeFromRow` 와 같은 처리).
	const bool bRowIsComplete =
		Row.MaxHealth > 0.f && Row.MaxStamina > 0.f &&
		Row.RunStaminaPerSecond >= 0.f && Row.AttackStaminaCost >= 0.f &&
		Row.StaminaRegenPerSecond >= 0.f && Row.StaminaRegenDelaySeconds >= 0.f &&
		Row.ExhaustionHealthPerSecond >= 0.f && Row.HealthRegenPerSecond >= 0.f &&
		Row.HealthRegenStaminaThreshold >= 0.f &&
		// 부활 체력이 0 이면 부활하자마자 다시 죽는다 — 0 도 미정으로 친다 (M11-03).
		Row.RespawnHealth > 0.f && Row.RespawnStamina >= 0.f &&
		// 누운 시간은 0 도 뜻이 있는 값이다(바로 일어남) — 음수만 미정이다 (M5-01e2).
		Row.DeathLingerSeconds >= 0.f;

	if (!bRowIsComplete)
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("플레이어 생존 수치가 아직 미정이다 (MaxHealth=%.1f, MaxStamina=%.1f) — 체력·스태미나를 켜지 않는다"),
			Row.MaxHealth, Row.MaxStamina);
		return false;
	}

	MaxHealth = Row.MaxHealth;
	MaxStamina = Row.MaxStamina;
	RunStaminaPerSecond = Row.RunStaminaPerSecond;
	AttackStaminaCost = Row.AttackStaminaCost;
	StaminaRegenPerSecond = Row.StaminaRegenPerSecond;
	StaminaRegenDelaySeconds = Row.StaminaRegenDelaySeconds;
	ExhaustionHealthPerSecond = Row.ExhaustionHealthPerSecond;
	HealthRegenPerSecond = Row.HealthRegenPerSecond;
	HealthRegenStaminaThreshold = Row.HealthRegenStaminaThreshold;
	RespawnHealth = FMath::Min(Row.RespawnHealth, Row.MaxHealth);
	RespawnStamina = FMath::Min(Row.RespawnStamina, Row.MaxStamina);
	DeathLingerSeconds = Row.DeathLingerSeconds;

	bInitialized = true;
	bDead = false;
	bExhausted = false;
	TimeSinceStaminaUseSeconds = StaminaRegenDelaySeconds;
	RestoreToFull();
	return true;
}

void UPlayerVitalsComponent::SetRunInput(bool bInRunInput)
{
	bRunInput = bInRunInput;
}

void UPlayerVitalsComponent::SetMoveInput(bool bInMoveInput)
{
	bMoveInput = bInMoveInput;

	// 멈추면 탈진이 풀린다 — 벌은 계속 밀어붙이는 동안만 이어진다 (DESIGN 3.1).
	if (!bMoveInput)
	{
		bExhausted = false;
	}
}

void UPlayerVitalsComponent::SetSwimState(bool bNowInWater, float StaminaPerSecond, float DrownHealthPerSecond)
{
	bInWater = bNowInWater;
	SwimStaminaPerSecond = StaminaPerSecond;
	SwimDrownHealthPerSecond = DrownHealthPerSecond;
}

void UPlayerVitalsComponent::SetZoneHealthDrain(float HealthPerSecond)
{
	ZoneHealthDrainPerSecond = FMath::Max(HealthPerSecond, 0.f);
}

void UPlayerVitalsComponent::SetEquipmentHealthRegen(float HealthPerSecond)
{
	EquipmentHealthRegenPerSecond = FMath::Max(HealthPerSecond, 0.f);
}

void UPlayerVitalsComponent::SetEquipmentSwimStaminaMul(float Mul)
{
	EquipmentSwimStaminaMul = Mul > 0.f ? Mul : 1.f;
}

bool UPlayerVitalsComponent::ApplyBuff(EBuffKind Kind, float Value, float Seconds)
{
	const int32 Index = static_cast<int32>(Kind);
	if (Kind == EBuffKind::None || Index >= BuffKindCount || Seconds <= 0.f)
	{
		return false;
	}
	BuffValues[Index] = Value;
	BuffSecondsLeft[Index] = Seconds;
	return true;
}

float UPlayerVitalsComponent::GetBuffValue(EBuffKind Kind) const
{
	const int32 Index = static_cast<int32>(Kind);
	return Index > 0 && Index < BuffKindCount && BuffSecondsLeft[Index] > 0.f ? BuffValues[Index] : 0.f;
}

float UPlayerVitalsComponent::GetBuffSecondsLeft(EBuffKind Kind) const
{
	const int32 Index = static_cast<int32>(Kind);
	return Index > 0 && Index < BuffKindCount ? BuffSecondsLeft[Index] : 0.f;
}

bool UPlayerVitalsComponent::CanRun() const
{
	// 스태미나가 바닥이면 걷기로 강제된다. 조작을 빼앗지는 않는다 (DESIGN 3.1).
	return bInitialized && !bDead && Stamina > 0.f;
}

bool UPlayerVitalsComponent::CanAttack() const
{
	return bInitialized && !bDead && Stamina >= AttackStaminaCost;
}

bool UPlayerVitalsComponent::TryConsumeAttackStamina()
{
	if (!CanAttack())
	{
		return false;
	}

	ApplyStaminaDelta(-AttackStaminaCost);
	TimeSinceStaminaUseSeconds = 0.f;
	return true;
}

void UPlayerVitalsComponent::RestoreToFull()
{
	if (!bInitialized)
	{
		return;
	}

	bDead = false;
	bExhausted = false;
	ApplyHealthDelta(MaxHealth - Health);
	ApplyStaminaDelta(MaxStamina - Stamina);
}

bool UPlayerVitalsComponent::ApplyHeal(float Amount)
{
	if (!bInitialized || bDead || Amount <= 0.f)
	{
		return false;
	}

	const float Before = Health;
	ApplyHealthDelta(Amount);
	return Health > Before;
}

void UPlayerVitalsComponent::ReviveAtRespawnValues()
{
	if (!bInitialized)
	{
		return;
	}

	// 살아 있는 사람에게 이 함수를 부르면 체력이 **깎일** 수 있다 (100 -> 50). 부활은 죽은 뒤에만이다.
	if (!bDead)
	{
		return;
	}

	bDead = false;
	bExhausted = false;
	// 🟡 죽으면 버프도 끝난다(DESIGN 13.6) — 부활 값 50/50 위에 버프가 남으면 죽음이 벌이 덜 된다.
	FMemory::Memzero(BuffSecondsLeft);

	// 사망 직후라 대기 시간이 0 에 머물러 있으면 부활하자마자 회복이 안 돈다 — 대기를 다 채워 둔다.
	TimeSinceStaminaUseSeconds = StaminaRegenDelaySeconds;

	// 값은 표에서 온다 (DESIGN 3.1 = 50/50). 여기 박힌 숫자는 0개다.
	ApplyHealthDelta(RespawnHealth - Health);
	ApplyStaminaDelta(RespawnStamina - Stamina);
}

bool UPlayerVitalsComponent::ApplyDamage(float Amount)
{
	if (!bInitialized || bDead || Amount <= 0.f)
	{
		return false;
	}

	ApplyHealthDelta(-Amount);
	return true;
}

void UPlayerVitalsComponent::KillForCheck()
{
	if (!bInitialized || bDead)
	{
		return;
	}

	// 값을 지어내지 않는다 — 지금 남은 체력만큼만 깎아 0 으로 만든다. 사망 알림은 아래가 낸다.
	ApplyHealthDelta(-Health);
	UE_LOG(LogSnarehaven, Log, TEXT("검사용 사망 (-SnareKillPlayer) - 체력을 0 으로 내렸다"));
}

void UPlayerVitalsComponent::Heal(float Amount)
{
	if (!bInitialized || bDead || Amount <= 0.f)
	{
		return;
	}

	ApplyHealthDelta(Amount);
}

void UPlayerVitalsComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AdvanceTime(DeltaTime);
}

void UPlayerVitalsComponent::AdvanceTime(float DeltaTime)
{
	if (!bInitialized || bDead || DeltaTime <= 0.f)
	{
		return;
	}

	// 버프 (DESIGN 13.6): 이번 틱 값을 먼저 읽고 시간을 줄인다 — 180초 버프가 180초 내내 먹게.
	const float ColdResist = GetBuffValue(EBuffKind::ColdResist);
	const float SwimCostPerSecond = SwimStaminaPerSecond * EquipmentSwimStaminaMul // 13.8 구명조끼
		* FMath::Max(1.f - GetBuffValue(EBuffKind::SwimSave), 0.f);
	const float RegenMul = 1.f + GetBuffValue(EBuffKind::Regen);
	for (float& SecondsLeft : BuffSecondsLeft)
	{
		SecondsLeft = FMath::Max(SecondsLeft - DeltaTime, 0.f);
	}

	// 존 특성 (DESIGN 1.3 설원 — M12-01): 칸 안에 있는 동안 물속이든 뭍이든 따로 깎는다.
	// 뭍에서는 아래 자연 회복과 겹쳐 순 감소가 (감소 - 회복)이 된다 — 1.3 근거 칸이 그 순 값으로 버티는 시간을 쟀다.
	// 추위 버프(13.6)는 감소 × (1 − 값). 존 감소는 설원 하나뿐이라 존을 가리지 않는다.
	if (ZoneHealthDrainPerSecond > 0.f)
	{
		ApplyHealthDelta(-ZoneHealthDrainPerSecond * FMath::Max(1.f - ColdResist, 0.f) * DeltaTime);
		if (bDead)
		{
			return;
		}
	}

	// 장비 회복 (DESIGN 13.5 딸기 모자): 상시 — 물속에서도 더한다.
	if (EquipmentHealthRegenPerSecond > 0.f)
	{
		ApplyHealthDelta(EquipmentHealthRegenPerSecond * DeltaTime);
	}

	// 수영 (DESIGN 8.9): 물속이면 스태미나만 닳고, 0 이면 체력이 닳는다. 회복·탈진 규칙은 안 탄다.
	if (bInWater)
	{
		const float StaminaBefore = Stamina;
		ApplyStaminaDelta(-SwimCostPerSecond * DeltaTime);
		TimeSinceStaminaUseSeconds = 0.f;
		// 이 틱 안에서 바닥난 **뒤의 시간만** 체력을 깎는다 — 틱 길이에 따라 깎이는 양이 달라지지 않게.
		const float SecondsOnEmpty = SwimCostPerSecond > 0.f
			? FMath::Clamp(DeltaTime - StaminaBefore / SwimCostPerSecond, 0.f, DeltaTime)
			: (Stamina <= 0.f ? DeltaTime : 0.f);
		if (Stamina <= 0.f && SecondsOnEmpty > 0.f)
		{
			ApplyHealthDelta(-SwimDrownHealthPerSecond * SecondsOnEmpty);
		}
		return;
	}

	// 달리기 소모가 0(DESIGN 3.1 "스태미나 = 수영 전용")이면 달리기는 스태미나를 쓰는 행동이 아니다 —
	// 회복 대기를 초기화하지도, 탈진을 걸지도 않는다 (8.9 "착수 때 같이 볼 것" ①). 안 끊으면 수영으로
	// 바닥난 뒤 물 밖에서 달리는 동안 회복이 멈추고 체력이 닳는다.
	const bool bRunCostsStamina = RunStaminaPerSecond > 0.f;
	const bool bRunning = bRunCostsStamina && bRunInput && CanRun();
	if (bRunning)
	{
		ApplyStaminaDelta(-RunStaminaPerSecond * DeltaTime);
		TimeSinceStaminaUseSeconds = 0.f;
	}
	else
	{
		TimeSinceStaminaUseSeconds += DeltaTime;
	}

	// 스태미나 0 인 채로 이동 입력을 계속하면 탈진 — 한 번 걸리면 멈출 때까지 회복도 막힌다.
	// 안 막으면 20/초 회복이 2/초 감소를 바로 덮어 "계속 밀어붙이면 죽는다" 가 성립하지 않는다.
	if (bRunCostsStamina && Stamina <= 0.f && bMoveInput)
	{
		bExhausted = true;
	}

	if (bExhausted)
	{
		ApplyHealthDelta(-ExhaustionHealthPerSecond * DeltaTime);
		return;
	}

	if (!bRunning)
	{
		// 대기가 끝난 **뒤의 시간만** 회복으로 친다 — 틱 하나가 대기 경계를 걸치면 그 조각만 찬다.
		// (`>= 대기` 로 틱 전체를 세면 틱 길이에 따라 회복량이 달라진다.)
		const float RegenSeconds = FMath::Clamp(TimeSinceStaminaUseSeconds - StaminaRegenDelaySeconds, 0.f, DeltaTime);
		if (RegenSeconds > 0.f)
		{
			ApplyStaminaDelta(StaminaRegenPerSecond * RegenSeconds);
		}
	}

	if (Stamina >= HealthRegenStaminaThreshold)
	{
		ApplyHealthDelta(HealthRegenPerSecond * RegenMul * DeltaTime);
	}
}

void UPlayerVitalsComponent::ApplyStaminaDelta(float Delta)
{
	const float NewStamina = FMath::Clamp(Stamina + Delta, 0.f, MaxStamina);
	if (FMath::IsNearlyEqual(NewStamina, Stamina))
	{
		return;
	}

	Stamina = NewStamina;
	OnStaminaChanged.Broadcast(Stamina);
}

void UPlayerVitalsComponent::ApplyHealthDelta(float Delta)
{
	const float NewHealth = FMath::Clamp(Health + Delta, 0.f, MaxHealth);
	if (FMath::IsNearlyEqual(NewHealth, Health))
	{
		return;
	}

	Health = NewHealth;
	OnHealthChanged.Broadcast(Health);

	// 사망 알림은 한 번뿐이다 — 부활·잃는 것 판정은 M11-03 이 여기에 붙는다.
	if (Health <= 0.f && !bDead)
	{
		bDead = true;
		bExhausted = false;
		OnDied.Broadcast();
	}
}
