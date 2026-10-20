// ROADMAP M39-06b 2층 검사 — 권총 겨눔 들기·반동 클립 (`tools/blender/pistol_aim_make.py`).
// 레퍼런스 `steam_01.jpg`: 총은 가슴 높이에서 몸 앞(보는 쪽)으로 내민다. 루프#577 3층은 든 자세가 없어 권총이 몸·모자에 가렸다.
// ① 행 클립 = 든 자세 `A_Player_HoldPistol` · 쓰기 `A_Player_PistolRecoil` (정해진 길이 아님 — 쏘기 간격 한 바퀴)
// ② 든 자세 첫 장: 총구(메시 +X)가 몸 앞 ±25° · 수평 ±20° · 총이 섬(메시 +Z 위) · 총구 끝이 캡슐 밖 앞 · 쥔 점이 골반 위~목 아래 · 총몸 안쪽 끝이 머리 옆 윤곽 밖
//    (루프#580 감독 #579 「옆·뒤에서 머리에 가린다」 — 머리 반폭 > 팔 길이라 두 손 대신 오른팔을 옆 앞으로 뻗는다)
// ③ 반동: 봉우리(클립 2장)에 총구가 위로 든다 · DESIGN 8.6-c 「반동 클립」 초(게임 시간)에 든 자세로 돌아옴(총구 2° · 오른손목 1cm 안)
// ④ 재장전(M39-06b2): 행 `ReloadAnim` = `A_Player_PistolReload` · 길이 = 행 `ReloadSeconds` ±1장 · 가운데(절반)에 총구가 위로 들리고
//    왼손목이 오른손목 가까이(탄창 넣기) · 끝 장은 든 자세(총구 2° · 오른손목 1cm 안)
// 현대 소총(M39-08c `Snarehaven.Combat.Gun.RifleAimPose`)도 같은 단정 — 든 자세는 권총 것 그대로(두 손은 팔 길이로 안 됨, `pistol_aim_make.py` 머리말),
//    반동 `A_Player_RifleRecoil`(DESIGN 8.6-c 소총 줄 「반동 0.25초」) · 재장전 `A_Player_RifleReload`(행 2.0초, 탄창 넣는 장 25 = 15 × 60/36).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Combat.Gun; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "SnarehavenCharacter.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace SnarehavenPistolAimTest
{
	// 시험 기준 (DESIGN 숫자가 아니라 「겨눔으로 보이나」의 허용 폭).
	constexpr double AimYawToleranceDeg = 25.0;
	constexpr double AimPitchToleranceDeg = 20.0;
	constexpr double UprightMinDot = 0.8;            // 총 위(+Z)가 월드 위와 37° 안
	// 머리 반폭(z 0.6~1.0 정점 |x| 최대)·목·골반 높이 — 바르코 몸 메시 Blender 측정(루프#580). 엔진 cm = 목-골반 높이 비로 바꾼다.
	constexpr double HeadHalfWidthB = 0.244;
	constexpr double NeckB = 0.577;
	constexpr double HipsB = 0.331;
	constexpr double KickMinDeg = 5.0;
	constexpr double BackMaxDeg = 2.0;
	constexpr double BackMaxCm = 1.0;
	constexpr float ClipFps = 30.f;                  // 바르코 클립 장 속도(`move_v2_lib.FPS`)
	constexpr float KickPeakFrame = 2.f;             // `pistol_aim_make.py` KICK_PEAK
	constexpr double ReloadMinLiftDeg = 20.0;        // 재장전 중 총구가 든 자세보다 위로 든 각(「탄창 넣는다」 가 읽히는 폭)
	constexpr double ReloadHandsMaxCm = 25.0;        // 탄창 넣는 순간 두 손목 사이
	const FName LeftHandBone(TEXT("LeftHand"));
	const FName RightHandBone(TEXT("RightHand"));

	UWorld* MakeWorld()
	{
		UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	/** 클립의 Seconds 자세로 뼈를 놓는다 — 시험 월드는 틱이 없어 한 번 평가해야 화면과 같은 자세가 된다. */
	void SetPose(USkeletalMeshComponent* Body, UAnimSequence* Anim, float Seconds)
	{
		Body->PlayAnimation(Anim, false);
		Body->SetPosition(Seconds, false);
		Body->TickAnimation(0.f, false);
		Body->RefreshBoneTransforms();
		Body->UpdateChildTransforms();
	}

	/** 총구 방향의 위로 든 각(도, + = 위). */
	double PitchDeg(const FVector& Muzzle)
	{
		return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Muzzle.GetSafeNormal().Z, -1.0, 1.0)));
	}

	/** 총 하나의 기대값 — 클립 이름·DESIGN 반동 줄·탄창 넣는 장(`pistol_aim_make.py` L_KEYS 15 × 늘림). */
	struct FGunAimCase
	{
		FName ItemID;
		const TCHAR* Label;
		const TCHAR* HoldName;
		const TCHAR* RecoilName;
		const TCHAR* ReloadName;
		const TCHAR* RecoilPattern;
		float ReloadMagFrame;
	};

	bool TestGunAimPose(FAutomationTestBase& Test, const FGunAimCase& Case);
}

using namespace SnarehavenPistolAimTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPistolAimPoseTest, "Snarehaven.Combat.Gun.PistolAimPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPistolAimPoseTest::RunTest(const FString& Parameters)
{
	return TestGunAimPose(*this, { FName(TEXT("item.pistol_semi")), TEXT("권총"), TEXT("A_Player_HoldPistol"), TEXT("A_Player_PistolRecoil"),
		TEXT("A_Player_PistolReload"), TEXT("\\| 반동 클립 \\| ([0-9.]+)초"), 15.f });
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRifleAimPoseTest, "Snarehaven.Combat.Gun.RifleAimPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRifleAimPoseTest::RunTest(const FString& Parameters)
{
	return TestGunAimPose(*this, { FName(TEXT("item.rifle_modern")), TEXT("소총"), TEXT("A_Player_HoldPistol"), TEXT("A_Player_RifleRecoil"),
		TEXT("A_Player_RifleReload"), TEXT("\\| 신식 소총 `item\\.rifle_modern`[^|]*\\|[^|]*반동 ([0-9.]+)초"), 15.f * 60.f / 36.f });
}

bool SnarehavenPistolAimTest::TestGunAimPose(FAutomationTestBase& Test, const FGunAimCase& Case)
{
	FString Doc;
	if (!Test.TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	FRegexMatcher Match(FRegexPattern(Case.RecoilPattern), Doc);
	if (!Test.TestTrue(TEXT("DESIGN 8.6-c 반동 줄이 있다"), Match.FindNext()))
	{
		return false;
	}
	const float RecoilSeconds = FCString::Atof(*Match.GetCaptureGroup(1));

	UWorld* const World = MakeWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	UEquipmentComponent* const Equipment = Player ? Player->GetEquipment() : nullptr;
	if (!Test.TestNotNull(TEXT("장비 컴포넌트"), Equipment) || !Test.TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody())
		|| !Test.TestTrue(FString::Printf(TEXT("%s을 들었다"), Case.Label), Equipment->EquipItemByItemID(Case.ItemID)))
	{
		DestroyWorld(World);
		return false;
	}
	const FItemRow* const Row = Equipment->GetEquippedRow(EEquipSlot::Weapon);
	const UStaticMeshComponent* const Gun = Equipment->GetEquippedMesh(EEquipSlot::Weapon);
	UAnimSequence* const Hold = Row ? Row->HoldPoseAnim.LoadSynchronous() : nullptr;
	UAnimSequence* const Recoil = Row ? Row->UseAnim.LoadSynchronous() : nullptr;
	if (!Test.TestNotNull(TEXT("총 행"), Row) || !Test.TestNotNull(TEXT("손에 붙은 총 메시"), Gun)
		|| !Test.TestNotNull(TEXT("든 자세 클립"), Hold) || !Test.TestNotNull(TEXT("반동 클립"), Recoil) || !Test.TestTrue(TEXT("쏘기 간격"), Row->FireIntervalSeconds > 0.f))
	{
		DestroyWorld(World);
		return false;
	}
	// ①
	Test.TestEqual(TEXT("든 자세 = 겨눔"), Hold->GetName(), FString(Case.HoldName));
	Test.TestEqual(TEXT("쓰기 = 반동"), Recoil->GetName(), FString(Case.RecoilName));
	Test.TestFalse(TEXT("정해진 길이 아님 — 쏘기 간격 한 바퀴"), Row->bUseAnimFixedLength);

	USkeletalMeshComponent* const Body = Player->GetMesh();
	const FVector Origin = Player->GetActorLocation();
	const FVector Forward = Player->GetActorForwardVector();
	const double CapsuleRadius = Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const double MuzzleTipX = Gun->GetStaticMesh()->GetBoundingBox().Max.X;
	struct FAim { FVector Muzzle; FVector Up; FVector Tip; FVector Grip; FVector Right; };
	const auto Measure = [&](UAnimSequence* Anim, float Seconds)
		{
			SetPose(Body, Anim, Seconds);
			const FTransform& T = Gun->GetComponentTransform();
			return FAim{ T.GetUnitAxis(EAxis::X), T.GetUnitAxis(EAxis::Z), T.TransformPosition(FVector(MuzzleTipX, 0.0, 0.0)), T.GetLocation(),
				Body->GetBoneLocation(RightHandBone) };
		};

	// ②
	const FAim Aim = Measure(Hold, 0.f);
	const FVector Flat = FVector(Aim.Muzzle.X, Aim.Muzzle.Y, 0.0).GetSafeNormal();
	const double YawDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Flat, Forward), -1.0, 1.0)));
	const double Pitch = PitchDeg(Aim.Muzzle);
	const double TipAhead = FVector::DotProduct(Aim.Tip - Origin, Forward);
	const double HipsZ = Body->GetBoneLocation(FName(TEXT("Hips"))).Z;
	const double NeckZ = Body->GetBoneLocation(FName(TEXT("Neck"))).Z;
	const double HeadHalfCm = HeadHalfWidthB * (NeckZ - HipsZ) / (NeckB - HipsB);
	const FVector HeadLoc = Body->GetBoneLocation(FName(TEXT("Head")));
	const double GunHalfWidth = Gun->GetStaticMesh()->GetBoundingBox().GetExtent().Y * Gun->GetComponentScale().Y;
	const double GunInnerSide = FVector::DotProduct(Aim.Grip - HeadLoc, Player->GetActorRightVector()) - GunHalfWidth;
	Test.AddInfo(FString::Printf(TEXT("[%s] 총구 좌우 %.1f° · 위아래 %.1f° · 총 위 내적 %.2f · 총구 끝 앞 %.1fcm(캡슐 %.1f) · 쥔 점 z %.1f(골반 %.1f~목 %.1f) · 총몸 안쪽 끝 머리에서 오른쪽 %.1fcm(머리 반폭 %.1f)"),
		Case.Label, YawDeg, Pitch, FVector::DotProduct(Aim.Up, FVector::UpVector), TipAhead, CapsuleRadius, Aim.Grip.Z, HipsZ, NeckZ, GunInnerSide, HeadHalfCm));
	Test.TestTrue(FString::Printf(TEXT("총구가 몸 앞 (좌우 %.1f° ≤ %.0f)"), YawDeg, AimYawToleranceDeg), YawDeg <= AimYawToleranceDeg);
	Test.TestTrue(FString::Printf(TEXT("총구 수평 (%.1f° ≤ ±%.0f)"), Pitch, AimPitchToleranceDeg), FMath::Abs(Pitch) <= AimPitchToleranceDeg);
	Test.TestTrue(TEXT("총이 섰다(슬라이드 위)"), FVector::DotProduct(Aim.Up, FVector::UpVector) >= UprightMinDot);
	Test.TestTrue(FString::Printf(TEXT("총구 끝이 몸 밖 앞 (%.1f > 캡슐 %.1f)"), TipAhead, CapsuleRadius), TipAhead > CapsuleRadius);
	Test.TestTrue(TEXT("쥔 점이 골반 위·목 아래(가슴 높이)"), Aim.Grip.Z > HipsZ && Aim.Grip.Z < NeckZ);
	Test.TestTrue(FString::Printf(TEXT("총몸이 머리 옆 윤곽 밖 (%.1f > 머리 반폭 %.1fcm)"), GunInnerSide, HeadHalfCm), GunInnerSide > HeadHalfCm);

	// ③ 몸 클립은 `클립 길이 ÷ 쏘기 간격` 속도로 돈다(`ASnarehavenCharacter` 휘두르기 갈래) — 게임 초 → 클립 초.
	const float Rate = Recoil->GetPlayLength() / Row->FireIntervalSeconds;
	const FAim Base = Measure(Recoil, 0.f);
	const FAim Peak = Measure(Recoil, KickPeakFrame / ClipFps);
	const FAim Back = Measure(Recoil, RecoilSeconds * Rate);
	const double Kick = PitchDeg(Peak.Muzzle) - PitchDeg(Base.Muzzle);
	const double BackDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Back.Muzzle, Aim.Muzzle), -1.0, 1.0)));
	const double BackCm = FVector::Dist(Back.Right, Aim.Right);
	Test.AddInfo(FString::Printf(TEXT("[%s] 반동 재생 속도 %.2f · 봉우리 총구 +%.1f° · %.2f초 뒤 든 자세와 %.2f° · 오른손목 %.2fcm · 반동 첫 장-든 자세 %.2fcm"),
		Case.Label, Rate, Kick, RecoilSeconds, BackDeg, BackCm, FVector::Dist(Base.Right, Aim.Right)));
	Test.TestTrue(FString::Printf(TEXT("봉우리에 총구가 위로 (+%.1f° ≥ %.0f)"), Kick, KickMinDeg), Kick >= KickMinDeg);
	Test.TestTrue(FString::Printf(TEXT("%.2f초에 총구가 든 자세로 (%.2f° ≤ %.0f)"), RecoilSeconds, BackDeg, BackMaxDeg), BackDeg <= BackMaxDeg);
	Test.TestTrue(FString::Printf(TEXT("%.2f초에 오른손목이 든 자세로 (%.2f ≤ %.0fcm)"), RecoilSeconds, BackCm, BackMaxCm), BackCm <= BackMaxCm);

	// ④
	UAnimSequence* const Reload = Row->ReloadAnim.LoadSynchronous();
	if (Test.TestNotNull(TEXT("재장전 클립"), Reload))
	{
		Test.TestEqual(TEXT("재장전 = 탄창 넣기"), Reload->GetName(), FString(Case.ReloadName));
		const float ReloadLength = Reload->GetPlayLength();
		const FAim Mag = Measure(Reload, Case.ReloadMagFrame / ClipFps);
		const double MagHandsCm = FVector::Dist(Mag.Right, Body->GetBoneLocation(LeftHandBone));
		const double Lift = PitchDeg(Mag.Muzzle) - PitchDeg(Aim.Muzzle);
		const FAim End = Measure(Reload, ReloadLength);
		const double EndDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(End.Muzzle, Aim.Muzzle), -1.0, 1.0)));
		const double EndCm = FVector::Dist(End.Right, Aim.Right);
		Test.AddInfo(FString::Printf(TEXT("[%s] 재장전 클립 %.2f초(행 %.2f) · 탄창 넣는 장 총구 +%.1f° · 두 손목 %.1fcm · 끝 장 든 자세와 %.2f° · 오른손목 %.2fcm"),
			Case.Label, ReloadLength, Row->ReloadSeconds, Lift, MagHandsCm, EndDeg, EndCm));
		Test.TestTrue(FString::Printf(TEXT("길이 = 행 재장전 초 ±1장 (%.2f ≈ %.2f)"), ReloadLength, Row->ReloadSeconds),
			FMath::Abs(ReloadLength - Row->ReloadSeconds) <= 1.f / ClipFps + KINDA_SMALL_NUMBER);
		Test.TestTrue(FString::Printf(TEXT("탄창 넣을 때 총구가 위로 (+%.1f° ≥ %.0f)"), Lift, ReloadMinLiftDeg), Lift >= ReloadMinLiftDeg);
		Test.TestTrue(FString::Printf(TEXT("탄창 넣을 때 두 손이 모임 (%.1f ≤ %.0fcm)"), MagHandsCm, ReloadHandsMaxCm), MagHandsCm <= ReloadHandsMaxCm);
		Test.TestTrue(FString::Printf(TEXT("끝 장 총구가 든 자세로 (%.2f° ≤ %.0f)"), EndDeg, BackMaxDeg), EndDeg <= BackMaxDeg);
		Test.TestTrue(FString::Printf(TEXT("끝 장 오른손목이 든 자세로 (%.2f ≤ %.0fcm)"), EndCm, BackMaxCm), EndCm <= BackMaxCm);
	}

	DestroyWorld(World);
	return true;
}

#endif
