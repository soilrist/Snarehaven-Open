// 바주카 어깨 자세 시험 (#1211, INBOX 보정 「오른 어깨에 얹고 포구가 앞을 본다」 — `tools/blender/pistol_aim_make.py baz_hold|baz_recoil|baz_reload`).
// ① 행 `HoldPoseAnim` = `A_Player_HoldBazooka` · `UseAnim` = `A_Player_BazookaRecoil` · `ReloadAnim` = `A_Player_BazookaReload`
// ② 든 자세: 포구가 몸 앞(좌우 25° 안) · 수평~위 15° · 쥔 점이 오른어깨 높이 안(25cm) · 발사관 뒤끝이 어깨 뒤로 나옴 · 앞끝이 캡슐 밖 앞
// ③ 반동: 봉우리(클립 3장)에 포구가 위로 5° 이상 · 0.5초(15장)에 든 자세로(2° 안)
// ④ 재장전: 길이 = 행 ReloadSeconds ±1장 · 가운데(50장)에 포구가 든 자세보다 25° 이상 내려감 · 끝 장은 든 자세(포구 2° 안)
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Combat.Gun.BazookaAimPose; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "SnarehavenCharacter.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/ItemRow.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBazookaAimPoseTest, "Snarehaven.Combat.Gun.BazookaAimPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBazookaAimPoseTest::RunTest(const FString& Parameters)
{
	constexpr double AimYawToleranceDeg = 25.0;
	constexpr double HoldPitchMaxDeg = 15.0;
	constexpr double ShoulderHeightToleranceCm = 25.0;   // #1215 보정: 통 밑이 어깨 위에 얹히게 쥔 점을 어깨 위 16cm 로 올림
	constexpr double KickMinDeg = 5.0;
	constexpr double BackMaxDeg = 2.0;
	constexpr double ReloadMinDropDeg = 25.0;
	constexpr float ClipFps = 30.f;
	constexpr float KickPeakFrame = 3.f;           // `pistol_aim_make.py` baz KICK_PEAK
	constexpr float KickBackFrame = 15.f;          // 〃 KICK_END (0.5초)
	constexpr float ReloadMidFrame = 50.f;         // 왼손이 앞에 와 밀어 넣는 장(R_KEYS 22 × 2.5 중 고른 가운데)

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	const auto Finish = [&]()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			return true;
		};

	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	UEquipmentComponent* const Equipment = Player ? Player->GetEquipment() : nullptr;
	if (!TestNotNull(TEXT("장비 컴포넌트"), Equipment) || !TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody())
		|| !TestTrue(TEXT("바주카를 들었다"), Equipment->EquipItemByItemID(FName(TEXT("item.bazooka")))))
	{
		return Finish() && false;
	}
	const FItemRow* const Row = Equipment->GetEquippedRow(EEquipSlot::Weapon);
	const UStaticMeshComponent* const Gun = Equipment->GetEquippedMesh(EEquipSlot::Weapon);
	UAnimSequence* const Hold = Row ? Row->HoldPoseAnim.LoadSynchronous() : nullptr;
	UAnimSequence* const Recoil = Row ? Row->UseAnim.LoadSynchronous() : nullptr;
	UAnimSequence* const Reload = Row ? Row->ReloadAnim.LoadSynchronous() : nullptr;
	if (!TestNotNull(TEXT("바주카 행"), Row) || !TestNotNull(TEXT("손에 붙은 바주카 메시"), Gun) || !TestNotNull(TEXT("든 자세 클립"), Hold)
		|| !TestNotNull(TEXT("반동 클립"), Recoil) || !TestNotNull(TEXT("재장전 클립"), Reload))
	{
		return Finish() && false;
	}
	// ①
	TestEqual(TEXT("든 자세 = 어깨 얹기"), Hold->GetName(), FString(TEXT("A_Player_HoldBazooka")));
	TestEqual(TEXT("쓰기 = 바주카 반동"), Recoil->GetName(), FString(TEXT("A_Player_BazookaRecoil")));
	TestEqual(TEXT("재장전 = 바주카 장전"), Reload->GetName(), FString(TEXT("A_Player_BazookaReload")));

	USkeletalMeshComponent* const Body = Player->GetMesh();
	const FVector Forward = Player->GetActorForwardVector();
	const double CapsuleRadius = Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const FBox MeshBox = Gun->GetStaticMesh()->GetBoundingBox();
	struct FAim { FVector Muzzle; FVector Grip; FVector Tip; FVector Rear; FVector Shoulder; };
	const auto Measure = [&](UAnimSequence* Anim, float Seconds)
		{
			Body->PlayAnimation(Anim, false);
			Body->SetPosition(Seconds, false);
			Body->TickAnimation(0.f, false);
			Body->RefreshBoneTransforms();
			Body->UpdateChildTransforms();
			const FTransform& T = Gun->GetComponentTransform();
			return FAim{ T.GetUnitAxis(EAxis::X), T.GetLocation(), T.TransformPosition(FVector(MeshBox.Max.X, 0.0, 0.0)),
				T.TransformPosition(FVector(MeshBox.Min.X, 0.0, 0.0)), Body->GetBoneLocation(FName(TEXT("RightArm"))) };
		};
	const auto PitchDeg = [](const FVector& Muzzle)
		{
			return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Muzzle.GetSafeNormal().Z, -1.0, 1.0)));
		};
	const auto AngleDeg = [](const FVector& A, const FVector& B)
		{
			return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A, B), -1.0, 1.0)));
		};

	// ②
	const FAim Aim = Measure(Hold, 0.f);
	const FVector Flat = FVector(Aim.Muzzle.X, Aim.Muzzle.Y, 0.0).GetSafeNormal();
	const double YawDeg = AngleDeg(Flat, Forward);
	const double Pitch = PitchDeg(Aim.Muzzle);
	const double TipAhead = FVector::DotProduct(Aim.Tip - Player->GetActorLocation(), Forward);
	const double RearBehindShoulder = FVector::DotProduct(Aim.Rear - Aim.Shoulder, Forward);
	const double GripAboveShoulder = Aim.Grip.Z - Aim.Shoulder.Z;
	AddInfo(FString::Printf(TEXT("[바주카] 포구 좌우 %.1f° · 위아래 %.1f° · 앞끝 앞 %.1fcm(캡슐 %.1f) · 뒤끝 어깨 기준 앞 %.1fcm · 쥔 점 어깨보다 %.1fcm"),
		YawDeg, Pitch, TipAhead, CapsuleRadius, RearBehindShoulder, GripAboveShoulder));
	TestTrue(FString::Printf(TEXT("포구가 몸 앞 (좌우 %.1f° ≤ %.0f)"), YawDeg, AimYawToleranceDeg), YawDeg <= AimYawToleranceDeg);
	TestTrue(FString::Printf(TEXT("포구가 수평~위 (%.1f° 0~%.0f)"), Pitch, HoldPitchMaxDeg), Pitch >= 0.0 && Pitch <= HoldPitchMaxDeg);
	TestTrue(FString::Printf(TEXT("앞끝이 몸 밖 앞 (%.1f > 캡슐 %.1f)"), TipAhead, CapsuleRadius), TipAhead > CapsuleRadius);
	TestTrue(FString::Printf(TEXT("뒤끝이 어깨 뒤로 나옴 (%.1f < 0)"), RearBehindShoulder), RearBehindShoulder < 0.0);
	TestTrue(FString::Printf(TEXT("쥔 점이 어깨 높이 안 (%.1f ≤ ±%.0fcm)"), GripAboveShoulder, ShoulderHeightToleranceCm),
		FMath::Abs(GripAboveShoulder) <= ShoulderHeightToleranceCm);

	// ③ — 클립 길이 = 쏘기 간격(재생 속도 1)
	const FAim Base = Measure(Recoil, 0.f);
	const FAim Peak = Measure(Recoil, KickPeakFrame / ClipFps);
	const FAim Back = Measure(Recoil, KickBackFrame / ClipFps);
	const double Kick = PitchDeg(Peak.Muzzle) - PitchDeg(Base.Muzzle);
	const double BackDeg = AngleDeg(Back.Muzzle, Aim.Muzzle);
	AddInfo(FString::Printf(TEXT("[바주카] 반동 클립 %.2f초(쏘기 간격 %.2f) · 봉우리 포구 +%.1f° · 0.5초 뒤 든 자세와 %.2f°"), Recoil->GetPlayLength(), Row->FireIntervalSeconds, Kick, BackDeg));
	TestTrue(TEXT("반동 클립 길이 = 쏘기 간격 ±1장"), FMath::Abs(Recoil->GetPlayLength() - Row->FireIntervalSeconds) <= 1.f / ClipFps + KINDA_SMALL_NUMBER);
	TestTrue(FString::Printf(TEXT("봉우리에 포구가 위로 (+%.1f° ≥ %.0f)"), Kick, KickMinDeg), Kick >= KickMinDeg);
	TestTrue(FString::Printf(TEXT("0.5초에 포구가 든 자세로 (%.2f° ≤ %.0f)"), BackDeg, BackMaxDeg), BackDeg <= BackMaxDeg);

	// ④
	const float ReloadLength = Reload->GetPlayLength();
	const FAim Mid = Measure(Reload, ReloadMidFrame / ClipFps);
	const double Drop = PitchDeg(Aim.Muzzle) - PitchDeg(Mid.Muzzle);
	const FAim End = Measure(Reload, ReloadLength);
	const double EndDeg = AngleDeg(End.Muzzle, Aim.Muzzle);
	AddInfo(FString::Printf(TEXT("[바주카] 재장전 클립 %.2f초(행 %.2f) · 가운데 포구 −%.1f° · 끝 장 든 자세와 %.2f°"), ReloadLength, Row->ReloadSeconds, Drop, EndDeg));
	TestTrue(FString::Printf(TEXT("재장전 길이 = 행 재장전 초 ±1장 (%.2f ≈ %.2f)"), ReloadLength, Row->ReloadSeconds),
		FMath::Abs(ReloadLength - Row->ReloadSeconds) <= 1.f / ClipFps + KINDA_SMALL_NUMBER);
	TestTrue(FString::Printf(TEXT("장전 가운데 포구가 내려감 (−%.1f° ≥ %.0f)"), Drop, ReloadMinDropDeg), Drop >= ReloadMinDropDeg);
	TestTrue(FString::Printf(TEXT("끝 장 포구가 든 자세로 (%.2f° ≤ %.0f)"), EndDeg, BackMaxDeg), EndDeg <= BackMaxDeg);
	return Finish();
}

#endif
