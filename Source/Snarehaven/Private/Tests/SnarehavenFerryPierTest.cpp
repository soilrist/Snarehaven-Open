// M13-39 2층 검사 — 잡화점 좌판·상인 몸과 선착장 부두가 DESIGN 8.14 「잡화점·선착장 진짜 모습」 표대로 섰는가:
// ① 좌판 메시 이름·수평 긴 변 260(±3)·카운터 앞면이 상인 뒤 40(±3), 상인 몸은 플레이어 스켈레탈 메시 + 대기 클립 + 상인 모자,
// ② 부두 메시 이름·긴 변 600(±3)·바다 끝이 배 타는 칸 가운데 앞 100, 판자 윗면이 걸을 수 있는 바닥,
// ③ `L_Hunt`·`L_Ranch` 의 포탈 부두가 시작점에서 가장 가까운 해안선 ±50cm 에서 바다로 뻗는다,
// ④ 부두 끝 판자 위에 선 폰은 수영이 아니다(같은 자리 수면이면 수영이다 — 검사가 물을 알아보는지 대조).
// ⑤ 섬 사이 배(DESIGN 8.14): `L_Hunt` 부두 열넷이 둘씩 짝이고 해안선에서 뻗으며, 버튼을 누르면 건너편 부두 뭍 쪽에 내린다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.FerryPier; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Player/PlayerBodyAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/Level.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Combat/MeleeAttackComponent.h"
#include "Equipment/EquipmentComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "SnarehavenCharacter.h"
#include "Data/ZoneRow.h"
#include "Engine/DataTable.h"
#include "World/SnareZoneRules.h"
#include "World/ZoneTravelComponent.h"
#include "World/SeaActor.h"
#include "World/ShopVendorActor.h"
#include "World/WaterTravelComponent.h"
#include "World/ZonePortalActor.h"

namespace SnarehavenFerryPierTest
{
	// DESIGN 8.14 표 값 — 코드 상수를 되읽지 않고 표를 따로 적어 대조한다.
	constexpr float StallLongSideCm = 260.f;
	// 「상인 자리 = 좌판 앞」: 카운터 앞면이 상인 캡슐 중심 뒤 40 (반지름 34 + 틈 6).
	constexpr float StallBehindCm = 40.f;
	constexpr float PierLongSideCm = 600.f;
	constexpr float PierSeaCm = 400.f;
	constexpr float BoardingCellCm = 200.f;
	constexpr float PlankAboveFloorCm = 15.f;
	constexpr float SizeToleranceCm = 3.f;
	constexpr float CoastToleranceCm = 50.f;
	constexpr float NearestSlackCm = 250.f;   // `tools/unreal/islands.py` 자체 검사와 같은 여유
	// 실제 맵과 같은 바다 — 섬 바닥 0, 수면 −10 (`tools/unreal/setup_sea.py` SEA_Z).
	constexpr float TestSeaSurfaceZ = -10.f;
	constexpr int32 IslandFerryCount = 12;       // 짝 여섯 — 작은 세계 군도: 헤이븐↔골무, 눈방울↔갈매기, 버들↔이끼섬, 단풍↔피라미, 수달↔호숫가, 서리골↔도토리 (`setup_zone_portals.py`)
	constexpr float FerryArriveBackCm = 400.f;
	constexpr float SwimReachCm = 940.f;         // DESIGN 8.9 수영 스태미나 줄 — 100/12 초 × 113cm/s
	constexpr float TravelRangeCm = 150.f;       // DESIGN 3.1 근접 사거리 = 포탈 사거리   // 내리는 자리 = 건너편 배 타는 칸에서 뭍 쪽 400

	UWorld* CreateTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	template <typename T>
	T* SpawnAndBegin(UWorld* World, const FVector& At, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		T* const Actor = World->SpawnActor<T>(At, Rotation, Params);
		// 게임모드 없는 검사 월드는 BeginPlay 를 저절로 돌리지 않는다 (SnarehavenWaterTest 와 같다).
		if (Actor && !Actor->HasActorBegunPlay())
		{
			Actor->DispatchBeginPlay();
		}
		return Actor;
	}

	FString MeshName(const UStaticMeshComponent* Component)
	{
		const UStaticMesh* const Mesh = Component != nullptr ? Component->GetStaticMesh() : nullptr;
		return Mesh != nullptr ? Mesh->GetName() : FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFerryPierStallTest, "Snarehaven.FerryPier.Stall.MeshSizeAndMerchant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFerryPierStallTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenFerryPierTest;

	UWorld* const World = CreateTestWorld();
	AShopVendorActor* const Vendor = SpawnAndBegin<AShopVendorActor>(World, FVector(0.f, 0.f, 88.f));
	const UStaticMeshComponent* const Stall = Vendor ? Vendor->GetStall() : nullptr;
	const USkeletalMeshComponent* const Body = Vendor ? Vendor->GetBody() : nullptr;
	const UEquipmentComponent* const Equipment = Vendor ? Vendor->GetEquipment() : nullptr;
	if (!TestNotNull(TEXT("상인"), Vendor) || !TestNotNull(TEXT("좌판"), Stall) || !TestNotNull(TEXT("몸"), Body)
		|| !TestNotNull(TEXT("장비"), Equipment))
	{
		DestroyTestWorld(World);
		return false;
	}

	// ① 좌판 — 이름·수평 긴 변·앞면 자리. 월드 상자로 잰다(배율·회전이 다 들어간 값).
	TestEqual(TEXT("좌판 메시"), MeshName(Stall), FString(TEXT("SM_Prop_ShopStall_01")));
	const FBox StallBox = Stall->Bounds.GetBox();
	const float LongSide = FMath::Max(StallBox.GetSize().X, StallBox.GetSize().Y);
	TestEqual(TEXT("좌판 수평 긴 변 260"), LongSide, StallLongSideCm, SizeToleranceCm);
	TestEqual(TEXT("좌판 카운터 앞면 = 상인 뒤(−X) 40"), static_cast<float>(StallBox.Max.X - Vendor->GetActorLocation().X),
		-StallBehindCm, SizeToleranceCm);
	TestNotEqual(TEXT("좌판 충돌 켜짐"), Stall->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	const float PlayerHalfHeight = GetDefault<ASnarehavenCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	TestEqual(TEXT("좌판 받침점 = 바닥(상인 중심 − 캡슐 반높이)"), static_cast<float>(StallBox.Min.Z),
		static_cast<float>(Vendor->GetActorLocation().Z - PlayerHalfHeight), SizeToleranceCm);

	// 상인 몸 — 플레이어와 같은 스켈레탈 메시, 대기 클립 반복, 충돌 끔.
	const USkeletalMesh* const BodyMesh = Body->GetSkeletalMeshAsset();
	TestEqual(TEXT("상인 몸 = 플레이어 몸"), BodyMesh ? BodyMesh->GetName() : FString(), FString(TEXT("SK_Player_Body")));
	TestEqual(TEXT("상인 몸 충돌 끔"), Body->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	// 몸 애님 = 플레이어 몸 애님(고개 들기 15° 몫 — 루프#1426, 단일 노드였을 땐 고개를 못 들었다).
	const UPlayerBodyAnimInstance* const BodyAnim = Cast<UPlayerBodyAnimInstance>(Body->GetAnimInstance());
	const UAnimSequence* const Playing = BodyAnim ? BodyAnim->GetBaseClip() : nullptr;
	TestEqual(TEXT("대기 클립"), Playing ? Playing->GetName() : FString(), FString(TEXT("A_Player_Idle")));
	TestTrue(TEXT("대기 클립 반복"), BodyAnim != nullptr && BodyAnim->IsShownClipLooping());

	// 상인 모자 — 플레이어와 같은 길로 머리에 붙었다.
	const UStaticMeshComponent* const Hat = Equipment->GetEquippedMesh(EEquipSlot::Head);
	TestEqual(TEXT("머리 = 상인 모자"), Equipment->GetEquippedItem(EEquipSlot::Head).IsNone() ? FString() : MeshName(Hat),
		FString(TEXT("SM_Hat_Merchant_01")));
	TestTrue(TEXT("모자가 상인 몸에 붙었다"), Hat != nullptr && Hat->GetAttachParent() == Body);
	AddInfo(FString::Printf(TEXT("[Pier] 좌판 %.1f×%.1f×%.1f · 앞면 %.1f · 모자 %s"),
		StallBox.GetSize().X, StallBox.GetSize().Y, StallBox.GetSize().Z,
		StallBox.Max.X - Vendor->GetActorLocation().X, Hat ? *Hat->GetComponentLocation().ToCompactString() : TEXT("없음")));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFerryPierMeshTest, "Snarehaven.FerryPier.Pier.MeshSizeAndPlank",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFerryPierMeshTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenFerryPierTest;

	UWorld* const World = CreateTestWorld();
	const FVector At(0.f, 0.f, PlankAboveFloorCm);
	AZonePortalActor* const Portal = SpawnAndBegin<AZonePortalActor>(World, At);
	const UStaticMeshComponent* const Body = Portal ? Portal->GetBody() : nullptr;
	if (!TestNotNull(TEXT("포탈"), Portal) || !TestNotNull(TEXT("부두"), Body))
	{
		DestroyTestWorld(World);
		return false;
	}

	// ② 이름·긴 변·바다 끝 자리 — 액터 자리가 배 타는 칸 가운데라 바다 끝은 그 앞 100.
	TestEqual(TEXT("부두 메시"), MeshName(Body), FString(TEXT("SM_Prop_FerryPier_01")));
	const FBox Box = Body->Bounds.GetBox();
	TestEqual(TEXT("부두 긴 변 600"), static_cast<float>(Box.GetSize().X), PierLongSideCm, SizeToleranceCm);
	TestEqual(TEXT("바다 끝 = 배 타는 칸 가운데 + 100"), static_cast<float>(Box.Max.X - At.X), BoardingCellCm * 0.5f,
		SizeToleranceCm);

	// 판자 윗면 — 위에서 폰 채널로 쏘아 부두에 닿고, 그 면이 걸을 수 있는 바닥이며 높이가 바닥 +15 다.
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 500.f), At - FVector(0.f, 0.f, 500.f), ECC_Pawn);
	TestTrue(TEXT("부두 판자에 닿는다"), bHit && Hit.GetActor() == Portal);
	const float WalkableZ = GetDefault<ASnarehavenCharacter>()->GetCharacterMovement()->GetWalkableFloorZ();
	TestTrue(TEXT("판자 면이 걸을 수 있는 경사"), bHit && Hit.ImpactNormal.Z >= WalkableZ);
	TestEqual(TEXT("판자 윗면 = 바닥 +15"), static_cast<float>(Hit.ImpactPoint.Z), PlankAboveFloorCm, SizeToleranceCm);
	AddInfo(FString::Printf(TEXT("[Pier] 부두 %.1f×%.1f×%.1f · 바다 끝 %.1f · 판자 z %.1f 법선 z %.2f"),
		Box.GetSize().X, Box.GetSize().Y, Box.GetSize().Z, Box.Max.X - At.X, Hit.ImpactPoint.Z, Hit.ImpactNormal.Z));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFerryPierLevelTest, "Snarehaven.FerryPier.Level.PortalOnNearestCoast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFerryPierLevelTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenFerryPierTest;

	for (const TCHAR* const LevelPath : { TEXT("/Game/Game/Maps/L_Hunt.L_Hunt"), TEXT("/Game/Game/Maps/L_Ranch.L_Ranch") })
	{
		const UWorld* const Level = LoadObject<UWorld>(nullptr, LevelPath);
		if (!TestNotNull(LevelPath, Level) || !TestNotNull(TEXT("레벨"), Level->PersistentLevel.Get()))
		{
			return false;
		}
		const AZonePortalActor* Portal = nullptr;
		const ASeaActor* Sea = nullptr;
		const APlayerStart* Start = nullptr;
		for (const AActor* const Actor : Level->PersistentLevel->Actors)
		{
			// 섬 사이 배(⑤)는 시작점과 무관한 자리라 여기서 빼고 존 포탈만 본다.
			const AZonePortalActor* const Found = Cast<AZonePortalActor>(Actor);
			Portal = Portal ? Portal : (Found && !Found->IsIslandFerry() ? Found : nullptr);
			Sea = Sea ? Sea : Cast<ASeaActor>(Actor);
			Start = Start ? Start : Cast<APlayerStart>(Actor);
		}
		if (!TestNotNull(TEXT("포탈"), Portal) || !TestNotNull(TEXT("바다"), Sea) || !TestNotNull(TEXT("시작점"), Start))
		{
			return false;
		}

		// ③ 부두 앞(+X)이 섬 밖을 향하는 축이고, 뭍·바다 경계(포탈 자리에서 뒤로 300)가 그 축의 해안선에 있다.
		const FVector Out = Portal->GetActorForwardVector();
		const FVector Junction = Portal->GetActorLocation() - Out * (PierSeaCm - BoardingCellCm * 0.5f);
		const float CoastGap = Sea->CoastDistance(Junction);
		TestTrue(*FString::Printf(TEXT("%s 부두가 축 방향"), LevelPath),
			FMath::IsNearlyEqual(FMath::Abs(Out.X) + FMath::Abs(Out.Y), 1.f, 0.01f));
		TestTrue(*FString::Printf(TEXT("%s 뭍·바다 경계가 해안선 ±50 (%.1f)"), LevelPath, CoastGap),
			FMath::Abs(CoastGap) <= CoastToleranceCm);
		TestEqual(*FString::Printf(TEXT("%s 포탈 = 판자 윗면(바닥 +15)"), LevelPath),
			static_cast<float>(Portal->GetActorLocation().Z), Sea->IslandFloorZ + PlankAboveFloorCm, 1.f);

		// 가까운 해안 — 시작점에서 부두 뿌리까지가 가장 가까운 해안 거리 + 여유 안이다(여러 섬은 해안이 구불구불해 정확히 최소일 수 없다).
		const float Chosen = FVector::DotProduct(FVector(Junction - Start->GetActorLocation()) * FVector(1.f, 1.f, 0.f), Out);
		const float Nearest = -Sea->CoastDistance(Start->GetActorLocation());
		TestTrue(*FString::Printf(TEXT("%s 시작점에서 가까운 해안 (부두 %.0f · 최단 %.0f)"), LevelPath, Chosen, Nearest),
			Chosen <= Nearest + NearestSlackCm);
		AddInfo(FString::Printf(TEXT("[Pier] %s 포탈 %s 앞 %s · 해안선 차 %.1f · 시작점→해안 %.0f"),
			LevelPath, *Portal->GetActorLocation().ToCompactString(), *Out.ToCompactString(), CoastGap, Chosen));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFerryPierNotSwimmingTest, "Snarehaven.FerryPier.Pier.StandingIsNotSwimming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFerryPierNotSwimmingTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenFerryPierTest;

	UWorld* const World = CreateTestWorld();
	ASeaActor* const Sea = SpawnAndBegin<ASeaActor>(World, FVector(0.f, 0.f, TestSeaSurfaceZ));
	const float Half = Sea ? Sea->IslandHalfExtentCm : 0.f;
	// 동쪽 해안에 부두 — 포탈 자리 = 해안 + 300, 판자 윗면 높이.
	AZonePortalActor* const Portal = SpawnAndBegin<AZonePortalActor>(World,
		FVector(Half + PierSeaCm - BoardingCellCm * 0.5f, 0.f, Sea ? Sea->IslandFloorZ + PlankAboveFloorCm : 0.f));
	ASnarehavenCharacter* const Player = SpawnAndBegin<ASnarehavenCharacter>(World, FVector::ZeroVector);
	UWaterTravelComponent* const Water = Player ? Player->FindComponentByClass<UWaterTravelComponent>() : nullptr;
	if (!TestNotNull(TEXT("바다"), Sea) || !TestNotNull(TEXT("포탈"), Portal) || !TestNotNull(TEXT("물 컴포넌트"), Water))
	{
		DestroyTestWorld(World);
		return false;
	}

	// ④ 부두 끝 = 배 타는 칸 가운데(바다 끝에서 100 안쪽 — 끝 30 안쪽은 종 기둥 꼭대기라 판자가 아니다).
	// 판자 위 발 높이는 트레이스가 찾은 면이다(판자가 없으면 여기서 못 선다).
	const FVector End(Half + PierSeaCm - BoardingCellCm * 0.5f, 0.f, 0.f);
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, End + FVector(0.f, 0.f, 500.f), End - FVector(0.f, 0.f, 500.f), ECC_Pawn);
	TestTrue(TEXT("부두 끝 아래에 판자가 있다"), bHit && Hit.GetActor() == Portal);
	TestEqual(TEXT("부두 끝 판자 = 바닥 +15"), bHit ? static_cast<float>(Hit.ImpactPoint.Z) : 0.f,
		Sea->IslandFloorZ + PlankAboveFloorCm, SizeToleranceCm);
	TestTrue(TEXT("부두 끝은 바다 위"), Sea->IsOverSea(End));
	const float HalfHeight = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Player->SetActorLocation(FVector(End.X, End.Y, (bHit ? Hit.ImpactPoint.Z : Sea->GetSurfaceZ()) + HalfHeight));
	Water->UpdateWaterState();
	TestFalse(TEXT("부두 끝 판자 위는 수영이 아니다"), Water->IsInWater());

	// 대조 — 같은 거리의 부두 옆 수면이면 수영이다(검사가 물을 알아보는지).
	Player->SetActorLocation(FVector(End.X, 500.f, Sea->GetSurfaceZ()));
	Water->UpdateWaterState();
	TestTrue(TEXT("부두 옆 수면은 수영"), Water->IsInWater());
	AddInfo(FString::Printf(TEXT("[Pier] 부두 끝 판자 z %.1f (수면 %.1f)"), bHit ? Hit.ImpactPoint.Z : 0.f, Sea->GetSurfaceZ()));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenIslandFerryLevelTest, "Snarehaven.FerryPier.Island.PairedOnCoast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenIslandFerryLevelTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenFerryPierTest;

	const UWorld* const Level = LoadObject<UWorld>(nullptr, TEXT("/Game/Game/Maps/L_Hunt.L_Hunt"));
	if (!TestNotNull(TEXT("L_Hunt"), Level) || !TestNotNull(TEXT("레벨"), Level->PersistentLevel.Get()))
	{
		return false;
	}
	TArray<const AZonePortalActor*> Ferries;
	const ASeaActor* Sea = nullptr;
	for (const AActor* const Actor : Level->PersistentLevel->Actors)
	{
		Sea = Sea ? Sea : Cast<ASeaActor>(Actor);
		const AZonePortalActor* const Portal = Cast<AZonePortalActor>(Actor);
		if (Portal && Portal->IsIslandFerry())
		{
			Ferries.Add(Portal);
		}
	}
	const UDataTable* const Zones = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
	if (!TestNotNull(TEXT("바다"), Sea) || !TestNotNull(TEXT("존 표"), Zones))
	{
		return false;
	}
	TestEqual(TEXT("섬 배 부두 수"), Ferries.Num(), IslandFerryCount);

	for (const AZonePortalActor* const Ferry : Ferries)
	{
		const AZonePortalActor* const Other = Ferry->GetFerryTo();
		TestTrue(*FString::Printf(TEXT("%s 짝이 나를 가리킨다"), *Ferry->GetName()), Other && Other->GetFerryTo() == Ferry);
		TestFalse(*FString::Printf(TEXT("%s 버튼 이름"), *Ferry->GetName()), Ferry->GetFerryDisplayName().IsEmpty());
		const FVector Out = Ferry->GetActorForwardVector();
		const FVector Junction = Ferry->GetActorLocation() - Out * (PierSeaCm - BoardingCellCm * 0.5f);
		const float CoastGap = Sea->CoastDistance(Junction);
		TestTrue(*FString::Printf(TEXT("%s 뭍·바다 경계가 해안선 ±50 (%.1f)"), *Ferry->GetName(), CoastGap),
			FMath::Abs(CoastGap) <= CoastToleranceCm);
		TestTrue(*FString::Printf(TEXT("%s 배 타는 칸은 바다 위"), *Ferry->GetName()), Sea->IsOverSea(Ferry->GetActorLocation()));
		if (Other == nullptr)
		{
			continue;
		}
		// 타는 칸과 내리는 자리가 다른 섬 — 같은 섬끼리 잇는 배는 쓸모가 없다.
		const FVector Arrive = Other->GetActorLocation() - Other->GetActorForwardVector() * FerryArriveBackCm;
		TestTrue(*FString::Printf(TEXT("%s 내리는 자리는 뭍 (%.0f)"), *Ferry->GetName(), Sea->CoastDistance(Arrive)),
			Sea->CoastDistance(Arrive) < 0.f);
		// 다른 섬 = 두 부두 사이 직선이 헤엄으로 못 건너는 물을 지난다 — 어딘가 해안에서 SwimReachCm 절반 넘게 떨어진 물(물길 한가운데).
		// 존이 같은 섬끼리도 잇는다(숲 ↔ 숲 이끼·갈대·고사리) — 존으로는 섬을 못 가린다.
		float WidestWaterCm = 0.f;
		for (int32 Step = 0; Step <= 200; ++Step)
		{
			WidestWaterCm = FMath::Max(WidestWaterCm, Sea->CoastDistance(FMath::Lerp(Ferry->GetActorLocation(), Other->GetActorLocation(), Step / 200.f)));
		}
		TestTrue(*FString::Printf(TEXT("%s 헤엄으로 못 건너는 물을 건넌다 (물 한가운데 %.0f)"), *Ferry->GetName(), WidestWaterCm),
			WidestWaterCm >= SwimReachCm * 0.5f);
		const FName MapName(TEXT("L_Hunt"));
		const FZoneRow* const BoardRow = SnareZones::FindRegionAt(Zones, MapName, Ferry->GetActorLocation() - Out * FerryArriveBackCm, Sea);
		const FZoneRow* const ArriveRow = SnareZones::FindRegionAt(Zones, MapName, Arrive, Sea);
		AddInfo(FString::Printf(TEXT("[IslandFerry] %s %s → %s 도착 %s (%s → %s) 해안선 차 %.1f"), *Ferry->GetName(),
			*Ferry->GetActorLocation().ToCompactString(), *Other->GetName(), *Arrive.ToCompactString(),
			BoardRow ? *BoardRow->ZoneID.ToString() : TEXT("칸 밖"), ArriveRow ? *ArriveRow->ZoneID.ToString() : TEXT("칸 밖"), CoastGap));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenIslandFerryRideTest, "Snarehaven.FerryPier.Island.ButtonCrosses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenIslandFerryRideTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenFerryPierTest;

	UWorld* const World = CreateTestWorld();
	// 두 부두 — A 는 +X 로, B 는 A 에서 3000 떨어져 −X 로 바다를 본다(서로 마주 본다).
	AZonePortalActor* const A = SpawnAndBegin<AZonePortalActor>(World, FVector(0.f, 0.f, 15.f));
	AZonePortalActor* const B = SpawnAndBegin<AZonePortalActor>(World, FVector(3000.f, 0.f, 15.f), FRotator(0.f, 180.f, 0.f));
	// 판자 위 선 폰 = 캡슐 중심이 판자 +90 (사거리 150 안).
	ASnarehavenCharacter* const Player = SpawnAndBegin<ASnarehavenCharacter>(World, FVector(0.f, 0.f, 105.f));
	UZoneTravelComponent* const Travel = Player ? Player->FindComponentByClass<UZoneTravelComponent>() : nullptr;
	if (!TestNotNull(TEXT("부두 A"), A) || !TestNotNull(TEXT("부두 B"), B) || !TestNotNull(TEXT("이동 컴포넌트"), Travel))
	{
		DestroyTestWorld(World);
		return false;
	}
	// 사거리는 근접 공격에서 빌린다 — DESIGN 3.1 150cm.
	Player->GetMeleeAttack()->SetAttackSettings(10.f, TravelRangeCm, 1.f);
	A->SetFerryData(B, FText::FromString(TEXT("건너편")));
	B->SetFerryData(A, FText::FromString(TEXT("이쪽")));
	A->InitializeFromTable();
	B->InitializeFromTable();

	// 짝 없는 부두는 섬 배가 아니다(대조) — 존 표·ID 가 없으니 버튼도 없다.
	AZonePortalActor* const Lone = SpawnAndBegin<AZonePortalActor>(World, FVector(0.f, 5000.f, 15.f));
	TestFalse(TEXT("짝 없는 부두는 못 탄다"), Lone && Lone->CanTravel());

	TestTrue(TEXT("부두 A 는 탈 수 있다"), A->CanTravel());
	TestEqual(TEXT("버튼 이름 = 건너편 섬"), Travel->RefreshTarget(), A);
	TestEqual(TEXT("버튼 글자 이름"), A->GetTargetDisplayName().ToString(), FString(TEXT("건너편")));
	TestTrue(TEXT("버튼을 누르면 요청이 뜬다(뱃삯 없음)"), Travel->TryFerryToNearest());
	const FVector Expected = B->GetActorLocation() - B->GetActorForwardVector() * FerryArriveBackCm;
	const FVector Got = Player->GetActorLocation();
	TestEqual(TEXT("건너편 부두 뭍 쪽에 내렸다 (X)"), static_cast<float>(Got.X), static_cast<float>(Expected.X), 5.f);
	TestEqual(TEXT("건너편 부두 뭍 쪽에 내렸다 (Y)"), static_cast<float>(Got.Y), static_cast<float>(Expected.Y), 5.f);
	TestEqual(TEXT("M53-03 나룻배 건넘 → 소리 1"), A->GetCrossingCueCount(), 1);
	TestEqual(TEXT("M53-03 나룻배 소리 = BoatBoard"), A->GetLastCrossingCue(), FName(TEXT("BoatBoard")));
	TestEqual(TEXT("M53-03 나룻배 멀티캐스트로 갔다"), A->GetCrossingMulticastCount(), 1);
	A->PlayCrossingCue(SnareSound::EZoneCrossing::Ferry, false, Got);
	TestEqual(TEXT("M53-03 확정 아니면 소리 +0"), A->GetCrossingCueCount(), 1);
	A->PlayCrossingCue(SnareSound::EZoneCrossing::Portal, true, Got);
	TestEqual(TEXT("M53-03 포털 소리 +1"), A->GetCrossingCueCount(), 2);
	TestEqual(TEXT("M53-03 포털 소리 = ZoneWarp"), A->GetLastCrossingCue(), FName(TEXT("ZoneWarp")));
	TestEqual(TEXT("M53-03 포털은 호스트 2D — 멀티캐스트 그대로"), A->GetCrossingMulticastCount(), 1);
	TestFalse(TEXT("내린 자리에서 되돌아가는 버튼이 바로 뜨지 않는다"), Travel->RefreshTarget() == B);
	AddInfo(FString::Printf(TEXT("[IslandFerry] 버튼 → 도착 %s (기대 %s)"), *Got.ToCompactString(), *Expected.ToCompactString()));

	DestroyTestWorld(World);
	return true;
}

#endif
