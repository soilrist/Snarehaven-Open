// ROADMAP M15-23 2층 검사 — 바닥 머티리얼 색이 레퍼런스 시트(`docs/reference/world_environment.md`)의 색 계열 안인지 잰다.
// 판정은 sRGB 로 바꾼 뒤의 색상(°)·채도다. 밝기는 재지 않는다 — 조명이 화면을 리니어 값보다 밝게 올려서 바닥을 일부러 짙게 골랐다(`setup_floor_colors.py`).
// 폭은 시험용 색으로 안·밖을 둘 다 잡고(회색·파랑은 밖), 그 다음 실제 머티리얼 인스턴스 값을 읽어 같은 자로 잰다.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.World.Palette; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Materials/MaterialInstance.h"

namespace SnarehavenPaletteTest
{
	constexpr float HueToleranceDeg = 35.f;
	constexpr float MinSaturation = 0.2f;
	constexpr float SaturationMargin = 0.1f; // 시트 채도보다 이만큼까지 더 진해도 계열 안

	struct FHueSat
	{
		float Hue = 0.f;
		float Saturation = 0.f;
	};

	FHueSat MeasureSrgb(const FLinearColor& Linear)
	{
		const FColor Srgb = Linear.ToFColor(true);
		const FLinearColor Plain(Srgb.R / 255.f, Srgb.G / 255.f, Srgb.B / 255.f);
		const FLinearColor Hsv = Plain.LinearRGBToHSV();
		return { Hsv.R, Hsv.G };
	}

	FHueSat MeasureHex(const TCHAR* Hex)
	{
		const FColor Srgb = FColor::FromHex(Hex);
		const FLinearColor Plain(Srgb.R / 255.f, Srgb.G / 255.f, Srgb.B / 255.f);
		const FLinearColor Hsv = Plain.LinearRGBToHSV();
		return { Hsv.R, Hsv.G };
	}

	bool IsInFamily(const FHueSat& Value, const FHueSat& Sheet)
	{
		float HueGap = FMath::Abs(Value.Hue - Sheet.Hue);
		HueGap = FMath::Min(HueGap, 360.f - HueGap);
		return HueGap <= HueToleranceDeg && Value.Saturation >= MinSaturation
			&& Value.Saturation <= Sheet.Saturation + SaturationMargin;
	}

	bool ReadFloorColor(const TCHAR* Path, FLinearColor& Out)
	{
		const UMaterialInstance* const Material = LoadObject<UMaterialInstance>(nullptr, Path);
		return Material != nullptr && Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("DiffuseColor")), Out);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWorldPaletteTest, "Snarehaven.World.Palette.MatchesReferenceSheet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWorldPaletteTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenPaletteTest;

	const FHueSat SheetGrass = MeasureHex(TEXT("7BBF5A"));
	const FHueSat SheetSand = MeasureHex(TEXT("F0C86A"));

	// 자 자체 — 폭 밖 색은 걸러야 한다.
	TestFalse(TEXT("회색은 풀색 계열 밖이다"), IsInFamily(MeasureSrgb(FLinearColor(0.2f, 0.2f, 0.2f)), SheetGrass));
	TestFalse(TEXT("파랑은 풀색 계열 밖이다"), IsInFamily(MeasureSrgb(FLinearColor(0.05f, 0.1f, 0.6f)), SheetGrass));
	TestTrue(TEXT("시트 풀색 자신은 계열 안이다"), IsInFamily(SheetGrass, SheetGrass));

	// 실제 바닥 머티리얼 — 사냥터 풀, 목장 흙(시트의 가을 모래 계열).
	FLinearColor Hunt;
	if (TestTrue(TEXT("사냥터 바닥 머티리얼 `DiffuseColor` 를 읽는다"), ReadFloorColor(TEXT("/Game/Game/Environment/Nature/MI_Nature_Grass.MI_Nature_Grass"), Hunt)))
	{
		const FHueSat Measured = MeasureSrgb(Hunt);
		TestTrue(*FString::Printf(TEXT("사냥터 바닥이 시트 풀색 계열 안 (색상 %.0f° 채도 %.2f, 시트 %.0f° %.2f)"),
			Measured.Hue, Measured.Saturation, SheetGrass.Hue, SheetGrass.Saturation), IsInFamily(Measured, SheetGrass));
	}
	FLinearColor Ranch;
	if (TestTrue(TEXT("목장 바닥 머티리얼 `DiffuseColor` 를 읽는다"), ReadFloorColor(TEXT("/Game/Game/Environment/Ground/MI_Ground_Dirt.MI_Ground_Dirt"), Ranch)))
	{
		const FHueSat Measured = MeasureSrgb(Ranch);
		TestTrue(*FString::Printf(TEXT("목장 바닥이 시트 모래 계열 안 (색상 %.0f° 채도 %.2f, 시트 %.0f° %.2f)"),
			Measured.Hue, Measured.Saturation, SheetSand.Hue, SheetSand.Saturation), IsInFamily(Measured, SheetSand));
	}
	return true;
}

#endif
