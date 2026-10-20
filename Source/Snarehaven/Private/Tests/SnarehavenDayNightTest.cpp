// 레퍼런스 낮밤 2층 검사 — 곡선이 DESIGN 3.3 「낮밤」 표(하루 20분·밤 8분·해질녘 1분)대로인가,
// 밤 색이 남보라이고 두 맵의 밤 불빛이 시트(world_environment.md)대로 주황·반경 캐릭터 3배·한 화면 4개 이하인가.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/PointLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "World/SnareDayNight.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDayNightCurveTest, "Snarehaven.World.DayNight.Curve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDayNightCurveTest::RunTest(const FString& Parameters)
{
	using namespace SnareDayNight;
	TestEqual(TEXT("하루 20분"), CycleSeconds, 1200.0);
	TestEqual(TEXT("밤 8분"), NightSeconds, 480.0);
	TestEqual(TEXT("아침(0초)은 낮"), NightAmount(0.0), 0.f);
	TestEqual(TEXT("해질녘 직전(659초)은 낮"), NightAmount(659.0), 0.f);
	TestTrue(TEXT("해질녘 한가운데(690초)는 반쯤"), FMath::IsNearlyEqual(NightAmount(690.0), 0.5f, 0.01f));
	TestEqual(TEXT("밤 한가운데(960초)는 밤"), NightAmount(960.0), 1.f);
	TestTrue(TEXT("새벽 한가운데(1170초)는 반쯤"), FMath::IsNearlyEqual(NightAmount(1170.0), 0.5f, 0.01f));
	TestEqual(TEXT("다음 날(1200+100초)은 다시 낮"), NightAmount(1300.0), 0.f);
	TestEqual(TEXT("음수 시각도 주기로 접힌다(-240초 = 960초)"), NightAmount(-240.0), 1.f);
	return true;
}

namespace SnarehavenDayNightTest
{
	// 테스트 전용 값 (Verification.md — 설계 숫자가 아니다).
	// 시트 밤 톤 `#5B4FA8` 의 선형 색상각 ≈ 245° — 달빛·밤 하늘빛이 그 ±25° 안이면 남보라로 본다.
	const FLinearColor ReferenceNightTone = FLinearColor::FromSRGBColor(FColor(0x5B, 0x4F, 0xA8));
	constexpr float HueToleranceDeg = 25.f;
	// 시트 「반경 캐릭터 3배」 — 기준 몸 176cm(DESIGN 카메라 표).
	constexpr float CharacterHeightCm = 176.f;
	constexpr float RadiusToleranceCm = 1.f;
	// 한 화면 — 기본 카메라(거리 4500·각 55·가로 화각 30°)가 땅에 덮는 넓이 ≈ 가로 2400 × 세로 1650cm.
	constexpr float ScreenWidthCm = 2400.f;
	constexpr float ScreenDepthCm = 1650.f;
	constexpr int32 MaxLightsPerScreen = 4;

	float HueOf(const FLinearColor& Color)
	{
		return Color.LinearRGBToHSV().R;
	}

	UWorld* CreateWorldWithLevel(const TCHAR* LevelPackage, ULevel*& OutLevel)
	{
		UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		bool bSuccess = false;
		const ULevelStreamingDynamic* const Streaming = ULevelStreamingDynamic::LoadLevelInstance(
			World, LevelPackage, FVector::ZeroVector, FRotator::ZeroRotator, bSuccess);
		World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
		OutLevel = bSuccess && Streaming != nullptr ? Streaming->GetLoadedLevel() : nullptr;
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDayNightToneTest, "Snarehaven.World.DayNight.NightTone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDayNightToneTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenDayNightTest;
	const float Target = HueOf(ReferenceNightTone);
	const float Moon = HueOf(SnareDayNight::MoonColor);
	const float Sky = HueOf(SnareDayNight::NightSkyColor);
	AddInfo(FString::Printf(TEXT("[DayNight] 목표 #5B4FA8 색상각 %.0f° · 달빛 %.0f° · 밤 하늘빛 %.0f° · 해 ×%.2f · 하늘빛 ×%.2f"),
		Target, Moon, Sky, SnareDayNight::NightSunScale, SnareDayNight::NightSkyScale));
	TestTrue(FString::Printf(TEXT("달빛 색상각 %.0f° 가 남보라(%.0f±%.0f°)"), Moon, Target, HueToleranceDeg),
		FMath::Abs(Moon - Target) <= HueToleranceDeg);
	TestTrue(FString::Printf(TEXT("밤 하늘빛 색상각 %.0f° 가 남보라(%.0f±%.0f°)"), Sky, Target, HueToleranceDeg),
		FMath::Abs(Sky - Target) <= HueToleranceDeg);
	TestTrue(TEXT("밤에도 해가 꺼지지 않는다(0 < 해 배율 < 1)"),
		SnareDayNight::NightSunScale > 0.f && SnareDayNight::NightSunScale < 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDayNightLightsTest, "Snarehaven.World.DayNight.NightLights",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDayNightLightsTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenDayNightTest;
	for (const TCHAR* const LevelPackage : { TEXT("/Game/Game/Maps/L_Hunt"), TEXT("/Game/Game/Maps/L_Ranch") })
	{
		ULevel* Level = nullptr;
		UWorld* const World = CreateWorldWithLevel(LevelPackage, Level);
		if (!TestNotNull(FString::Printf(TEXT("%s 를 불러왔다"), LevelPackage), Level))
		{
			DestroyTestWorld(World);
			continue;
		}
		TArray<const UPointLightComponent*> Lights;
		for (const AActor* const Actor : Level->Actors)
		{
			if (Actor == nullptr || !Actor->ActorHasTag(USnareDayNightSubsystem::NightLightTag))
			{
				continue;
			}
			Actor->ForEachComponent<UPointLightComponent>(false, [&Lights](const UPointLightComponent* Light)
			{
				Lights.Add(Light);
			});
		}
		TestTrue(FString::Printf(TEXT("%s 에 밤 불빛이 있다 (%d)"), LevelPackage, Lights.Num()), Lights.Num() > 0);
		int32 Worst = 0;
		for (const UPointLightComponent* const Light : Lights)
		{
			const FLinearColor Color = Light->GetLightColor();
			TestTrue(FString::Printf(TEXT("%s %s 주황(R>G>B)"), LevelPackage, *Light->GetOwner()->GetActorLabel()),
				Color.R > Color.G && Color.G > Color.B);
			TestTrue(FString::Printf(TEXT("%s %s 반경 %.0f = 캐릭터 3배"), LevelPackage, *Light->GetOwner()->GetActorLabel(),
				Light->AttenuationRadius), FMath::IsNearlyEqual(Light->AttenuationRadius, 3.f * CharacterHeightCm, RadiusToleranceCm));
			TestFalse(TEXT("그림자 끔"), Light->CastShadows);
			// 이 불빛을 화면 한가운데 둔 화면 하나 — 카메라 요가 돌 수 있으니 가로·세로 둘 다 본다.
			const FVector Center = Light->GetComponentLocation();
			for (const FVector2D Half : { FVector2D(ScreenDepthCm, ScreenWidthCm) * 0.5, FVector2D(ScreenWidthCm, ScreenDepthCm) * 0.5 })
			{
				int32 InScreen = 0;
				for (const UPointLightComponent* const Other : Lights)
				{
					const FVector Delta = Other->GetComponentLocation() - Center;
					InScreen += FMath::Abs(Delta.X) <= Half.X && FMath::Abs(Delta.Y) <= Half.Y ? 1 : 0;
				}
				Worst = FMath::Max(Worst, InScreen);
			}
		}
		AddInfo(FString::Printf(TEXT("[DayNight] %s 밤 불빛 %d · 한 화면 최다 %d"), LevelPackage, Lights.Num(), Worst));
		TestTrue(FString::Printf(TEXT("%s 한 화면 불빛 %d ≤ %d"), LevelPackage, Worst, MaxLightsPerScreen), Worst <= MaxLightsPerScreen);
		DestroyTestWorld(World);
	}
	return true;
}

#endif
