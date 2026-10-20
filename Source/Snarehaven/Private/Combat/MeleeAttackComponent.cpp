#include "Combat/MeleeAttackComponent.h"
#include "Snarehaven.h"
#include "World/SnareCampCageActor.h"
#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareServerReply.h"
#include "Building/SnareBaseBuildComponent.h"
#include "Building/SnareBaseSubsystem.h"
#include "CollisionQueryParams.h"
#include "Net/UnrealNetwork.h"
#include "Dom/JsonObject.h"
#include "UI/HuntHudWidget.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Data/ItemRow.h"
#include "Data/PlayerCombatRow.h"
#include "Equipment/EquipmentComponent.h"
#include "Player/PlayerVitalsComponent.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "World/ZonePortalActor.h"
#include "SnarehavenCharacter.h"
#include "World/SnareSound.h"

#define LOCTEXT_NAMESPACE "MeleeAttack"

UMeleeAttackComponent::UMeleeAttackComponent()
{
	// 대상 찾기·타격 지연은 전부 타이머가 돌린다 — 매 프레임 볼 것이 없다
	// (engines/unreal.md 코드 규칙 2 — Tick 에서 액터 전수 조사 금지).
	// 틱은 카메라 흔들림(M21-02) 0.12초 동안만 켠다 — 평소엔 꺼져 있다.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// M3-02: 컴포넌트가 복제 대상이어야 `Server_` RPC 가 호스트로 간다. 복제하는 값은 탄창 수(M39-03) 하나다.
	SetIsReplicatedByDefault(true);
}

bool UMeleeAttackComponent::HasAttackAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner == nullptr || Owner->HasAuthority();
}

void UMeleeAttackComponent::Server_AttackOnce_Implementation()
{
	AttackOnceOnAuthority();
}

void UMeleeAttackComponent::Server_FireOnce_Implementation()
{
	FireOnceOnAuthority();
}

void UMeleeAttackComponent::Server_SetLoadedRounds_Implementation(int32 Rounds)
{
	SetLoadedRounds(Rounds);
}

void UMeleeAttackComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMeleeAttackComponent, LoadedRounds);
}

void UMeleeAttackComponent::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 하이라이트는 플레이어가 걸어다니는 동안에도 따라와야 한다. 주기를 새로 만들지 않으려고
	// 공격 간격과 같은 값을 쓴다 (DESIGN 3.1 0.6초) — 숫자를 하나도 새로 만들지 않으려는 선택이다.
	if (AttackIntervalSeconds > 0.f)
	{
		World->GetTimerManager().SetTimer(
			TargetScanTimerHandle, this, &UMeleeAttackComponent::HandleTargetScan, AttackIntervalSeconds, true);
	}
}

void UMeleeAttackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 타이머를 남겨 두면 파괴된 컴포넌트를 다시 부른다 (engines/unreal.md 코드 규칙 7).
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TargetScanTimerHandle);
		World->GetTimerManager().ClearTimer(ActionCooldownHandle);
		World->GetTimerManager().ClearTimer(HitDelayTimerHandle);
		World->GetTimerManager().ClearTimer(SwingEndTimerHandle);
		World->GetTimerManager().ClearTimer(HitStopTimerHandle);
		// 날아가는 총알(`LandShot` 예약)은 핸들을 따로 안 든다 — 이 컴포넌트에 묶인 것을 통째로 끈다.
		World->GetTimerManager().ClearAllTimersForObject(this);
	}

	// 느려진 몸·흔들린 카메라를 남긴 채 사라지면 그대로 굳는다.
	EndHitStop();
	EndCameraShake();

	// 하이라이트를 켠 채로 사라지면 동물이 계속 빛난다.
	SetTarget(nullptr);

	Super::EndPlay(EndPlayReason);
}

void UMeleeAttackComponent::StartAttacking()
{
	bIsAttacking = true;

	// 한 번 누름 = 휘두르기 한 번 (사용자 2026-10-04 「레퍼런스는 한 번 클릭, 다시 누를 수 있을 때까지 딜레이」).
	// 간격 안 누름은 버린다 — 미리 눌러 두기도 없다. 누르고 있어도 반복하지 않으므로 옛 「포획 창이 열리면
	// 자동 반복을 멈춘다」(2026-09-20 12:45)도 필요 없다: 5타째는 언제나 사람이 새로 누른 것이다.
	if (!IsActionReady())
	{
		++IgnoredPressCount;
		UE_LOG(LogSnarehaven, Log, TEXT("[입력] 공격 간격 안 누름이라 버린다 (%d번째)"), IgnoredPressCount);
		return;
	}

	// 총을 들면 공격 = 쏘기 (M39-03). 한 번 누름 = 한 발이고 간격은 총 행의 `FireIntervalSeconds`(`GetActionLengthSeconds`).
	// 휘두르기 대신 한 발 나갈 때 `Multicast_PlayShot` 이 반동 클립을 튼다(M39-06b). 거점(목장 존)에서는 쏘지 않는다: 목장 동물 오사 방지 🟡,
	// 그 존의 버튼 동작(넣기·수확·가구)은 `BeginAttack` 이 이 줄보다 먼저 맡는다.
	if (IsHoldingGun())
	{
		if (USnareBaseBuildComponent::GetTentZone(GetWorld()) == SnareBaseZones::Ranch)
		{
			UE_LOG(LogSnarehaven, Log, TEXT("[총] 거점에서는 쏘지 않는다"));
			return;
		}
		MarkActionUsed();
		FireOnce();
		return;
	}

	// 버튼을 누른 순간 휘두르기가 바로 시작된다 — 모바일에서 첫 동작이 간격만큼 늦으면 반응이 없다고 느낀다.
	// 데미지는 여기가 아니라 `AttackHitDelaySeconds` 뒤다 (DESIGN 3.1 공격 타격 지연).
	SwingOnce();
}

void UMeleeAttackComponent::StopAttacking()
{
	// 누름 상태만 끈다. 예약된 타격(`HitDelayTimerHandle`)과 도는 중인 휘두르기(`SwingEndTimerHandle`)는
	// 그대로 둔다 — 0.1초만 눌렀다 떼도 휘두르기 한 바퀴가 끝까지 돌고 그 안의 한 대가 나간다
	// (사용자 결함 2026-09-23 16:40 ①·③). 끄면 탭 공격이 헛손질이 되고 몸 클립이 툭 끊긴다.
	bIsAttacking = false;
}

bool UMeleeAttackComponent::IsActionReady() const
{
	const UWorld* const World = GetWorld();
	return World == nullptr || !World->GetTimerManager().IsTimerActive(ActionCooldownHandle);
}

float UMeleeAttackComponent::GetActionLengthSeconds() const
{
	// 간격이 미정(-1)이면 고정 길이도 쓰지 않는다 — 표를 못 읽은 채 휘두르지 않는 `SwingOnce` 와 같은 줄.
	if (AttackIntervalSeconds <= 0.f)
	{
		return AttackIntervalSeconds;
	}
	// 총은 쏘기 간격이 동작 길이다 (8.6-c 권총 0.4). 칸이 0 이면 근접 공격 간격.
	const FItemRow* const Weapon = FindEquippedWeaponRow();
	if (Weapon != nullptr && Weapon->MagazineSize > 0 && Weapon->FireIntervalSeconds > 0.f)
	{
		return Weapon->FireIntervalSeconds;
	}
	const ASnarehavenCharacter* const Body = Cast<ASnarehavenCharacter>(GetOwner());
	const float FixedSeconds = Body != nullptr ? Body->GetHeldFixedUseSeconds() : 0.f;
	return FixedSeconds > KINDA_SMALL_NUMBER ? FixedSeconds : AttackIntervalSeconds;
}

void UMeleeAttackComponent::MarkActionUsed()
{
	UWorld* const World = GetWorld();
	const float LengthSeconds = GetActionLengthSeconds();
	if (World != nullptr && LengthSeconds > 0.f)
	{
		World->GetTimerManager().SetTimer(ActionCooldownHandle, FTimerDelegate(), LengthSeconds, false);
	}
}

void UMeleeAttackComponent::SwingOnce()
{
	// 표를 아직 못 읽었으면(-1) 추측으로 때리지 않는다 — 지연 0 으로 눌러 버리면 "타격이 클립보다
	// 먼저 들어간다"(사용자 결함 2026-09-23 16:40)가 조용히 되돌아온다 (Verification.md 7).
	if (AttackIntervalSeconds < 0.f || AttackHitDelaySeconds < 0.f)
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("공격 설정이 아직 미정(-1)이라 휘두르지 않는다 - 간격 %.2f · 타격 지연 %.2f (`DT_PlayerCombatTable`)"),
			AttackIntervalSeconds, AttackHitDelaySeconds);
		return;
	}

	++SwingCount;
	bSwinging = true;
	MarkActionUsed();

	// `SwingCount`·`bSwinging` 은 복제되지 않는다 — 남의 화면에도 휘두르기 동작을 틀게 알린다 (INBOX 2026-10-15 감사 (c)).
	// 혼자 하기(Standalone)는 알릴 곳이 없다. 피해 판정은 그대로 `AttackOnce` 길이다.
	if (GetNetMode() != NM_Standalone)
	{
		if (HasAttackAuthority())
		{
			Multicast_PlaySwing();
		}
		else
		{
			Server_PlaySwing();
		}
	}

	UWorld* const World = GetWorld();
	if (World == nullptr)
	{
		// 월드가 없으면 지연을 걸 곳이 없다 (컴포넌트만 띄운 검사 자리). 동작 없이 바로 판정한다.
		bSwinging = false;
		AttackOnce();
		return;
	}

	FTimerManager& Timers = World->GetTimerManager();

	// 한 바퀴 길이는 공격 간격과 같다 — 몸 클립 재생 속도가 `클립 길이 ÷ 공격 간격`(DESIGN 3.3)이라
	// 그렇게 맞아떨어진다. 새 숫자를 만들지 않는다 (DataDriven.md 3).
	// 「정해진 길이」 행(전기톱·고급/전기 곡괭이)이면 한 바퀴 = 그 클립 길이(재생 속도 1), 타격은 그 안에 한 번.
	const float LengthSeconds = GetActionLengthSeconds();
	if (LengthSeconds > 0.f)
	{
		Timers.SetTimer(SwingEndTimerHandle, this, &UMeleeAttackComponent::EndSwing, LengthSeconds, false);
	}
	else
	{
		bSwinging = false;
	}

	// 3층에서 "언제 휘둘렀고 언제 맞았나" 를 재는 유일한 짝이다 — 아래 `타격 %d회` 줄과의 시각 차가
	// 곧 타격 지연이다. 빗나간 휘두르기도 여기 남는다(타격 줄은 맞았을 때만 난다).
	UE_LOG(LogSnarehaven, Log, TEXT("휘두르기 %d회 시작 - 타격 지연 %.2f초 · 한 바퀴 %.2f초"),
		SwingCount, AttackHitDelaySeconds, LengthSeconds);
	if (const AActor* const Owner = GetOwner())
	{
		PlayCue(TEXT("Swing"), Owner->GetActorLocation(), 0.5f);   // DESIGN 2.2 소리
	}

	if (AttackHitDelaySeconds > 0.f)
	{
		Timers.SetTimer(HitDelayTimerHandle, this, &UMeleeAttackComponent::HandleDelayedHit,
			AttackHitDelaySeconds, false);
		return;
	}

	AttackOnce();
}

void UMeleeAttackComponent::HandleDelayedHit()
{
	// 타격음은 누른 기기에서 — 게스트는 호스트 판정을 기다리면 한 박자 늦게 들린다.
	if (const AAnimalCharacter* const Target = GetCurrentTarget())
	{
		// 든 무기 행에 소리 칸(`UseSoundCue`)이 있으면 그 소리, 맨손이면 기본 `Hit` (M16-07 — 무기 이름은 가르지 않는다).
		const FItemRow* const Weapon = FindEquippedWeaponRow();
		const FName HitCue = (Weapon != nullptr && !Weapon->UseSoundCue.IsNone()) ? Weapon->UseSoundCue : FName(TEXT("Hit"));
		PlayCue(HitCue, Target->GetActorLocation(), 0.8f);
		// 때린 사람 본인 기기만 손이 톡 떤다 (M63-02) — 남의 기기·끔·간격 안은 `PlayStrikeHaptic` 이 거른다.
		if (ASnarehavenCharacter* const Striker = Cast<ASnarehavenCharacter>(GetOwner()))
		{
			Striker->PlayStrikeHaptic(SnareBodyHaptic::EStrike::Melee);
		}
	}
	else if (const AActor* const Owner = GetOwner(); IsValid(Owner) && GetWorld() != nullptr)
	{
		// 맞힐 동물이 없으면 곁의 캠프 우리를 친다 (18단계 부수기 — 내 기기 로컬 장식이라 판정도 여기서 끝).
		ASnareCampCageActor::StrikeNearest(GetWorld(), Owner->GetActorLocation());
	}
	AttackOnce();
}

void UMeleeAttackComponent::PlayCue(FName Cue, const FVector& Location, float Volume)
{
	// 폰이면 남의 기기에도 그 사람 자리에서 들리게 중계한다 (M16-08).
	if (ASnarehavenCharacter* const Body = Cast<ASnarehavenCharacter>(GetOwner()))
	{
		Body->PlayActionSound(Cue, Location, Volume);
		return;
	}
	USnareSoundSubsystem::PlayAt(this, Cue, Location, Volume);
}

void UMeleeAttackComponent::EndSwing()
{
	bSwinging = false;
}

void UMeleeAttackComponent::HandleTargetScan()
{
	RefreshTarget();
}

bool UMeleeAttackComponent::AttackOnce()
{
	// M11-04: 스태미나는 **입력이 난 기기**에서 뺀다. `UPlayerVitalsComponent` 는 복제되지 않아서
	// (M11-01 · 루프#146 `확인`) 호스트는 남의 스태미나를 모른다 — 호스트 쪽에서 빼면 게스트는
	// 자기 값이 그대로인 채 호스트의 남의 값이 깎인다. 모자라면 요청 자체를 안 보낸다.
	// 여기가 아니라 `AttackOnceOnAuthority` 에서 빼면 게스트 1타에 두 번(게스트 자기 + 호스트) 깎인다.
	if (UPlayerVitalsComponent* const Vitals = GetOwner() != nullptr
		? GetOwner()->FindComponentByClass<UPlayerVitalsComponent>() : nullptr)
	{
		if (!Vitals->TryConsumeAttackStamina())
		{
			return false;
		}
	}

	if (!HasAttackAuthority())
	{
		// 게스트는 요청만 보낸다. 하이라이트는 스캔 타이머가 게스트 화면에서도 계속 갱신한다.
		Server_AttackOnce();
		return false;
	}

	return AttackOnceOnAuthority();
}

bool UMeleeAttackComponent::AttackOnceOnAuthority()
{
	const AActor* const Owner = GetOwner();
	AAnimalCharacter* Target = RefreshTarget();
	if (Target == nullptr || !IsValid(Owner))
	{
		return false;
	}

	UAnimalStateComponent* State = Target->GetAnimalState();
	if (!IsValid(State))
	{
		return false;
	}

	// M11-05: 데미지가 미정(-1)이면 때리지 않는다 — 음수를 그대로 넣으면 동물 체력이 **늘어난다**.
	if (AttackDamage < 0.f)
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("1타 데미지가 아직 미정(-1)이라 타격을 판정하지 않는다 (`DT_PlayerCombatTable`)"));
		return false;
	}

	// M11-04: 끝거리로 갈수록 데미지가 준다 (DESIGN 8.6-b). 근접은 계수가 1.0/1.0 이라 실제로는
	// 줄지 않는다 — 곡선은 총기가 들어올 때(8절 항목7) 값만 갈아 끼우면 그대로 산다.
	const float DistanceCm = FVector::Dist(Owner->GetActorLocation(), Target->GetActorLocation());
	return ApplyHitOnAuthority(Target, AttackDamage * GetDamageRatioAtDistance(DistanceCm));
}

bool UMeleeAttackComponent::ApplyHitOnAuthority(AAnimalCharacter* Target, float Damage)
{
	const AActor* const Owner = GetOwner();
	UAnimalStateComponent* const State = IsValid(Target) ? Target->GetAnimalState() : nullptr;
	if (!IsValid(Owner) || !IsValid(State))
	{
		return false;
	}
	// 13.5 사냥 모자 — 막타를 친 사람의 머리 칸 배율이 최상급 드롭 확률에 곱해진다(호스트 판정, M22).
	const UEquipmentComponent* const Equipment = Owner->FindComponentByClass<UEquipmentComponent>();
	const FItemRow* const Hat = IsValid(Equipment) ? Equipment->GetEquippedRow(EEquipSlot::Head) : nullptr;
	const float PristineMul = Hat != nullptr && Hat->PristineChanceMul > 0.f ? Hat->PristineChanceMul : 1.f;
	if (!State->ApplyDamage(Damage, PristineMul))
	{
		return false;
	}
	// M21-06 같이 잡기 — 데미지가 실제로 들어간 사람만 남긴다(빗나간 입력은 안 센다).
	const double NowSeconds = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
	State->RecordHitter(GetOwner(), NowSeconds);

	OnMeleeHit.Broadcast(Target, Damage);

	// M21-02 때리는 손맛. 그림(클립·깜빡임·파편)은 모두에게, 넉백은 호스트가 밀고 복제로, 멈춤·흔들림은 때린 사람 기기만.
	if (HasHitFeel())
	{
		const int32 DebrisCount = FMath::RandRange(FMath::FloorToInt32(HitDebrisMin), FMath::FloorToInt32(HitDebrisMax));
		Target->Multicast_PlayHitReaction(HitFlashSeconds, static_cast<uint8>(FMath::Clamp(DebrisCount, 0, 255)));

		FVector Away = Target->GetActorLocation() - Owner->GetActorLocation();
		Away.Z = 0.f;
		if (Away.IsNearlyZero())
		{
			Away = -Target->GetActorForwardVector();
		}
		Target->StartKnockback(Away.GetSafeNormal2D() * KnockbackCm, KnockbackSeconds);

		Client_PlayHitFeel(Target);
	}

	// 몇 타째인지 로그 한 줄로 남긴다 — 3층 인용만으로는 타격 수가 안 보여서 "5타 뒤 스태미나가 15"
	// 같은 주장이 증거로 서지 못했다 (루프#153 ⓑ, `tools/jev.py evidence` NOT_SUPPORTED 세 번).
	// 세는 자리는 여기 하나뿐이다 — 호스트가 데미지를 실제로 넣은 뒤라 빗나간 입력은 안 센다.
	// 스태미나는 **입력이 난 기기**의 값이라(M11-01 비복제) 게스트 타격을 호스트가 대신 찍을 때는
	// 호스트가 가진 사본이 찍힌다. 헤드리스 3층은 혼자 도는 리슨 서버라 둘이 같다.
	++LandedHitCount;
	const UPlayerVitalsComponent* const AttackerVitals =
		Owner->FindComponentByClass<UPlayerVitalsComponent>();
	UE_LOG(LogSnarehaven, Log,
		TEXT("타격 %d회 - 대상 %s · 데미지 %.1f · 동물 체력 %.1f · 내 스태미나 %.1f"),
		LandedHitCount, *Target->GetName(), Damage, State->GetCurrentHP(),
		IsValid(AttackerVitals) ? AttackerVitals->GetStamina() : 0.f);

	// 이번 타격이 마지막 타였다면 드롭을 알린다. 드롭 목록은 동물이 죽는 순간 이미 정해져 있다
	// (UAnimalStateComponent::RollDeathDrops) — 여기서 무엇이 나올지 정하지 않는다.
	if (State->GetState() == EAnimalState::Dead)
	{
		OnAnimalKilled.Broadcast(Target, State->GetDeathDrops());

		// M21-07f 주간 우두머리 (DESIGN 10.6) — 막타 사람 + 창 안 명중자 각자의 기기가 자기 계정으로 `ClaimBoss` 를 묻는다.
		// 창이 미정(음수)이면 막타 사람만. 보상 판정·주 1회는 서버 몫이다 (CLAUDE.md 8).
		if (State->IsBoss())
		{
			TArray<const AActor*> Claimers = { Owner };
			if (ShareDropWindowSeconds >= 0.f)
			{
				for (AActor* const Hitter : State->GetHittersWithin(NowSeconds, ShareDropWindowSeconds))
				{
					Claimers.AddUnique(Hitter);
				}
			}
			for (const AActor* const Claimer : Claimers)
			{
				if (UMeleeAttackComponent* const Melee = Claimer->FindComponentByClass<UMeleeAttackComponent>())
				{
					Melee->Client_ClaimBossReward();
				}
			}
			UE_LOG(LogSnarehaven, Log, TEXT("우두머리 처치 - %s · 보상 묻기 %d명"), *Target->GetName(), Claimers.Num());
		}

		// M21-06 같이 잡기 (DESIGN 3.1) — 창 안에 명중한 다른 사람도 각자 한 마리 몫을 받는다. 각자의 몸이 자기 기기 대기열에
		// 종 ID 를 쌓고(`Client_QueueKillGrant`), 드롭은 서버 `ClaimLoot {kills}` 가 사람마다 굴린다 (지시 ① · CLAUDE.md 8).
		if (ShareDropWindowSeconds >= 0.f)
		{
			int32 SharedCount = 0;
			for (AActor* const Hitter : State->GetHittersWithin(NowSeconds, ShareDropWindowSeconds))
			{
				UMeleeAttackComponent* const Other = Hitter != Owner ? Hitter->FindComponentByClass<UMeleeAttackComponent>() : nullptr;
				if (IsValid(Other))
				{
					Other->OnAnimalKilled.Broadcast(Target, State->GetDeathDrops());
					++SharedCount;
				}
			}
			if (SharedCount > 0)
			{
				UE_LOG(LogSnarehaven, Log, TEXT("같이 잡기 - %s 드롭 %d개를 막타 말고 %d명도 받는다 (창 %.0f초)"),
					*Target->GetName(), State->GetDeathDrops().Num(), SharedCount, ShareDropWindowSeconds);
			}
		}

		// M8-03: 분석 이벤트는 **이 함수 안에서만** 나간다 — 여기는 호스트 권위 자리다. 게스트도 보내면
		// 한 사건이 두 번 세어진다 (DESIGN 4.1 권위 2단 · `docs/ANALYTICS.md` 표 A).
		// 싣는 값은 호스트가 이미 판정해 둔 것뿐이고, **지급도 잔액도 여기서 건드리지 않는다** (CLAUDE.md 8).
		UGameInstance* const GameInstance = UGameplayStatics::GetGameInstance(this);
		if (USnareBackendSubsystem* const Backend =
			IsValid(GameInstance) ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr)
		{
			TMap<FName, FString> Payload;
			Payload.Add(TEXT("pet_item_id"), State->GetPetItemID().ToString());
			Payload.Add(TEXT("drop_count"), FString::FromInt(State->GetDeathDrops().Num()));
			Payload.Add(TEXT("zone_id"), AZonePortalActor::FindCurrentZoneID(GetWorld()).ToString());
			Backend->WriteGameEvent(TEXT("animal_killed"), Payload);
		}
	}

	// 방금 죽거나 포획된 동물은 더 때릴 수 없다 — 하이라이트를 즉시 넘긴다.
	if (State->IsFinished())
	{
		RefreshTarget();
	}

	return true;
}

void UMeleeAttackComponent::Client_ClaimBossReward_Implementation()
{
	++BossClaimRequestCount;

	UGameInstance* const GameInstance = UGameplayStatics::GetGameInstance(this);
	USnareBackendSubsystem* const Backend = IsValid(GameInstance) ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
	if (Backend == nullptr)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("우두머리 보상 - 백엔드가 없어 묻지 않는다 (%s)"), *GetNameSafe(GetOwner()));
		return;
	}

	const TWeakObjectPtr<UMeleeAttackComponent> WeakThis(this);
	const TWeakObjectPtr<USnareBackendSubsystem> WeakBackend(Backend);
	Backend->CallServerFunction(TEXT("ClaimBoss"), MakeShared<FJsonObject>(),
		[WeakThis, WeakBackend](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			const FText Notice = DescribeBossClaimReply(bOk, Response, Error);
			UE_LOG(LogSnarehaven, Log, TEXT("우두머리 보상 답 - %s"), *Notice.ToString());
			UMeleeAttackComponent* const Self = WeakThis.Get();
			const ASnarehavenCharacter* const Body = Self != nullptr ? Cast<ASnarehavenCharacter>(Self->GetOwner()) : nullptr;
			if (Body != nullptr && IsValid(Body->GetHuntHud()))
			{
				// 서버 실패 답만 빨간 오류 알약 (M26-04) — 「이미 받았다」 는 거절이 아니라 안내라 크림 카드.
				const bool bFailed = SnareServerReply::GetFailureCode(Response) != TEXT("already_claimed")
					&& !SnareServerReply::DescribeFailure(bOk, Response, Error).IsEmpty();
				if (bFailed)
				{
					Body->GetHuntHud()->ShowErrorBanner(Notice);
				}
				else
				{
					Body->GetHuntHud()->ShowNoticeBanner(Notice);
				}
			}
			// 모자·코인은 서버가 넣었다 — 손으로 더하지 않고 다시 받는다 (CLAUDE.md 8).
			bool bWon = false;
			if (bOk && Response.IsValid() && !Response->HasField(TEXT("error")) && Response->TryGetBoolField(TEXT("won"), bWon) && bWon && WeakBackend.IsValid())
			{
				WeakBackend->FetchPlayerState();
			}
		});
}

FText UMeleeAttackComponent::DescribeBossClaimReply(bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
{
	if (SnareServerReply::GetFailureCode(Response) == TEXT("already_claimed"))
	{
		return LOCTEXT("BossAlreadyClaimed", "우두머리 처치 — 이번 주 보상은 이미 받았다");
	}
	const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
	if (!Failure.IsEmpty())
	{
		return FText::Format(LOCTEXT("BossClaimFailed", "우두머리 보상 실패 — {0}"), Failure);
	}

	const TSharedPtr<FJsonObject>* Reward = nullptr;
	if (Response->TryGetObjectField(TEXT("reward"), Reward) && Reward != nullptr && (*Reward).IsValid())
	{
		int32 Coins = 0;
		if ((*Reward)->TryGetNumberField(TEXT("coins"), Coins) && Coins > 0)
		{
			return FText::Format(LOCTEXT("BossRewardCoins", "우두머리 처치 — 모자 당첨! 이미 있어 코인 {0}"), FText::AsNumber(Coins));
		}
		return LOCTEXT("BossRewardHat", "우두머리 처치 — 우두머리 모자를 받았다!");
	}
	return LOCTEXT("BossRewardNone", "우두머리 처치 — 이번 주 모자는 꽝");
}

bool UMeleeAttackComponent::HasHitFeel() const
{
	return HitStopSeconds >= 0.f && HitStopTimeDilation >= 0.f && KnockbackCm >= 0.f && KnockbackSeconds >= 0.f
		&& CameraShakeAmplitudeCm >= 0.f && CameraShakeSeconds >= 0.f && HitFlashSeconds >= 0.f
		&& HitDebrisMin >= 0.f && HitDebrisMax >= HitDebrisMin;
}

void UMeleeAttackComponent::Client_PlayHitFeel_Implementation(AAnimalCharacter* Target)
{
	AActor* const Owner = GetOwner();
	UWorld* const World = GetWorld();
	if (!HasHitFeel() || !IsValid(Target) || !IsValid(Owner) || World == nullptr)
	{
		return;
	}

	// ① 히트스톱 — 전역 시간 배율(`SetGlobalTimeDilation`)은 쓰지 않는다: 리슨 호스트가 때리면 같은 월드의
	// 다른 플레이어까지 멈춘다. 때린 몸과 맞은 동물 둘만 느리게 한다 (감독 지시 M21-02).
	if (HitStopSeconds > 0.f)
	{
		EndHitStop();
		Owner->CustomTimeDilation = HitStopTimeDilation;
		Target->CustomTimeDilation = HitStopTimeDilation;
		HitStopAttacker = Owner;
		HitStopTarget = Target;
		bHitStopActive = true;
		// 월드 타이머는 액터 배율을 안 탄다 — 0.06초 뒤에 정확히 풀린다.
		World->GetTimerManager().SetTimer(HitStopTimerHandle, this, &UMeleeAttackComponent::EndHitStop, HitStopSeconds, false);
		UE_LOG(LogSnarehaven, Log, TEXT("히트스톱 시작 - %s · %s 배율 %.2f · %.2f초"),
			*Owner->GetName(), *Target->GetName(), HitStopTimeDilation, HitStopSeconds);
	}

	// ② 카메라 흔들림 — 이 기기의 카메라만. 폰이 아니면(검사가 띄운 맨 액터) 카메라가 없다.
	const ASnarehavenCharacter* const Body = Cast<ASnarehavenCharacter>(Owner);
	UCameraComponent* const Camera = Body != nullptr ? Body->GetTopDownCamera() : nullptr;
	if (Camera != nullptr && CameraShakeSeconds > 0.f && CameraShakeAmplitudeCm > 0.f)
	{
		if (!bShaking)
		{
			ShakeBaseLocation = Camera->GetRelativeLocation();
		}
		bShaking = true;
		ShakeStartRealSeconds = World->GetRealTimeSeconds();
		SetComponentTickEnabled(true);
	}
}

void UMeleeAttackComponent::EndHitStop()
{
	if (!bHitStopActive)
	{
		return;
	}
	bHitStopActive = false;
	if (AActor* const Attacker = HitStopAttacker.Get())
	{
		Attacker->CustomTimeDilation = 1.f;
	}
	if (AActor* const Target = HitStopTarget.Get())
	{
		Target->CustomTimeDilation = 1.f;
	}
	UE_LOG(LogSnarehaven, Log, TEXT("히트스톱 끝 - %s · %s 배율 1.00"),
		HitStopAttacker.IsValid() ? *HitStopAttacker->GetName() : TEXT("(없음)"),
		HitStopTarget.IsValid() ? *HitStopTarget->GetName() : TEXT("(없음)"));
	HitStopAttacker.Reset();
	HitStopTarget.Reset();
}

void UMeleeAttackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const ASnarehavenCharacter* const Body = Cast<ASnarehavenCharacter>(GetOwner());
	UCameraComponent* const Camera = Body != nullptr ? Body->GetTopDownCamera() : nullptr;
	const UWorld* const World = GetWorld();
	const double Elapsed = World != nullptr ? World->GetRealTimeSeconds() - ShakeStartRealSeconds : CameraShakeSeconds;
	if (!bShaking || Camera == nullptr || Elapsed >= CameraShakeSeconds)
	{
		EndCameraShake();
		return;
	}

	// 끝으로 갈수록 0 으로 준다 — 툭 멈추면 카메라가 튄 것처럼 보인다.
	const float Falloff = 1.f - static_cast<float>(Elapsed / CameraShakeSeconds);
	Camera->SetRelativeLocation(ShakeBaseLocation + FMath::VRand() * (CameraShakeAmplitudeCm * Falloff));
}

void UMeleeAttackComponent::EndCameraShake()
{
	SetComponentTickEnabled(false);
	if (!bShaking)
	{
		return;
	}
	bShaking = false;
	const ASnarehavenCharacter* const Body = Cast<ASnarehavenCharacter>(GetOwner());
	if (UCameraComponent* const Camera = Body != nullptr ? Body->GetTopDownCamera() : nullptr)
	{
		Camera->SetRelativeLocation(ShakeBaseLocation);
	}
}

const FItemRow* UMeleeAttackComponent::FindEquippedWeaponRow() const
{
	const AActor* const Owner = GetOwner();
	const UEquipmentComponent* const Equipment =
		IsValid(Owner) ? Owner->FindComponentByClass<UEquipmentComponent>() : nullptr;
	return IsValid(Equipment) ? Equipment->GetEquippedRow(EEquipSlot::Weapon) : nullptr;
}

bool UMeleeAttackComponent::IsHoldingGun() const
{
	const FItemRow* const Weapon = FindEquippedWeaponRow();
	return Weapon != nullptr && Weapon->MagazineSize > 0;
}

int32 UMeleeAttackComponent::SetLoadedRounds(int32 Rounds)
{
	if (!HasAttackAuthority())
	{
		return LoadedRounds;
	}
	const FItemRow* const Weapon = FindEquippedWeaponRow();
	const int32 Magazine = Weapon != nullptr ? FMath::Max(Weapon->MagazineSize, 0) : 0;
	LoadedRounds = FMath::Clamp(Rounds, 0, Magazine);
	return LoadedRounds;
}

bool UMeleeAttackComponent::RequestReload()
{
	const FItemRow* const Gun = FindEquippedWeaponRow();
	if (Gun == nullptr || Gun->MagazineSize <= 0 || bReloadPending || bReloading || LoadedRounds >= Gun->MagazineSize)
	{
		return false;
	}
	// 남의 폰 복제본은 묻지 않는다 — 가방은 그 기기 계정 것이다 (배낭 `ResolveBackend` 와 같은 판단).
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	if (IsValid(OwnerPawn) && OwnerPawn->GetController() != nullptr && !OwnerPawn->IsLocallyControlled())
	{
		return false;
	}
	const UGameInstance* const GameInstance = GetOwner() != nullptr ? GetOwner()->GetGameInstance() : nullptr;
	USnareBackendSubsystem* const Backend = IsValid(GameInstance) ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
	if (Backend == nullptr)
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("gun"), Gun->ItemID.ToString());
	Args->SetNumberField(TEXT("have"), LoadedRounds);
	bReloadPending = true;
	const TWeakObjectPtr<UMeleeAttackComponent> WeakThis(this);
	Backend->CallServerFunction(TEXT("LoadAmmo"), Args,
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			if (UMeleeAttackComponent* const Self = WeakThis.Get())
			{
				Self->FinishReload(bOk, Response, Error);
			}
		});
	return true;
}

void UMeleeAttackComponent::FinishReload(bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
{
	bReloadPending = false;
	FString Code;
	FString Message;
	double Loaded = 0.0;
	if (!SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message) || !Response->TryGetNumberField(TEXT("loaded"), Loaded))
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[총] LoadAmmo 거절 %s (%s)"), *Code, *Message);
		OnReloadReply.Broadcast(false, Code);
		return;
	}
	// 탄창 수는 서버가 준 값 그대로 — 호스트는 직접, 게스트는 호스트에 알린다(호스트가 다시 자른다).
	const int32 Rounds = FMath::RoundToInt(Loaded);
	if (HasAttackAuthority())
	{
		SetLoadedRounds(Rounds);
	}
	else
	{
		Server_SetLoadedRounds(Rounds);
	}
	UE_LOG(LogSnarehaven, Log, TEXT("[총] LoadAmmo 받음 - 탄창 %d"), Rounds);
	// 몸 동작은 받아들여진 뒤에만 — 탄약이 없어 거절되면 탄창을 넣는 척하지 않는다. 호스트는 모든 기기에 바로,
	// 게스트는 내 화면에서 먼저 틀고 호스트에게 남의 화면을 부탁한다.
	if (HasAttackAuthority())
	{
		Multicast_PlayReload();
	}
	else
	{
		StartReloadMotion();
		Server_PlayReload();
	}
	// 가방 탄약 수는 이어서 오는 스냅샷이 바꾼다 (CLAUDE.md 8).
	const UGameInstance* const GameInstance = GetOwner() != nullptr ? GetOwner()->GetGameInstance() : nullptr;
	if (USnareBackendSubsystem* const Backend = IsValid(GameInstance) ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr)
	{
		Backend->FetchPlayerState();
	}
	OnReloadReply.Broadcast(true, FString());
}

bool UMeleeAttackComponent::FireOnce()
{
	// 재장전 답을 기다리는 동안·재장전 동작 동안 누름은 무시한다 (명세 2절 · DESIGN 8.6-c).
	if (!IsHoldingGun() || bReloadPending || bReloading)
	{
		return false;
	}
	// 빈 탄창은 누른 기기에서 바로 거른다 — 게스트도 복제된 탄창 수를 보니 서버 요청 0 (명세 2절 「찰칵」).
	// 빈 소리·탄 글 깜빡임은 M39-04·06 이 이 계수기를 듣는다.
	if (LoadedRounds <= 0)
	{
		++EmptyClickCount;
		UE_LOG(LogSnarehaven, Log, TEXT("[총] 탄창이 비어 안 나간다 (찰칵 %d번째)"), EmptyClickCount);
		if (const AActor* const Owner = GetOwner())
		{
			PlayGunSound(GunEmptyClickCue, Owner->GetActorLocation(), GunEmptyClickVolume);
		}
		return false;
	}
	if (!HasAttackAuthority())
	{
		Server_FireOnce();
		return false;
	}
	return FireOnceOnAuthority();
}

bool UMeleeAttackComponent::FireOnceOnAuthority()
{
	AActor* const Owner = GetOwner();
	UWorld* const World = GetWorld();
	const FItemRow* const Gun = FindEquippedWeaponRow();
	if (!IsValid(Owner) || World == nullptr || Gun == nullptr || Gun->MagazineSize <= 0)
	{
		return false;
	}
	if (LoadedRounds <= 0)
	{
		++EmptyClickCount;
		return false;
	}
	// 속도·피해가 비면 추측으로 쏘지 않는다 — 속도 0 이면 도착 시각이 무한이다 (Verification.md 7).
	const float BaseDamage = Gun->AttackDamage > 0.f ? Gun->AttackDamage : AttackDamage;
	if (Gun->ProjectileSpeedCmPerSecond <= 0.f || BaseDamage < 0.f)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[총] 총 행에 속도·피해가 비어 쏘지 않는다 - 속도 %.0f · 피해 %.1f"),
			Gun->ProjectileSpeedCmPerSecond, BaseDamage);
		return false;
	}

	--LoadedRounds;
	++ShotCount;

	// 조준 없음(3.3) — 부채꼴 안 가장 가까운 사냥 동물에게 몸을 돌려 쏘고, 없으면 바라보는 쪽으로 사거리 끝까지.
	AAnimalCharacter* Target = FindGunTarget();
	const FVector Start = Owner->GetActorLocation();
	FVector End = IsValid(Target) ? Target->GetActorLocation() : Start + Owner->GetActorForwardVector() * GetEffectiveAttackRangeCm();
	if (IsValid(Target))
	{
		FRotator Facing = (End - Start).Rotation();
		Facing.Pitch = 0.f;
		Facing.Roll = 0.f;
		Owner->SetActorRotation(Facing);
	}

	// 첫 막힘 — 나무·바위·땅은 WorldStatic 이다. 사람·동물·탈것 몸(Pawn)은 이 질의에 안 걸리므로 총알이 **지나간다**
	// (결정 008 덧붙임: 사람 피해 0). 쏜 사람과 손에 붙은 장비 액터는 뺀다.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SnareGunShot), false, Owner);
	TArray<AActor*> Attached;
	Owner->GetAttachedActors(Attached, true, true);
	Params.AddIgnoredActors(Attached);
	FHitResult Hit;
	ESnareGunHitKind HitKind = IsValid(Target) ? ESnareGunHitKind::Animal : ESnareGunHitKind::Miss;
	if (World->LineTraceSingleByObjectType(Hit, Start, End, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		End = Hit.ImpactPoint;
		HitKind = ESnareGunHitKind::World;
		Target = nullptr;
	}

	// 보이는 총알이 박힐 자리 — 동물 = 하얀 별, 막힘 = 맞은 것 따라, 빗나감 = 사거리 끝 아래 땅 먼지(명세 2절).
	FVector SplashEnd = End;
	ESnareGunSplash Splash = HitKind == ESnareGunHitKind::Animal ? ESnareGunSplash::Animal
		: HitKind == ESnareGunHitKind::World ? ASnareGunTracerActor::ClassifyWorldHit(Hit) : ESnareGunSplash::Dust;
	if (HitKind == ESnareGunHitKind::Miss)
	{
		FHitResult Ground;
		const FVector Down(0.f, 0.f, -MissGroundProbeCm);
		if (World->LineTraceSingleByObjectType(Ground, End, End + Down, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			SplashEnd = Ground.ImpactPoint;
		}
	}

	const float DistanceCm = FVector::Dist(Start, End);
	LastShot.Start = Start;
	LastShot.End = End;
	LastShot.HitKind = HitKind;
	LastShot.Target = Target;
	// 펠릿 총은 알마다 `BaseDamage / 알 수` 라 바로 앞에선 총 피해가 같고, 멀수록 빗나가는 알이 는다(DESIGN 8.6-c).
	const int32 PelletCount = FMath::Max(1, Gun->PelletCount);
	const int32 PelletsHit = Target != nullptr
		? CountPelletHits(PelletCount, Gun->PelletSpreadHalfDegrees, DistanceCm, Target->GetSimpleCollisionRadius())
		: 0;
	LastShot.PelletCount = PelletCount;
	LastShot.PelletsHit = PelletsHit;
	LastShot.Damage = PelletsHit > 0
		? BaseDamage * static_cast<float>(PelletsHit) / static_cast<float>(PelletCount) * GetDamageRatioAtDistance(DistanceCm)
		: 0.f;
	LastShot.FlightSeconds = DistanceCm / Gun->ProjectileSpeedCmPerSecond;
	LastShot.Splash = Splash;
	LastShot.SplashEnd = SplashEnd;
	LastShot.SplashFlightSeconds = FVector::Dist(Start, SplashEnd) / Gun->ProjectileSpeedCmPerSecond;

	UE_LOG(LogSnarehaven, Log, TEXT("[총] %d발째 - 맞을 것 %s · 거리 %.0fcm · 도착 %.3f초 · 피해 %.1f · 탄창 %d 남음"),
		ShotCount, Target != nullptr ? *Target->GetName() : (HitKind == ESnareGunHitKind::World ? TEXT("막힘") : TEXT("없음")),
		DistanceCm, LastShot.FlightSeconds, LastShot.Damage, LoadedRounds);

	Multicast_PlayShot(Start, SplashEnd, Splash, LastShot.SplashFlightSeconds);

	// 폭발 총은 맞힌 동물이 아니라 박힌 자리가 중심이다 — 막힘·빗나감에도 터져 둘레 동물을 맞힌다.
	if (Gun->SplashRadiusCm > 0.f)
	{
		const FVector Center = SplashEnd;
		const float EdgeRatio = Gun->SplashEdgeRatio;
		const float Radius = Gun->SplashRadiusCm;
		if (LastShot.SplashFlightSeconds > 0.f)
		{
			FTimerHandle BlastHandle;
			World->GetTimerManager().SetTimer(BlastHandle,
				FTimerDelegate::CreateUObject(this, &UMeleeAttackComponent::LandSplash, Center, BaseDamage, Radius, EdgeRatio),
				LastShot.SplashFlightSeconds, false);
		}
		else
		{
			LandSplash(Center, BaseDamage, Radius, EdgeRatio);
		}
		return true;
	}

	// 피해는 총알이 닿는 순간이다 — 쏜 순간 깎으면 날아가는 총알(M39-05)보다 체력 막대가 먼저 준다.
	if (Target != nullptr && PelletsHit > 0)
	{
		const TWeakObjectPtr<AAnimalCharacter> WeakTarget(Target);
		if (LastShot.FlightSeconds > 0.f)
		{
			FTimerHandle LandHandle;
			World->GetTimerManager().SetTimer(LandHandle,
				FTimerDelegate::CreateUObject(this, &UMeleeAttackComponent::LandShot, WeakTarget, LastShot.Damage),
				LastShot.FlightSeconds, false);
		}
		else
		{
			LandShot(WeakTarget, LastShot.Damage);
		}
	}
	return true;
}

FVector UMeleeAttackComponent::GetMuzzleLocation(const FVector& BodyCenter, const FVector& Forward) const
{
	const AActor* const Owner = GetOwner();
	const UEquipmentComponent* const Equipment = IsValid(Owner) ? Owner->FindComponentByClass<UEquipmentComponent>() : nullptr;
	const UStaticMeshComponent* const Gun = Equipment ? Equipment->GetEquippedMesh(EEquipSlot::Weapon) : nullptr;
	if (IsValid(Gun) && Gun->GetStaticMesh() != nullptr)
	{
		const double TipX = Gun->GetStaticMesh()->GetBoundingBox().Max.X;
		return Gun->GetComponentTransform().TransformPosition(FVector(TipX, 0.0, 0.0));
	}
	return BodyCenter + Forward * MuzzleForwardCm + FVector(0.f, 0.f, MuzzleUpCm);
}

void UMeleeAttackComponent::Multicast_PlayShot_Implementation(FVector_NetQuantize Start, FVector_NetQuantize End,
	ESnareGunSplash Splash, float FlightSeconds)
{
	UWorld* const World = GetWorld();
	AActor* const Owner = GetOwner();
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	StartShotRecoil();
	// 쏜 사람 본인 기기만 손이 떨린다 (M62-02) — 종류는 총 행의 `ShotHapticKind` 가 정한다. None 이면 무음 + 경고.
	if (ASnarehavenCharacter* const Shooter = Cast<ASnarehavenCharacter>(Owner))
	{
		const FItemRow* const GunRow = FindEquippedWeaponRow();
		SnareBodyHaptic::EGun Kind = SnareBodyHaptic::EGun::None;
		if (GunRow != nullptr)
		{
			switch (GunRow->ShotHapticKind)
			{
			case EShotHapticKind::Pistol: Kind = SnareBodyHaptic::EGun::Pistol; break;
			case EShotHapticKind::Rifle: Kind = SnareBodyHaptic::EGun::Rifle; break;
			default: break;
			}
		}
		if (Kind == SnareBodyHaptic::EGun::None)
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("총 행 %s 의 ShotHapticKind 가 None — 쏘기 진동 없음"), GunRow != nullptr ? *GunRow->ItemID.ToString() : TEXT("(없음)"));
		}
		else
		{
			Shooter->PlayShotHaptic(Kind);
		}
	}
	// 몸을 이미 과녁 쪽으로 돌렸으니 쏜 방향(수평)이 앞이다.
	FVector Flat = FVector(End) - FVector(Start);
	Flat.Z = 0.f;
	const FVector Forward = Flat.IsNearlyZero() && IsValid(Owner) ? Owner->GetActorForwardVector() : Flat.GetSafeNormal();
	const FVector Muzzle = GetMuzzleLocation(FVector(Start), Forward);
	{
		const UEquipmentComponent* const Equipment = IsValid(Owner) ? Owner->FindComponentByClass<UEquipmentComponent>() : nullptr;
		const UStaticMeshComponent* const Gun = Equipment ? Equipment->GetEquippedMesh(EEquipSlot::Weapon) : nullptr;
		UE_LOG(LogSnarehaven, Log, TEXT("[총구] 몸 %s · 총구 %s · 총 메시 %s · 과녁 %s"), *FVector(Start).ToString(), *Muzzle.ToString(),
			IsValid(Gun) ? *Gun->GetComponentLocation().ToString() : TEXT("(없음)"), *FVector(End).ToString());
	}
	// 날아가는 시간은 몸 가운데 기준 거리로 판정한 값 — 총구가 앞에 있어 화면의 속도는 그만큼 조금 느리다(75cm 미만 차이).
	const FItemRow* const ShotRow = FindEquippedWeaponRow();
	const float BlastRadiusCm = ShotRow != nullptr ? ShotRow->SplashRadiusCm : 0.f;
	PlayGunSound(BlastRadiusCm > 0.f ? GunRocketLaunchCue : GunShotCue, Muzzle, GunShotVolume);
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASnareGunTracerActor* const Tracer = World->SpawnActor<ASnareGunTracerActor>(Muzzle, FRotator::ZeroRotator, SpawnParams);
	if (Tracer == nullptr)
	{
		return;
	}
	Tracer->Start(Muzzle, End, FlightSeconds, Splash, ShotRow != nullptr ? ShotRow->PelletCount : 1,
		ShotRow != nullptr ? ShotRow->PelletSpreadHalfDegrees : 0.f, BlastRadiusCm);
	LastTracer = Tracer;
	++ShotVisualCount;
	OnShotVisual().Broadcast(ShotVisualCount);
}

UMeleeAttackComponent::FOnShotVisual& UMeleeAttackComponent::OnShotVisual()
{
	static FOnShotVisual Delegate;
	return Delegate;
}

void UMeleeAttackComponent::Server_PlayReload_Implementation()
{
	if (IsHoldingGun())
	{
		Multicast_PlayReload();
	}
}

void UMeleeAttackComponent::Server_PlaySwing_Implementation()
{
	Multicast_PlaySwing();
}

void UMeleeAttackComponent::Multicast_PlaySwing_Implementation()
{
	// 누른 기기는 `SwingOnce` 에서 이미 틀었다 — 남의 몸에만 같은 되감기 신호(`StartShotRecoil` 과 같은 길).
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	if (!IsValid(OwnerPawn) || OwnerPawn->IsLocallyControlled())
	{
		return;
	}
	StartShotRecoil();
}

void UMeleeAttackComponent::Multicast_PlayReload_Implementation()
{
	// 게스트 내 기기는 답을 받자마자 이미 틀었다 — 늦게 온 이 신호로 처음부터 다시 감지 않는다.
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	if (!HasAttackAuthority() && IsValid(OwnerPawn) && OwnerPawn->IsLocallyControlled())
	{
		return;
	}
	StartReloadMotion();
}

void UMeleeAttackComponent::StartReloadMotion()
{
	// 재장전 몸 동작 (M39-06b2) — 행 `ReloadSeconds` 한 바퀴. 탄창 수는 이미 서버 응답으로 들어갔고, 이건 그 동안 쏘기를 막는 박자다.
	const FItemRow* const Gun = FindEquippedWeaponRow();
	UWorld* const World = GetWorld();
	if (World == nullptr || Gun == nullptr || Gun->ReloadSeconds <= 0.f)
	{
		return;
	}
	++ReloadMotionCount;
	bReloading = true;
	if (const AActor* const Owner = GetOwner())
	{
		PlayGunSound(GunReloadCue, Owner->GetActorLocation(), GunReloadVolume);
	}
	World->GetTimerManager().SetTimer(ReloadEndTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]() { bReloading = false; }),
		Gun->ReloadSeconds, false);
}

void UMeleeAttackComponent::PlayGunSound(const TCHAR* Cue, const FVector& Location, float Volume)
{
	++GunSoundCount;
	LastGunSound = FName(Cue);
	USnareSoundSubsystem::PlayAt(this, LastGunSound, Location, Volume);
}

void UMeleeAttackComponent::StartShotRecoil()
{
	// 반동 몸 클립 (M39-06b) — 쏜 사람 몸이 **모든 기기에서** 총 행의 쓰기 클립(`A_Player_PistolRecoil`)을 튼다.
	// 휘두르기와 같은 신호(`SwingCount` 되감기·`bSwinging` 한 바퀴)라 캐릭터는 근접 휘두르기와 같은 길로 쏘기 간격 한 바퀴에 돌린다.
	// 타격 판정은 걸지 않는다 — 총 피해는 `FireOnceOnAuthority` 의 도착 시각 타이머 몫이다.
	UWorld* const World = GetWorld();
	const float LengthSeconds = GetActionLengthSeconds();
	if (World == nullptr || LengthSeconds <= 0.f)
	{
		return;
	}
	++SwingCount;
	bSwinging = true;
	World->GetTimerManager().SetTimer(SwingEndTimerHandle, this, &UMeleeAttackComponent::EndSwing, LengthSeconds, false);
}

void UMeleeAttackComponent::LandShot(TWeakObjectPtr<AAnimalCharacter> Target, float Damage)
{
	AAnimalCharacter* const Animal = Target.Get();
	const UAnimalStateComponent* const State = IsValid(Animal) ? Animal->GetAnimalState() : nullptr;
	if (!IsValid(State) || State->IsFinished())
	{
		return;
	}
	ApplyHitOnAuthority(Animal, Damage);
}

void UMeleeAttackComponent::LandSplash(FVector Center, float DirectDamage, float RadiusCm, float EdgeRatio)
{
	UWorld* const World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	// 돌면서 피해를 넣으면 죽은 동물이 반복자를 흔들 수 있어 먼저 모은다.
	TArray<TPair<TWeakObjectPtr<AAnimalCharacter>, float>> Victims;
	for (TActorIterator<AAnimalCharacter> It(World); It; ++It)
	{
		AAnimalCharacter* const Animal = *It;
		const UAnimalStateComponent* const State = IsValid(Animal) ? Animal->GetAnimalState() : nullptr;
		if (!IsValid(State) || State->IsFinished())
		{
			continue;
		}
		const float Damage = ComputeSplashDamage(DirectDamage, RadiusCm, EdgeRatio, FVector::Dist(Center, Animal->GetActorLocation()));
		if (Damage > 0.f)
		{
			Victims.Emplace(Animal, Damage);
		}
	}
	UE_LOG(LogSnarehaven, Log, TEXT("[총] 폭발 - 반경 %.0fcm 안 %d마리"), RadiusCm, Victims.Num());
	for (const TPair<TWeakObjectPtr<AAnimalCharacter>, float>& Victim : Victims)
	{
		LandShot(Victim.Key, Victim.Value);
	}
}

float UMeleeAttackComponent::ComputeSplashDamage(float DirectDamage, float RadiusCm, float EdgeRatio, float DistanceCm)
{
	if (RadiusCm <= 0.f || DistanceCm > RadiusCm)
	{
		return 0.f;
	}
	const float Alpha = FMath::Clamp(DistanceCm / RadiusCm, 0.f, 1.f);
	return DirectDamage * FMath::Lerp(1.f, EdgeRatio, Alpha);
}

AAnimalCharacter* UMeleeAttackComponent::FindGunTarget() const
{
	const AActor* const Owner = GetOwner();
	UWorld* const World = GetWorld();
	const float RangeCm = GetEffectiveAttackRangeCm();
	if (!IsValid(Owner) || World == nullptr || RangeCm <= 0.f)
	{
		return nullptr;
	}

	// 부채꼴 ±60° = 명세 2절 「앞쪽 부채꼴」. 뒤에 있는 동물에게 몸을 홱 돌려 쏘면 바라본 쪽과 총알이 갈린다.
	const float MinFacingDot = FMath::Cos(FMath::DegreesToRadians(60.f));
	const FVector Origin = Owner->GetActorLocation();
	const FVector Forward = Owner->GetActorForwardVector().GetSafeNormal2D();
	AAnimalCharacter* Nearest = nullptr;
	float NearestSquared = 0.f;
	for (TActorIterator<AAnimalCharacter> It(World); It; ++It)
	{
		AAnimalCharacter* const Animal = *It;
		const UAnimalStateComponent* const State = IsValid(Animal) ? Animal->GetAnimalState() : nullptr;
		if (!IsValid(State) || State->IsFinished())
		{
			continue;
		}
		const FVector Offset = Animal->GetActorLocation() - Origin;
		const float DistanceSquared = Offset.SizeSquared();
		if (DistanceSquared > RangeCm * RangeCm || FVector::DotProduct(Forward, Offset.GetSafeNormal2D()) < MinFacingDot)
		{
			continue;
		}
		if (Nearest == nullptr || DistanceSquared < NearestSquared)
		{
			Nearest = Animal;
			NearestSquared = DistanceSquared;
		}
	}
	return Nearest;
}

float UMeleeAttackComponent::GetEffectiveAttackRangeCm() const
{
	const FItemRow* const Weapon = FindEquippedWeaponRow();
	return (Weapon != nullptr && Weapon->AttackRangeCm > 0.f) ? Weapon->AttackRangeCm : AttackRangeCm;
}

int32 UMeleeAttackComponent::CountPelletHits(int32 PelletCount, float SpreadHalfDegrees, float DistanceCm, float TargetRadiusCm)
{
	if (PelletCount <= 1)
	{
		return 1;
	}
	const float HitHalfDegrees = DistanceCm > 0.f ? FMath::RadiansToDegrees(FMath::Atan(TargetRadiusCm / DistanceCm)) : 180.f;
	int32 Hits = 0;
	for (int32 Index = 0; Index < PelletCount; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / static_cast<float>(PelletCount - 1);
		const float AngleDegrees = FMath::Lerp(-SpreadHalfDegrees, SpreadHalfDegrees, Alpha);
		if (FMath::Abs(AngleDegrees) <= HitHalfDegrees + KINDA_SMALL_NUMBER)
		{
			++Hits;
		}
	}
	return Hits;
}

float UMeleeAttackComponent::GetDamageRatioAtDistance(float DistanceCm) const
{
	const FItemRow* const Weapon = FindEquippedWeaponRow();
	const float StartRatio =
		(Weapon != nullptr && Weapon->FalloffStartRatio > 0.f) ? Weapon->FalloffStartRatio : FalloffStartRatio;
	const float EndRatio =
		(Weapon != nullptr && Weapon->MinDamageRatio > 0.f) ? Weapon->MinDamageRatio : MinDamageRatio;

	// 감쇠가 사거리 끝(또는 그 밖)에서 시작하면 줄어들 구간 자체가 없다 — 근접(s = 1.0)이 이 길이다.
	// 나눗셈보다 먼저 걸러야 한다(`Range - Range*s` 가 0 이 된다).
	const float Range = GetEffectiveAttackRangeCm();
	if (Range <= 0.f || StartRatio >= 1.f)
	{
		return 1.f;
	}

	const float FullDamageCm = Range * StartRatio;
	if (DistanceCm <= FullDamageCm)
	{
		return 1.f;
	}
	if (DistanceCm >= Range)
	{
		return EndRatio;
	}

	return FMath::Lerp(1.f, EndRatio, (DistanceCm - FullDamageCm) / (Range - FullDamageCm));
}

AAnimalCharacter* UMeleeAttackComponent::RefreshTarget()
{
	AAnimalCharacter* NewTarget = FindNearestAnimalInRange();
	SetTarget(NewTarget);
	return NewTarget;
}

bool UMeleeAttackComponent::ApplyCombatRow(const FPlayerCombatRow& Row)
{
	// 네 칸을 **한 번에** 본다 — 한 칸만 미정인 반쪽 상태로 세워 두면 "사거리는 표, 지연은 옛 값" 같은
	// 섞인 설정으로 돌게 된다 (Verification.md 7 · DataDriven.md 3).
	if (Row.AttackDamage < 0.f || Row.AttackRangeCm < 0.f
		|| Row.AttackIntervalSeconds < 0.f || Row.AttackHitDelaySeconds < 0.f)
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("공격 표 행에 미정(-1) 칸이 있어 아무 칸도 안 채운다 - 데미지 %.2f · 사거리 %.2f · 간격 %.2f · 지연 %.2f"),
			Row.AttackDamage, Row.AttackRangeCm, Row.AttackIntervalSeconds, Row.AttackHitDelaySeconds);
		return false;
	}

	// 채우는 길은 기존 세터 둘을 그대로 탄다 — 검사가 테스트 전용 값으로 지나가는 길과 같은 길이라야
	// 실제 플레이가 한 번도 안 지나간 코드가 안 생긴다 (감독 지시 2026-09-23 17:25).
	SetAttackSettings(Row.AttackDamage, Row.AttackRangeCm, Row.AttackIntervalSeconds);
	SetAttackHitDelaySeconds(Row.AttackHitDelaySeconds);
	SetNpcTalkRangeCm(Row.NpcTalkRangeCm); // 따로 본다 — 미정(-1)이면 상인만 안 열리고 싸움은 그대로다.
	SetShareDropWindowSeconds(Row.ShareDropWindowSeconds); // 미정(-1)이면 막타만 받는다 (M21-06).

	UE_LOG(LogSnarehaven, Log,
		TEXT("공격 설정을 표에서 채웠다 - 데미지 %.1f · 사거리 %.0fcm · 간격 %.2f초 · 타격 지연 %.2f초"),
		AttackDamage, AttackRangeCm, AttackIntervalSeconds, AttackHitDelaySeconds);

	// M21-02 손맛 아홉 칸은 따로 본다 — 미정이어도 때리는 건 된다(검사용 행은 네 칸만 채운다). 반쪽으로는 안 넣는다.
	const bool bHitFeelReady = Row.HitStopSeconds >= 0.f && Row.HitStopTimeDilation >= 0.f && Row.KnockbackCm >= 0.f
		&& Row.KnockbackSeconds >= 0.f && Row.CameraShakeAmplitudeCm >= 0.f && Row.CameraShakeSeconds >= 0.f
		&& Row.HitFlashSeconds >= 0.f && Row.HitDebrisMin >= 0.f && Row.HitDebrisMax >= Row.HitDebrisMin;
	if (!bHitFeelReady)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("공격 표 행의 손맛 칸에 미정(-1)이 있어 손맛을 끈다 (`DT_PlayerCombatTable` · DESIGN 3.1)"));
		return true;
	}
	HitStopSeconds = Row.HitStopSeconds;
	HitStopTimeDilation = Row.HitStopTimeDilation;
	KnockbackCm = Row.KnockbackCm;
	KnockbackSeconds = Row.KnockbackSeconds;
	CameraShakeAmplitudeCm = Row.CameraShakeAmplitudeCm;
	CameraShakeSeconds = Row.CameraShakeSeconds;
	HitFlashSeconds = Row.HitFlashSeconds;
	HitDebrisMin = Row.HitDebrisMin;
	HitDebrisMax = Row.HitDebrisMax;
	UE_LOG(LogSnarehaven, Log,
		TEXT("손맛 설정을 표에서 채웠다 - 히트스톱 %.2f초(배율 %.2f) · 넉백 %.0fcm/%.2f초 · 흔들림 %.1fcm/%.2f초 · 깜빡임 %.2f초 · 파편 %.0f~%.0f개"),
		HitStopSeconds, HitStopTimeDilation, KnockbackCm, KnockbackSeconds, CameraShakeAmplitudeCm, CameraShakeSeconds,
		HitFlashSeconds, HitDebrisMin, HitDebrisMax);
	return true;
}

void UMeleeAttackComponent::SetAttackSettings(float Damage, float RangeCm, float IntervalSeconds)
{
	// 아직 안 정한 숫자(음수)는 추측으로 채우지 않는다 (DataDriven.md 3 · Verification.md 7).
	if (Damage < 0.f || RangeCm < 0.f || IntervalSeconds < 0.f)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("근접 공격 설정에 미정(-1) 값이 있어 기존 값을 그대로 둔다"));
		return;
	}

	AttackDamage = Damage;
	AttackRangeCm = RangeCm;
	AttackIntervalSeconds = IntervalSeconds;
}

void UMeleeAttackComponent::SetAttackHitDelaySeconds(float Seconds)
{
	if (Seconds < 0.f)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("공격 타격 지연에 미정(-1) 값이 와서 기존 값을 그대로 둔다"));
		return;
	}

	AttackHitDelaySeconds = Seconds;
}

void UMeleeAttackComponent::SetFalloffSettings(float StartRatio, float MinRatio)
{
	if (StartRatio < 0.f || MinRatio < 0.f)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("감쇠 설정에 미정(-1) 값이 있어 기존 값을 그대로 둔다"));
		return;
	}

	FalloffStartRatio = StartRatio;
	MinDamageRatio = MinRatio;
}

AAnimalCharacter* UMeleeAttackComponent::FindNearestAnimalInRange() const
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	const float RangeCm = GetEffectiveAttackRangeCm();
	if (!IsValid(Owner) || World == nullptr || RangeCm < 0.f)
	{
		return nullptr;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	const float RangeSquared = RangeCm * RangeCm;

	// 동물은 스폰·포획으로 계속 바뀌어서 BeginPlay 캐싱이 통하지 않는다. 대신 매 프레임이 아니라
	// 공격 간격마다만 도는 타이머에서 부른다 (engines/unreal.md 코드 규칙 2 의 취지).
	AAnimalCharacter* Nearest = nullptr;
	float NearestDistanceSquared = 0.f;
	for (TActorIterator<AAnimalCharacter> It(World); It; ++It)
	{
		AAnimalCharacter* Animal = *It;
		if (!IsValid(Animal))
		{
			continue;
		}

		// 죽었거나 이미 포획한 동물은 하이라이트도 타격도 하지 않는다 (DESIGN 3.1 끝 상태).
		const UAnimalStateComponent* State = Animal->GetAnimalState();
		if (!IsValid(State) || State->IsFinished())
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(OwnerLocation, Animal->GetActorLocation());
		if (DistanceSquared > RangeSquared)
		{
			continue;
		}

		if (Nearest == nullptr || DistanceSquared < NearestDistanceSquared)
		{
			Nearest = Animal;
			NearestDistanceSquared = DistanceSquared;
		}
	}

	return Nearest;
}

void UMeleeAttackComponent::SetTarget(AAnimalCharacter* NewTarget)
{
	if (CurrentTarget == NewTarget)
	{
		return;
	}

	SetHighlight(CurrentTarget, false);
	CurrentTarget = NewTarget;
	SetHighlight(CurrentTarget, true);

	OnTargetChanged.Broadcast(CurrentTarget);
}

void UMeleeAttackComponent::SetHighlight(AAnimalCharacter* Animal, bool bHighlighted) const
{
	if (!IsValid(Animal))
	{
		return;
	}

	// 커스텀 뎁스 스텐실만 켜 둔다 — 실제 외곽선은 포스트프로세스 머티리얼 몫이고,
	// 그건 동물 메시(M1-07)·HUD(M1-08) 가 들어온 뒤 아트 단계에서 붙는다.
	// 여기서 색·두께 같은 숫자를 정하지 않는 이유다 (DataDriven.md 3).
	Animal->ForEachComponent<UPrimitiveComponent>(false, [bHighlighted](UPrimitiveComponent* Primitive)
	{
		Primitive->SetRenderCustomDepth(bHighlighted);
	});
}

#undef LOCTEXT_NAMESPACE
