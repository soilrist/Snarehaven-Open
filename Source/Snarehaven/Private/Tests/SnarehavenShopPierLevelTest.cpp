// M13-41 2층 검사 — 저장된 맵을 **실제로 불러와** 잡화상·부두를 잰다 (DESIGN 8.14 새 4행, 감독 3층 #0232):
// (a) `L_Ranch`·`L_Hunt` 에 놓인 포탈 `Body` 긴 변 600±1 · 판자 윗면 z = 액터 z ±1 (맵 저장값이 생성자 값을 덮던 결함),
// (b) `L_Ranch` 에서 캡슐(34·88)을 `PlayerStart` 부터 손님 자리(상인에서 카메라 쪽 114cm)까지 쓸어 막힘이 없고,
//     그 자리에서 상인까지 거리 ≤ `UZoneTravelComponent::GetTravelRangeCm()` (수레가 길을 막던 결함),
// (c) 그 손님 자리의 게임 카메라 위치(캐릭터 스프링암이 계산)에서 상인 머리·가슴까지 선이 좌판에 안 걸린다(차양에 가리던 결함).
// 맵은 검사 월드에 레벨 인스턴스로 스트리밍한다 — 게임 월드라 `OnConstruction` 이 다시 돌지 않아 **저장된 값**을 잰다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.ShopPierLevel; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Snarehaven.h"
#include "Camera/CameraComponent.h"
#include "Combat/MeleeAttackComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/SpringArmComponent.h"
#include "Ranch/RanchGameMode.h"
#include "SnarehavenCharacter.h"
#include "World/ShopVendorActor.h"
#include "World/ZonePortalActor.h"
#include "World/ZoneTravelComponent.h"

namespace SnarehavenShopPierLevelTest
{
	// DESIGN 8.14 표 값 — 코드 상수를 되읽지 않고 표를 따로 적어 대조한다.
	constexpr float PierLongSideCm = 600.f;
	constexpr float LevelToleranceCm = 1.f;
	// 「잡화상 액터 자리」 행: 카운터 앞 손님은 상인에서 수평 114cm (상인 캡슐 반지름 34 + 80).
	constexpr float CustomerSpotCm = 114.f;
	// ROADMAP M13-41 ④(b) 캡슐 — 플레이어 캡슐과 같아야 한다(아래에서 대조).
	constexpr float CapsuleRadiusCm = 34.f;
	constexpr float CapsuleHalfHeightCm = 88.f;
	// 상인 가슴 뼈 — `SK_Player_Body` 는 믹사모 뼈대(`Hips/Spine…/Head`, `setup_equipment_socket.py`).
	const FName HeadBone(TEXT("Head"));
	const FName ChestBone(TEXT("Spine2"));

	/** 맵을 게임 월드에 인스턴스로 스트리밍한다. 월드 BeginPlay 는 안 부른다 — 맵 액터의 게임 로직을 돌리지 않는다. */
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

	template <typename T>
	T* FindFirst(const ULevel* Level)
	{
		for (AActor* const Actor : Level->Actors)
		{
			if (T* const Found = Cast<T>(Actor))
			{
				return Found;
			}
		}
		return nullptr;
	}

	/** 목장 맵 + 게임모드가 세우는 자리의 상인 + 손님 자리의 플레이어. 실패하면 false. */
	struct FRanchScene
	{
		UWorld* World = nullptr;
		ULevel* Level = nullptr;
		const APlayerStart* Start = nullptr;
		AShopVendorActor* Vendor = nullptr;
		ASnarehavenCharacter* Player = nullptr;
		FVector CustomerSpot = FVector::ZeroVector;

		bool Build(FAutomationTestBase& Test)
		{
			World = CreateWorldWithLevel(TEXT("/Game/Game/Maps/L_Ranch"), Level);
			if (!Test.TestNotNull(TEXT("L_Ranch 스트리밍"), Level))
			{
				return false;
			}
			Start = FindFirst<APlayerStart>(Level);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Vendor = World->SpawnActor<AShopVendorActor>(AShopVendorActor::StaticClass(),
				ARanchGameMode::GetVendorSpawnTransform(false), Params);
			if (!Test.TestNotNull(TEXT("시작점"), Start) || !Test.TestNotNull(TEXT("상인"), Vendor))
			{
				return false;
			}
			// 손님 자리 = 상인 앞(액터 +X = 카메라 쪽) 114cm, 상인과 같은 캡슐 중심 높이.
			CustomerSpot = Vendor->GetActorLocation() + Vendor->GetActorForwardVector() * CustomerSpotCm;
			Player = World->SpawnActor<ASnarehavenCharacter>(ASnarehavenCharacter::StaticClass(),
				FTransform(CustomerSpot), Params);
			if (!Test.TestNotNull(TEXT("플레이어"), Player))
			{
				return false;
			}
			// 카메라 거리·각은 BeginPlay 가 건다(`ApplyCameraSettings`).
			Player->DispatchBeginPlay();
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShopPierLevelPierTest, "Snarehaven.ShopPierLevel.Pier.SavedSizeAndPlank",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShopPierLevelPierTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenShopPierLevelTest;

	for (const TCHAR* const LevelPackage : { TEXT("/Game/Game/Maps/L_Ranch"), TEXT("/Game/Game/Maps/L_Hunt") })
	{
		ULevel* Level = nullptr;
		UWorld* const World = CreateWorldWithLevel(LevelPackage, Level);
		const AZonePortalActor* const Portal = Level != nullptr ? FindFirst<AZonePortalActor>(Level) : nullptr;
		UStaticMeshComponent* const Body = Portal != nullptr ? Portal->GetBody() : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s 포탈 부두"), LevelPackage), Body))
		{
			DestroyTestWorld(World);
			return false;
		}

		// 긴 변 — 월드 상자(배율·회전 포함). 부두가 해안에 따라 X 나 Y 로 눕는다.
		const FBox Box = Body->Bounds.GetBox();
		const float LongSide = FMath::Max(Box.GetSize().X, Box.GetSize().Y);
		TestEqual(*FString::Printf(TEXT("%s 놓인 부두 긴 변 600±1"), LevelPackage), LongSide, PierLongSideCm, LevelToleranceCm);

		// 판자 윗면 — 액터 자리(배 타는 칸 가운데)에서 부두만 수직으로 쏜다.
		const FVector At = Portal->GetActorLocation();
		FHitResult Hit;
		const bool bHit = Body->LineTraceComponent(Hit, At + FVector(0.f, 0.f, 500.f), At - FVector(0.f, 0.f, 500.f),
			FCollisionQueryParams(SCENE_QUERY_STAT(ShopPierLevelPlank), false));
		TestTrue(*FString::Printf(TEXT("%s 액터 자리 아래 판자"), LevelPackage), bHit);
		TestEqual(*FString::Printf(TEXT("%s 판자 윗면 = 액터 z ±1"), LevelPackage),
			bHit ? static_cast<float>(Hit.ImpactPoint.Z) : TNumericLimits<float>::Max(), static_cast<float>(At.Z), LevelToleranceCm);
		UE_LOG(LogSnarehaven, Display, TEXT("[ShopPierLevel] %s 부두 긴 변 %.1f · 배율 %.3f · 판자 z %.1f · 액터 z %.1f"),
			LevelPackage, LongSide, Body->GetRelativeScale3D().X, bHit ? Hit.ImpactPoint.Z : 0.f, At.Z);

		DestroyTestWorld(World);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShopPierLevelReachTest, "Snarehaven.ShopPierLevel.Shop.CustomerReachesCounter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShopPierLevelReachTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenShopPierLevelTest;

	FRanchScene Scene;
	if (!Scene.Build(*this))
	{
		if (Scene.World != nullptr)
		{
			DestroyTestWorld(Scene.World);
		}
		return false;
	}
	const UCapsuleComponent* const Capsule = Scene.Player->GetCapsuleComponent();
	const UZoneTravelComponent* const Travel = Scene.Player->FindComponentByClass<UZoneTravelComponent>();
	if (!TestNotNull(TEXT("이동 컴포넌트"), Travel))
	{
		DestroyTestWorld(Scene.World);
		return false;
	}
	TestEqual(TEXT("플레이어 캡슐 반지름 34"), Capsule->GetScaledCapsuleRadius(), CapsuleRadiusCm);
	TestEqual(TEXT("플레이어 캡슐 반높이 88"), Capsule->GetScaledCapsuleHalfHeight(), CapsuleHalfHeightCm);
	TestEqual(TEXT("상인이 카메라 쪽(세계 −X)을 본다"), static_cast<float>(Scene.Vendor->GetActorForwardVector().X), -1.f, 0.01f);

	// 시작점에서 손님 자리까지 — 시작점 높이 그대로 수평으로 쓴다(바닥에 긁히지 않게). 폰 채널 반응은 플레이어 캡슐 것.
	const FVector From = Scene.Start->GetActorLocation();
	const FVector To(Scene.CustomerSpot.X, Scene.CustomerSpot.Y, From.Z);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ShopPierLevelSweep), false);
	Query.AddIgnoredActor(Scene.Player);
	Query.AddIgnoredActor(Scene.Start);
	FHitResult Hit;
	const bool bBlocked = Scene.World->SweepSingleByChannel(Hit, From, To, FQuat::Identity, Capsule->GetCollisionObjectType(),
		FCollisionShape::MakeCapsule(CapsuleRadiusCm, CapsuleHalfHeightCm), Query,
		FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
	const FString Blocker = bBlocked && Hit.GetComponent() != nullptr
		? FString::Printf(TEXT("%s.%s"), *GetNameSafe(Hit.GetActor()), *Hit.GetComponent()->GetName()) : FString(TEXT("없음"));
	TestFalse(*FString::Printf(TEXT("시작점→손님 자리 막힘 없음 (막은 것: %s)"), *Blocker), bBlocked);

	const FVector Reached = bBlocked ? Hit.Location : To;
	const float Distance = FVector::Dist(Reached, Scene.Vendor->GetActorLocation());
	const float RangeCm = Scene.Player->GetMeleeAttack()->GetNpcTalkRangeCm();
	TestTrue(*FString::Printf(TEXT("손님 자리→상인 %.1f ≤ 사거리 %.1f"), Distance, RangeCm), RangeCm > 0.f && Distance <= RangeCm);
	UE_LOG(LogSnarehaven, Display, TEXT("[ShopPierLevel] 시작점 %s → 손님 자리 %s 막힘 %s (%s) · 상인 %s 까지 %.1f / 사거리 %.1f"),
		*From.ToCompactString(), *To.ToCompactString(), bBlocked ? TEXT("있음") : TEXT("없음"), *Blocker,
		*Scene.Vendor->GetActorLocation().ToCompactString(), Distance, RangeCm);

	DestroyTestWorld(Scene.World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShopPierLevelSightTest, "Snarehaven.ShopPierLevel.Shop.CameraSeesMerchant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShopPierLevelSightTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenShopPierLevelTest;

	FRanchScene Scene;
	if (!Scene.Build(*this))
	{
		if (Scene.World != nullptr)
		{
			DestroyTestWorld(Scene.World);
		}
		return false;
	}
	USpringArmComponent* const Boom = Scene.Player->GetCameraBoom();
	const UCameraComponent* const Camera = Scene.Player->GetTopDownCamera();
	const USkeletalMeshComponent* const Body = Scene.Vendor->GetBody();
	UStaticMeshComponent* const Stall = Scene.Vendor->GetStall();
	if (!TestNotNull(TEXT("스프링암"), Boom) || !TestNotNull(TEXT("카메라"), Camera) || !TestNotNull(TEXT("상인 몸"), Body)
		|| !TestNotNull(TEXT("좌판"), Stall))
	{
		DestroyTestWorld(Scene.World);
		return false;
	}

	// 카메라 자리는 엔진 스프링암이 한 번 계산하게 한다(게임과 같은 거리·각 — 숫자를 여기서 짓지 않는다).
	Boom->TickComponent(0.f, LEVELTICK_All, nullptr);
	const FVector CameraAt = Camera->GetComponentLocation();
	TestTrue(TEXT("카메라가 손님보다 높다"), CameraAt.Z > Scene.CustomerSpot.Z);
	// 대조 — 같은 추적이 좌판 가운데로는 좌판에 걸린다(추적이 아무것도 못 맞히는 헛검사가 아님을 먼저 본다).
	FHitResult ControlHit;
	const FVector StallCenter = Stall->Bounds.Origin;
	TestTrue(TEXT("대조: 카메라→좌판 가운데 선은 좌판에 걸린다"), Stall->LineTraceComponent(ControlHit, CameraAt,
		StallCenter + (StallCenter - CameraAt).GetSafeNormal() * 500.f, FCollisionQueryParams(SCENE_QUERY_STAT(ShopPierLevelSight), true)));

	for (const FName Bone : { HeadBone, ChestBone })
	{
		TestTrue(*FString::Printf(TEXT("상인 몸에 %s 뼈"), *Bone.ToString()), Body->GetBoneIndex(Bone) != INDEX_NONE);
		const FVector Target = Body->GetBoneLocation(Bone);
		FHitResult Hit;
		const bool bBlocked = Stall->LineTraceComponent(Hit, CameraAt, Target,
			FCollisionQueryParams(SCENE_QUERY_STAT(ShopPierLevelSight), true));
		TestFalse(*FString::Printf(TEXT("카메라→상인 %s 선이 좌판에 안 걸림"), *Bone.ToString()), bBlocked);
		const FString BlockedAt = bBlocked ? FString::Printf(TEXT("있음 %s"), *Hit.ImpactPoint.ToCompactString()) : FString(TEXT("없음"));
		UE_LOG(LogSnarehaven, Display, TEXT("[ShopPierLevel] 카메라 %s → 상인 %s %s 좌판 걸림 %s"),
			*CameraAt.ToCompactString(), *Bone.ToString(), *Target.ToCompactString(), *BlockedAt);
	}

	DestroyTestWorld(Scene.World);
	return true;
}

#endif
