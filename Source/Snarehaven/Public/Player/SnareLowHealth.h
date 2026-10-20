#pragma once

#include "CoreMinimal.h"

// 체력이 위험할 때 붉은 테두리·심장 소리 규칙 (ROADMAP M60-02 · DESIGN 2.2 「위험 체력 알림」).
// 순수 함수 — 테두리(M60-03)와 심장 소리(M60-04)가 같은 세기를 읽는다. 체력 값은 읽기만 하고 바꾸지 않는다.
namespace SnareLowHealth
{
	/** 체력 ÷ 최대 체력 이 이 값 이하일 때부터 켠다 (표 「문턱」). */
	constexpr float Threshold = 0.30f;

	/** 위험 세기 0~1. 문턱 위·체력 0·쓰러짐 중·최대 체력 ≤ 0 = 0, 문턱에서 0 으로 시작해 바닥에서 1. */
	SNAREHAVEN_API float StateFor(float Health, float MaxHealth, bool bDowned);

	/** 표 「붉은 테두리」 불투명도 범위 — 세기 0+ 에서 하한, 세기 1 에서 상한. */
	constexpr float EdgeOpacityMin = 0.35f;
	constexpr float EdgeOpacityMax = 0.75f;

	/** 표 「테두리 두께」 — 겹마다 고정 px 두께 × 겹 수(안쪽으로 갈수록 EdgeLayerScale 배 불투명도), 펄스 깊이. */
	constexpr float EdgeBandWidth = 12.f;
	constexpr int32 EdgeBandLayers = 6;
	constexpr float EdgePulseDepth = 0.4f;

	/** 겹 번호(0 = 가장 바깥) 의 불투명도 배율 — 1.0 에서 0.05 로 안쪽으로 사라진다(각진 안쪽 선 없음). 범위 밖 번호는 0. */
	SNAREHAVEN_API float EdgeLayerScale(int32 Layer);

	/** 표 「간격」 — 세기 0+ 에서 느린 쪽, 세기 1 에서 빠른 쪽(초). 테두리 펄스 주기와 심장 소리 간격이 같다. */
	constexpr float BeatSlowSeconds = 1.2f;
	constexpr float BeatFastSeconds = 0.6f;

	/** 표 「크기」 — 심장 소리 음량. */
	constexpr float HeartbeatVolume = 0.5f;

	/** 테두리 불투명도. 세기 0 = 0(안 보임), 0 초과 = 하한~상한 선형. */
	SNAREHAVEN_API float EdgeOpacity(float Strength);

	/** 펄스 중 불투명도. Wave 1(밝음)→0(어두움). 가장 어두워도 하한(EdgeOpacityMin) 아래로 내려가지 않는다. 세기 0 = 0. */
	SNAREHAVEN_API float PulsedEdgeOpacity(float Strength, float Wave);

	/** 심장 한 번 뛰는 간격(초). 세기 0 초과에서만 뜻이 있다. */
	SNAREHAVEN_API float BeatInterval(float Strength);
}
