#pragma once

#include "CoreMinimal.h"
#include "Misc/CoreDelegates.h"

// M17-04: 폰 성능 예산 자. 사냥터에서 1초마다 프레임 시간·동물 수·드로콜을 로그 한 줄로 찍는다(`-SnarePerfLog`,
// 출시 구성 제외). 목표는 30fps — 평균 프레임 시간이 34.0ms 안이면 예산 안이다(30fps 상한 흔들림 여유). 판정은 이 순수 함수가 하고,
// 게임 쪽 표본 수집(`StartLogging`)은 이 함수를 부른다.
namespace SnarePerfLog
{
	struct FSample
	{
		float AverageFrameMs = 0.f;
		float WorstFrameMs = 0.f;
		int32 Animals = 0;
		int32 DrawCalls = 0;
		FCoreDelegates::ETemperatureSeverity Thermal = FCoreDelegates::ETemperatureSeverity::Unknown;
	};

	/** 30fps 예산 (ms). 근거: 폰은 vsync 30fps 상한에 붙어 평균이 33.33 위아래로 ±0.1ms 흔들려(아이폰 15 Pro 실측 33.3~33.4),
	 *  1000/30 이면 33.4 를 초과로 잘못 찍는다. 34ms = 29.4fps 로 ROADMAP M17 합격선 "30fps 근처" 안. */
	constexpr float BudgetFrameMs = 34.0f;

	/** 프레임 개수와 걸린 시간(초)으로 평균 프레임 시간(ms). 프레임이 0 이면 0. */
	SNAREHAVEN_API float AverageFrameMs(int32 Frames, double ElapsedSeconds);

	/** 평균 프레임 시간이 30fps 예산 안인가 (0 이하는 표본 없음이라 false). */
	SNAREHAVEN_API bool IsWithinBudget(float AverageFrameMs);

	/** 드로콜 원천 고르기. RHI 카운터가 0 이면(맥·iOS Metal 은 UE 5.8 에서 안 채운다) 렌더러의 메시 드로 명령 수를 쓴다. */
	SNAREHAVEN_API int32 ChooseDrawCalls(int32 RhiDrawCalls, int32 MeshDrawCalls);

	/** 로그 한 줄. `[Perf] 평균 33.1ms (30.2fps) 최악 50.0ms 동물 12 드로콜 840 예산 안 발열 좋음` */
	SNAREHAVEN_API FString FormatLine(const FSample& Sample);

	/** M21-08b: 콜드 스타트 → 조작 가능 목표 (초). 근거: DESIGN 4.2 성능 예산 표 「콜드 스타트」(ROADMAP M21-08 원문 ≤15). */
	constexpr float ColdStartTargetSeconds = 15.0f;

	/** 프로세스 시작 시각(`GStartTime`)부터 지금까지 (초). 거꾸로 가면 0. */
	SNAREHAVEN_API float ColdStartSeconds(double ProcessStartSeconds, double NowSeconds);

	/** 콜드 스타트 줄. `[Perf] 콜드 스타트 → 조작 가능 12.3초 (목표 15초 안) 발열 좋음` */
	SNAREHAVEN_API FString FormatColdStartLine(float Seconds, FCoreDelegates::ETemperatureSeverity Thermal);

	/** iOS 발열 단계 이름. 맥·안드로이드처럼 OS 가 알려 주지 않으면 `모름`. */
	SNAREHAVEN_API const TCHAR* ThermalName(FCoreDelegates::ETemperatureSeverity Thermal);

	/** 애플 `NSProcessInfoThermalState` 값(0 보통 · 1 조금 · 2 심함 · 3 위험)을 엔진 단계로 — 엔진 iOS 대리자와 같은 짝. 범위 밖은 `Unknown`. */
	SNAREHAVEN_API FCoreDelegates::ETemperatureSeverity SeverityFromAppleThermalState(int64 State);

	/** 지금 발열 단계. OS 가 바뀜을 알려 줬으면(`OnTemperatureChange`) 그 값, 아직이면 애플 기기는 OS 에 직접 묻고, 그 밖은 `Unknown`.
	 *  엔진 iOS 대리자는 「바뀔 때」만 알려 줘 앱을 켠 직후엔 아무것도 안 온다. */
	SNAREHAVEN_API FCoreDelegates::ETemperatureSeverity GetLatestThermal();

#if !UE_BUILD_SHIPPING
	/** 1초 간격 반복 표본을 시작한다 (`-SnarePerfLog`). 주인 액터가 사라지면 멈춘다. */
	SNAREHAVEN_API void StartLogging(AActor* Owner);

	/** 내 몸에 입력이 붙은 순간(= 조작 가능) 프로세스당 한 번 콜드 스타트 줄을 찍는다. 두 번째부터는 아무것도 안 한다. */
	SNAREHAVEN_API void LogColdStartOnce();
#endif
}
