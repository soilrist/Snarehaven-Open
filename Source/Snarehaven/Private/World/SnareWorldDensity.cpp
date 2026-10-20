#include "World/SnareWorldDensity.h"
#include "Snarehaven.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

namespace SnareWorldDensity
{
	EKind ClassifyMeshName(const FString& MeshName)
	{
		// 그루터기(`Stump`)·통나무(`Log`)는 이름에 Tree 가 없어 기타로 떨어진다. 마른 나무 행이 그루터기를 쓴다(M15-21).
		if (MeshName.Contains(TEXT("Tree")) || MeshName.Contains(TEXT("Pine")) || MeshName.Contains(TEXT("Birch")))
		{
			return EKind::Tree;
		}
		if (MeshName.Contains(TEXT("Rock")) || MeshName.Contains(TEXT("Boulder")))
		{
			return EKind::Rock;
		}
		return EKind::Other;
	}

	FResult Measure(const TArray<FPlacement>& Placements, const FBox2D& View)
	{
		FResult Result;
		TArray<FVector2D> Rocks;
		TArray<FVector2D> Blockers;
		for (const FPlacement& Placement : Placements)
		{
			if (Placement.Kind == EKind::Other || !View.IsInside(Placement.Location))
			{
				continue;
			}
			Blockers.Add(Placement.Location);
			if (Placement.Kind == EKind::Tree)
			{
				++Result.Trees;
			}
			else
			{
				Rocks.Add(Placement.Location);
			}
		}

		// 바위 무리: 서로 `RockClusterGapCm` 안이면 같은 무리 (연결 성분 수).
		TArray<int32> Group;
		Group.Init(INDEX_NONE, Rocks.Num());
		for (int32 Start = 0; Start < Rocks.Num(); ++Start)
		{
			if (Group[Start] != INDEX_NONE)
			{
				continue;
			}
			++Result.RockClusters;
			TArray<int32> Open{Start};
			Group[Start] = Start;
			while (Open.Num() > 0)
			{
				const int32 Current = Open.Pop();
				for (int32 Other = 0; Other < Rocks.Num(); ++Other)
				{
					if (Group[Other] == INDEX_NONE && FVector2D::Distance(Rocks[Current], Rocks[Other]) <= RockClusterGapCm)
					{
						Group[Other] = Start;
						Open.Add(Other);
					}
				}
			}
		}

		// 빈 공간: 화면 바닥을 격자로 훑어 어떤 나무·바위 발밑 반경에도 안 닿는 칸의 몫.
		constexpr int32 Cells = 40;
		const FVector2D Size = View.GetSize();
		int32 Empty = 0;
		for (int32 Y = 0; Y < Cells; ++Y)
		{
			for (int32 X = 0; X < Cells; ++X)
			{
				const FVector2D Point = View.Min + FVector2D((X + 0.5) / Cells * Size.X, (Y + 0.5) / Cells * Size.Y);
				bool bCovered = false;
				for (const FVector2D& Blocker : Blockers)
				{
					if (FVector2D::Distance(Point, Blocker) <= FootprintRadiusCm)
					{
						bCovered = true;
						break;
					}
				}
				Empty += bCovered ? 0 : 1;
			}
		}
		Result.EmptyShare = static_cast<float>(Empty) / (Cells * Cells);
		return Result;
	}

	bool IsWithinSheet(const FResult& Result)
	{
		return Result.Trees >= SheetTreesMin && Result.Trees <= SheetTreesMax
			&& Result.RockClusters >= SheetRockClustersMin && Result.RockClusters <= SheetRockClustersMax
			&& Result.EmptyShare >= SheetEmptyShareMin;
	}

	bool LogWorldMeasure(APlayerController* Controller)
	{
		UWorld* const World = Controller ? Controller->GetWorld() : nullptr;
		int32 SizeX = 0, SizeY = 0;
		if (!World)
		{
			return false;
		}
		Controller->GetViewportSize(SizeX, SizeY);
		if (SizeX <= 0 || SizeY <= 0)
		{
			return false;
		}

		// 화면 네 모서리 → 바닥(Z=0) 교점의 바깥 사각형.
		FBox2D View(ForceInit);
		for (const FVector2D Corner : {FVector2D(0, 0), FVector2D(SizeX, 0), FVector2D(0, SizeY), FVector2D(SizeX, SizeY)})
		{
			FVector Origin, Direction;
			if (!Controller->DeprojectScreenPositionToWorld(Corner.X, Corner.Y, Origin, Direction) || FMath::IsNearlyZero(Direction.Z))
			{
				return false;
			}
			const float T = -Origin.Z / Direction.Z;
			if (T < 0.f)
			{
				return false;
			}
			View += FVector2D(Origin.X + Direction.X * T, Origin.Y + Direction.Y * T);
		}

		TArray<FPlacement> Placements;
		auto AddMesh = [&Placements](const UStaticMesh* Mesh, const FVector& Where)
		{
			if (Mesh)
			{
				Placements.Add({ClassifyMeshName(Mesh->GetName()), FVector2D(Where.X, Where.Y)});
			}
		};
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			AddMesh(It->GetStaticMeshComponent()->GetStaticMesh(), It->GetActorLocation());
		}
		for (TObjectIterator<UInstancedStaticMeshComponent> It; It; ++It)
		{
			if (It->GetWorld() != World)
			{
				continue;
			}
			for (int32 Index = 0; Index < It->GetInstanceCount(); ++Index)
			{
				FTransform Instance;
				if (It->GetInstanceTransform(Index, Instance, true))
				{
					AddMesh(It->GetStaticMesh(), Instance.GetLocation());
				}
			}
		}

		const FResult Result = Measure(Placements, View);
		int32 WorldTrees = 0, WorldRocks = 0;
		for (const FPlacement& Placement : Placements)
		{
			WorldTrees += Placement.Kind == EKind::Tree ? 1 : 0;
			WorldRocks += Placement.Kind == EKind::Rock ? 1 : 0;
		}
		const APawn* const Pawn = Controller->GetPawn();
		UE_LOG(LogSnarehaven, Log, TEXT("[Density] 플레이어 %s · 월드 전체 나무 %d · 바위 %d"),
			Pawn ? *Pawn->GetActorLocation().ToString() : TEXT("없음"), WorldTrees, WorldRocks);
		UE_LOG(LogSnarehaven, Log, TEXT("[Density] 화면 바닥 %.0f~%.0f x %.0f~%.0f cm · 표본 %d · 나무 %d(시트 %d~%d) · 바위 무리 %d(시트 %d~%d) · 빈 공간 %.2f(시트 %.2f 이상) · 시트 안 %s"),
			View.Min.X, View.Max.X, View.Min.Y, View.Max.Y, Placements.Num(),
			Result.Trees, SheetTreesMin, SheetTreesMax, Result.RockClusters, SheetRockClustersMin, SheetRockClustersMax,
			Result.EmptyShare, SheetEmptyShareMin, IsWithinSheet(Result) ? TEXT("예") : TEXT("아니오"));
		return true;
	}
}
