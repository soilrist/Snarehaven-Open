#pragma once

#include "CoreMinimal.h"

// 맞음·쓰러짐·위험 체력 심장 진동 규칙 (ROADMAP M61-02 · DESIGN 2.2 「몸 진동」 표).
// 순수 함수 — 연결(M61-03·04)이 같은 세기·길이를 읽는다. 체력·쓰러짐 판정은 읽기만 하고 바꾸지 않는다.
namespace SnareBodyHaptic
{
	enum class EEvent : uint8
	{
		None,
		Hurt,
		Down,
		Heartbeat,
	};

	/** 표 「맞음」「쓰러짐」「위험 체력 심장」 — 세기·길이(초). */
	constexpr float HurtIntensity = 0.4f;
	constexpr float HurtSeconds = 0.1f;
	constexpr float DownIntensity = 0.7f;
	constexpr float DownSeconds = 0.4f;
	constexpr float HeartbeatIntensity = 0.3f;
	constexpr float HeartbeatSeconds = 0.05f;

	/** 표 「최소 간격」 — 같은 종류 진동 사이 이 초 안이면 무음. */
	constexpr float MinGapSeconds = 0.3f;

	/** 쏘기 진동 — 표 「총 반동 진동」(ROADMAP M62-01). 총 종류는 쏘기 쪽이 정한다. */
	enum class EGun : uint8
	{
		None,
		Pistol,
		Rifle,
	};

	constexpr float PistolShotIntensity = 0.5f;
	constexpr float PistolShotSeconds = 0.08f;
	constexpr float RifleShotIntensity = 0.6f;
	constexpr float RifleShotSeconds = 0.15f;

	/** 표 「최소 간격」(쏘기) — 쏘기 진동 사이 이 초 안이면 무음. */
	constexpr float ShotMinGapSeconds = 0.2f;

	/** 타격 진동 — 표 「타격 진동」(ROADMAP M63-01). 맨손 줍기는 타격이 아니라 None 으로 둔다. */
	enum class EStrike : uint8
	{
		None,
		Melee,
		Gather,
	};

	constexpr float MeleeStrikeIntensity = 0.4f;
	constexpr float MeleeStrikeSeconds = 0.06f;
	constexpr float GatherStrikeIntensity = 0.3f;
	constexpr float GatherStrikeSeconds = 0.05f;

	/** 표 「최소 간격」(타격) — 타격 진동 사이 이 초 안이면 무음. */
	constexpr float StrikeMinGapSeconds = 0.25f;

	/** 먹기 진동 — 표 「먹기 진동」(ROADMAP M64-01). 독 = 회복 < 0 인 먹기. */
	enum class EEat : uint8
	{
		None,
		Food,
		Poison,
	};

	constexpr float EatIntensity = 0.3f;
	constexpr float EatSeconds = 0.05f;
	constexpr float PoisonIntensity = 0.4f;
	constexpr float PoisonSeconds = 0.1f;

	/** 표 「최소 간격」(먹기) — 먹기 진동 사이 이 초 안이면 무음. */
	constexpr float EatMinGapSeconds = 0.3f;

	struct FCue
	{
		float Intensity = 0.f;
		float Seconds = 0.f;

		/** 틀 게 없으면 true (세기·길이 0). */
		bool IsNone() const { return Intensity <= 0.f || Seconds <= 0.f; }
	};

	/** 그 일의 진동. 모르는 일(None)·같은 종류 마지막 진동 뒤 MinGapSeconds 안이면 무음(0·0). */
	SNAREHAVEN_API FCue CueFor(EEvent Event, float SecondsSinceLast);

	/** 쏜 총의 반동 진동. 모르는 총(None)·마지막 쏘기 진동 뒤 ShotMinGapSeconds 안이면 무음(0·0). */
	SNAREHAVEN_API FCue CueFor(EGun Gun, float SecondsSinceLastShot);

	/** 때리거나 벤 타격 진동. 모르는 종류(None)·마지막 타격 진동 뒤 StrikeMinGapSeconds 안이면 무음(0·0). */
	SNAREHAVEN_API FCue CueFor(EStrike Strike, float SecondsSinceLastStrike);

	/** 먹은 진동. 모르는 종류(None)·마지막 먹기 진동 뒤 EatMinGapSeconds 안이면 무음(0·0). */
	SNAREHAVEN_API FCue CueFor(EEat Eat, float SecondsSinceLastEat);
}
