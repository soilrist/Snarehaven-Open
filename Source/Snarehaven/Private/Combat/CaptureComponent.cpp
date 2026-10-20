#include "Combat/CaptureComponent.h"
#include "Snarehaven.h"
#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Combat/MeleeAttackComponent.h"
#include "Combat/SnareCaptureRing.h"
#include "Data/PlayerCombatRow.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "World/ZonePortalActor.h"

UCaptureComponent::UCaptureComponent()
{
	// 포획은 버튼을 누른 순간에만 일어난다 — 매 프레임 볼 것이 없다
	// (engines/unreal.md 코드 규칙 2).
	PrimaryComponentTick.bCanEverTick = false;

	// M3-02: 컴포넌트가 복제 대상이어야 `Server_` RPC 가 호스트로 간다 (값은 하나도 복제하지 않는다).
	SetIsReplicatedByDefault(true);
}

bool UCaptureComponent::HasCaptureAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner == nullptr || Owner->HasAuthority();
}

void UCaptureComponent::Server_TryCaptureTarget_Implementation()
{
	TryCaptureTargetOnAuthority();
}

void UCaptureComponent::Server_BeginCaptureRing_Implementation()
{
	// 게스트 링은 둘째 누름이 흔들림만큼 늦게 닿을 수 있어 호스트 시간 초과를 허용 오차만큼 늦춘다 (M21-09c).
	BeginCaptureRingOnAuthority(FMath::Max(ToleranceSeconds, 0.f));
}

void UCaptureComponent::Server_ResolveCaptureRing_Implementation(float GuestElapsedSeconds)
{
	if (!IsCaptureRingActive())
	{
		// 링 없이 온 판정 요청 — 시간 초과가 먼저 끝냈거나 시작 요청이 없었다. 실패로 세지 않는다.
		UE_LOG(LogSnarehaven, Log, TEXT("올가미 링 판정 요청 무시 - 호스트에 링이 없다 (게스트 경과 %.2f초)"), GuestElapsedSeconds);
		return;
	}
	if (RingTarget.Get() != GetCaptureTarget())
	{
		// 호스트 쪽 대상이 바뀌었다 — 호스트 둘째 누름과 같이 옛 링은 조용히 지운다.
		ClearCaptureRing();
		return;
	}
	ResolveGuestCaptureRingAt(GuestElapsedSeconds, GetCaptureRingElapsed());
}

UMeleeAttackComponent* UCaptureComponent::FindMeleeAttack() const
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return nullptr;
	}

	UMeleeAttackComponent* Melee = Owner->FindComponentByClass<UMeleeAttackComponent>();
	if (!IsValid(Melee))
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("%s: 근접 공격 컴포넌트가 없어 포획 대상을 알 수 없다"), *Owner->GetName());
		return nullptr;
	}

	return Melee;
}

AAnimalCharacter* UCaptureComponent::GetCaptureTarget() const
{
	const UMeleeAttackComponent* Melee = FindMeleeAttack();
	return IsValid(Melee) ? Melee->GetCurrentTarget() : nullptr;
}

bool UCaptureComponent::CanCapture() const
{
	const AAnimalCharacter* Target = GetCaptureTarget();
	if (!IsValid(Target))
	{
		return false;
	}

	const UAnimalStateComponent* State = Target->GetAnimalState();
	return IsValid(State) && State->GetState() == EAnimalState::Capturable;
}

USnareBackendSubsystem* UCaptureComponent::FindBackendForAnalytics() const
{
	UGameInstance* const GameInstance = UGameplayStatics::GetGameInstance(this);
	return IsValid(GameInstance) ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
}

FString UCaptureComponent::CaptureFailureReason(const UAnimalStateComponent* State)
{
	// 이름을 여기서 고른 이유: 표(`DT_AnalyticsEventTable`)는 **칸 이름**까지만 정하고 값은 안 정한다.
	if (!IsValid(State))
	{
		return TEXT("no_state");
	}
	if (State->GetState() == EAnimalState::Escaping)
	{
		// 링을 놓쳐 달아나는 중 — 버튼이 꺼져 있지만 게스트 요청이 늦게 닿을 수 있다 (M21-09b).
		return TEXT("escaping");
	}
	return State->IsFinished() ? TEXT("already_finished") : TEXT("above_threshold");
}

bool UCaptureComponent::TryCaptureTarget()
{
	// 전수 표 실측(#0510): 링 시작·둘째 누름에 몸짓·소리가 없어 화면 링만 돌았다. 헛누름(대상 없음)은 조용히 둔다.
	if (CanCapture() || IsCaptureRingActive())
	{
		OnCapturePressed.Broadcast(IsCaptureRingActive() ? RingTarget.Get() : GetCaptureTarget());
	}

	if (!HasCaptureAuthority())
	{
		// 게스트는 요청만 보낸다. 포획됐는지는 동물 상태 복제(Captured)로 돌아온다.
		if (RingSeconds > 0.f)
		{
			TryCaptureRingAsGuest();
		}
		else
		{
			Server_TryCaptureTarget();
		}
		return false;
	}

	return TryCaptureTargetOnAuthority();
}

bool UCaptureComponent::ApplyCombatRow(const FPlayerCombatRow& Row)
{
	// 링 길이 0 = 되돌리기 스위치(DESIGN 3.1 「올가미 링 길이」). 창 칸이 미정인데 링만 켜는 반쪽 설정은 만들지 않는다.
	const bool bRingReady = Row.CaptureRingSeconds > 0.f && Row.CaptureWindowSeconds >= 0.f
		&& Row.CaptureWindowHardSeconds >= 0.f && Row.CaptureHardMinHP >= 0.f;
	if (!bRingReady)
	{
		if (Row.CaptureRingSeconds > 0.f)
		{
			UE_LOG(LogSnarehaven, Warning,
				TEXT("공격 표 행의 올가미 링 칸에 미정(-1)이 있어 링을 끈다 - 창 %.2f · 좁은 창 %.2f · 큰 종 HP %.0f"),
				Row.CaptureWindowSeconds, Row.CaptureWindowHardSeconds, Row.CaptureHardMinHP);
		}
		SetCaptureRingSettings(0.f, 0.f, 0.f, 0.f);
		SetCaptureEscapeSeconds(0.f);
		return false;
	}

	SetCaptureRingSettings(Row.CaptureRingSeconds, Row.CaptureWindowSeconds, Row.CaptureWindowHardSeconds, Row.CaptureHardMinHP);
	// 도주 칸만 미정(-1)이면 링은 켜고 도주 없이 둔다 — 실패해도 HP 는 그대로라 반쪽이어도 해가 없다.
	SetCaptureEscapeSeconds(Row.CaptureEscapeSeconds);
	// 허용 오차가 미정(-1)이면 게스트 시각을 안 믿고 호스트가 잰 간격으로 판정한다 (09a 길) — 링은 그대로 돈다.
	SetCaptureRingTolerance(Row.CaptureRingToleranceSeconds);
	UE_LOG(LogSnarehaven, Log, TEXT("올가미 링을 표에서 채웠다 - 링 %.2f초 · 창 %.2f초 · 좁은 창 %.2f초 (MaxHP ≥ %.0f·변종) · 실패 도주 %.2f초 · 게스트 허용 오차 %.2f초"),
		RingSeconds, WindowSeconds, WindowHardSeconds, HardMinHP, EscapeSeconds, ToleranceSeconds);
	return true;
}

void UCaptureComponent::SetCaptureRingSettings(float InRingSeconds, float InWindowSeconds, float InWindowHardSeconds, float InHardMinHP)
{
	ClearCaptureRing();
	RingSeconds = FMath::Max(InRingSeconds, 0.f);
	WindowSeconds = FMath::Max(InWindowSeconds, 0.f);
	WindowHardSeconds = FMath::Max(InWindowHardSeconds, 0.f);
	HardMinHP = InHardMinHP;
}

float UCaptureComponent::GetCaptureWindowFor(const AAnimalCharacter* Target) const
{
	const UAnimalStateComponent* State = IsValid(Target) ? Target->GetAnimalState() : nullptr;
	if (!IsValid(State))
	{
		return WindowSeconds;
	}

	// 희귀 = 변종, 큰 종 = 행 `MaxHP` 가 기준 이상 (DESIGN 3.1 「올가미 링 성공 창 (희귀·큰 종)」 — 새 표 칸 없이).
	const bool bHard = State->IsVariant() || (HardMinHP > 0.f && State->GetMaxHP() >= HardMinHP);
	return bHard ? WindowHardSeconds : WindowSeconds;
}

float UCaptureComponent::GetCaptureRingElapsed() const
{
	const UWorld* World = GetWorld();
	if (!IsCaptureRingActive() || World == nullptr)
	{
		return 0.f;
	}
	return static_cast<float>(World->GetTimeSeconds() - RingStartSeconds);
}

void UCaptureComponent::ReportCaptureFailureOnAuthority(const UAnimalStateComponent* State, const FString& Reason)
{
	LastFailureReason = Reason;

	// M8-03: 실패도 센다 — 임계값 위에서 누른 것·이미 끝난 동물·링 타이밍은 다른 이야기다.
	// 이 함수가 호스트 권위 자리라 게스트의 헛손질은 여기 한 번만 잡힌다 (DESIGN 4.1).
	if (USnareBackendSubsystem* const Backend = FindBackendForAnalytics())
	{
		TMap<FName, FString> Payload;
		Payload.Add(TEXT("pet_item_id"), IsValid(State) ? State->GetPetItemID().ToString() : FString());
		Payload.Add(TEXT("zone_id"), AZonePortalActor::FindCurrentZoneID(GetWorld()).ToString());
		Payload.Add(TEXT("reason"), Reason);
		Backend->WriteGameEvent(TEXT("animal_capture_failed"), Payload);
	}
}

void UCaptureComponent::BeginCaptureRing(AAnimalCharacter* Target, float TimeoutExtraSeconds)
{
	UWorld* const World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	RingTarget = Target;
	RingStartSeconds = World->GetTimeSeconds();
	World->GetTimerManager().SetTimer(
		RingTimeoutTimer, this, &UCaptureComponent::HandleCaptureRingTimeout, RingSeconds + TimeoutExtraSeconds, false);

	const float Window = GetCaptureWindowFor(Target);
	UE_LOG(LogSnarehaven, Log, TEXT("올가미 링 시작 - %s · 링 %.2f초 · 창 %.2f초"), *Target->GetName(), RingSeconds, Window);
	OnCaptureRingStarted.Broadcast(Target, RingSeconds, Window);
}

void UCaptureComponent::ClearCaptureRing()
{
	RingTarget.Reset();
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RingTimeoutTimer);
	}
}

void UCaptureComponent::HandleCaptureRingTimeout()
{
	if (!HasCaptureAuthority())
	{
		// 게스트 기기 링은 화면용이다 — 시간 초과 실패는 호스트 링이 정한다 (M21-09c).
		ClearCaptureRing();
		return;
	}

	// 타이머가 정확히 링 끝에 울려도 「끝까지 안 눌렀다」 이므로 링 길이를 조금 넘긴 시각으로 판정한다.
	ResolveCaptureRingAt(RingSeconds + KINDA_SMALL_NUMBER);
}

bool UCaptureComponent::ResolveCaptureRingAt(float ElapsedSeconds)
{
	AAnimalCharacter* const Target = RingTarget.Get();
	ClearCaptureRing();

	UAnimalStateComponent* const State = IsValid(Target) ? Target->GetAnimalState() : nullptr;
	if (!IsValid(State) || State->GetState() != EAnimalState::Capturable)
	{
		// 링 도중 죽었거나 남이 먼저 잡았다 — 실패로 세지 않고 조용히 사라진다 (명세 2 「실패·예외」).
		return false;
	}

	const float Window = GetCaptureWindowFor(Target);
	const SnareCaptureRing::EResult Result = SnareCaptureRing::Judge(ElapsedSeconds, RingSeconds, Window);
	if (Result == SnareCaptureRing::EResult::InWindow)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("올가미 링 성공 - %s · 경과 %.2f초 · 창 [%.2f, %.2f]"),
			*Target->GetName(), ElapsedSeconds, RingSeconds - Window, RingSeconds);
		return CaptureTargetNowOnAuthority(Target);
	}

	UE_LOG(LogSnarehaven, Log, TEXT("올가미 링 실패 (timing, %s) - %s · 경과 %.2f초 · 창 [%.2f, %.2f] · 도주 %.2f초"),
		Result == SnareCaptureRing::EResult::Early ? TEXT("이른 누름") : TEXT("시간 초과"),
		*Target->GetName(), ElapsedSeconds, RingSeconds - Window, RingSeconds, EscapeSeconds);
	FailCaptureRingOnAuthority(State);
	return false;
}

void UCaptureComponent::FailCaptureRingOnAuthority(UAnimalStateComponent* State)
{
	// 실패해도 HP 는 그대로다 — 동물은 잠깐 달아났다가 다시 대기한다 (DESIGN 3.1 「올가미 링 실패 도주」, M21-09b).
	ReportCaptureFailureOnAuthority(State, TEXT("timing"));
	State->BeginEscape(EscapeSeconds);
}

bool UCaptureComponent::ResolveGuestCaptureRingAt(float GuestElapsedSeconds, float HostElapsedSeconds)
{
	if (!IsCaptureRingActive())
	{
		return false;
	}

	// 허용 오차 미정이면 게스트 시각을 안 쓰고 호스트가 잰 간격으로 본다 (09a 와 같은 길).
	if (ToleranceSeconds < 0.f)
	{
		return ResolveCaptureRingAt(HostElapsedSeconds);
	}

	if (SnareCaptureRing::IsGuestElapsedTrusted(GuestElapsedSeconds, HostElapsedSeconds, ToleranceSeconds))
	{
		return ResolveCaptureRingAt(GuestElapsedSeconds);
	}

	// 차가 허용 오차 밖 — 시계를 믿을 수 없으니 성공으로 치지 않는다. 대상이 이미 끝났으면 조용히 지운다.
	AAnimalCharacter* const Target = RingTarget.Get();
	ClearCaptureRing();
	UAnimalStateComponent* const State = IsValid(Target) ? Target->GetAnimalState() : nullptr;
	if (!IsValid(State) || State->GetState() != EAnimalState::Capturable)
	{
		return false;
	}
	UE_LOG(LogSnarehaven, Log, TEXT("올가미 링 실패 (timing, 게스트 시각 불일치) - %s · 게스트 %.2f초 · 호스트 %.2f초 · 허용 오차 %.2f초 · 도주 %.2f초"),
		*Target->GetName(), GuestElapsedSeconds, HostElapsedSeconds, ToleranceSeconds, EscapeSeconds);
	FailCaptureRingOnAuthority(State);
	return false;
}

void UCaptureComponent::TryCaptureRingAsGuest()
{
	AAnimalCharacter* const Target = GetCaptureTarget();

	// 둘째 누름 — 기기 시계로 잰 경과를 실어 보낸다. 판정·지급은 호스트 몫이다 (DESIGN 4.1).
	if (IsCaptureRingActive())
	{
		if (RingTarget.Get() == Target)
		{
			const float Elapsed = GetCaptureRingElapsed();
			ClearCaptureRing();
			Server_ResolveCaptureRing(Elapsed);
			return;
		}
		ClearCaptureRing();
	}

	// 첫 누름 — 버튼이 켜진 대상이면 기기 링(화면용)을 세운다. 아니어도 시작 요청은 보내 호스트가 실패 사유를 남긴다.
	if (CanCapture())
	{
		BeginCaptureRing(Target);
	}
	Server_BeginCaptureRing();
}

bool UCaptureComponent::TryCaptureInWindowForTest()
{
	if (!HasCaptureAuthority() || RingSeconds <= 0.f)
	{
		return TryCaptureTarget();
	}

	TryCaptureTarget();
	if (!IsCaptureRingActive())
	{
		return false;
	}
	return ResolveCaptureRingAt(RingSeconds - GetCaptureWindowFor(RingTarget.Get()) * 0.5f);
}

bool UCaptureComponent::TryCaptureTargetOnAuthority()
{
	AAnimalCharacter* Target = GetCaptureTarget();

	// M21-09a: 링이 돌고 있으면 이 누름이 둘째 누름이다. 대상이 바뀌었으면 옛 링은 조용히 지우고 새 누름으로 본다.
	// 링이 켜진 게스트는 이 길 대신 `Server_BeginCaptureRing`·`Server_ResolveCaptureRing` 으로 온다 (M21-09c).
	if (IsCaptureRingActive())
	{
		if (RingTarget.Get() == Target)
		{
			return ResolveCaptureRingAt(GetCaptureRingElapsed());
		}
		ClearCaptureRing();
	}

	return BeginCaptureRingOnAuthority(0.f);
}

bool UCaptureComponent::BeginCaptureRingOnAuthority(float TimeoutExtraSeconds)
{
	// 시작 요청이 다시 오면 옛 링은 새 링으로 갈아 끼운다 (게스트가 대상을 바꿔 다시 누른 때).
	ClearCaptureRing();

	AAnimalCharacter* const Target = GetCaptureTarget();
	if (!IsValid(Target))
	{
		return false;
	}

	UAnimalStateComponent* State = Target->GetAnimalState();
	// 임계값 위에서 눌렀거나 이미 죽은 동물이면 아무 일도 없다 (DESIGN 3.1 — 조건은 상태뿐).
	if (!IsValid(State) || State->GetState() != EAnimalState::Capturable)
	{
		ReportCaptureFailureOnAuthority(State, CaptureFailureReason(State));
		return false;
	}

	if (RingSeconds > 0.f)
	{
		BeginCaptureRing(Target, TimeoutExtraSeconds);
		return false;
	}

	return CaptureTargetNowOnAuthority(Target);
}

bool UCaptureComponent::CaptureTargetNowOnAuthority(AAnimalCharacter* Target)
{
	UAnimalStateComponent* State = Target->GetAnimalState();
	if (!IsValid(State) || !State->TryCapture())
	{
		ReportCaptureFailureOnAuthority(State, CaptureFailureReason(State));
		return false;
	}

	const FName PetItemID = State->GetPetItemID();

	// 포획된 동물은 더 이상 대상이 아니다 — 하이라이트를 바로 다음 동물로 넘긴다
	// (`UMeleeAttackComponent::FindNearestAnimalInRange` 가 끝 상태를 거른다).
	if (UMeleeAttackComponent* Melee = FindMeleeAttack())
	{
		Melee->RefreshTarget();
	}

	// **펫을 인벤토리에 직접 넣지 않는다.** 등록은 백엔드가 한다 (DESIGN 4.1 · CLAUDE.md 8).
	OnAnimalCaptured.Broadcast(Target, PetItemID);

	// M8-03: 성립한 포획만 여기서 한 번 나간다. 펫 지급 수량·인벤토리는 싣지 않는다 — 그 원본은 백엔드다.
	if (USnareBackendSubsystem* const Backend = FindBackendForAnalytics())
	{
		TMap<FName, FString> Payload;
		Payload.Add(TEXT("pet_item_id"), PetItemID.ToString());
		Payload.Add(TEXT("zone_id"), AZonePortalActor::FindCurrentZoneID(GetWorld()).ToString());
		Backend->WriteGameEvent(TEXT("animal_capture_succeeded"), Payload);
	}

	return true;
}
