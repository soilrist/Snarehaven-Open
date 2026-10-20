#include "World/SnareHookNpcActor.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/EquipmentComponent.h"
#include "Snarehaven.h"
#include "SnarehavenCharacter.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	/** 「우두머리 모자」 — 주간 우두머리 보상 모자(DESIGN 10.6)를 빌린다. 작살단 두목이라 같은 말. */
	const FName HookHatItemID(TEXT("item.chief_hat"));
	/** 짙은 쇳빛 머리 — 「회색 작살단」 천막 회색보다 어둡게, 솔방울·상인 머리색과 겹치지 않게. */
	const FLinearColor HookHairColor(0.12f, 0.12f, 0.14f);
}

ASnareHookNpcActor::ASnareHookNpcActor()
{
	// 캠프와 같이 각자 자기 화면에 세운다 — 복제하면 게스트 화면에 캠프 없이 갈고리만 설 수 있다.
	bReplicates = false;
	// 좌판 없이 몸만 — 캠프 메시(천막·모닥불)가 무대다.
	GetStall()->SetVisibility(false);
	GetStall()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VendorName = LOCTEXT("HookNpcName", "갈고리");
	HatItemID = HookHatItemID;
}

void ASnareHookNpcActor::ApplyKindLook()
{
	// 상점 종류 표를 물려받지 않는다(의뢰인과 같은 결).
	VendorName = LOCTEXT("HookNpcName", "갈고리");
	HatItemID = HookHatItemID;
	if (UEquipmentComponent* const Equip = GetEquipment())
	{
		Equip->SetHairColor(HookHairColor);
	}
}

UUserWidget* ASnareHookNpcActor::CreateScreen(APlayerController* Controller)
{
	return nullptr;
}

ASnareHookNpcActor* ASnareHookNpcActor::SpawnAtCamp(UWorld* World, const FVector& CampGround)
{
	if (World == nullptr)
	{
		return nullptr;
	}
	if (TActorIterator<ASnareHookNpcActor> Existing(World); Existing)
	{
		return *Existing;
	}
	// 액터 자리 = 캡슐 중심(상인과 같은 기준) — 바닥에서 플레이어 캡슐 반높이만큼 위.
	const float HalfHeight = GetDefault<ASnarehavenCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const FVector Spot = CampGround + FVector(CampOffsetXCm, 0.f, HalfHeight);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASnareHookNpcActor* const Hook = World->SpawnActor<ASnareHookNpcActor>(Spot, FRotator::ZeroRotator, Params);
	UE_LOG(LogSnarehaven, Log, TEXT("[Camp] 갈고리를 모닥불 앞에 세움 — (%.0f, %.0f, %.0f)"), Spot.X, Spot.Y, Spot.Z);
	return Hook;
}

#undef LOCTEXT_NAMESPACE
