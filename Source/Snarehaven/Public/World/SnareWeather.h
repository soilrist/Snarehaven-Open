#pragma once

#include "CoreMinimal.h"

// 날씨 (DESIGN 3.3 「날씨」 표). 낮밤처럼 서버 시계(`GetServerWorldTimeSeconds`)에서만 계산한다 —
// 슬롯 번호가 곧 난수 씨앗이라 복제할 값이 없고 모든 기기가 같은 날씨를 본다.
UENUM()
enum class ESnareWeather : uint8
{
	Clear,
	Rain,
	Snow,
};

namespace SnareWeather
{
	constexpr double SlotSeconds = 600.0;   // 한 칸 10분
	constexpr double BlendSeconds = 30.0;   // 바뀔 때 섞기
	constexpr int32 ClearPercent = 60;      // 맑음 60 · 비 25 · 눈 15
	constexpr int32 RainPercent = 25;
	constexpr float SnowColdPerSecond = 0.5f; // 눈 오는 동안 설원 밖 추위 가산 (DESIGN 3.3 「추위 가산」)

	/** 슬롯 번호 → 날씨. 같은 번호는 언제 어디서나 같은 값. */
	SNAREHAVEN_API ESnareWeather ForSlot(int64 Slot);

	/** 서버 시각(초) → 날씨. */
	SNAREHAVEN_API ESnareWeather At(double Seconds);

	/**
	 * 서버 시각(초)의 비·눈 정도 0~1. 칸이 바뀐 뒤 BlendSeconds 동안 앞 칸 날씨는 줄고 새 날씨는 늘어난다
	 * (맑음이 끼면 둘 다 줄어든다). 입자 수·화면 색조가 이 값으로 움직인다.
	 */
	SNAREHAVEN_API void Amounts(double Seconds, float& OutRain, float& OutSnow);

	/**
	 * 눈 정도(0~1)·지붕 아래 여부·설원 칸 피해(초당) → 이번에 체력에 줄 존 피해(초당).
	 * 설원 안(칸 피해 > 0)은 그대로(+0), 지붕 아래는 가산 0, 설원 밖에서 눈이 오면 SnowColdPerSecond × 눈 정도.
	 */
	SNAREHAVEN_API float ColdDrain(float SnowAmount, bool bUnderRoof, float ZoneDrainPerSecond);
}
