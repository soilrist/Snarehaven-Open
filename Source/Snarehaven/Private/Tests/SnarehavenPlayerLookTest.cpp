// M15-05 2층 검사 — 플레이어의 **보이는 외형**을 레퍼런스 시트 `docs/reference/character_player.md` 와 견준다.
// 시트의 3층 눈대중 항목 중 숫자로 잴 수 있는 둘을 여기로 옮긴다(사람이 캡처를 볼 때까지 기다리지 않게):
// ① **2.5등신** — "머리(둥근 공)가 전체 높이의 약 40%". 정수리(`Socket_Head` = 몸 경계 맨 위)에서 목 뼈까지가 머리,
//    발(캡슐 바닥)에서 정수리까지가 키다. 시트가 "목 없음 — 머리가 몸통에 바로 얹힘" 이라 목 뼈가 곧 머리 밑이다.
// ② **등에 배낭 — 항상** — 기본 겉모습(M15-10 기본 배낭, DESIGN 3.5 ×0.9)도 **등 한가운데**에 붙어야 한다.
//    `Snarehaven.Equipment.DefaultLook.BareHeadSmallBackpack` 은 크기만 봤고 자리는 안 봤다.
//    자는 `Snarehaven.Equipment.RodBackpack.RealMeshes` 와 같다 — 가슴 뼈보다 등 쪽 · 몸 쪽 면과 등 표면 사이 틈 0~3cm.
// ③ **든 도구가 손에 보인다** — 손 소켓(`Socket_Tool`)에 붙고 그 자리가 손 뼈 곁이다.
// 자세는 게임이 서 있을 때 거는 클립(`A_Player_Idle`) 첫 프레임에서 잰다 — 시험 월드는 틱이 없어
// 그냥 두면 참조 자세(T자)에 머문다(`RodBackpack` 시험이 같은 함정을 이미 밟았다).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Player.Look; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "SnarehavenCharacter.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Snarehaven.h"

namespace SnarehavenPlayerLookTest
{
	// 레퍼런스 시트 `character_player.md` — "2.5등신. 머리가 전체 높이의 약 40%".
	// 폭은 "약" 을 ±4%p 로 읽었다(2.38~2.78등신) — 그 밖이면 실루엣이 2.5등신으로 안 보인다.
	constexpr double HeadFractionTarget = 0.40;
	constexpr double HeadFractionSlack = 0.04;

	// DESIGN 3.5 하이킹 배낭 행 — 가슴 뼈에서 등 표면까지(메시 공간 cm). `setup_equipment_socket.py` BACK_SURFACE_CM 과 같다.
	constexpr double BackSurfaceCm = 10.2;
	constexpr double BackGapMaxCm = 3.0;
	constexpr double FloatSlackCm = 0.05;   // 틈 0 은 경계라 부동소수 오차만큼만 봐준다 — 뚫림(음수)은 그대로 잡힌다
	// 손에 든 것이 "손에 있다" 고 할 거리 — 손도끼 한 뼘(45cm, DESIGN 3.5 도구 크기)의 절반. 검사 전용 여유다.
	constexpr double HandNearCm = 25.0;

	const FName CrownSocket(TEXT("Socket_Head"));   // 정수리 — `setup_equipment_socket.py` 가 몸 경계 맨 위에 잡았다

	const TCHAR* const IdlePath = TEXT("/Game/Game/Characters/Player/Snare/A_Player_Idle.A_Player_Idle");

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

	/** 클립 첫 프레임 자세로 뼈를 놓는다 — 시험 월드는 틱이 없어 한 번 평가해야 화면과 같은 자세가 된다. */
	bool SetIdlePose(USkeletalMeshComponent* Body)
	{
		UAnimSequence* const Anim = LoadObject<UAnimSequence>(nullptr, IdlePath);
		if (Anim == nullptr)
		{
			return false;
		}
		Body->PlayAnimation(Anim, false);
		Body->SetPosition(0.f, false);
		Body->TickAnimation(0.f, false);
		Body->RefreshBoneTransforms();
		return true;
	}

	/** 메시 로컬 상자 8 꼭짓점을 월드로 옮겨 Direction 방향으로 가장 먼 값 — 월드 AABB 는 돌면 부푼다. */
	double FarthestAlong(const UStaticMeshComponent* Visual, const FVector& Direction)
	{
		const FBox Local = Visual->GetStaticMesh()->GetBoundingBox();
		double Best = -UE_BIG_NUMBER;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Point((Corner & 1) ? Local.Max.X : Local.Min.X, (Corner & 2) ? Local.Max.Y : Local.Min.Y,
				(Corner & 4) ? Local.Max.Z : Local.Min.Z);
			Best = FMath::Max(Best, FVector::DotProduct(Visual->GetComponentTransform().TransformPosition(Point), Direction));
		}
		return Best;
	}

	/** 목 뼈 이름을 지어내지 않고 스켈레톤에서 읽는다 — 머리 뼈의 부모가 목이다. */
	FName NeckBone(const USkeletalMeshComponent* Body)
	{
		return Body->GetParentBone(TEXT("Head"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerLookTest, "Snarehaven.Player.Look.MatchesReferenceSheet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerLookTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenPlayerLookTest;

	UWorld* const World = MakeWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	UEquipmentComponent* const Equipment = Player ? Player->GetEquipment() : nullptr;
	if (!TestNotNull(TEXT("장비 컴포넌트가 있다"), Equipment)
		|| !TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		DestroyWorld(World);
		return false;
	}
	USkeletalMeshComponent* const Body = Player->GetMesh();
	if (!TestTrue(TEXT("서 있기 클립 첫 프레임 자세"), SetIdlePose(Body)))
	{
		DestroyWorld(World);
		return false;
	}
	Equipment->RefreshEquippedVisuals();   // 몸이 선 뒤에야 소켓이 있다 (게임에서도 BeginPlay 가 이 순서다)

	const double BodyScale = Body->GetComponentScale().Z;
	const FVector Forward = Player->GetActorForwardVector();

	// ① 2.5등신 — 시트 「머리가 전체 높이의 약 40%」.
	const FName Neck = NeckBone(Body);
	if (!TestFalse(TEXT("몸 스켈레톤에 목 뼈(머리 뼈의 부모)가 있다"), Neck.IsNone()))
	{
		DestroyWorld(World);
		return false;
	}
	const double FootZ = Player->GetActorLocation().Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	// 정수리는 `Socket_Head` 로 잰다 — `setup_equipment_socket.py` 가 **몸 경계 맨 위**에 잡아 둔 자리다.
	// 컴포넌트 경계(`CalcBounds`)는 자세에 따라 엔진이 여유를 덧대 실제보다 크게 나온다(첫 실행 182.4cm).
	if (!TestTrue(TEXT("몸에 정수리 소켓(Socket_Head)이 있다"), Body->DoesSocketExist(CrownSocket)))
	{
		DestroyWorld(World);
		return false;
	}
	const double TopZ = Body->GetSocketLocation(CrownSocket).Z;
	const double NeckZ = Body->GetBoneLocation(Neck).Z;
	const double TotalHeight = TopZ - FootZ;
	const double HeadHeight = TopZ - NeckZ;
	const double HeadFraction = HeadHeight / FMath::Max(TotalHeight, UE_KINDA_SMALL_NUMBER);
	UE_LOG(LogSnarehaven, Display,
		TEXT("[PlayerLook] 키 %.1fcm (발 %.1f → 정수리 소켓 %.1f) · 머리 %.1fcm (목 뼈 %s z %.1f) · 머리 몫 %.1f%% = %.2f등신"),
		TotalHeight, FootZ, TopZ, HeadHeight, *Neck.ToString(), NeckZ, HeadFraction * 100.0, 1.0 / HeadFraction);
	TestTrue(FString::Printf(TEXT("머리 몫 %.1f%% 가 시트의 %.0f±%.0f%% (2.5등신)"),
		HeadFraction * 100.0, HeadFractionTarget * 100.0, HeadFractionSlack * 100.0),
		FMath::Abs(HeadFraction - HeadFractionTarget) <= HeadFractionSlack);

	// ② 등에 배낭 — **기본 겉모습**(×0.9)도 등 한가운데다. 크기는 `DefaultLook.BareHeadSmallBackpack` 이 본다.
	UStaticMeshComponent* const Pack = Equipment->GetEquippedMesh(EEquipSlot::Backpack);
	if (!TestNotNull(TEXT("기본 겉모습에 등 배낭이 있다"), Pack)
		|| !TestNotNull(TEXT("기본 배낭 메시"), Pack->GetStaticMesh().Get()))
	{
		DestroyWorld(World);
		return false;
	}
	Pack->UpdateComponentToWorld();   // 자세를 잡은 뒤라 가슴 뼈가 움직였다 — 배낭 상자를 그 자세로 옮긴다
	const FName ChestBone = Body->GetSocketBoneName(Pack->GetAttachSocketName());
	const FVector Chest = Body->GetBoneLocation(ChestBone);
	const FVector PackCenter = Pack->CalcBounds(Pack->GetComponentTransform()).Origin;
	const double Back = FVector::DotProduct(PackCenter - Chest, Forward);
	TestTrue(FString::Printf(TEXT("작은 기본 배낭 가운데가 가슴 뼈 %s 보다 등 쪽 (앞 내적 %.1f < 0)"), *ChestBone.ToString(), Back),
		Back < 0.0);
	const double BackSurface = FVector::DotProduct(Chest, Forward) - BackSurfaceCm * BodyScale;
	const double Gap = BackSurface - FarthestAlong(Pack, Forward);
	UE_LOG(LogSnarehaven, Display,
		TEXT("[PlayerLook] 작은 기본 배낭 — 배율 %s · 가슴 %s · 가운데 %s · 앞 내적 %.1f · 등 표면과 틈 %.2fcm"),
		*Pack->GetRelativeScale3D().ToCompactString(), *Chest.ToCompactString(), *PackCenter.ToCompactString(), Back, Gap);
	TestTrue(FString::Printf(TEXT("작은 기본 배낭 몸 쪽 면–등 표면 틈 %.2fcm 이 0~%.0f"), Gap, BackGapMaxCm),
		Gap >= -FloatSlackCm && Gap <= BackGapMaxCm);

	// ③ 든 도구가 손에 보인다 — 시트 「들고 있는 것이 곧 현재 도구」.
	const FName AxeRow = Equipment->FindEquipmentRowByItemID(FName(TEXT("item.hatchet")));
	if (TestFalse(TEXT("표에 손도끼 장비 행이 있다"), AxeRow.IsNone())
		&& TestTrue(TEXT("손도끼를 들었다"), Equipment->EquipItem(AxeRow)))
	{
		const UStaticMeshComponent* const Tool = Equipment->GetEquippedMesh(EEquipSlot::Tool);
		if (TestNotNull(TEXT("Tool 칸 메시 컴포넌트"), Tool))
		{
			const FName HandBone = Body->GetSocketBoneName(Tool->GetAttachSocketName());
			// 메시 가운데가 아니라 메시 상자까지 잰다 — 손도구는 손잡이 끝을 쥐어(−0.4×길이) 가운데가 길이에 따라 손에서 멀어진다(80cm 손도끼 32cm)
			const double ToHand = FMath::Sqrt(Tool->Bounds.GetBox().ComputeSquaredDistanceToPoint(Body->GetBoneLocation(HandBone)));
			UE_LOG(LogSnarehaven, Display, TEXT("[PlayerLook] 손도끼 소켓 %s(뼈 %s) · 손 뼈와 거리 %.1fcm"),
				*Tool->GetAttachSocketName().ToString(), *HandBone.ToString(), ToHand);
			TestTrue(FString::Printf(TEXT("든 도구가 손 뼈 %s 곁 %.1fcm (≤ %.0f)"), *HandBone.ToString(), ToHand, HandNearCm),
				ToHand <= HandNearCm);
		}
	}

	DestroyWorld(World);
	return true;
}

#endif
