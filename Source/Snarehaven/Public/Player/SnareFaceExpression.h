#pragma once

#include "CoreMinimal.h"

// 얼굴 표정 규칙 (ROADMAP M85-02 · DESIGN 2.2 「표정 3종」).
// 순수 함수 — 상태를 받아 표정 이름만 돌려준다. 체력 값은 읽기만 하고 바꾸지 않는다.
// 우선순위: 쓰러짐 Down > 아픔 Hurt > 웃음 Happy > 기본 Neutral.
enum class ESnareFaceExpression : uint8
{
	Neutral,
	Hurt,
	Down,
	Happy,
};

namespace SnareFaceExpression
{
	/** 독 먹은 뒤 아픈 얼굴을 유지하는 시간(초). 표 「Hurt」. */
	constexpr float PoisonHurtSeconds = 1.5f;

	/** 먹기 성공 뒤 웃는 얼굴을 유지하는 시간(초). 표 「Happy」. */
	constexpr float HappySeconds = 1.5f;

	/** 지난 시간이 음수면 "그런 일 없었음" 이다. 최대 체력 ≤ 0 이면 체력 비율은 아픔 판정에서 뺀다. */
	SNAREHAVEN_API ESnareFaceExpression For(float Health, float MaxHealth, bool bDowned, float SecondsSincePoison,
		float SecondsSinceAte);

	/** 검사 스위치 `-SnareEatPlayer=<초>` 값을 예약해도 되는지(M86-02). 0 이하·NaN·무한대는 거절한다. */
	SNAREHAVEN_API bool IsValidCheckDelay(float Seconds);

	/** 표정 이름 문자열(로그·시험용). */
	SNAREHAVEN_API const TCHAR* NameOf(ESnareFaceExpression Expression);
}
