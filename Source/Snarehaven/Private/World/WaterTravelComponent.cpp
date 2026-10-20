#include "World/WaterTravelComponent.h"
#include "SnareCheckSwitch.h"
#include "SnarehavenCharacter.h"
#include "Snarehaven.h"
#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Building/SnareBaseSubsystem.h"
#include "Building/SnareBuildRules.h"
#include "Building/SnarePlacedStructureActor.h"
#include "Combat/MeleeAttackComponent.h"
#include "Components/CapsuleComponent.h"
#include "Data/BaseRuleRow.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Gathering/GatherableActor.h"
#include "Gathering/TapFishingComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "Player/PlayerVitalsComponent.h"
#include "Ranch/RanchSlotActor.h"
#include "UI/PauseMenuWidget.h"
#include "UI/WaterActionWidget.h"
#include "World/BoatActor.h"
#include "World/FishingSpotActor.h"
#include "World/SeaActor.h"
#include "World/ShopVendorActor.h"
#include "World/SnareSound.h"
#include "World/WaterRules.h"

namespace
{
	const FName LifeboatItemId(TEXT("item.lifeboat"));
	const FName BoatMotorItemId(TEXT("item.boat_motor"));
	const FName ElectricBoatMotorItemId(TEXT("item.boat_motor_electric"));
	// 검사 스위치 `-SnareStandNear=` 값(밸런스 아님) — 채집물이 설 때까지 기다리는 초, 채집물에서 떨어져 설 거리.
	constexpr float TestStandNearWaitSeconds = 10.f;
	constexpr float TestStandNearGapCm = 120.f;
	/** `-SnareStandNearRepeat` — 근처 채집물이 다 베이면 이만큼(cm) 걸어 나가 새 채집물 액터를 세운다(숲 나무는 사람 곁에만 액터가 된다). */
	constexpr float TestStandNearHopCm = 2500.f;
	constexpr float TestStandNearHopTurnDeg = 70.f;
	constexpr float TestStandNearHopAfterSeconds = 2.f;
/** `-SnareHuntHerd` — 폰이 선 뒤 이만큼 기다렸다(동물이 서는 시간) 옮기고, 이 안에 동물이 모자라면 있는 만큼만 옮긴다. */
constexpr float TestHuntHerdSettleSeconds = 3.f;
constexpr float TestHuntHerdGiveUpSeconds = 12.f;
constexpr int32 TestHuntHerdMinAnimals = 3;
constexpr int32 TestHuntHerdMaxAnimals = 4;
	// `-SnareStandNear=fishing.<spotId>` — 낚시 포인트는 물 위라 옆에 서면 헤엄친다. 포인트에서 이 거리부터
	// 던질 거리 안쪽까지 바깥으로 넓혀 가며 해안선에서 이만큼 안쪽인 뭍을 찾는다(물가 헤엄 판정을 비킨다).
	const FString TestStandNearFishingPrefix(TEXT("fishing."));
	constexpr float TestStandNearFishingMinCm = 150.f;
	constexpr float TestStandNearFishingStepCm = 25.f;
	constexpr float TestStandNearFishingInlandCm = 40.f;
	constexpr int32 TestStandNearFishingDirections = 24;
	// `-SnareStandNear=animal.<동물 행 이름>` — 포획 행 실측(M28-09c). `-SnareChase` 만으로는 장애물에 막혀
	// 동물까지 못 좁힌다(루프#437). 이름이 비면 아무 동물이나 가장 가까운 것.
	const FString TestStandNearAnimalPrefix(TEXT("animal."));
	// `-SnareStandNear=ranch.<칸 번호>`·`vendor.<상점 종류>` — 목장 칸·상인 행 실측(M28-09d). 값이 비면 가장 가까운 것.
	const FString TestStandNearRanchPrefix(TEXT("ranch."));
	const FString TestStandNearVendorPrefix(TEXT("vendor."));
	// 목장 칸은 바닥(Z 0)에 서고 고르기는 3D 거리(`URanchInteractComponent::FindNearestSlotInRange`)라
	// 120cm 떨어지면 캡슐 높이 95 와 합쳐 사거리 150 밖이다 — 칸은 이만큼만 떨어져 선다.
	constexpr float TestStandNearRanchGapCm = 60.f;
	// `-SnareStandNear=structure.<아이템 ID>` — 놓인 가구 행 실측(M28-09d2). 가구는 서버 집 스냅샷으로만 서서
	// 오프라인엔 없다 — 기다려도 없으면 내 화면에만 하나 세운다. 세울 땅은 몸에서 이 반지름들 위 방향을 돌며
	// 목장 칸·상인·다른 가구가 이만큼 안에 없는 첫 자리(상황 버튼이 그것들을 먼저 고르지 않게).
	const FString TestStandNearStructurePrefix(TEXT("structure."));
	constexpr float TestStandNearStructureRingMinCm = 400.f;
	constexpr float TestStandNearStructureRingMaxCm = 1200.f;
	constexpr float TestStandNearStructureRingStepCm = 200.f;
	constexpr int32 TestStandNearStructureDirections = 16;
	constexpr float TestStandNearStructureClearCm = 500.f;
	// 가구 가장자리에서 떨어져 설 거리 — 가구 고르기는 가운데까지 사거리 + 반 발자국이다.
	constexpr float TestStandNearStructureEdgeGapCm = 60.f;
}

UWaterTravelComponent::UWaterTravelComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UWaterTravelComponent::BeginPlay()
{
	Super::BeginPlay();
	WaterTable = LoadObject<UDataTable>(nullptr, SnareWater::TablePath);
	CachedRow = SnareWater::FindRow(WaterTable);
}

// `-SnareMates=` 동료 폰(PlayerState 는 있고 조종자는 없음)은 검사 순간이동을 스스로 타지 않고 내 순간이동을 같은 만큼
// 따라온다 — 스스로 타면 같은 대상 옆 한 자리로 겹치고(10-18 ③), 안 따라오면 시작점에 남는다. 검사 스위치 전용.
static void MoveTestMatesBy(UWorld* World, const APawn& Leader, const FVector& Delta)
{
	for (TActorIterator<ASnarehavenCharacter> It(World); World != nullptr && It; ++It)
	{
		if (*It != &Leader && It->GetController() == nullptr)
		{
			It->SetActorLocationAndRotation(It->GetActorLocation() + Delta, Leader.GetActorRotation(), false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
}

void UWaterTravelComponent::ApplyTestTeleportOnce()
{
	// 3층 검사 스위치 `-SnareWaterAt=X,Y` — 호스트 폰을 그 자리(바다)로 옮겨 수영을 바로 보인다.
	// 폰은 BeginPlay 뒤에 빙의되므로 조종이 붙은 첫 틱에 한 번만 본다.
	APawn* const Pawn = Cast<APawn>(GetOwner());
	const ASeaActor* const Sea = GetSea();
	if (bTestTeleportChecked || !Pawn || !Pawn->IsPlayerControlled() || Pawn->GetController() == nullptr || !Sea)
	{
		return;
	}
	bTestTeleportChecked = true;
	FString WaterAt, X, Y;
	if (Pawn->HasAuthority() && SnareCheckSwitch::Value(TEXT("SnareWaterAt="), WaterAt, false) && WaterAt.Split(TEXT(","), &X, &Y))
	{
		const FVector To(FCString::Atof(*X), FCString::Atof(*Y), Sea->GetSurfaceZ());
		Pawn->SetActorLocation(To, false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogSnarehaven, Log, TEXT("[Swim] 검사용 순간이동 (-SnareWaterAt=%s) → %s"), *WaterAt, *To.ToString());
		// `-SnareLaunchBoat` — 그 자리에 배를 바로 띄워 태운다. 배낭 차감(`LaunchBoat`)을 건너뛰는 자리표시
		// (`-SnareMountNow` 와 같은 결 — 백엔드 안 건드림). 배를 띄운 모습 3층 캡처용.
		if (SnareCheckSwitch::Param(TEXT("SnareLaunchBoat")))
		{
			const ABoatActor* const Boat = SpawnBoatOnAuthority(0);
			UE_LOG(LogSnarehaven, Log, TEXT("[Boat] 검사용 띄움 (-SnareLaunchBoat) → %s"), *GetNameSafe(Boat));
		}
#if !UE_BUILD_SHIPPING
		// `-SnareBoatSoundAt=2/3/4/5` — 3층 소리 확인 전용 (M51-06). 적은 초마다 띄우기(서버가 받아들인 답)→내리기→타기→넣기
		// (받아들인 답) 순으로 사람 길과 같은 함수를 부른다. 배낭·재화는 안 건드린다 (CLAUDE.md 8). 쉼표는 `FParse::Value` 가 끊으므로 `/`.
		FString BoatAt;
		if (SnareCheckSwitch::Value(TEXT("SnareBoatSoundAt="), BoatAt))
		{
			TArray<FString> Seconds;
			BoatAt.ParseIntoArray(Seconds, TEXT("/"));
			for (int32 Index = 0; Index < Seconds.Num(); ++Index)
			{
				FTimerHandle Unused;
				GetWorld()->GetTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this, Index]()
				{
					const FVector Where = GetOwner()->GetActorLocation();
					switch (Index % 4)
					{
					case 0: PlayBoatCue(SnareSound::EBoatAction::Launch, true, FString(), Where); break;
					case 1: LeaveOnAuthority(); break;
					case 2: BoardOnAuthority(FindBoardableBoat()); break;
					default: PlayBoatCue(SnareSound::EBoatAction::Stow, true, FString(), Where); break;
					}
				}), FCString::Atof(*Seconds[Index]), false);
			}
		}
#endif
	}
	// `-SnareStandAt=X,Y` — 같은 순간이동을 땅 위로. 높이는 시작점 그대로(존 바닥은 모두 Z=0 평면)라
	// 수면으로 내리지 않는다 — 수면 높이로 땅에 박으면 겹침 풀기가 캡슐을 바닥 아래로 밀 수 있다.
	FString StandAt;
	if (Pawn->HasAuthority() && SnareCheckSwitch::Value(TEXT("SnareStandAt="), StandAt, false) && StandAt.Split(TEXT(","), &X, &Y))
	{
		const FVector To(FCString::Atof(*X), FCString::Atof(*Y), Pawn->GetActorLocation().Z);
		const FVector Delta = To - Pawn->GetActorLocation();
		Pawn->SetActorLocation(To, false, nullptr, ETeleportType::TeleportPhysics);
		MoveTestMatesBy(GetWorld(), *Pawn, Delta);
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 순간이동 (-SnareStandAt=%s) → %s"), *StandAt, *To.ToString());
	}
}

void UWaterTravelComponent::ApplyTestStandNear(float DeltaTime)
{
	// 3층 검사 스위치 `-SnareStandNear=<채집물 ID>` (M28-09 전수 표 실측) — 광맥·덤불처럼 자리를 외우기 힘든
	// 채집물 앞에 세운다. 채집물은 게임 모드가 첫 틱 뒤에 세우기도 해서 몇 초 동안 다시 찾는다.
	if (TestStandNearSecondsLeft == 0.f)
	{
		return;
	}
	APawn* const Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsPlayerControlled() || Pawn->GetController() == nullptr || !Pawn->HasAuthority())
	{
		return;
	}
	FString WantedID;
	if (!SnareCheckSwitch::Value(TEXT("SnareStandNear="), WantedID, false))
	{
		TestStandNearSecondsLeft = 0.f;
		return;
	}
	if (TestStandNearSecondsLeft < 0.f)
	{
		TestStandNearSecondsLeft = TestStandNearWaitSeconds;
	}
	if (WantedID.StartsWith(TestStandNearFishingPrefix))
	{
		TestStandNearSecondsLeft = 0.f;
		ApplyTestStandNearFishingSpot(*Pawn, WantedID.RightChop(TestStandNearFishingPrefix.Len()));
		return;
	}
	if (WantedID.StartsWith(TestStandNearAnimalPrefix))
	{
		ApplyTestStandNearAnimal(*Pawn, WantedID.RightChop(TestStandNearAnimalPrefix.Len()), DeltaTime);
		return;
	}
	if (WantedID.StartsWith(TestStandNearRanchPrefix) || WantedID.StartsWith(TestStandNearVendorPrefix))
	{
		ApplyTestStandNearPlace(*Pawn, WantedID, DeltaTime);
		return;
	}
	if (WantedID.StartsWith(TestStandNearStructurePrefix))
	{
		ApplyTestStandNearStructure(*Pawn, FName(*WantedID.RightChop(TestStandNearStructurePrefix.Len())), DeltaTime);
		return;
	}
	// `-SnareStandNearRepeat` — 한 그루로 끝내지 않고, 앞의 것이 베이면 다음 가까운 것으로 옮긴다(스토리 건네기 재료 모으기).
	const bool bRepeat = SnareCheckSwitch::Param(TEXT("SnareStandNearRepeat"));
	// 몸이 밀려 사거리(150cm) 밖으로 나가면(10-16 #1228: 곁의 꿩을 맞고 157cm) 손이 쉬므로 다시 붙여 세운다.
	if (bRepeat && TestStandNearTarget.IsValid() && !TestStandNearTarget->IsGathered())
	{
		TestStandNearSecondsLeft = TestStandNearWaitSeconds;
		if (FVector::DistSquared2D(Pawn->GetActorLocation(), TestStandNearTarget->GetActorLocation()) <= FMath::Square(TestStandNearGapCm * 1.1f))
		{
			return;
		}
	}
	const FVector From = Pawn->GetActorLocation();
	const FName Wanted(*WantedID);
	AGatherableActor* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<AGatherableActor> It(GetWorld()); It; ++It)
	{
		const float DistSq = FVector::DistSquared2D(From, It->GetActorLocation());
		if (It->GetGatherableID() == Wanted && !It->IsGathered() && DistSq < NearestDistSq)
		{
			Nearest = *It;
			NearestDistSq = DistSq;
		}
	}
	if (!Nearest && bRepeat)
	{
		// 근처에 남은 게 없다 — 잠깐 기다려 새로 세워지길 보고, 그래도 없으면 방향을 돌려 가며 한 칸 옮긴다.
		TestStandNearNoneSeconds += DeltaTime;
		if (TestStandNearNoneSeconds >= TestStandNearHopAfterSeconds)
		{
			TestStandNearNoneSeconds = 0.f;
			const FVector Dir = FRotator(0.f, TestStandNearHopTurnDeg * TestStandNearHopCount, 0.f).RotateVector(FVector::ForwardVector);
			++TestStandNearHopCount;
			const FVector To = From + Dir * TestStandNearHopCm;
			Pawn->SetActorLocation(To, false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 순간이동 (-SnareStandNearRepeat) — 근처에 %s 없음, 숲 안쪽으로 %d번째 이동 → %s"), *WantedID, TestStandNearHopCount, *To.ToString());
		}
		return;
	}
	TestStandNearNoneSeconds = 0.f;
	if (!Nearest)
	{
		TestStandNearSecondsLeft = FMath::Max(TestStandNearSecondsLeft - DeltaTime, 0.f);
		if (TestStandNearSecondsLeft == 0.f)
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("검사용 순간이동 (-SnareStandNear=%s) — 그 채집물이 맵에 없다"), *WantedID);
		}
		return;
	}
	TestStandNearSecondsLeft = bRepeat ? TestStandNearWaitSeconds : 0.f;
	TestStandNearTarget = Nearest;
	// 채집물에서 지금 선 쪽으로 한 발 떨어진 자리, 얼굴은 채집물로 — 상황 버튼은 앞에 있는 것을 고른다.
	const FVector Target = Nearest->GetActorLocation();
	FVector Away = (From - Target).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = FVector::ForwardVector;
	}
	const FVector To(Target.X + Away.X * TestStandNearGapCm, Target.Y + Away.Y * TestStandNearGapCm, From.Z);
	const FRotator Facing(0.f, (-Away).Rotation().Yaw, 0.f);
	Pawn->SetActorLocationAndRotation(To, Facing, false, nullptr, ETeleportType::TeleportPhysics);
	if (AController* const Controller = Pawn->GetController())
	{
		Controller->SetControlRotation(Facing);
	}
	UE_LOG(LogSnarehaven, Log, TEXT("검사용 순간이동 (-SnareStandNear=%s) → %s · 채집물 %s %s"),
		*WantedID, *To.ToString(), *Nearest->GetName(), *Target.ToString());
}

void UWaterTravelComponent::ApplyTestHuntHerd(float DeltaTime)
{
	if (TestHuntHerdWaitedSeconds < 0.f)
	{
		return;
	}
	APawn* const Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsPlayerControlled() || Pawn->GetController() == nullptr || !Pawn->HasAuthority())
	{
		return;
	}
	if (!AAnimalCharacter::ShouldHuntHerd())
	{
		TestHuntHerdWaitedSeconds = -1.f;
		return;
	}
	TestHuntHerdWaitedSeconds += DeltaTime;
	if (TestHuntHerdWaitedSeconds < TestHuntHerdSettleSeconds)
	{
		return;
	}

	// 가까운 순으로 산 동물을 모은다 — 동물은 게임 모드·분산 액터가 첫 틱 뒤에 세우므로 모자라면 다음 틱에 다시 본다.
	const FVector From = Pawn->GetActorLocation();
	TArray<AAnimalCharacter*> Animals;
	for (TActorIterator<AAnimalCharacter> It(GetWorld()); It; ++It)
	{
		const UAnimalStateComponent* const State = It->GetAnimalState();
		if (State && !State->IsFinished())
		{
			Animals.Add(*It);
		}
	}
	const bool bGaveUp = TestHuntHerdWaitedSeconds >= TestHuntHerdGiveUpSeconds;
	if (Animals.Num() < TestHuntHerdMinAnimals && !bGaveUp)
	{
		return;
	}
	Animals.Sort([&From](const AAnimalCharacter& A, const AAnimalCharacter& B)
	{
		return FVector::DistSquared2D(From, A.GetActorLocation()) < FVector::DistSquared2D(From, B.GetActorLocation());
	});
	TestHuntHerdWaitedSeconds = -1.f;

	const float ReachCm = GetInteractRangeCm();
	const int32 Count = FMath::Min(Animals.Num(), TestHuntHerdMaxAnimals);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Spot = AAnimalCharacter::HuntHerdSpot(From, Pawn->GetActorForwardVector(), Index, ReachCm);
		Animals[Index]->SetActorLocation(FVector(Spot.X, Spot.Y, Animals[Index]->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 동물 모으기 (-SnareHuntHerd) %d번 %s → %s"), Index, *Animals[Index]->GetAnimalRowName().ToString(), *Spot.ToString());
	}
	if (Count < TestHuntHerdMinAnimals)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("검사용 동물 모으기 (-SnareHuntHerd) — 동물이 %d마리뿐이다"), Count);
	}
}

void UWaterTravelComponent::ApplyTestStandNearAnimal(APawn& Pawn, const FString& RowName, float DeltaTime)
{
	// 동물도 게임 모드가 첫 틱 뒤에 세운다 — 채집물과 같이 몇 초 다시 찾는다.
	const FVector From = Pawn.GetActorLocation();
	AAnimalCharacter* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<AAnimalCharacter> It(GetWorld()); It; ++It)
	{
		const UAnimalStateComponent* const State = It->GetAnimalState();
		const float DistSq = FVector::DistSquared2D(From, It->GetActorLocation());
		if (State && !State->IsFinished() && (RowName.IsEmpty() || It->GetAnimalRowName() == FName(*RowName)) && DistSq < NearestDistSq)
		{
			Nearest = *It;
			NearestDistSq = DistSq;
		}
	}
	if (!Nearest)
	{
		TestStandNearSecondsLeft = FMath::Max(TestStandNearSecondsLeft - DeltaTime, 0.f);
		if (TestStandNearSecondsLeft == 0.f)
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("검사용 순간이동 (-SnareStandNear=animal.%s) — 그 동물이 맵에 없다"), *RowName);
		}
		return;
	}
	TestStandNearSecondsLeft = 0.f;
	StandBesideForTest(Pawn, *Nearest, FString::Printf(TEXT("animal.%s"), *RowName), TestStandNearGapCm);
	UE_LOG(LogSnarehaven, Log, TEXT("검사용 순간이동 — 동물 행 %s"), *Nearest->GetAnimalRowName().ToString());
	if (ASnarehavenCharacter* const Snare = Cast<ASnarehavenCharacter>(&Pawn))
	{
		Snare->SetCheckChaseTargetForTest(Nearest);
	}
}

void UWaterTravelComponent::ApplyTestStandNearPlace(APawn& Pawn, const FString& WantedID, float DeltaTime)
{
	// 목장 칸·상인도 게임 모드가 첫 틱 뒤에 세운다 — 채집물과 같이 몇 초 다시 찾는다.
	const bool bRanch = WantedID.StartsWith(TestStandNearRanchPrefix);
	const FString Value = WantedID.RightChop(bRanch ? TestStandNearRanchPrefix.Len() : TestStandNearVendorPrefix.Len());
	const FVector From = Pawn.GetActorLocation();
	const AActor* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	auto Consider = [&](const AActor& Candidate, bool bMatches)
	{
		const float DistSq = FVector::DistSquared2D(From, Candidate.GetActorLocation());
		if (bMatches && DistSq < NearestDistSq)
		{
			Nearest = &Candidate;
			NearestDistSq = DistSq;
		}
	};
	if (bRanch)
	{
		for (TActorIterator<ARanchSlotActor> It(GetWorld()); It; ++It)
		{
			// 숨은 칸은 서버가 아직 안 연 확장 칸이라 상황 버튼도 안 고른다 — 같이 건너뛴다.
			Consider(**It, !It->IsHidden() && (Value.IsEmpty() || It->GetSlotIndex() == FCString::Atoi(*Value)));
		}
	}
	else
	{
		for (TActorIterator<AShopVendorActor> It(GetWorld()); It; ++It)
		{
			const FString Kind = StaticEnum<EShopKind>()->GetNameStringByValue(static_cast<int64>(It->GetShopKind()));
			Consider(**It, Value.IsEmpty() || Kind.Equals(Value, ESearchCase::IgnoreCase));
		}
	}
	if (!Nearest)
	{
		TestStandNearSecondsLeft = FMath::Max(TestStandNearSecondsLeft - DeltaTime, 0.f);
		if (TestStandNearSecondsLeft == 0.f)
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("검사용 순간이동 (-SnareStandNear=%s) — 그 목장 칸·상인이 맵에 없다"), *WantedID);
		}
		return;
	}
	TestStandNearSecondsLeft = 0.f;
	StandBesideForTest(Pawn, *Nearest, WantedID, bRanch ? TestStandNearRanchGapCm : TestStandNearGapCm);
}

void UWaterTravelComponent::ApplyTestStandNearStructure(APawn& Pawn, FName ItemID, float DeltaTime)
{
	UWorld* const World = GetWorld();
	const FVector From = Pawn.GetActorLocation();
	ASnarePlacedStructureActor* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
	{
		const float DistSq = FVector::DistSquared2D(From, It->GetActorLocation());
		if (!It->IsPreview() && It->GetItemID() == ItemID && DistSq < NearestDistSq)
		{
			Nearest = *It;
			NearestDistSq = DistSq;
		}
	}
	if (!Nearest)
	{
		// 집 스냅샷이 늦게 오기도 한다 — 기다리는 동안은 다시 찾기만 한다.
		TestStandNearSecondsLeft = FMath::Max(TestStandNearSecondsLeft - DeltaTime, 0.f);
		if (TestStandNearSecondsLeft > 0.f)
		{
			return;
		}
		Nearest = SpawnLocalStructureForTest(Pawn, ItemID);
		if (!Nearest)
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("검사용 순간이동 (-SnareStandNear=structure.%s) — 그 가구를 세울 수 없다"),
				*ItemID.ToString());
			return;
		}
	}
	TestStandNearSecondsLeft = 0.f;
	const float HalfFootprintCm = 0.5f * Nearest->GetFootprint().GetMax() * FMath::Max(1.f, Nearest->GetCellSizeCm());
	StandBesideForTest(Pawn, *Nearest, FString::Printf(TEXT("structure.%s"), *ItemID.ToString()),
		HalfFootprintCm + TestStandNearStructureEdgeGapCm);
}

ASnarePlacedStructureActor* UWaterTravelComponent::SpawnLocalStructureForTest(APawn& Pawn, FName ItemID)
{
	UWorld* const World = GetWorld();
	const UGameInstance* const GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	const USnareBaseSubsystem* const Base = GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBaseSubsystem>() : nullptr;
	const FBaseRuleRow* const Rules = Base != nullptr ? Base->GetRules() : nullptr;
	if (Rules == nullptr || Rules->CellSizeCm <= 0.f)
	{
		return nullptr;
	}
	// 상황 버튼이 가구보다 먼저 고르는 것(목장 칸·상인)과 다른 가구에서 떨어진 땅.
	auto IsClear = [World](const FVector& Where)
	{
		const float ClearSq = FMath::Square(TestStandNearStructureClearCm);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			const bool bBlocks = (It->IsA<ARanchSlotActor>() && !It->IsHidden()) || It->IsA<AShopVendorActor>()
				|| It->IsA<ASnarePlacedStructureActor>();
			if (bBlocks && FVector::DistSquared2D(Where, It->GetActorLocation()) < ClearSq)
			{
				return false;
			}
		}
		return true;
	};
	const FVector From = Pawn.GetActorLocation();
	FVector Where = FVector::ZeroVector;
	bool bFound = false;
	for (float Radius = TestStandNearStructureRingMinCm; !bFound && Radius <= TestStandNearStructureRingMaxCm;
		Radius += TestStandNearStructureRingStepCm)
	{
		for (int32 Index = 0; !bFound && Index < TestStandNearStructureDirections; ++Index)
		{
			const float Yaw = 360.f * Index / TestStandNearStructureDirections;
			const FVector Candidate = From + FRotator(0.f, Yaw, 0.f).Vector() * Radius;
			bFound = IsClear(Candidate);
			Where = Candidate;
		}
	}
	if (!bFound)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	ASnarePlacedStructureActor* const Shown = World->SpawnActor<ASnarePlacedStructureActor>(
		ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
	if (!IsValid(Shown))
	{
		return nullptr;
	}
	Shown->SetReplicates(false);   // 내 화면에만 — 서버 사본이 없는 가구다(`-SnarePowerDemo` 와 같은 길).
	Shown->InitStructure(ItemID, TEXT("stand_near_test"), TEXT("stand_near_test_") + ItemID.ToString(), FString(), 0,
		Rules->CellSizeCm);
	Shown->FinishSpawning(FTransform(ASnarePlacedStructureActor::CellToWorld(World,
		SnareBuildRules::WorldToCell(Where, Rules->CellSizeCm), Shown->GetFootprint(), Rules->CellSizeCm)));
	UE_LOG(LogSnarehaven, Log, TEXT("검사용 놓인 가구 (-SnareStandNear=structure.%s) — 내 화면에만 세움 · 기능 %s · 발자국 %s · 자리 %s"),
		*ItemID.ToString(), *StaticEnum<EPlaceableFunction>()->GetNameStringByValue(static_cast<int64>(Shown->GetPlaceableFunction())),
		*Shown->GetFootprint().ToString(), *Shown->GetActorLocation().ToString());
	return Shown;
}

void UWaterTravelComponent::StandBesideForTest(APawn& Pawn, const AActor& Target, const FString& WantedID, float GapCm)
{
	const FVector From = Pawn.GetActorLocation();
	const FVector TargetLocation = Target.GetActorLocation();
	FVector Away = (From - TargetLocation).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = FVector::ForwardVector;
	}
	const FVector To(TargetLocation.X + Away.X * GapCm, TargetLocation.Y + Away.Y * GapCm, From.Z);
	const FRotator Facing(0.f, (-Away).Rotation().Yaw, 0.f);
	Pawn.SetActorLocationAndRotation(To, Facing, false, nullptr, ETeleportType::TeleportPhysics);
	MoveTestMatesBy(GetWorld(), Pawn, To - From);
	if (AController* const Controller = Pawn.GetController())
	{
		Controller->SetControlRotation(Facing);
	}
	UE_LOG(LogSnarehaven, Log, TEXT("검사용 순간이동 (-SnareStandNear=%s) → %s · 대상 %s %s"),
		*WantedID, *To.ToString(), *Target.GetName(), *TargetLocation.ToString());
}

void UWaterTravelComponent::ApplyTestStandNearFishingSpot(APawn& Pawn, const FString& SpotId)
{
	AFishingSpotActor* Spot = nullptr;
	for (TActorIterator<AFishingSpotActor> It(GetWorld()); It; ++It)
	{
		if (It->SpotId == SpotId)
		{
			Spot = *It;
			break;
		}
	}
	const ASeaActor* const Sea = GetSea();
	const UTapFishingComponent* const Fishing = Pawn.FindComponentByClass<UTapFishingComponent>();
	const float CastRangeCm = Fishing ? Fishing->GetCastRangeCm() : 0.f;
	if (!Spot || !Sea || CastRangeCm <= TestStandNearFishingMinCm)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("검사용 순간이동 (-SnareStandNear=fishing.%s) — 포인트·바다·던질 거리 중 없는 것이 있다"), *SpotId);
		return;
	}
	// 가까운 고리부터 — 같은 고리 안에서는 해안선에서 가장 안쪽인 자리.
	const FVector Target = Spot->GetActorLocation();
	for (float Radius = TestStandNearFishingMinCm; Radius < CastRangeCm; Radius += TestStandNearFishingStepCm)
	{
		FVector Best = FVector::ZeroVector;
		float BestCoast = -TestStandNearFishingInlandCm;
		for (int32 Index = 0; Index < TestStandNearFishingDirections; ++Index)
		{
			const float Angle = 2.f * PI * Index / TestStandNearFishingDirections;
			const FVector Probe(Target.X + Radius * FMath::Cos(Angle), Target.Y + Radius * FMath::Sin(Angle), Pawn.GetActorLocation().Z);
			const float Coast = Sea->CoastDistance(Probe);
			if (Coast < BestCoast)
			{
				Best = Probe;
				BestCoast = Coast;
			}
		}
		if (BestCoast < -TestStandNearFishingInlandCm)
		{
			const FRotator Facing(0.f, (Target - Best).GetSafeNormal2D().Rotation().Yaw, 0.f);
			Pawn.SetActorLocationAndRotation(Best, Facing, false, nullptr, ETeleportType::TeleportPhysics);
			if (AController* const Controller = Pawn.GetController())
			{
				Controller->SetControlRotation(Facing);
			}
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 순간이동 (-SnareStandNear=fishing.%s) → %s · 포인트 %s %s · 거리 %.0fcm · 해안선 안쪽 %.0fcm"),
				*SpotId, *Best.ToString(), *Spot->GetName(), *Target.ToString(), Radius, -BestCoast);
			return;
		}
	}
	UE_LOG(LogSnarehaven, Warning, TEXT("검사용 순간이동 (-SnareStandNear=fishing.%s) — 던질 거리 %.0fcm 안에 뭍이 없다"), *SpotId, CastRangeCm);
}

void UWaterTravelComponent::EnsureWidget()
{
	const APawn* const Pawn = Cast<APawn>(GetOwner());
	APlayerController* const PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	UWorld* const World = GetWorld();
	if (ActionWidget || !PC || !PC->IsLocalController() || !World || !UPauseMenuWidget::ShouldAllowPauseMenu(World->GetMapName()))
	{
		return;
	}
	ActionWidget = CreateWidget<UWaterActionWidget>(PC, UWaterActionWidget::StaticClass());
	if (ActionWidget)
	{
		ActionWidget->BindToPawn(this, Pawn->FindComponentByClass<UTapFishingComponent>());
		ActionWidget->AddToViewport();
	}
}

const FWaterActivityRow* UWaterTravelComponent::GetRow() const
{
	return CachedRow;
}

ASeaActor* UWaterTravelComponent::GetSea() const
{
	if (!IsValid(CachedSea))
	{
		const_cast<UWaterTravelComponent*>(this)->CachedSea = ASeaActor::FindSea(this);
	}
	return CachedSea;
}

float UWaterTravelComponent::GetInteractRangeCm() const
{
	const UMeleeAttackComponent* const Melee = GetOwner() ? GetOwner()->FindComponentByClass<UMeleeAttackComponent>() : nullptr;
	return Melee ? Melee->GetAttackRangeCm() : -1.f;
}

void UWaterTravelComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	EnsureWidget();
	ApplyTestTeleportOnce();
	ApplyTestStandNear(DeltaTime);
	ApplyTestHuntHerd(DeltaTime);
	UpdateWaterState();

	// 운전해서 섬에 닿으면 저절로 내린다 — 배를 뭍으로 끌고 다니지 않는다.
	const ASeaActor* const Sea = GetSea();
	if (GetOwner() && GetOwner()->HasAuthority() && IsDriving() && Sea && !Sea->IsOverSea(GetOwner()->GetActorLocation()))
	{
		LeaveOnAuthority();
	}
}

void UWaterTravelComponent::UpdateWaterState()
{
	const AActor* const Owner = GetOwner();
	const ASeaActor* const Sea = GetSea();
	const FWaterActivityRow* const Row = GetRow();
	bool bNowInWater = false;
	if (Owner && Sea && Row && GetRiddenBoat() == nullptr)
	{
		const FVector Location = Owner->GetActorLocation();
		const ACharacter* const Character = Cast<ACharacter>(Owner);
		const float FootZ = Location.Z - (Character ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f);
		bNowInWater = Sea->IsOverSea(Location) && FootZ < Sea->GetSurfaceZ();
	}
	if (bNowInWater != bInWater)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Swim] %s %s"), *GetNameSafe(Owner), bNowInWater ? TEXT("물에 들어감") : TEXT("물에서 나옴"));
		if (Owner != nullptr)
		{
			USnareSoundSubsystem::PlayAt(this, TEXT("Splash"), Owner->GetActorLocation(), bNowInWater ? 0.8f : 0.4f);
		}
		SwimStrokeSecondsLeft = 0.6f;
	}
	bInWater = bNowInWater;
	if (bInWater && Owner->GetVelocity().Size2D() > 50.f)
	{
		SwimStrokeSecondsLeft -= GetWorld()->GetDeltaSeconds();
		if (SwimStrokeSecondsLeft <= 0.f)
		{
			SwimStrokeSecondsLeft = 0.9f;   // 헤엄 클립 한 번 젓기
			USnareSoundSubsystem::PlayAt(this, TEXT("Swim"), Owner->GetActorLocation(), 0.5f);
		}
	}

	if (UPlayerVitalsComponent* const Vitals = Owner ? Owner->FindComponentByClass<UPlayerVitalsComponent>() : nullptr)
	{
		Vitals->SetSwimState(bInWater, Row ? Row->SwimStaminaPerSecond : 0.f, Row ? Row->DrownHealthPerSecond : 0.f);
	}
}

float UWaterTravelComponent::GetMoveSpeedMultiplier() const
{
	const FWaterActivityRow* const Row = GetRow();
	if (Row == nullptr)
	{
		return 1.f;
	}
	if (IsDriving())
	{
		return SnareWater::BoatSpeedMul(*Row, GetRiddenBoat()->GetMotorTier());
	}
	return bInWater ? Row->SwimSpeedMul : 1.f;
}

ABoatActor* UWaterTravelComponent::GetRiddenBoat() const
{
	return ABoatActor::FindBoatOf(Cast<APawn>(GetOwner()));
}

bool UWaterTravelComponent::IsDriving() const
{
	const ABoatActor* const Boat = GetRiddenBoat();
	return Boat && Boat->GetDriver() == GetOwner();
}

int32 UWaterTravelComponent::ReadOwnedMotorTier() const
{
	if (SnareWater::CountOwned(this, ElectricBoatMotorItemId) > 0)
	{
		return 2;
	}
	return SnareWater::CountOwned(this, BoatMotorItemId) > 0 ? 1 : 0;
}

// ── 띄우기 ──

bool UWaterTravelComponent::CanLaunchBoat() const
{
	const ASeaActor* const Sea = GetSea();
	const APawn* const Pawn = Cast<APawn>(GetOwner());
	if (bBusy || !Pawn || !Sea || !GetRow() || GetRiddenBoat() || FindStowableBoat())
	{
		return false;
	}
	// 🟡 물가(바다 쪽 얕은 곳 포함)에 서 있을 때만 — 원본 "물가에서 놓기".
	return Sea->IsOverSea(Pawn->GetActorLocation())
		&& (bServerBoatLaunched || SnareWater::CountOwned(this, LifeboatItemId) > 0);
}

void UWaterTravelComponent::LaunchBoat()
{
	if (!CanLaunchBoat())
	{
		return;
	}
	bBusy = true;
	TWeakObjectPtr<UWaterTravelComponent> WeakThis(this);
	SnareWater::CallServer(this, TEXT("LaunchBoat"), MakeShared<FJsonObject>(),
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>&, const FString& Error)
		{
			UWaterTravelComponent* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bBusy = false;
			// "already" = 서버엔 띄운 배가 있는데 월드엔 없다(맵을 옮겼다) — 배낭은 이미 빠졌으니 다시 띄워 준다.
			const FVector Where = Self->GetOwner() != nullptr ? Self->GetOwner()->GetActorLocation() : FVector::ZeroVector;
			Self->PlayBoatCue(SnareSound::EBoatAction::Launch, bOk, Error, Where);
			if (!bOk && Error != TEXT("already"))
			{
				Self->LastMessage = Error;
				return;
			}
			Self->bServerBoatLaunched = true;
			SnareWater::RefreshPlayerState(Self);
			Self->Server_SpawnBoat(Self->ReadOwnedMotorTier());
		});
}

void UWaterTravelComponent::Server_SpawnBoat_Implementation(int32 MotorTier)
{
	// ponytail: 호스트는 손님의 "서버가 허락했다" 를 믿는다(배 한 척·재화 이동 없음). 위조가 문제되면 호스트가 손님 표를 서버에 되묻는다.
	SpawnBoatOnAuthority(MotorTier);
}

ABoatActor* UWaterTravelComponent::SpawnBoatOnAuthority(int32 MotorTier)
{
	APawn* const Pawn = Cast<APawn>(GetOwner());
	const ASeaActor* const Sea = GetSea();
	const FWaterActivityRow* const Row = GetRow();
	UWorld* const World = GetWorld();
	if (!Pawn || !Sea || !Row || !World || GetRiddenBoat())
	{
		return nullptr;
	}
	if (ABoatActor* const Existing = FindStowableBoat())
	{
		Existing->SeatPawn(Pawn);
		return Existing;
	}
	const FVector At(Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y, Sea->GetSurfaceZ());
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ABoatActor* const Boat = World->SpawnActor<ABoatActor>(At, FRotator(0.f, Pawn->GetActorRotation().Yaw, 0.f), Params);
	if (Boat == nullptr)
	{
		return nullptr;
	}
	Boat->InitBoat(Pawn, MotorTier, Row->BoatSeats, Sea->GetSurfaceZ());
	Boat->SeatPawn(Pawn);
	return Boat;
}

// ── 타기·내리기 ──

ABoatActor* UWaterTravelComponent::FindBoardableBoat() const
{
	const APawn* const Pawn = Cast<APawn>(GetOwner());
	const float Range = GetInteractRangeCm();
	if (!Pawn || Range < 0.f || GetRiddenBoat() || !GetWorld())
	{
		return nullptr;
	}
	for (TActorIterator<ABoatActor> It(GetWorld()); It; ++It)
	{
		if (It->HasFreeSeat() && FVector::Dist2D(It->GetActorLocation(), Pawn->GetActorLocation()) <= Range + It->GetBoardRadiusCm())
		{
			return *It;
		}
	}
	return nullptr;
}

void UWaterTravelComponent::Board()
{
	ABoatActor* const Boat = FindBoardableBoat();
	if (Boat == nullptr)
	{
		return;
	}
	if (GetOwner()->HasAuthority())
	{
		BoardOnAuthority(Boat);
	}
	else
	{
		Server_Board(Boat);
	}
}

void UWaterTravelComponent::Server_Board_Implementation(ABoatActor* Boat)
{
	BoardOnAuthority(Boat);
}

void UWaterTravelComponent::BoardOnAuthority(ABoatActor* Boat)
{
	// 호스트가 거리를 다시 본다 — 손님이 먼 배를 가리켜도 안 태운다.
	if (IsValid(Boat) && FindBoardableBoat() == Boat)
	{
		if (Boat->SeatPawn(Cast<APawn>(GetOwner())) != INDEX_NONE)
		{
			PlayRideCueOnAuthority(SnareSound::EBoatAction::Board);
		}
	}
}

void UWaterTravelComponent::Leave()
{
	if (GetOwner()->HasAuthority())
	{
		LeaveOnAuthority();
	}
	else
	{
		Server_Leave();
	}
}

void UWaterTravelComponent::Server_Leave_Implementation()
{
	LeaveOnAuthority();
}

void UWaterTravelComponent::LeaveOnAuthority()
{
	if (ABoatActor* const Boat = GetRiddenBoat())
	{
		Boat->UnseatPawn(Cast<APawn>(GetOwner()));
		PlayRideCueOnAuthority(SnareSound::EBoatAction::Leave);
	}
}

// ── 넣기 ──

ABoatActor* UWaterTravelComponent::FindStowableBoat() const
{
	const APawn* const Pawn = Cast<APawn>(GetOwner());
	const float Range = GetInteractRangeCm();
	if (!Pawn || !GetWorld())
	{
		return nullptr;
	}
	for (TActorIterator<ABoatActor> It(GetWorld()); It; ++It)
	{
		const bool bNear = Range >= 0.f && FVector::Dist2D(It->GetActorLocation(), Pawn->GetActorLocation()) <= Range + It->GetBoardRadiusCm();
		if (It->GetOwnerPawn() == Pawn && (bNear || It->FindSeat(Pawn) != INDEX_NONE))
		{
			return *It;
		}
	}
	return nullptr;
}

void UWaterTravelComponent::Stow()
{
	ABoatActor* const Boat = FindStowableBoat();
	if (bBusy || Boat == nullptr)
	{
		return;
	}
	bBusy = true;
	TWeakObjectPtr<UWaterTravelComponent> WeakThis(this);
	TWeakObjectPtr<ABoatActor> WeakBoat(Boat);
	SnareWater::CallServer(this, TEXT("StowBoat"), MakeShared<FJsonObject>(),
		[WeakThis, WeakBoat](bool bOk, const TSharedPtr<FJsonObject>&, const FString& Error)
		{
			UWaterTravelComponent* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bBusy = false;
			// "not_launched" = 서버엔 이미 넣었다 — 월드에 남은 배만 치운다.
			const ABoatActor* const StowedBoat = WeakBoat.Get();
			const FVector Where = StowedBoat != nullptr ? StowedBoat->GetActorLocation()
				: (Self->GetOwner() != nullptr ? Self->GetOwner()->GetActorLocation() : FVector::ZeroVector);
			Self->PlayBoatCue(SnareSound::EBoatAction::Stow, bOk, Error, Where);
			if (!bOk && Error != TEXT("not_launched"))
			{
				Self->LastMessage = Error;
				return;
			}
			Self->bServerBoatLaunched = false;
			SnareWater::RefreshPlayerState(Self);
			Self->Server_DestroyBoat(WeakBoat.Get());
		});
}

void UWaterTravelComponent::Server_DestroyBoat_Implementation(ABoatActor* Boat)
{
	if (IsValid(Boat) && Boat->GetOwnerPawn() == GetOwner())
	{
		Boat->UnseatAll();
		Boat->Destroy();
	}
}

void UWaterTravelComponent::PlayBoatCue(SnareSound::EBoatAction Action, bool bAccepted, const FString& ErrorCode, const FVector& Where)
{
	const SnareSound::FChangeCue Cue = SnareSound::BoatCue(Action, bAccepted, ErrorCode);
	if (Cue.Cue.IsNone())
	{
		return;
	}
	++BoatCueCount;
	LastBoatCue = Cue.Cue;
	UE_LOG(LogSnarehaven, Log, TEXT("[Sound] 배 %s — 크기 %.2f · %s"), *Cue.Cue.ToString(), Cue.Volume,
		Cue.bWorld ? TEXT("모든 기기") : TEXT("내 기기만"));
	if (Cue.bWorld)
	{
		Server_PlayBoatCue(Cue.Cue, Cue.Volume, Where);
		return;
	}
	USnareSoundSubsystem::Play2D(this, Cue.Cue, Cue.Volume);
}

void UWaterTravelComponent::Server_PlayBoatCue_Implementation(FName Cue, float Volume, FVector Where)
{
	Multicast_PlayBoatCue(Cue, Volume, Where);
}

void UWaterTravelComponent::Multicast_PlayBoatCue_Implementation(FName Cue, float Volume, FVector Where)
{
	USnareSoundSubsystem::PlayAt(this, Cue, Where, Volume);
}

void UWaterTravelComponent::PlayRideCueOnAuthority(SnareSound::EBoatAction Action)
{
	// 호스트가 실제로 상태를 바꾼 뒤에만 부른다 — 타기·내리기는 탄/내린 사람 기기에서만 난다 (DESIGN 8.9).
	const SnareSound::FChangeCue Cue = SnareSound::BoatCue(Action, true, FString());
	const APawn* const Pawn = Cast<APawn>(GetOwner());
	if (Cue.Cue.IsNone() || Pawn == nullptr)
	{
		return;
	}
	++BoatCueCount;
	LastBoatCue = Cue.Cue;
	UE_LOG(LogSnarehaven, Log, TEXT("[Sound] 배 %s — 크기 %.2f · 내 기기만"), *Cue.Cue.ToString(), Cue.Volume);
	if (Pawn->IsLocallyControlled() || GetNetMode() == NM_Standalone)
	{
		USnareSoundSubsystem::Play2D(this, Cue.Cue, Cue.Volume);
	}
	else
	{
		Client_PlayBoatCue(Cue.Cue, Cue.Volume);
	}
}

void UWaterTravelComponent::Client_PlayBoatCue_Implementation(FName Cue, float Volume)
{
	USnareSoundSubsystem::Play2D(this, Cue, Volume);
}
