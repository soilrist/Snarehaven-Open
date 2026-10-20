#pragma once

#include "CoreMinimal.h"
#include "World/SnareSound.h"

// 설원 추위로 체력이 깎일 때 소리 (ROADMAP M57-02 · DESIGN 2.2 「추위 떨림 소리」).
// 규칙만 담은 순수 함수 — 재생 연결은 M57-03. 체력·추위 계산은 읽기만 하고 바꾸지 않는다.
namespace SnareShiver
{
	/** 칸 피해 초당 × (1 − 추위 막이) 가 이 값 이하면 체력이 안 깎이는 것으로 본다. */
	constexpr float NoDamage = 0.f;

	/** 실제 피해가 있고 지난 소리 뒤 `SecondsSinceLast` 가 간격(3.0초) 이상이면 `Shiver`, 모든 기기 월드 소리.
	 *  칸 밖(피해 0)·추위 막이로 실제 피해 0·간격 안이면 무음(빈 이름). 쓰러지는 틱(체력 0)은 부르는 쪽이 건너뛴다. */
	SNAREHAVEN_API SnareSound::FChangeCue CueFor(float ZoneDamagePerSecond, float ColdResist, float SecondsSinceLast);
}
