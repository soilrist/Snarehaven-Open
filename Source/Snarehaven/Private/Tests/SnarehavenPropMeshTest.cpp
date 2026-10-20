// M13-40 2층 검사 — 목장 칸 말뚝·물고기 그림자가 진짜 메시로, 설계서 크기로 서는가.
// ① `ARanchSlotActor`: 보이는 몸이 `SM_Prop_TetherPost_01` 하나, 월드 높이 120 ±2 · 밑면 z 0 ±2 (DESIGN 3.2 「목장 칸 말뚝 진짜 모습」),
//    하이라이트가 말뚝에 켜진다.
// ② `AFishingSpotActor`: 그림자 셋이 `SM_Prop_FishShadow_01`, 월드 길이 50 ±2 · 높이 ≤3 (DESIGN 8.13 「물고기 그림자 진짜 모습」),
//    머리(끝 10% 폭이 넓은 쪽)가 도는 방향 앞(접선과 내적 > 0). 레벨에 놓인 포인트는 전부 그림자 재질이 채워져 있다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.PropMesh; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "MeshDescription.h"
#include "Ranch/RanchSlotActor.h"
#include "World/FishingSpotActor.h"

namespace SnarehavenPropMeshTest
{
	const TCHAR* TetherMeshName = TEXT("SM_Prop_TetherPost_01");
	const TCHAR* FishMeshName = TEXT("SM_Prop_FishShadow_01");
	// DESIGN 3.2 · 8.13 표 값 — 코드가 아니라 설계서 숫자를 그대로 대조한다.
	constexpr float TetherHeightCm = 120.f;
	constexpr float FishLengthCm = 50.f;
	constexpr float FishMaxHeightCm = 3.f;
	constexpr float ToleranceCm = 2.f;

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

	FString MeshNameOf(const UStaticMeshComponent* Component)
	{
		const UStaticMesh* const Mesh = Component ? Component->GetStaticMesh() : nullptr;
		return Mesh ? Mesh->GetName() : FString();
	}

	/**
	 * 메시 로컬 X 에서 머리 쪽 부호. 양 끝 10% 구간의 Y 폭을 재 넓은 쪽이 머리다 (DESIGN 8.13 「방향」 — 원본 실측과 같은 방법).
	 * 원본 정점(메시 설명)을 읽으므로 에디터 전용이다.
	 */
	float HeadSignAlongX(const UStaticMesh* Mesh, float& OutMinusWidth, float& OutPlusWidth)
	{
		OutMinusWidth = OutPlusWidth = 0.f;
		const FMeshDescription* const Description = Mesh ? const_cast<UStaticMesh*>(Mesh)->GetMeshDescription(0) : nullptr;
		if (Description == nullptr)
		{
			return 0.f;
		}
		const FBox Box = Mesh->GetBoundingBox();
		const float Band = (Box.Max.X - Box.Min.X) * 0.1f;
		FFloatInterval MinusY(TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
		FFloatInterval PlusY = MinusY;
		const TVertexAttributesConstRef<FVector3f> Positions = Description->GetVertexPositions();
		for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
		{
			const FVector3f P = Positions[Vertex];
			if (P.X <= Box.Min.X + Band)
			{
				MinusY.Include(P.Y);
			}
			else if (P.X >= Box.Max.X - Band)
			{
				PlusY.Include(P.Y);
			}
		}
		OutMinusWidth = MinusY.IsValid() ? MinusY.Size() : 0.f;
		OutPlusWidth = PlusY.IsValid() ? PlusY.Size() : 0.f;
		return OutPlusWidth > OutMinusWidth ? 1.f : (OutPlusWidth < OutMinusWidth ? -1.f : 0.f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenTetherPostMeshTest, "Snarehaven.PropMesh.RanchSlot.TetherPost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenTetherPostMeshTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenPropMeshTest;

	UWorld* const World = MakeWorld();
	ARanchSlotActor* const Slot = World->SpawnActor<ARanchSlotActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("슬롯 스폰"), Slot))
	{
		DestroyWorld(World);
		return false;
	}

	// 보이는 몸 = 메시가 있고 보이는 컴포넌트. 말뚝 하나만 남아야 한다(원기둥 자리표시가 같이 서면 안 된다).
	TArray<UStaticMeshComponent*> Meshes;
	Slot->GetComponents(Meshes);
	TArray<UStaticMeshComponent*> Bodies = Meshes.FilterByPredicate([](const UStaticMeshComponent* Component)
	{
		return Component->GetStaticMesh() != nullptr && Component->IsVisible();
	});
	TestEqual(TEXT("보이는 몸은 하나"), Bodies.Num(), 1);
	if (Bodies.Num() == 1)
	{
		UStaticMeshComponent* const Body = Bodies[0];
		const FBox WorldBox = Body->CalcBounds(Body->GetComponentTransform()).GetBox();
		const float Height = WorldBox.Max.Z - WorldBox.Min.Z;
		TestEqual(TEXT("말뚝 메시 이름"), MeshNameOf(Body), FString(TetherMeshName));
		TestEqual(TEXT("말뚝 월드 높이 = DESIGN 3.2 120cm ±2"), Height, TetherHeightCm, ToleranceCm);
		TestEqual(TEXT("말뚝 밑면 z = 0 ±2"), static_cast<float>(WorldBox.Min.Z), 0.f, ToleranceCm);
		TestEqual(TEXT("말뚝 충돌 끔"), Body->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		Slot->SetHighlighted(true);
		TestTrue(TEXT("하이라이트가 말뚝에 켜진다"), Body->bRenderCustomDepth);
		AddInfo(FString::Printf(TEXT("[PropMesh] 말뚝 %s 월드 %.1f x %.1f x %.1f cm · 밑면 z %.2f"), *MeshNameOf(Body),
			WorldBox.Max.X - WorldBox.Min.X, WorldBox.Max.Y - WorldBox.Min.Y, Height, WorldBox.Min.Z));
	}

	DestroyWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFishShadowMeshTest, "Snarehaven.PropMesh.FishingSpot.FishShadow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFishShadowMeshTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenPropMeshTest;

	UWorld* const World = MakeWorld();
	AFishingSpotActor* const Spot = World->SpawnActor<AFishingSpotActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	const URotatingMovementComponent* const Circling = Spot ? Spot->FindComponentByClass<URotatingMovementComponent>() : nullptr;
	if (!TestNotNull(TEXT("포인트 스폰"), Spot) || !TestNotNull(TEXT("도는 컴포넌트"), Circling)
		|| !TestNotNull(TEXT("도는 대상"), Circling->UpdatedComponent.Get()))
	{
		DestroyWorld(World);
		return false;
	}
	const FVector Center = Circling->UpdatedComponent->GetComponentLocation();

	TArray<UStaticMeshComponent*> Shadows;
	Spot->GetComponents(Shadows);
	TestEqual(TEXT("그림자 셋"), Shadows.Num(), 3);
	for (const UStaticMeshComponent* const Shadow : Shadows)
	{
		const UStaticMesh* const Mesh = Shadow->GetStaticMesh();
		TestEqual(TEXT("그림자 메시 이름"), MeshNameOf(Shadow), FString(FishMeshName));
		if (Mesh == nullptr)
		{
			continue;
		}
		// 월드 길이·높이 = 메시 크기 × 월드 배율 (돌아 있어서 월드 AABB 로는 길이를 못 잰다).
		const FVector Size = Mesh->GetBoundingBox().GetSize() * Shadow->GetComponentScale();
		TestEqual(*FString::Printf(TEXT("%s 월드 길이 = DESIGN 8.13 50cm ±2"), *Shadow->GetName()),
			static_cast<float>(Size.X), FishLengthCm, ToleranceCm);
		TestTrue(*FString::Printf(TEXT("%s 월드 높이 ≤ 3cm (%.2f)"), *Shadow->GetName(), Size.Z), Size.Z <= FishMaxHeightCm);

		float MinusWidth = 0.f;
		float PlusWidth = 0.f;
		const float HeadSign = HeadSignAlongX(Mesh, MinusWidth, PlusWidth);
		TestTrue(TEXT("머리 쪽을 가릴 수 있다(양 끝 폭이 다르다)"), HeadSign != 0.f);
		const FVector Head = Shadow->GetComponentTransform().TransformVectorNoScale(FVector(HeadSign, 0.f, 0.f));
		// 도는 방향 = 회전 속도 요만큼 조금 돌렸을 때 옮겨 간 쪽 (`URotatingMovementComponent` 가 피벗을 도는 방향 그대로).
		const FVector Radial = Shadow->GetComponentLocation() - Center;
		const FVector Motion = FRotator(0.f, Circling->RotationRate.Yaw * 0.1f, 0.f).RotateVector(Radial) - Radial;
		const double Dot = FVector::DotProduct(Head.GetSafeNormal2D(), Motion.GetSafeNormal2D());
		TestTrue(*FString::Printf(TEXT("%s 머리가 도는 방향 앞 (내적 %.2f > 0)"), *Shadow->GetName(), Dot), Dot > 0.0);
		AddInfo(FString::Printf(TEXT("[PropMesh] 그림자 %s %s 월드 %.1f x %.1f x %.2f cm · 끝 10%% 폭 -X %.3f / +X %.3f · 머리·도는 방향 내적 %.2f"),
			*Shadow->GetName(), *MeshNameOf(Shadow), Size.X, Size.Y, Size.Z, MinusWidth, PlusWidth, Dot));
	}
	DestroyWorld(World);

	// 재질은 레벨 배치(`tools/unreal/setup_sea.py` → `MI_Sea_FishShadow`)가 채운다 — 비면 BeginPlay 가 아무것도 안 씌운다.
	int32 SpotsInLevels = 0;
	for (const TCHAR* LevelPath : { TEXT("/Game/Game/Maps/L_Ranch.L_Ranch"), TEXT("/Game/Game/Maps/L_Hunt.L_Hunt") })
	{
		const UWorld* const Level = LoadObject<UWorld>(nullptr, LevelPath);
		if (!TestNotNull(LevelPath, Level) || !TestNotNull(LevelPath, Level->PersistentLevel.Get()))
		{
			continue;
		}
		for (const AActor* const Actor : Level->PersistentLevel->Actors)
		{
			if (const AFishingSpotActor* const LevelSpot = Cast<AFishingSpotActor>(Actor))
			{
				TestNotNull(*FString::Printf(TEXT("%s 그림자 재질"), *LevelSpot->SpotId), LevelSpot->ShadowMaterial.Get());
				++SpotsInLevels;
			}
		}
	}
	TestEqual(TEXT("레벨의 낚시 포인트 = DESIGN 8.13 거점 연못 2 · 해안·먼바다·강·늪 (L_Ranch 5 + L_Hunt 9)"), SpotsInLevels, 14);
	return true;
}

#endif
