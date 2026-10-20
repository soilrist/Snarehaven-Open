#include "World/SnareViewFade.h"

#include "SnareCheckSwitch.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

namespace SnareViewFade
{
	float Coverage(const FVector& Camera, const FVector& Target, const FVector& Point, const float Strength)
	{
		// 머티리얼 Custom 노드와 같은 식 — 바꾸면 `create_view_fade_material.py` 도 같이.
		const FVector ToCamera = Camera - Target;
		const float Length = ToCamera.Size();
		if (Length < 1.f)
		{
			return 0.f;
		}
		const FVector Dir = ToCamera / Length;
		const FVector V = Point - Target;
		const float Along = FVector::DotProduct(V, Dir);
		const float Perp = (V - Dir * Along).Size();
		const float Ahead = FMath::Clamp((Along - AheadStartCm) / AheadRampCm, 0.f, 1.f);
		const float Edge = FMath::Clamp((RadiusCm - Perp) / (RadiusCm * EdgeSoftFraction), 0.f, 1.f);
		return Ahead * Edge * FMath::Clamp(Strength, 0.f, 1.f) * MaxCoverage;
	}

	bool IsInsideRoofFootprint(const FVector2D& Min, const FVector2D& Max, const FVector& Pawn)
	{
		return Pawn.X > Min.X + RoofInsideMarginCm && Pawn.X < Max.X - RoofInsideMarginCm
			&& Pawn.Y > Min.Y + RoofInsideMarginCm && Pawn.Y < Max.Y - RoofInsideMarginCm;
	}

	float StepStrength(const float Current, const bool bWant, const float DeltaSeconds)
	{
		const float Step = DeltaSeconds / FadeSeconds;
		return FMath::Clamp(Current + (bWant ? Step : -Step), 0.f, 1.f);
	}

	bool WantFade(const bool bHasViewer, const bool bStoreShot)
	{
		return bHasViewer && !bStoreShot;
	}
}

namespace
{
	const TCHAR* const CollectionPath = TEXT("/Game/Game/Environment/Nature/MPC_ViewFade.MPC_ViewFade");
	const FName TargetParam(TEXT("ViewFadeTarget"));   // xyz 캐릭터 가슴, w 반지름
	const FName CameraParam(TEXT("ViewFadeCamera"));   // xyz 카메라, w 켜짐 정도
}

bool USnareViewFadeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USnareViewFadeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USnareViewFadeSubsystem, STATGROUP_Tickables);
}

void USnareViewFadeSubsystem::UpdateRoofs(const APawn* Pawn)
{
	if (!bRoofsCached)
	{
		bRoofsCached = true;
		for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
		{
			const UStaticMeshComponent* const Mesh = It->GetStaticMeshComponent();
			if (Mesh && Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetName().StartsWith(TEXT("SM_Building_CottageRoof")))
			{
				RoofActors.Add(*It);
			}
		}
	}
	const FVector Location = Pawn ? Pawn->GetActorLocation() : FVector(FLT_MAX);
	for (const TWeakObjectPtr<AActor>& Weak : RoofActors)
	{
		AActor* const Roof = Weak.Get();
		if (Roof == nullptr)
		{
			continue;
		}
		FVector Origin, Extent;
		Roof->GetActorBounds(true, Origin, Extent);
		const bool bInside = Pawn != nullptr
			&& SnareViewFade::IsInsideRoofFootprint(FVector2D(Origin.X - Extent.X, Origin.Y - Extent.Y), FVector2D(Origin.X + Extent.X, Origin.Y + Extent.Y), Location);
		if (Roof->IsHidden() != bInside)
		{
			Roof->SetActorHiddenInGame(bInside);
		}
	}
}

void USnareViewFadeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* const World = GetWorld();
	// 이 기기에서 보는 사람 하나만 — 남의 캐릭터 때문에 내 화면 나무가 뚫리지 않는다.
	const APlayerController* const Controller = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* const Pawn = Controller ? Controller->GetPawn() : nullptr;
	bool bStoreShot = false;
#if !UE_BUILD_SHIPPING
	// 스토어 스크린샷엔 디더 자국이 남지 않게 끈다 (출시 구성엔 없음).
	int32 StoreShotCoins = 0;
	bStoreShot = SnareCheckSwitch::Value(TEXT("SnareStoreShot="), StoreShotCoins);
#endif
	const bool bWant = SnareViewFade::WantFade(Pawn != nullptr && Controller->PlayerCameraManager != nullptr, bStoreShot);
	if (World != nullptr)
	{
		UpdateRoofs(Pawn);
	}
	if (!bWant && Strength <= 0.f)
	{
		return;
	}
	Strength = SnareViewFade::StepStrength(Strength, bWant, DeltaTime);
	UMaterialParameterCollection* const Collection = LoadObject<UMaterialParameterCollection>(nullptr, CollectionPath);
	if (Collection == nullptr)
	{
		return;
	}
	UMaterialParameterCollectionInstance* const Instance = World->GetParameterCollectionInstance(Collection);
	if (Instance == nullptr)
	{
		return;
	}
	if (bWant)
	{
		const FVector Target = Pawn->GetActorLocation() + FVector(0.f, 0.f, SnareViewFade::AimHeightCm);
		Instance->SetVectorParameterValue(TargetParam, FLinearColor(Target.X, Target.Y, Target.Z, SnareViewFade::RadiusCm));
		const FVector Camera = Controller->PlayerCameraManager->GetCameraLocation();
		Instance->SetVectorParameterValue(CameraParam, FLinearColor(Camera.X, Camera.Y, Camera.Z, Strength));
	}
	else
	{
		Instance->SetVectorParameterValue(CameraParam, FLinearColor(0.f, 0.f, 0.f, Strength));
	}
}
