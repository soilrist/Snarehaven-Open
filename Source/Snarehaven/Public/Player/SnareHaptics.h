#pragma once

#include "CoreMinimal.h"

class APlayerController;

// 폰 진동 (ROADMAP M21-03 · DESIGN 3.1 「포획 순간」·2.2 「설정」 진동 줄).
// 엔진 동적 힘 되먹임을 쓴다 — iOS 는 그 값을 `UIImpactFeedbackGenerator` 세기(0.3 넘으면 가벼움, 0.5 넘으면 보통)로 바꿔 튼다
// (`FIOSInputInterface::SetForceFeedbackChannelValue`). 설정에서 끄면 아무것도 안 튼다. 켬/끔은 기기에 남는다(`GameUserSettings.ini`).
namespace SnareHaptics
{
	/** 가벼운 진동 세기 — iOS 가벼움(0.3 초과)·보통(0.5 초과) 문턱 사이. */
	constexpr float LightIntensity = 0.4f;
	/** 가벼운 진동 길이(초). */
	constexpr float LightSeconds = 0.1f;

	/** 진동을 켜 뒀는지. 처음 부를 때 ini 에서 읽는다(없으면 켬). */
	SNAREHAVEN_API bool IsEnabled();

	/** 켬/끔을 저장한다. */
	SNAREHAVEN_API void SetEnabled(bool bEnabled);

	/**
	 * 그 플레이어 기기에서 세기·길이를 정해 진동 한 번 (M61-01). 설정이 꺼졌거나 조종기가 없거나 세기·길이가 0 이하면 안 튼다.
	 * @return 실제로 틀었으면 true
	 */
	SNAREHAVEN_API bool Play(APlayerController* Controller, float Intensity, float Seconds);

	/**
	 * 그 플레이어 기기에서 가벼운 진동 한 번 (`Play` 에 `LightIntensity`·`LightSeconds`). 설정이 꺼졌거나 조종기가 없으면 안 튼다.
	 * @return 실제로 틀었으면 true
	 */
	SNAREHAVEN_API bool PlayLight(APlayerController* Controller);

	/** 이 실행에서 실제로 튼 진동 수 — 2층 검사가 "진동이 났나" 를 본다. */
	SNAREHAVEN_API int32 GetPlayCount();
}
