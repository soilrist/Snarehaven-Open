// 2층 검사 — DESIGN 8.16 「광산」: 설원 입구 문과 굴 안 문이 서로를 가리키고, 도착 자리가 걸을 수 있는 곳인가:
// ① `L_Hunt` 에 문이 둘, 각 도착 자리가 어느 문 발판(+캡슐 반지름)에도 들지 않고(되돌아 튕기지 않음),
//    바닥 상자 윗면(z 0 ±5)이 그 자리를 덮고, 바다 위가 아니다. 입구 문은 뭍(해안 안쪽)이다.
// ② 사람이 조종하지 않는 폰은 문을 못 지나고, 조종하는 폰은 도착 자리 + 캡슐 반높이로 옮겨진다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Mine; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Gathering/GatherableActor.h"
#include "SnarehavenCharacter.h"
#include "World/MineDoorActor.h"
#include "World/SeaActor.h"

namespace SnarehavenMineTest
{
	constexpr float CapsuleRadiusCm = 34.f;
	constexpr float FloorToleranceCm = 5.f;

	// 맵에서 불러온 레벨은 컴포넌트 Bounds 가 갱신 전이라, 루트 상대 변환으로 직접 잰다.
	FTransform RootTransform(const AActor* Actor)
	{
		const USceneComponent* const Root = Actor->GetRootComponent();
		return FTransform(Root->GetRelativeRotation(), Root->GetRelativeLocation(), Root->GetRelativeScale3D());
	}

	FBox DoorBox(const AMineDoorActor* Door)
	{
		const FTransform T = RootTransform(Door);
		const FVector Extent = Door->GetTrigger()->GetUnscaledBoxExtent() * T.GetScale3D().GetAbs()
			+ FVector(CapsuleRadiusCm, CapsuleRadiusCm, 0.f);
		return FBox(T.GetLocation() - Extent, T.GetLocation() + Extent);
	}

	bool HasFloorUnder(const TArray<const AStaticMeshActor*>& Meshes, const FVector& At)
	{
		for (const AStaticMeshActor* const Actor : Meshes)
		{
			const UStaticMesh* const Mesh = Actor->GetStaticMeshComponent()->GetStaticMesh();
			const FBox Box = Mesh->GetBounds().GetBox().TransformBy(RootTransform(Actor));
			if (FMath::Abs(Box.Max.Z - At.Z) <= FloorToleranceCm && Box.Min.X <= At.X && At.X <= Box.Max.X
				&& Box.Min.Y <= At.Y && At.Y <= Box.Max.Y)
			{
				return true;
			}
		}
		return false;
	}

	void SetExit(AMineDoorActor* Door, const FVector& Exit)
	{
		const FStructProperty* const Prop = FindFProperty<FStructProperty>(AMineDoorActor::StaticClass(), TEXT("ExitLocation"));
		*Prop->ContainerPtrToValuePtr<FVector>(Door) = Exit;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMineLevelTest, "Snarehaven.Mine.Level.DoorsLandOnFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMineLevelTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenMineTest;

	const UWorld* const Level = LoadObject<UWorld>(nullptr, TEXT("/Game/Game/Maps/L_Hunt.L_Hunt"));
	if (!TestNotNull(TEXT("L_Hunt"), Level) || !TestNotNull(TEXT("레벨"), Level->PersistentLevel.Get()))
	{
		return false;
	}
	TArray<const AMineDoorActor*> Doors;
	TArray<const AStaticMeshActor*> Meshes;
	const ASeaActor* Sea = nullptr;
	for (const AActor* const Actor : Level->PersistentLevel->Actors)
	{
		if (const AMineDoorActor* const Door = Cast<AMineDoorActor>(Actor))
		{
			Doors.Add(Door);
		}
		const AStaticMeshActor* const MeshActor = Cast<AStaticMeshActor>(Actor);
		if (MeshActor && MeshActor->GetStaticMeshComponent()->GetStaticMesh())
		{
			Meshes.Add(MeshActor);
		}
		Sea = Sea ? Sea : Cast<ASeaActor>(Actor);
	}
	if (!TestEqual(TEXT("광산 문 2개"), Doors.Num(), 2) || !TestNotNull(TEXT("바다"), Sea))
	{
		return false;
	}

	bool bOneOnLand = false;
	for (const AMineDoorActor* const Door : Doors)
	{
		const FVector Exit = Door->GetExitLocation();
		const FString Name = Door->GetName();
		TestFalse(*FString::Printf(TEXT("%s 도착 자리가 비지 않았다"), *Name), Exit.IsZero());
		for (const AMineDoorActor* const Other : Doors)
		{
			TestFalse(*FString::Printf(TEXT("%s 도착 %s 가 %s 발판 밖"), *Name, *Exit.ToCompactString(), *Other->GetName()),
				DoorBox(Other).IsInsideXY(Exit));
		}
		TestTrue(*FString::Printf(TEXT("%s 도착 %s 아래 바닥"), *Name, *Exit.ToCompactString()), HasFloorUnder(Meshes, Exit));
		TestFalse(*FString::Printf(TEXT("%s 도착은 바다 위가 아니다"), *Name), Sea->IsOverSea(Exit));
		// 입구 문(설원)은 섬 안 — 해안까지 음수 거리.
		bOneOnLand |= Sea->CoastDistance(RootTransform(Door).GetLocation()) < 0.f;
		AddInfo(FString::Printf(TEXT("[Mine] %s 자리 %s → 도착 %s"), *Name,
			*RootTransform(Door).GetLocation().ToCompactString(), *Exit.ToCompactString()));
	}
	TestTrue(TEXT("입구 문 하나는 섬 뭍에 있다"), bOneOnLand);
	return true;
}

// ③ 광맥이 맵에 실제로 저장돼 있다 — `setup_zones.py` 를 다시 돌리면 `Gather_` 라벨이 다 지워져 광맥 0개가 된 일이 있었다(10-19).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMineOreCountTest, "Snarehaven.Mine.Level.OresPlaced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMineOreCountTest::RunTest(const FString& Parameters)
{
	const UWorld* const Level = LoadObject<UWorld>(nullptr, TEXT("/Game/Game/Maps/L_Hunt.L_Hunt"));
	if (!TestNotNull(TEXT("L_Hunt"), Level) || !TestNotNull(TEXT("레벨"), Level->PersistentLevel.Get()))
	{
		return false;
	}
	const FNameProperty* const RowProp = FindFProperty<FNameProperty>(AGatherableActor::StaticClass(), TEXT("GatherableRowName"));
	if (!TestNotNull(TEXT("GatherableRowName 속성"), RowProp))
	{
		return false;
	}
	int32 Basic = 0;
	int32 Advanced = 0;
	for (const AActor* const Actor : Level->PersistentLevel->Actors)
	{
		const AGatherableActor* const Gatherable = Cast<AGatherableActor>(Actor);
		if (!Gatherable)
		{
			continue;
		}
		const FName Row = RowProp->GetPropertyValue_InContainer(Gatherable);
		Basic += Row == TEXT("Gatherable_OreBasic") ? 1 : 0;
		Advanced += Row == TEXT("Gatherable_OreAdvanced") ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("[Mine] 광맥 기본 %d · 고급 %d"), Basic, Advanced));
	TestEqual(TEXT("기본 광맥 6개 (DESIGN 8.16 큰 방)"), Basic, 6);
	TestEqual(TEXT("고급 광맥 4개 (DESIGN 8.16 깊은 방)"), Advanced, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMineSendThroughTest, "Snarehaven.Mine.Door.OnlyPlayerPawnMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMineSendThroughTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenMineTest;

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMineDoorActor* const Door = World->SpawnActor<AMineDoorActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector(-500.f, 0.f, 100.f), FRotator::ZeroRotator, Params);
	APlayerController* const Controller = World->SpawnActor<APlayerController>(Params);
	if (TestNotNull(TEXT("문"), Door) && TestNotNull(TEXT("캐릭터"), Player) && TestNotNull(TEXT("컨트롤러"), Controller))
	{
		TestFalse(TEXT("도착 자리가 없으면 문이 꺼졌다"), Door->SendThrough(Player));
		const FVector Exit(3000.f, -2000.f, 0.f);
		SetExit(Door, Exit);
		TestFalse(TEXT("조종하지 않는 폰(동물 자리)은 못 지난다"), Door->SendThrough(Player));

		TestEqual(TEXT("M53-03 못 지난 길 → 소리 0"), Door->GetCrossingCueCount(), 0);

		Controller->Possess(Player);
		TestTrue(TEXT("조종하는 폰은 지난다"), Door->SendThrough(Player));
		TestEqual(TEXT("M53-03 지남 → 소리 1"), Door->GetCrossingCueCount(), 1);
		TestEqual(TEXT("M53-03 소리 = MineDoor"), Door->GetLastCrossingCue(), FName(TEXT("MineDoor")));
		TestEqual(TEXT("M53-03 멀티캐스트로 갔다"), Door->GetCrossingMulticastCount(), 1);
		const FVector Expected = Exit + FVector(0.f, 0.f, Player->GetSimpleCollisionHalfHeight());
		TestTrue(*FString::Printf(TEXT("도착 %s = %s"), *Player->GetActorLocation().ToCompactString(), *Expected.ToCompactString()),
			Player->GetActorLocation().Equals(Expected, 1.f));
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
