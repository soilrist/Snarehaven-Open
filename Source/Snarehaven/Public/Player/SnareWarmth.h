#pragma once

#include "CoreMinimal.h"
#include "World/SnareSound.h"

// 불 옆에서 체력이 차오를 때 소리 (ROADMAP M58-02 · DESIGN 2.2 「불 옆 온기 소리」).
// 규칙만 담은 순수 함수 — 재생 연결은 M58-03. 체력·회복 계산은 읽기만 하고 바꾸지 않는다.
namespace SnareWarmth
{
	/** 불 옆 회복 초당이 이 값 이하면 회복 중이 아닌 것으로 본다 (불 밖). */
	constexpr float NoHeal = 0.f;

	/** 불 옆 회복이 있고 체력이 가득 차지 않았고 지난 소리 뒤 `SecondsSinceLast` 가 간격(4.0초) 이상이면 `Warmth`, 모든 기기 월드 소리.
	 *  불 밖(회복 0)·체력 가득 참·간격 안이면 무음(빈 이름). 첫 틱은 부르는 쪽이 큰 값을 넘긴다. 맞음·쓰러짐과 같은 틱이면 부르는 쪽이 건너뛴다. */
	SNAREHAVEN_API SnareSound::FChangeCue CueFor(float HealPerSecond, bool bHealthFull, float SecondsSinceLast);
}
