#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CaptureComponent.generated.h"

class AAnimalCharacter;
class UAnimalStateComponent;
class UMeleeAttackComponent;
class USnareBackendSubsystem;
struct FPlayerCombatRow;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAnimalCapturedByOwnerSignature, AAnimalCharacter*, Target, FName, PetItemID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCaptureRingStartedSignature, AAnimalCharacter*, Target, float, RingSeconds, float, WindowSeconds);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCapturePressedSignature, AAnimalCharacter*, Target);

// ROADMAP M1-05: 포획 액션. DESIGN 3.1 의 "사냥 / 포획 분기" 에서 포획 쪽 입력을 받는다.
//
// 대상을 **새로 찾지 않는다** — 자동 상호작용이라 하이라이트된 동물 한 마리가 곧 대상이고,
// 그 한 마리를 고르는 일은 `UMeleeAttackComponent` 가 이미 한다 (DESIGN 3.3). 같은 탐색을
// 여기서 또 만들면 사거리 규칙이 두 군데로 갈린다.
//
// 성공률 롤이 없는 것도 의도다 — DESIGN 3.1 "포획 성공률" 줄.
// M21-09a 올가미 링: 링 길이가 0 보다 크면 첫 누름은 링 시작, **둘째 누름의 경과 시간**이 성공 창 안이어야 잡힌다
// (`SnareCaptureRing::Judge`). 창 밖·시간 초과는 `animal_capture_failed` `timing` 이고 HP 는 그대로, 동물은 표의 도주 시간만큼 `Escaping`(M21-09b).
// 링 길이 0(컴포넌트 기본값)이면 예전처럼 첫 누름에 즉시 포획한다 — 표 한 칸으로 되돌리는 스위치다.
//
// 권위: 포획 판정까지가 호스트 몫이다 (DESIGN 4.1 권위 표). 다만 **펫을 인벤토리에 넣지는 않는다** —
// 포획한 클라이언트가 백엔드에 "펫 등록" 을 요청하고 백엔드가 목장 슬롯에 기록한다 (DESIGN 4.1·4.5 · CLAUDE.md 8).
// 그래서 이 컴포넌트는 `OnAnimalCaptured` 로 알리기만 하고, 요청을 쌓는 건 그걸 듣는 쪽이다.
// 멀티(M3-02): **게스트는 포획을 확정하지 않는다.** `TryCaptureTarget` 을 게스트에서 부르면
// `Server_TryCaptureTarget` 요청만 나가고, 대상 선택과 상태 판정은 호스트가 다시 한다.
// M21-09c: 링이 켜져 있으면 게스트는 **자기 기기 시계로** 링을 돌린다 — 첫 누름 `Server_BeginCaptureRing`,
// 둘째 누름 `Server_ResolveCaptureRing(경과)`. 호스트는 자기가 잰 간격과의 차가 허용 오차 안일 때만 그 경과로 판정한다.
// `CanCapture`(버튼 켜기)는 게스트에서도 그대로 돈다 — 대상 상태가 복제로 오기 때문이다.
UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API UCaptureComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCaptureComponent();

	/**
	 * 지금 포획할 수 있는 동물. 근접 공격 컴포넌트가 하이라이트한 그 한 마리다.
	 * @return 하이라이트된 동물 (없으면 nullptr)
	 */
	UFUNCTION(BlueprintPure, Category = "Capture")
	AAnimalCharacter* GetCaptureTarget() const;

	/**
	 * 포획 버튼을 켜도 되는지. DESIGN 3.1 대로 대상이 `Capturable` 일 때만 참이다.
	 * 사냥 HUD(M1-08)의 포획 버튼 활성화가 이걸 본다 — 대상·상태가 바뀌는 때는
	 * `UMeleeAttackComponent::OnTargetChanged` 와 동물의 `OnStateChanged` 가 알려 준다.
	 */
	UFUNCTION(BlueprintPure, Category = "Capture")
	bool CanCapture() const;

	/**
	 * 포획 버튼을 누른 순간. 대상이 `Capturable` 이면 포획하고 행의 펫 아이템 ID 를 알린다.
	 * 2층 검사도 이 함수를 직접 부른다 (입력 없이 같은 길을 타려고).
	 * 링이 켜져 있으면 첫 누름은 링 시작(false), 둘째 누름이 판정이다 (M21-09a).
	 * @return 호스트에서 실제로 포획했으면 true. **게스트에서는 요청만 보내고 언제나 false** (M3-02)
	 */
	UFUNCTION(BlueprintCallable, Category = "Capture")
	bool TryCaptureTarget();

	/** 이 폰의 포획을 여기서 확정해도 되는지 (호스트인지). 액터에 안 붙었으면 권위로 본다. */
	UFUNCTION(BlueprintPure, Category = "Capture")
	bool HasCaptureAuthority() const;

	/** 게스트 → 호스트 포획 요청. 대상은 호스트가 자기 쪽 하이라이트로 다시 고른다 (인자 없음이 의도다). */
	UFUNCTION(Server, Reliable)
	void Server_TryCaptureTarget();

	/** 게스트 첫 누름 → 호스트에 링을 건다 (M21-09c). 대상은 호스트 쪽 하이라이트, 시작 시각은 호스트가 받은 순간. */
	UFUNCTION(Server, Reliable)
	void Server_BeginCaptureRing();

	/** 게스트 둘째 누름 → 기기가 잰 경과로 판정해 달라 (M21-09c). 호스트에 링이 없으면 무시한다. */
	UFUNCTION(Server, Reliable)
	void Server_ResolveCaptureRing(float GuestElapsedSeconds);

	/** 포획에 성공했을 때. 펫 등록 요청을 쌓는 건 이걸 듣는 쪽이다 (ASnarehavenCharacter). */
	UPROPERTY(BlueprintAssignable, Category = "Capture")
	FOnAnimalCapturedByOwnerSignature OnAnimalCaptured;

	/** 올가미 링이 시작됐을 때 (M21-09a). 링 화면(M21-09d)이 듣는다. 게스트는 자기 기기 링이 설 때 알린다 (M21-09c). */
	UPROPERTY(BlueprintAssignable, Category = "Capture")
	FOnCaptureRingStartedSignature OnCaptureRingStarted;

	/**
	 * 이 기기에서 포획을 눌렀고 그 누름이 무언가 할 때(포획 가능한 대상이 있거나 링이 도는 중) — 판정 전에 알린다 (M28-09c).
	 * 던지는 몸짓·소리는 이걸 듣는 쪽(ASnarehavenCharacter)이 낸다. 누르는 길(상황 버튼·IA_Capture·HUD 버튼·검사)이 모두 `TryCaptureTarget` 이라 한 번만 난다.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Capture")
	FOnCapturePressedSignature OnCapturePressed;

	/**
	 * 링 숫자 넷을 `DT_PlayerCombatTable` 행에서 채운다 (DESIGN 3.1 「올가미 링」).
	 * 링 길이가 0 이하이거나 창 칸이 미정(-1)이면 링을 끈다 — 반쪽 설정으로 돌지 않는다.
	 * @return 링을 켰으면 true
	 */
	bool ApplyCombatRow(const FPlayerCombatRow& Row);

	/** 링 숫자를 직접 넣는다. 표와 검사가 같은 길을 탄다. 링 길이 0 이하 = 링 없음(즉시 포획). */
	void SetCaptureRingSettings(float InRingSeconds, float InWindowSeconds, float InWindowHardSeconds, float InHardMinHP);

	/** 링 실패 도주 시간을 직접 넣는다 (M21-09b). 0 이하 = 도주 없이 그 자리 `Capturable`. */
	void SetCaptureEscapeSeconds(float InEscapeSeconds) { EscapeSeconds = FMath::Max(InEscapeSeconds, 0.f); }

	/** 링 실패 도주 시간(초). */
	float GetCaptureEscapeSeconds() const { return EscapeSeconds; }

	/** 게스트 판정 시각 허용 오차를 직접 넣는다 (M21-09c). 음수 = 게스트 시각을 안 쓰고 호스트가 잰 간격으로 판정. */
	void SetCaptureRingTolerance(float InToleranceSeconds) { ToleranceSeconds = InToleranceSeconds; }

	/** 게스트 판정 시각 허용 오차(초). 음수면 호스트 시계로만 판정한다. */
	float GetCaptureRingToleranceSeconds() const { return ToleranceSeconds; }

	/** 링 길이(초). 0 이하면 링이 꺼져 있다. */
	float GetCaptureRingSeconds() const { return RingSeconds; }

	/** 그 동물에게 쓰는 성공 창(초) — 큰 종(`MaxHP` ≥ 기준)·변종이면 좁은 창. */
	float GetCaptureWindowFor(const AAnimalCharacter* Target) const;

	/** 링이 돌고 있는지 (첫 누름 뒤 둘째 누름·시간 초과 전). */
	bool IsCaptureRingActive() const { return RingTarget.IsValid(); }

	/** 링을 건 동물 (없으면 nullptr). */
	AAnimalCharacter* GetCaptureRingTarget() const { return RingTarget.Get(); }

	/** 링 시작부터 지금까지(초). 링이 없으면 0. */
	float GetCaptureRingElapsed() const;

	/**
	 * 링을 이 경과 시간으로 판정한다 (호스트 전용). 둘째 누름·시간 초과 타이머가 여기로 모이고,
	 * 2층 검사는 시계를 돌리지 않고 이 함수로 경계를 본다. 대상이 더는 `Capturable` 이 아니면 조용히 링만 지운다.
	 * @return 잡았으면 true
	 */
	bool ResolveCaptureRingAt(float ElapsedSeconds);

	/**
	 * 게스트 둘째 누름 판정 (호스트 전용, M21-09c). `Server_ResolveCaptureRing` 이 호스트가 잰 경과를 붙여 부르고,
	 * 2층 검사는 시계 없이 두 경과를 직접 넣는다. 링이 없으면 무시(false, 실패로 안 셈) · 차가 허용 오차 밖이면 `timing` 실패 ·
	 * 안이면 게스트 경과로 `ResolveCaptureRingAt`. 허용 오차가 음수(미정)면 호스트 경과로 판정한다.
	 * @return 잡았으면 true
	 */
	bool ResolveGuestCaptureRingAt(float GuestElapsedSeconds, float HostElapsedSeconds);

	/** 마지막 포획 실패 사유 (`animal_capture_failed` 의 `reason` 과 같은 글). 2층 검사가 본다. */
	const FString& GetLastCaptureFailureReason() const { return LastFailureReason; }

	/**
	 * 검사용 「창 안에서 누르기」 — 링이 켜져 있으면 첫 누름 뒤 창 한가운데 시각으로 판정한다.
	 * 포획 자체를 보려는 기존 2층 검사가 링 시각을 몰라도 같은 포획 길을 타게 한다. 링이 꺼져 있으면 `TryCaptureTarget` 과 같다.
	 */
	bool TryCaptureInWindowForTest();

private:
	/** 호스트에서만 도는 실제 포획. `TryCaptureTarget` 과 `Server_TryCaptureTarget` 이 여기로 모인다. */
	bool TryCaptureTargetOnAuthority();

	/**
	 * 같은 액터에 붙은 근접 공격 컴포넌트 (대상 선택을 거기서 빌려 쓴다).
	 * 캐싱하지 않고 쓸 때마다 찾는 이유: 컴포넌트 등록 순서에 기대지 않고, 나중에 런타임에
	 * 떼었다 붙여도 조용히 낡은 포인터를 보지 않기 위해서다. 후보는 액터 자기 컴포넌트 몇 개뿐이다.
	 */
	UMeleeAttackComponent* FindMeleeAttack() const;

	/** 분석 이벤트를 내보낼 통로 (M8-03). 없으면 nullptr — 이벤트가 빠질 뿐 포획은 그대로 된다. */
	USnareBackendSubsystem* FindBackendForAnalytics() const;

	/** 포획이 왜 성립하지 않았는지 — `animal_capture_failed` 의 `reason` 칸. */
	static FString CaptureFailureReason(const UAnimalStateComponent* State);

	/** 실패를 기록하고 `animal_capture_failed` 를 낸다. */
	void ReportCaptureFailureOnAuthority(const UAnimalStateComponent* State, const FString& Reason);

	/** `Capturable` 인 대상을 지금 잡는다 — 즉시 포획과 링 성공이 같은 길을 탄다. */
	bool CaptureTargetNowOnAuthority(AAnimalCharacter* Target);

	/** 대상에 링을 건다 (시작 시각·시간 초과 타이머). 타이머 = 링 길이 + 여유(게스트 링은 허용 오차만큼 늦게 끝낸다). */
	void BeginCaptureRing(AAnimalCharacter* Target, float TimeoutExtraSeconds = 0.f);

	/** 호스트에서 링 시작 요청을 받는다 — 대상이 `Capturable` 이 아니면 실패 사유만 남긴다. 링 길이 0 이면 즉시 포획. */
	bool BeginCaptureRingOnAuthority(float TimeoutExtraSeconds);

	/** 게스트 기기의 포획 누름 (M21-09c) — 첫 누름은 기기 링 + 시작 요청, 둘째 누름은 경과를 실은 판정 요청. */
	void TryCaptureRingAsGuest();

	/** 링 실패 — `timing` 을 남기고 동물을 도주시킨다 (HP 그대로). */
	void FailCaptureRingOnAuthority(UAnimalStateComponent* State);

	/** 링을 지운다 (판정 없이). */
	void ClearCaptureRing();

	/** 링이 다 줄도록 둘째 누름이 없을 때. */
	void HandleCaptureRingTimeout();

	/** 링 길이(초). 0 이하 = 링 없음. 기본값 0 — 표를 안 받은 컴포넌트는 예전처럼 즉시 포획한다. */
	float RingSeconds = 0.f;

	/** 보통 성공 창(초). */
	float WindowSeconds = 0.f;

	/** 희귀·큰 종 성공 창(초). */
	float WindowHardSeconds = 0.f;

	/** 큰 종 기준 `MaxHP`. 0 이하면 큰 종 구분이 없다. */
	float HardMinHP = 0.f;

	/** 링 실패 도주 시간(초). 0 = 도주 없음 — 표를 안 받은 컴포넌트는 09a 처럼 그 자리에 둔다. */
	float EscapeSeconds = 0.f;

	/** 게스트 판정 시각 허용 오차(초). 음수 = 미정 — 호스트 시계로만 판정(M21-09a 길). */
	float ToleranceSeconds = -1.f;

	/** 링을 건 동물. 약한 포인터 — 링 도중 사라지면 링도 사라진 것으로 본다. */
	TWeakObjectPtr<AAnimalCharacter> RingTarget;

	/** 링 시작 시각(월드 시간, 초). */
	double RingStartSeconds = 0.0;

	/** 시간 초과 타이머. */
	FTimerHandle RingTimeoutTimer;

	/** 마지막 실패 사유. */
	FString LastFailureReason;
};
