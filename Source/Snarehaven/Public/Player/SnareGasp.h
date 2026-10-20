#pragma once

#include "CoreMinimal.h"
#include "World/SnareSound.h"

// 물에서 숨이 차 체력이 깎일 때 소리 (ROADMAP M56-02 · DESIGN 2.2 「숨참 소리」).
// 규칙만 담은 순수 함수 — 재생 연결은 M56-03. 체력·스태미나 값은 읽기만 하고 바꾸지 않는다.
namespace SnareGasp
{
	/** 스태미나가 이 값 이하면 바닥난 것으로 본다(체력이 깎이는 때). */
	constexpr float EmptyStamina = 0.f;

	/** 물속이고 스태미나가 바닥났고 지난 소리 뒤 `SecondsSinceLast` 가 간격(2.0초) 이상이면 `Gasp`, 모든 기기 월드 소리.
	 *  물 밖·스태미나 남음·간격 안이면 무음(빈 이름). 쓰러지는 틱(체력 0)은 부르는 쪽이 건너뛴다. */
	SNAREHAVEN_API SnareSound::FChangeCue CueFor(bool bInWater, float Stamina, float SecondsSinceLast);
}
