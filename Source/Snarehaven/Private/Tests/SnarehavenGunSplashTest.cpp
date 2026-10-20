// INBOX 새 무기 바주카 1조각 2층 검사 — 폭발 피해 함수 `ComputeSplashDamage` 가 중심 전부·가장자리 비율·반경 밖 0·폭발 없는 총 0 인지,
// 거리가 멀수록 줄기만 하는지. 숫자는 시험 전용 값이라 설계서 표가 바뀌어도 안 깨진다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Combat.Gun.Splash; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/MeleeAttackComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGunSplashTest, "Snarehaven.Combat.Gun.Splash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGunSplashTest::RunTest(const FString& Parameters)
{
	const float Direct = 100.f;
	const float Radius = 400.f;
	const float Edge = 0.25f;
	TestEqual(TEXT("중심은 직격 피해 그대로"), UMeleeAttackComponent::ComputeSplashDamage(Direct, Radius, Edge, 0.f), 100.f);
	TestEqual(TEXT("반경 가장자리는 직격 × 가장자리 비율"), UMeleeAttackComponent::ComputeSplashDamage(Direct, Radius, Edge, Radius), 25.f);
	TestEqual(TEXT("반경 절반은 선형 가운데"), UMeleeAttackComponent::ComputeSplashDamage(Direct, Radius, Edge, 200.f), 62.5f);
	TestEqual(TEXT("반경 밖은 0"), UMeleeAttackComponent::ComputeSplashDamage(Direct, Radius, Edge, Radius + 1.f), 0.f);
	TestEqual(TEXT("반경 0 (폭발 없는 총)은 0"), UMeleeAttackComponent::ComputeSplashDamage(Direct, 0.f, Edge, 0.f), 0.f);
	float Previous = Direct + 1.f;
	bool bMonotonic = true;
	for (float Distance = 0.f; Distance <= Radius; Distance += 25.f)
	{
		const float Damage = UMeleeAttackComponent::ComputeSplashDamage(Direct, Radius, Edge, Distance);
		bMonotonic &= Damage <= Previous;
		Previous = Damage;
	}
	TestTrue(TEXT("멀수록 줄기만 한다"), bMonotonic);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
