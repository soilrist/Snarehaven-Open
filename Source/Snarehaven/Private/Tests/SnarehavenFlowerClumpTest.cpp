// M19-03 2층 검사 — 저장된 `L_Ranch`·`L_Hunt` 를 **실제로 불러와** 들꽃 무리 크기를 잰다 (DESIGN 8.9 들꽃 무리 표, ROADMAP M19-03):
// 꽃 포기(인스턴스·낱개 액터·몸이 꽃인 채집 더미)끼리 LinkCm 안이면 한 무리로 이어 붙이고(단일 연결),
// (a) 무리당 송이 수 최댓값 ≤ 7 — 땅을 덮는 융단이 없다, (b) 무리가 MinClumps 이상 — 꽃을 다 지워서 통과하지 않는다.
// 솎는 도구는 `tools/unreal/thin_flower_clumps.py`. 맵은 `SnarehavenPondShapeTest.cpp` 처럼 스트리밍한다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.World.Flowers; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Snarehaven.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/GatherableRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Gathering/GatherableActor.h"
#include "UObject/UnrealType.h"

namespace SnarehavenFlowerClumpTest
{
	// 테스트 전용 값 (Verification.md — 설계 숫자가 아니다). 송이 상한은 ROADMAP M19-03 합격선.
	constexpr int32 MaxBloomsPerClump = 7;
	constexpr int32 BloomsPerPlant = 3;        // 들꽃 포기 하나 = 꽃 세 송이(`make_house_meshes.py` wild_flower)
	constexpr double LinkCm = 200.0;           // 포기 폭(≈110cm)보다 넓게 — 이 안이면 화면에서 한 덩이로 붙어 보인다
	constexpr int32 MinClumps = 10;
	const TCHAR* FlowerMeshPrefixes[] = { TEXT("SM_Nature_WildFlower"), TEXT("SM_Nature_Flower") };

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

	bool IsFlowerName(const FString& MeshName)
	{
		for (const TCHAR* const Prefix : FlowerMeshPrefixes)
		{
			if (MeshName.StartsWith(Prefix))
			{
				return true;
			}
		}
		return false;
	}

	bool IsFlower(const UStaticMesh* Mesh)
	{
		return Mesh != nullptr && IsFlowerName(Mesh->GetName());
	}

	/** 채집 더미 몸 메시가 꽃인가 — 맵에 저장된 표·행 이름(보호 칸)을 리플렉션으로 읽어 행의 `MeshRef` 로 판단한다. */
	bool IsFlowerGatherable(const AGatherableActor* Gatherable)
	{
		const FObjectProperty* const TableProp = FindFProperty<FObjectProperty>(AGatherableActor::StaticClass(), TEXT("GatherableTable"));
		const FNameProperty* const RowProp = FindFProperty<FNameProperty>(AGatherableActor::StaticClass(), TEXT("GatherableRowName"));
		if (TableProp == nullptr || RowProp == nullptr)
		{
			return false;
		}
		const UDataTable* const Table = Cast<UDataTable>(TableProp->GetObjectPropertyValue_InContainer(Gatherable));
		const FName RowName = RowProp->GetPropertyValue_InContainer(Gatherable);
		const FGatherableRow* const Row = Table != nullptr ? Table->FindRow<FGatherableRow>(RowName, TEXT("FlowerClumpTest")) : nullptr;
		return Row != nullptr && IsFlowerName(Row->MeshRef.GetAssetName());
	}

	void CollectFlowers(const ULevel* Level, TArray<FVector2D>& OutPlants)
	{
		for (const AActor* const Actor : Level->Actors)
		{
			if (Actor == nullptr)
			{
				continue;
			}
			if (const AGatherableActor* const Gatherable = Cast<AGatherableActor>(Actor))
			{
				if (IsFlowerGatherable(Gatherable))
				{
					OutPlants.Add(FVector2D(Gatherable->GetActorLocation()));
				}
				continue;
			}
			TArray<const UInstancedStaticMeshComponent*> Instanced;
			Actor->GetComponents(Instanced);
			for (const UInstancedStaticMeshComponent* const Component : Instanced)
			{
				if (!IsFlower(Component->GetStaticMesh()))
				{
					continue;
				}
				for (int32 Index = 0; Index < Component->GetInstanceCount(); ++Index)
				{
					FTransform Transform;
					Component->GetInstanceTransform(Index, Transform, true);
					OutPlants.Add(FVector2D(Transform.GetLocation()));
				}
			}
			if (const AStaticMeshActor* const MeshActor = Cast<AStaticMeshActor>(Actor))
			{
				if (IsFlower(MeshActor->GetStaticMeshComponent()->GetStaticMesh()))
				{
					OutPlants.Add(FVector2D(MeshActor->GetActorLocation()));
				}
			}
		}
	}

	int32 FindRoot(TArray<int32>& Parent, int32 Index)
	{
		while (Parent[Index] != Index)
		{
			Parent[Index] = Parent[Parent[Index]];
			Index = Parent[Index];
		}
		return Index;
	}

	/** 단일 연결 무리마다 포기 수 — LinkCm 칸 격자로 이웃 칸만 본다(사냥터 수천 포기). */
	TArray<int32> ClumpSizes(const TArray<FVector2D>& Plants)
	{
		TArray<int32> Parent;
		Parent.SetNumUninitialized(Plants.Num());
		TMap<FIntPoint, TArray<int32>> Grid;
		for (int32 Index = 0; Index < Plants.Num(); ++Index)
		{
			Parent[Index] = Index;
			Grid.FindOrAdd(FIntPoint(FMath::FloorToInt(Plants[Index].X / LinkCm), FMath::FloorToInt(Plants[Index].Y / LinkCm))).Add(Index);
		}
		for (const TPair<FIntPoint, TArray<int32>>& Cell : Grid)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					const TArray<int32>* const Other = Grid.Find(Cell.Key + FIntPoint(DX, DY));
					if (Other == nullptr)
					{
						continue;
					}
					for (const int32 A : Cell.Value)
					{
						for (const int32 B : *Other)
						{
							if (A < B && FVector2D::Distance(Plants[A], Plants[B]) <= LinkCm)
							{
								Parent[FindRoot(Parent, A)] = FindRoot(Parent, B);
							}
						}
					}
				}
			}
		}
		TMap<int32, int32> Sizes;
		for (int32 Index = 0; Index < Plants.Num(); ++Index)
		{
			++Sizes.FindOrAdd(FindRoot(Parent, Index));
		}
		TArray<int32> Result;
		Sizes.GenerateValueArray(Result);
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFlowerClumpTest, "Snarehaven.World.Flowers.SmallClumpsNotCarpet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFlowerClumpTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenFlowerClumpTest;

	for (const TCHAR* const LevelPackage : { TEXT("/Game/Game/Maps/L_Ranch"), TEXT("/Game/Game/Maps/L_Hunt") })
	{
		ULevel* Level = nullptr;
		UWorld* const World = CreateWorldWithLevel(LevelPackage, Level);
		if (!TestNotNull(*FString::Printf(TEXT("%s 스트리밍"), LevelPackage), Level))
		{
			DestroyTestWorld(World);
			return false;
		}

		TArray<FVector2D> Plants;
		CollectFlowers(Level, Plants);
		const TArray<int32> Sizes = ClumpSizes(Plants);
		int32 Largest = 0;
		int32 Singles = 0;
		for (const int32 Size : Sizes)
		{
			Largest = FMath::Max(Largest, Size);
			Singles += Size == 1 ? 1 : 0;
		}
		const int32 LargestBlooms = Largest * BloomsPerPlant;
		TestTrue(*FString::Printf(TEXT("%s 무리당 송이 최댓값 %d ≤ %d"), LevelPackage, LargestBlooms, MaxBloomsPerClump),
			LargestBlooms <= MaxBloomsPerClump);
		TestTrue(*FString::Printf(TEXT("%s 꽃 무리 %d ≥ %d"), LevelPackage, Sizes.Num(), MinClumps), Sizes.Num() >= MinClumps);
		UE_LOG(LogSnarehaven, Display, TEXT("[FlowerClumps] %s 포기 %d · 무리 %d (한 포기 %d) · 가장 큰 무리 %d포기 = %d송이"),
			LevelPackage, Plants.Num(), Sizes.Num(), Singles, Largest, LargestBlooms);
		DestroyTestWorld(World);
	}
	return true;
}

#endif
