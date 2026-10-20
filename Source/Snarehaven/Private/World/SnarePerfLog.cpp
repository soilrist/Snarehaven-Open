#include "World/SnarePerfLog.h"
#include "Snarehaven.h"
#include "Animals/AnimalCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "CoreGlobals.h"
#include "Misc/App.h"
#include "RHIStats.h"
#include "Engine/Engine.h"
#include "Stats/StatsData.h"
#include "TimerManager.h"

// 발열 단계를 OS 에 직접 묻는다(`NSProcessInfo`). 엔진 시스템 헤더가 Carbon 의 `FVector` 와 엔진 `FVector` 충돌을 막는다.
#if PLATFORM_MAC
#include "Mac/MacSystemIncludes.h"
#elif PLATFORM_IOS
#include "IOS/IOSSystemIncludes.h"
#endif
#if PLATFORM_APPLE
#import <Foundation/Foundation.h>
#endif

namespace SnarePerfLog
{
	float AverageFrameMs(int32 Frames, double ElapsedSeconds)
	{
		if (Frames <= 0 || ElapsedSeconds <= 0.0)
		{
			return 0.f;
		}
		return static_cast<float>(ElapsedSeconds * 1000.0 / Frames);
	}

	bool IsWithinBudget(float AverageFrameMs)
	{
		return AverageFrameMs > 0.f && AverageFrameMs <= BudgetFrameMs;
	}

	int32 ChooseDrawCalls(int32 RhiDrawCalls, int32 MeshDrawCalls)
	{
		return RhiDrawCalls > 0 ? RhiDrawCalls : FMath::Max(MeshDrawCalls, 0);
	}

	FString FormatLine(const FSample& Sample)
	{
		const float Fps = Sample.AverageFrameMs > 0.f ? 1000.f / Sample.AverageFrameMs : 0.f;
		return FString::Printf(TEXT("[Perf] 평균 %.1fms (%.1ffps) 최악 %.1fms 동물 %d 드로콜 %d %s 발열 %s"),
			Sample.AverageFrameMs, Fps, Sample.WorstFrameMs, Sample.Animals, Sample.DrawCalls,
			IsWithinBudget(Sample.AverageFrameMs) ? TEXT("예산 안") : TEXT("예산 초과"), ThermalName(Sample.Thermal));
	}

	float ColdStartSeconds(double ProcessStartSeconds, double NowSeconds)
	{
		return static_cast<float>(FMath::Max(NowSeconds - ProcessStartSeconds, 0.0));
	}

	FString FormatColdStartLine(float Seconds, FCoreDelegates::ETemperatureSeverity Thermal)
	{
		return FString::Printf(TEXT("[Perf] 콜드 스타트 → 조작 가능 %.1f초 (목표 %.0f초 %s) 발열 %s"),
			Seconds, ColdStartTargetSeconds, Seconds <= ColdStartTargetSeconds ? TEXT("안") : TEXT("초과"),
			ThermalName(Thermal));
	}

	const TCHAR* ThermalName(FCoreDelegates::ETemperatureSeverity Thermal)
	{
		switch (Thermal)
		{
		case FCoreDelegates::ETemperatureSeverity::Good: return TEXT("좋음");
		case FCoreDelegates::ETemperatureSeverity::Bad: return TEXT("따뜻함");
		case FCoreDelegates::ETemperatureSeverity::Serious: return TEXT("뜨거움");
		case FCoreDelegates::ETemperatureSeverity::Critical: return TEXT("위험");
		default: return TEXT("모름");
		}
	}

	FCoreDelegates::ETemperatureSeverity SeverityFromAppleThermalState(int64 State)
	{
		switch (State)
		{
		case 0: return FCoreDelegates::ETemperatureSeverity::Good;
		case 1: return FCoreDelegates::ETemperatureSeverity::Bad;
		case 2: return FCoreDelegates::ETemperatureSeverity::Serious;
		case 3: return FCoreDelegates::ETemperatureSeverity::Critical;
		default: return FCoreDelegates::ETemperatureSeverity::Unknown;
		}
	}

	namespace
	{
		/** OS 가 마지막으로 알려 준 발열 단계. 델리게이트는 게임 스레드에서 울린다(IOSAppDelegate 가 게임 스레드로 넘김). */
		FCoreDelegates::ETemperatureSeverity GLatestThermal = FCoreDelegates::ETemperatureSeverity::Unknown;
	}

	FCoreDelegates::ETemperatureSeverity GetLatestThermal()
	{
		if (GLatestThermal != FCoreDelegates::ETemperatureSeverity::Unknown)
		{
			return GLatestThermal;
		}
#if PLATFORM_APPLE
		return SeverityFromAppleThermalState(static_cast<int64>([[NSProcessInfo processInfo] thermalState]));
#else
		return FCoreDelegates::ETemperatureSeverity::Unknown;
#endif
	}

#if !UE_BUILD_SHIPPING
	namespace
	{
		void ListenThermalOnce()
		{
			static bool bListening = false;
			if (!bListening)
			{
				bListening = true;
				FCoreDelegates::OnTemperatureChange.AddLambda([](FCoreDelegates::ETemperatureSeverity Severity)
				{
					GLatestThermal = Severity;
					UE_LOG(LogSnarehaven, Log, TEXT("[Perf] 발열 바뀜 → %s"), ThermalName(Severity));
				});
			}
		}

		struct FPerfState
		{
			uint64 LastFrame = 0;
			double LastTime = 0.0;
		};

		/** 렌더러의 `Mesh draw calls` 카운터(STAT_MeshDrawCalls, SceneRendering 묶음). 통계가 안 켜졌으면 0. */
		int32 ReadMeshDrawCalls()
		{
#if STATS
			static const FName StatName(TEXT("STAT_MeshDrawCalls"));
			if (const FGameThreadStatsData* Data = FLatestGameThreadStatsData::Get().Latest)
			{
				for (const FActiveStatGroupInfo& Group : Data->ActiveStatGroups)
				{
					for (const FComplexStatMessage& Message : Group.CountersAggregate)
					{
						if (Message.GetShortName() == StatName)
						{
							// 평균 칸은 double 로 담기기도 한다 — HUD(StatsRender2.cpp)와 같이 형식을 보고 꺼낸다.
							return Message.NameAndInfo.GetField<EStatDataType>() == EStatDataType::ST_double
								? FMath::RoundToInt(Message.GetValue_double(EComplexStatField::IncAve))
								: static_cast<int32>(Message.GetValue_int64(EComplexStatField::IncAve));
						}
					}
				}
			}
#endif
			return 0;
		}
	}

	void StartLogging(AActor* Owner)
	{
		if (!IsValid(Owner) || Owner->GetWorld() == nullptr)
		{
			return;
		}
		ListenThermalOnce();
		const TSharedRef<FPerfState> State = MakeShared<FPerfState>();
		State->LastFrame = GFrameCounter;
		State->LastTime = FPlatformTime::Seconds();
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 성능 로그 시작 (-SnarePerfLog, 1초 간격)"));
		// 맥·iOS(Metal)는 RHI 드로콜 카운터가 0 이라(M18-05) 렌더러 통계 묶음을 화면 표시 없이 켠다. 켜기/끄기 토글이라 한 번만.
		static bool bStatsOn = false;
		if (!bStatsOn && GEngine != nullptr)
		{
			bStatsOn = true;
			GEngine->Exec(Owner->GetWorld(), TEXT("stat SceneRendering -nodisplay"));
		}

		FTimerHandle Handle;
		Owner->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Owner, [Owner, State]()
		{
			const double Now = FPlatformTime::Seconds();
			const int32 Frames = static_cast<int32>(GFrameCounter - State->LastFrame);
			FSample Sample;
			Sample.AverageFrameMs = AverageFrameMs(Frames, Now - State->LastTime);
			// 최악 프레임은 엔진이 마지막 프레임만 알려 준다 — 1초 평균과 같이 보이도록 델타 시간을 쓴다.
			Sample.WorstFrameMs = FMath::Max(Sample.AverageFrameMs, FApp::GetDeltaTime() * 1000.f);
			for (TActorIterator<AAnimalCharacter> It(Owner->GetWorld()); It; ++It)
			{
				++Sample.Animals;
			}
			Sample.DrawCalls = ChooseDrawCalls(GNumDrawCallsRHI[0], ReadMeshDrawCalls());
			Sample.Thermal = GetLatestThermal();
			State->LastFrame = GFrameCounter;
			State->LastTime = Now;
			UE_LOG(LogSnarehaven, Log, TEXT("%s"), *FormatLine(Sample));
		}), 1.f, true, 1.f);
	}

	void LogColdStartOnce()
	{
		ListenThermalOnce();
		static bool bLogged = false;
		if (bLogged)
		{
			return;
		}
		bLogged = true;
		UE_LOG(LogSnarehaven, Log, TEXT("%s"),
			*FormatColdStartLine(ColdStartSeconds(GStartTime, FPlatformTime::Seconds()), GetLatestThermal()));
	}
#endif
}
