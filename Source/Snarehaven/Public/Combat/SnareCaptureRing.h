#pragma once

#include "CoreMinimal.h"

// 올가미 조이기 링 판정 (ROADMAP M21-09a · DESIGN 3.1 「올가미 링」 줄들 · 명세 `docs/specs/snare-ring.md`).
// 포획 버튼 첫 누름이 링을 시작하고 둘째 누름의 경과 시간 하나로 결과가 정해진다 — 확률 굴림은 없다(3.1 「포획 성공률」).
// 순수 함수만 둔다 — 2층 검사가 월드 없이 경계를 본다. 판정을 부르는 쪽은 호스트의 `UCaptureComponent` 다(4.1 권위 표).
namespace SnareCaptureRing
{
	/** 둘째 누름의 결과. */
	enum class EResult : uint8
	{
		/** 링 끝 직전 성공 창 `[링 − 창, 링]` 안 — 잡힌다. */
		InWindow,
		/** 창보다 일찍 눌렀다 — 실패(`timing`). */
		Early,
		/** 링이 다 줄도록 안 눌렀다 — 실패(`timing`). */
		Timeout
	};

	/**
	 * 경과 시간으로 결과를 정한다. 창 양끝은 성공이다(손가락이 정확히 닿은 순간을 놓친 것으로 치지 않는다).
	 * @param ElapsedSeconds 첫 누름부터 둘째 누름(또는 시간 초과)까지
	 * @param RingSeconds 링 길이 (DESIGN 3.1 = 1.5)
	 * @param WindowSeconds 성공 창 (보통 0.6 · 큰 종·변종 0.4). 링보다 길면 링 길이로 자른다
	 */
	inline EResult Judge(float ElapsedSeconds, float RingSeconds, float WindowSeconds)
	{
		if (ElapsedSeconds > RingSeconds)
		{
			return EResult::Timeout;
		}
		const float WindowStart = RingSeconds - FMath::Clamp(WindowSeconds, 0.f, RingSeconds);
		return ElapsedSeconds >= WindowStart ? EResult::InWindow : EResult::Early;
	}

	/**
	 * 게스트가 보낸 경과 시간을 믿어도 되는지 (M21-09c · DESIGN 3.1 「올가미 링 게스트 허용 오차」).
	 * 첫 누름·둘째 누름 RPC 가 같은 길로 가 왕복 지연은 상쇄되고 흔들림만 남는다 — 그 차가 허용 오차 안이면 게스트 기기 시각으로 판정한다.
	 * 음수·숫자 아님은 믿지 않는다(부풀리거나 깨진 값).
	 * @param GuestElapsedSeconds 게스트 기기가 잰 첫 누름 → 둘째 누름
	 * @param HostElapsedSeconds 호스트가 잰 시작 요청 → 판정 요청
	 * @param ToleranceSeconds 허용 오차 (DESIGN 3.1 = 0.25)
	 */
	inline bool IsGuestElapsedTrusted(float GuestElapsedSeconds, float HostElapsedSeconds, float ToleranceSeconds)
	{
		if (!FMath::IsFinite(GuestElapsedSeconds) || GuestElapsedSeconds < 0.f)
		{
			return false;
		}
		return FMath::Abs(GuestElapsedSeconds - HostElapsedSeconds) <= ToleranceSeconds;
	}
}
