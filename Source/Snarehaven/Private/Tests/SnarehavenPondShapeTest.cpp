// M19-02 2층 검사 — 저장된 `L_Ranch` 를 **실제로 불러와** 연못 둘을 잰다 (DESIGN 8.9 연못·강 물빛 표, ROADMAP M19-02):
// (a) 둘레가 원이 아니다 — 피벗에서 잰 둘레 반지름 최대/최소 비 ≥ 1.25,
// (b) 낚시찌 자리(`pond_` 낚시 포인트)와 물속 바위가 물가 안쪽에 있다,
// (c) 셰이더가 물가 흰 선을 그리는 거리(UV u × 바운드 x 반폭)가 실제 둘레까지 거리와 맞는다.
// 연못 메시는 `tools/unreal/make_ranch_ground_meshes.py`, 셰이더는 `setup_sea.py` POND_HLSL. 맵은 `SnarehavenHouseFurnitureTest.cpp` 처럼 스트리밍한다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.World.Pond; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Snarehaven.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "MeshDescription.h"
#include "World/FishingSpotActor.h"

namespace SnarehavenPondShapeTest
{
	// 테스트 전용 값 (Verification.md — 설계 숫자가 아니다). 비는 ROADMAP M19-02 합격선.
	constexpr double MinShoreRatio = 1.25;
	constexpr double PondReachCm = 450.0;        // 연못 피벗에서 이 안의 낚시 포인트·바위만 그 연못 것으로 본다
	constexpr double SpotInsideCm = 50.0;        // 낚시찌가 물가에서 적어도 이만큼 안쪽
	constexpr double RockInsideCm = 40.0;        // 바위 피벗이 물가에서 적어도 이만큼 안쪽
	constexpr int32 MinRocksPerPond = 3;
	constexpr double ShoreUvToleranceCm = 3.0;   // 물가 흰 실선 폭 ≈ 8cm — 그 절반 안으로 맞아야 선이 둘레를 따른다
	constexpr double RayToleranceRad = 0.5 * UE_PI / 180.0;   // 둘레 줄 간격(96 줄 ≈ 3.75°)보다 훨씬 좁다
	constexpr double CenterSkipCm = 1.0;
	const TCHAR* PondMeshName = TEXT("SM_Ground_PondDisc_01");
	const TCHAR* RockMeshName = TEXT("SM_Nature_Boulder_02");

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

	FString MeshNameOf(const AStaticMeshActor* Actor)
	{
		const UStaticMeshComponent* const Component = Actor->GetStaticMeshComponent();
		const UStaticMesh* const Mesh = Component != nullptr ? Component->GetStaticMesh() : nullptr;
		return Mesh != nullptr ? Mesh->GetName() : FString();
	}

	double AngleOf(const FVector2D& Offset)
	{
		return FMath::Atan2(Offset.Y, Offset.X);
	}

	double AngleGap(double A, double B)
	{
		return FMath::Abs(FMath::UnwindRadians(A - B));
	}

	/** 메시가 피벗에서 뻗은 한 방향 위에 고리 꼭짓점을 쌓은 줄. 둘레 = 그 줄에서 가장 먼 꼭짓점. */
	struct FRay
	{
		double Angle = 0.0;
		double Rim = 0.0;
	};

	/** 연못 하나 — 월드 피벗과 방향 줄들. OBJ 좌표가 소수 둘째 자리로 잘려 같은 줄 꼭짓점도 각도가 조금씩 어긋난다 → 각도 차 RayToleranceRad 안이면 한 줄. */
	struct FPond
	{
		const AStaticMeshActor* Actor = nullptr;
		FVector2D Pivot = FVector2D::ZeroVector;
		TArray<FRay> Rays;
		double MinRim = UE_BIG_NUMBER;
		double MaxRim = 0.0;
		double WorstUvErrorCm = 0.0;

		int32 NearestRay(double Angle) const
		{
			int32 Best = INDEX_NONE;
			for (int32 Index = 0; Index < Rays.Num(); ++Index)
			{
				Best = Best == INDEX_NONE || AngleGap(Rays[Index].Angle, Angle) < AngleGap(Rays[Best].Angle, Angle) ? Index : Best;
			}
			return Best;
		}

		/** 줄 사이 방향은 양옆 두 줄 중 작은 둘레 — 물 안 판정을 느슨하게 하지 않는다. */
		double RimToward(const FVector2D& Point) const
		{
			const double Angle = AngleOf(Point - Pivot);
			const int32 Nearest = NearestRay(Angle);
			const double Side = FMath::UnwindRadians(Angle - Rays[Nearest].Angle) >= 0.0 ? 1.0 : -1.0;
			int32 Other = INDEX_NONE;
			for (int32 Index = 0; Index < Rays.Num(); ++Index)
			{
				const double Delta = FMath::UnwindRadians(Rays[Index].Angle - Rays[Nearest].Angle) * Side;
				if (Index != Nearest && Delta > 0.0
					&& (Other == INDEX_NONE || Delta < FMath::UnwindRadians(Rays[Other].Angle - Rays[Nearest].Angle) * Side))
				{
					Other = Index;
				}
			}
			return Other == INDEX_NONE ? Rays[Nearest].Rim : FMath::Min(Rays[Nearest].Rim, Rays[Other].Rim);
		}
	};

	bool MeasurePond(const AStaticMeshActor* Actor, FPond& Out)
	{
		const UStaticMeshComponent* const Component = Actor->GetStaticMeshComponent();
		UStaticMesh* const Mesh = Component->GetStaticMesh();
		const FMeshDescription* const Description = Mesh->GetMeshDescription(0);
		if (Description == nullptr || Description->Vertices().Num() == 0)
		{
			return false;
		}
		const FTransform& ToWorld = Component->GetComponentTransform();
		Out.Actor = Actor;
		Out.Pivot = FVector2D(ToWorld.GetLocation());
		auto OffsetOf = [&](const FVertexID Vertex)
		{
			return FVector2D(ToWorld.TransformPosition(FVector(Description->GetVertexPosition(Vertex)))) - Out.Pivot;
		};
		for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
		{
			const FVector2D Offset = OffsetOf(Vertex);
			if (Offset.Size() <= CenterSkipCm)
			{
				continue;
			}
			const int32 Ray = Out.NearestRay(AngleOf(Offset));
			if (Ray != INDEX_NONE && AngleGap(Out.Rays[Ray].Angle, AngleOf(Offset)) < RayToleranceRad)
			{
				// 둘레 꼭짓점 각도를 줄 각도로 쓴다(가장 바깥이 반올림 오차가 가장 작다)
				if (Offset.Size() > Out.Rays[Ray].Rim)
				{
					Out.Rays[Ray] = FRay{ AngleOf(Offset), Offset.Size() };
				}
			}
			else
			{
				Out.Rays.Add(FRay{ AngleOf(Offset), Offset.Size() });
			}
		}
		for (const FRay& Ray : Out.Rays)
		{
			Out.MinRim = FMath::Min(Out.MinRim, Ray.Rim);
			Out.MaxRim = FMath::Max(Out.MaxRim, Ray.Rim);
		}

		// (c) 셰이더 거리: d = u × ObjectBounds x 반폭(월드). 피벗 위 꼭짓점(가운데)은 방향이 없어 뺀다.
		const double HalfX = Component->Bounds.BoxExtent.X;
		// 이름으로 찾는다 — `FStaticMeshConstAttributes` 는 StaticMeshDescription 모듈 링크가 따로 필요하다
		const TVertexInstanceAttributesConstRef<FVector2f> UVs =
			Description->VertexInstanceAttributes().GetAttributesRef<FVector2f>(FName(TEXT("TextureCoordinate")));
		if (!UVs.IsValid())
		{
			return false;
		}
		for (const FVertexInstanceID Instance : Description->VertexInstances().GetElementIDs())
		{
			const FVector2D Offset = OffsetOf(Description->GetVertexInstanceVertex(Instance));
			if (Offset.Size() > CenterSkipCm)
			{
				const double ShaderCm = UVs.Get(Instance, 0).X * HalfX;
				const double TrueCm = Out.Rays[Out.NearestRay(AngleOf(Offset))].Rim - Offset.Size();
				Out.WorstUvErrorCm = FMath::Max(Out.WorstUvErrorCm, FMath::Abs(ShaderCm - TrueCm));
			}
		}
		return Out.Rays.Num() > 0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPondShapeTest, "Snarehaven.World.Pond.IrregularShoreAndBobberInWater",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPondShapeTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenPondShapeTest;

	ULevel* Level = nullptr;
	UWorld* const World = CreateWorldWithLevel(TEXT("/Game/Game/Maps/L_Ranch"), Level);
	if (!TestNotNull(TEXT("L_Ranch 스트리밍"), Level))
	{
		DestroyTestWorld(World);
		return false;
	}

	TArray<FPond> Ponds;
	TArray<FVector2D> Rocks;
	TArray<const AFishingSpotActor*> Spots;
	for (const AActor* const Actor : Level->Actors)
	{
		if (const AFishingSpotActor* const Spot = Cast<AFishingSpotActor>(Actor))
		{
			if (Spot->SpotId.StartsWith(TEXT("pond_")))
			{
				Spots.Add(Spot);
			}
			continue;
		}
		const AStaticMeshActor* const MeshActor = Cast<AStaticMeshActor>(Actor);
		if (MeshActor == nullptr)
		{
			continue;
		}
		const FString MeshName = MeshNameOf(MeshActor);
		if (MeshName == PondMeshName)
		{
			FPond Pond;
			if (TestTrue(*FString::Printf(TEXT("%s 메시 원본 꼭짓점"), *MeshActor->GetName()), MeasurePond(MeshActor, Pond)))
			{
				Ponds.Add(MoveTemp(Pond));
			}
		}
		else if (MeshName == RockMeshName)
		{
			Rocks.Add(FVector2D(MeshActor->GetActorLocation()));
		}
	}
	TestEqual(TEXT("연못 수"), Ponds.Num(), 2);
	TestEqual(TEXT("연못 낚시 포인트 수"), Spots.Num(), 2);

	for (const FPond& Pond : Ponds)
	{
		const double Ratio = Pond.MaxRim / Pond.MinRim;
		TestTrue(*FString::Printf(TEXT("둘레 반지름 비 %.3f ≥ %.2f"), Ratio, MinShoreRatio), Ratio >= MinShoreRatio);
		TestTrue(*FString::Printf(TEXT("셰이더 물가 거리 오차 %.2fcm ≤ %.1f"), Pond.WorstUvErrorCm, ShoreUvToleranceCm),
			Pond.WorstUvErrorCm <= ShoreUvToleranceCm);

		int32 SpotCount = 0;
		for (const AFishingSpotActor* const Spot : Spots)
		{
			const FVector2D Point(Spot->GetActorLocation());
			if (FVector2D::Distance(Point, Pond.Pivot) > PondReachCm)
			{
				continue;
			}
			++SpotCount;
			const double Inside = Pond.RimToward(Point) - FVector2D::Distance(Point, Pond.Pivot);
			TestTrue(*FString::Printf(TEXT("%s 낚시찌가 물가 %.0fcm 안쪽 (≥ %.0f)"), *Spot->SpotId, Inside, SpotInsideCm), Inside >= SpotInsideCm);
		}
		TestEqual(TEXT("연못마다 낚시 포인트 1"), SpotCount, 1);

		int32 RockCount = 0;
		double ClosestRockInside = UE_BIG_NUMBER;
		for (const FVector2D& Rock : Rocks)
		{
			if (FVector2D::Distance(Rock, Pond.Pivot) > PondReachCm)
			{
				continue;
			}
			++RockCount;
			ClosestRockInside = FMath::Min(ClosestRockInside, Pond.RimToward(Rock) - FVector2D::Distance(Rock, Pond.Pivot));
		}
		TestTrue(*FString::Printf(TEXT("물속 바위 %d ≥ %d"), RockCount, MinRocksPerPond), RockCount >= MinRocksPerPond);
		TestTrue(*FString::Printf(TEXT("바위가 물가 %.0fcm 안쪽 (≥ %.0f)"), ClosestRockInside, RockInsideCm), ClosestRockInside >= RockInsideCm);

		UE_LOG(LogSnarehaven, Display, TEXT("[PondShape] %s 피벗 %s 둘레 %.0f~%.0fcm 비 %.3f · 셰이더 거리 오차 최대 %.2fcm · 바위 %d (물가에서 최소 %.0fcm)"),
			*Pond.Actor->GetName(), *Pond.Pivot.ToString(), Pond.MinRim, Pond.MaxRim, Ratio, Pond.WorstUvErrorCm, RockCount, ClosestRockInside);
	}

	DestroyTestWorld(World);
	return true;
}

#endif
