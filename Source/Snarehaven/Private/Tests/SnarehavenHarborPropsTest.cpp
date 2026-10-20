// M19-05 2층 검사 — 거점 항구 부두가 흰 판만이 아니라 소품 무리로 채워졌고 배 타는 칸은 비었는가 (ROADMAP M19-05):
// (a) 저장된 `L_Ranch` 를 **실제로 불러와** 부두(`Harbor_Quay`) 경계 안에 선 항구 소품 메시 ≥ MinProps,
// (b) 배 타는 칸 — 잔교(`Harbor_Pier_*`) 입구와 존 포탈(`AZonePortalActor`) — 둘레 ClearCm 안 소품 0.
// 놓는 도구는 `tools/unreal/setup_harbor.py`(`BOARDING_CLEAR_CM`). 맵은 `SnarehavenCliffBaseTest.cpp` 처럼 스트리밍한다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.World.Harbor; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Snarehaven.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "World/ZonePortalActor.h"

namespace SnarehavenHarborPropsTest
{
	// 테스트 전용 값 (Verification.md — 설계 숫자가 아니다). 소품 수·빈 반경은 ROADMAP M19-05 합격선.
	constexpr int32 MinProps = 8;
	constexpr double ClearCm = 200.0;
	const TCHAR* RanchLevel = TEXT("/Game/Game/Maps/L_Ranch");
	const TCHAR* QuayLabel = TEXT("Harbor_Quay");
	const TCHAR* PierLabelPrefix = TEXT("Harbor_Pier_");
	const TCHAR* PropMeshFolder = TEXT("/Game/Game/Environment/Props/Harbor/");

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

	bool IsHarborProp(const AStaticMeshActor* MeshActor)
	{
		const UStaticMesh* const Mesh = MeshActor != nullptr ? MeshActor->GetStaticMeshComponent()->GetStaticMesh() : nullptr;
		return Mesh != nullptr && Mesh->GetPathName().StartsWith(PropMeshFolder);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHarborPropsTest, "Snarehaven.World.Harbor.PropsOnQuayBoardingClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHarborPropsTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenHarborPropsTest;

	ULevel* Level = nullptr;
	UWorld* const World = CreateWorldWithLevel(RanchLevel, Level);
	if (!TestNotNull(TEXT("L_Ranch 스트리밍"), Level))
	{
		DestroyTestWorld(World);
		return false;
	}

	const AActor* Quay = nullptr;
	TArray<FVector2D> BoardingSpots;
	for (const AActor* const Actor : Level->Actors)
	{
		if (Actor == nullptr)
		{
			continue;
		}
		if (Actor->GetActorLabel() == QuayLabel)
		{
			Quay = Actor;
		}
		else if (Actor->GetActorLabel().StartsWith(PierLabelPrefix))
		{
			// 잔교 입구 = 잔교 경계 상자에서 부두 쪽 끝(잔교는 -Y 바다로 뻗으니 가장 큰 Y) 가운데
			FVector Origin;
			FVector Extent;
			Actor->GetActorBounds(false, Origin, Extent);
			BoardingSpots.Add(FVector2D(Origin.X, Origin.Y + Extent.Y));
		}
		else if (Actor->IsA<AZonePortalActor>())
		{
			BoardingSpots.Add(FVector2D(Actor->GetActorLocation()));
		}
	}
	if (!TestNotNull(TEXT("L_Ranch Harbor_Quay"), Quay))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestTrue(*FString::Printf(TEXT("배 타는 칸 %d 곳(잔교 입구 2 이상)"), BoardingSpots.Num()), BoardingSpots.Num() >= 2);

	FVector QuayOrigin;
	FVector QuayExtent;
	Quay->GetActorBounds(false, QuayOrigin, QuayExtent);
	const FBox2D QuayBox(FVector2D(QuayOrigin - QuayExtent), FVector2D(QuayOrigin + QuayExtent));
	int32 Props = 0;
	int32 Blocking = 0;
	for (const AActor* const Actor : Level->Actors)
	{
		const AStaticMeshActor* const MeshActor = Cast<AStaticMeshActor>(Actor);
		if (!IsHarborProp(MeshActor))
		{
			continue;
		}
		const FVector2D At(MeshActor->GetActorLocation());
		if (QuayBox.IsInside(At))
		{
			++Props;
		}
		for (const FVector2D& Spot : BoardingSpots)
		{
			const double Distance = FVector2D::Distance(At, Spot);
			if (Distance < ClearCm)
			{
				++Blocking;
				AddError(FString::Printf(TEXT("%s (%.0f,%.0f) 가 배 타는 칸 (%.0f,%.0f) 에서 %.0fcm"),
					*MeshActor->GetActorLabel(), At.X, At.Y, Spot.X, Spot.Y, Distance));
			}
		}
	}
	TestTrue(*FString::Printf(TEXT("부두 위 소품 %d ≥ %d"), Props, MinProps), Props >= MinProps);
	TestEqual(TEXT("배 타는 칸 200cm 안 소품"), Blocking, 0);
	UE_LOG(LogSnarehaven, Display, TEXT("[HarborProps] 부두 위 소품 %d · 배 타는 칸 %d 곳 · %.0fcm 안 소품 %d"),
		Props, BoardingSpots.Num(), ClearCm, Blocking);
	DestroyTestWorld(World);
	return true;
}

#endif
