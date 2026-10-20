#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PlayerCombatRow.generated.h"

class UAnimSequence;

// ROADMAP M11-05: 플레이어 근접 공격 숫자 한 벌 (`DT_PlayerCombatTable` 의 행 구조체).
// **이 표의 숫자는 DESIGN 3.1 "확정 수치" 표의 네 줄(데미지·사거리·간격·타격 지연)의 사본이다** —
// `UMeleeAttackComponent` 는 숫자를 하나도 들고 있지 않고 이 행을 받아 채운다 (DataDriven.md 3).
// 감독 지시(2026-09-23 17:25): 타격 지연 0.2 가 코드 기본값이라 값을 넣는 곳이 검사뿐이었다 —
// 네 칸이 **같은 설정 경로**(`UMeleeAttackComponent::ApplyCombatRow`)로 들어오게 옮겼다.
//
// 행이 하나뿐인 표다(`Player`). 무기별 차등은 여기가 아니라 `FItemRow` 의 무기 칸이다
// (DESIGN 3.1 "무기 없음·종별 차등 없음" · 8.6-b "0 이면 컴포넌트 기본값") — 이 표는 **맨손 기준**이다.
//
// 음수는 **미정**이다 (Verification.md 7) — 컴포넌트는 미정 행으로 초기화되지 않고 경고만 남긴다.
USTRUCT(BlueprintType)
struct FPlayerCombatRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 1타 데미지. DESIGN 3.1 = 20 (동물 HP 100 기준 4타째 Capturable · 5타째 Dead). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float AttackDamage = -1.0f;

	/** 맨손 사거리 = 자동 상호작용 하이라이트 거리(cm). DESIGN 3.1 = 150 (채집·목장·존이 같이 쓴다). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float AttackRangeCm = -1.0f;

	/** 공격 간격(초) — 한 번 누른 뒤 다음 누름을 받기까지(누르고 있어도 반복 없음). DESIGN 3.1 = 0.6. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float AttackIntervalSeconds = -1.0f;

	/** 휘두르기 시작 → 데미지 판정까지 지연(초). DESIGN 3.1 = 0.2 (`club_swing2` 타격 프레임 f6 ÷ 30fps). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float AttackHitDelaySeconds = -1.0f;

	/** 상인·의뢰인 대화 거리(cm). DESIGN 3.1 = 250 — 노점 충돌로 손님 자리가 상인에서 184cm 라 맨손 사거리(150)로는 못 닿는다 (M22). 음수면 상인이 안 열린다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float NpcTalkRangeCm = -1.0f;

	// ── 때리는 손맛 (M21-02) — DESIGN 3.1 표의 같은 이름 줄의 사본. 음수(미정)면 손맛만 꺼지고 타격은 그대로다. ──

	/** 히트스톱 시간(초). DESIGN 3.1 = 0.06 — 때린 사람 기기에서만, 때린 몸·맞은 동물의 `CustomTimeDilation` 으로. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitFeel")
	float HitStopSeconds = -1.0f;

	/** 히트스톱 동안의 시간 배율. DESIGN 3.1 = 0.05 (0 은 클립이 튀어 피한다). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitFeel")
	float HitStopTimeDilation = -1.0f;

	/** 넉백 거리(cm). DESIGN 3.1 = 120 — 호스트가 Fight 인 동물만 민다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitFeel")
	float KnockbackCm = -1.0f;

	/** 넉백에 걸리는 시간(초). DESIGN 3.1 = 0.15. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitFeel")
	float KnockbackSeconds = -1.0f;

	/** 카메라 흔들림 진폭(cm). DESIGN 3.1 = 3 — 때린 사람 기기에서만. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitFeel")
	float CameraShakeAmplitudeCm = -1.0f;

	/** 카메라 흔들림 시간(초). DESIGN 3.1 = 0.12. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitFeel")
	float CameraShakeSeconds = -1.0f;

	/** 맞은 동물 흰 깜빡임 시간(초). DESIGN 3.1 = 0.08 — 모든 기기. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitFeel")
	float HitFlashSeconds = -1.0f;

	/** 피격 파편 최소 개수. DESIGN 3.1 = 6 (개수라 소수점은 버린다 — 대조 시험이 float 칸만 읽어 float 로 둔다). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitFeel")
	float HitDebrisMin = -1.0f;

	/** 피격 파편 최대 개수. DESIGN 3.1 = 10. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitFeel")
	float HitDebrisMax = -1.0f;

	/** 같이 잡기 드롭 창(초). DESIGN 3.1 = 10 — 막타 전 이 시간 안에 명중한 사람도 같은 드롭을 각자 받는다 (M21-06). 음수면 막타만 받는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float ShareDropWindowSeconds = -1.0f;

	// ── 올가미 조이기 링 (M21-09) — DESIGN 3.1 「올가미 링」 줄들의 사본. 링 길이가 0 이하(미정 포함)면 링 없이 첫 누름에 즉시 포획한다. ──

	/** 링 길이(초). DESIGN 3.1 = 1.5 — 첫 누름부터 올가미 원이 목표 원에 닿기까지. 0 이면 링을 끈다(되돌리기 스위치). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capture")
	float CaptureRingSeconds = -1.0f;

	/** 성공 창(초). DESIGN 3.1 = 0.6 — 링 끝 직전 이 시간 안의 둘째 누름이 잡는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capture")
	float CaptureWindowSeconds = -1.0f;

	/** 희귀·큰 종 성공 창(초). DESIGN 3.1 = 0.4. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capture")
	float CaptureWindowHardSeconds = -1.0f;

	/** 큰 종 기준 — 동물 `MaxHP` 가 이 이상이면 좁은 창. DESIGN 3.1 = 140. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capture")
	float CaptureHardMinHP = -1.0f;

	/** 실패 뒤 도주 시간(초). DESIGN 3.1 = 1.5 (M21-09b 가 읽는다). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capture")
	float CaptureEscapeSeconds = -1.0f;

	/** 게스트 판정 시각 허용 오차(초). DESIGN 3.1 = 0.25 (M21-09c 가 읽는다). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capture")
	float CaptureRingToleranceSeconds = -1.0f;

	/** 맨손일 때 되풀이하는 주먹 자세 (M18-11). 무기·도구 행의 `HoldPoseAnim` 과 같은 자리. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimSequence> HoldPoseAnim;

	/** 맨손 휘두르기 홀수 번째 동작 (M18-11 잽). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimSequence> UseAnim;

	/** 맨손 휘두르기 짝수 번째 동작 (M18-11 크로스) — `UseAnim` 과 번갈아 튼다. 비면 `UseAnim` 만. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimSequence> UseAnimAlt;
};
