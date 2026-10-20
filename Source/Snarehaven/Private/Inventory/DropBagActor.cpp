#include "Inventory/DropBagActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** 가방 월드 폭 (DESIGN 8.11 「가구·상자 진짜 메시 크기」 `DropBagActor` 행). */
	constexpr float DropBagWidthCm = 50.f;
}

ADropBagActor::ADropBagActor()
{
	// 사라지는 건 수명(SetLifeSpan)이 센다 — 매 프레임 볼 것이 없다 (engines/unreal.md 코드 규칙 2).
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicatingMovement(false);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	RootComponent = Body;
	// 줍기는 거리로 판정한다(채집물과 같다) — 콜리전은 켜지 않는다.
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);

	// 가방 메시 (M13-37) — 받침점이 바닥이라 옮기지 않고, 수평 긴 변을 표 폭에 맞추는 한 배율만 준다.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BagMesh(
		TEXT("/Game/Game/Environment/Props/DropBag/SM_Prop_DropBag_01.SM_Prop_DropBag_01"));
	if (BagMesh.Succeeded())
	{
		Body->SetStaticMesh(BagMesh.Object);
		const FVector Size = BagMesh.Object->GetBoundingBox().GetSize();
		Body->SetRelativeScale3D(FVector(DropBagWidthCm / FMath::Max(KINDA_SMALL_NUMBER, FMath::Max(Size.X, Size.Y))));
	}
}

void ADropBagActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADropBagActor, OwnerPlayFabId);
	DOREPLIFETIME(ADropBagActor, DropId);
	DOREPLIFETIME(ADropBagActor, Items);
	DOREPLIFETIME(ADropBagActor, ExpiresAtUtc);
}

float ADropBagActor::SecondsUntil(const FString& InExpiresAtUtc, const FDateTime& NowUtc)
{
	FDateTime Expires;
	if (!FDateTime::ParseIso8601(*InExpiresAtUtc, Expires))
	{
		return 0.f;
	}
	return FMath::Max(0.f, static_cast<float>((Expires - NowUtc).GetTotalSeconds()));
}

void ADropBagActor::InitFromServer(const FString& InOwnerId, const FString& InDropId,
	const TArray<FInventorySlot>& InItems, const FString& InExpiresAtUtc)
{
	OwnerPlayFabId = InOwnerId;
	DropId = InDropId;
	Items = InItems;
	ExpiresAtUtc = InExpiresAtUtc;

	// 서버가 정한 시각에 맞춰 사라진다. 시각을 못 읽으면 수명을 두지 않는다 — 줍기를 서버가 `expired` 로 막고
	// 그 응답에 가방을 치운다(UBackpackComponent). 여기서 초를 지어내지 않는다 (CLAUDE.md 1).
	if (HasAuthority())
	{
		const float Seconds = SecondsUntil(ExpiresAtUtc, FDateTime::UtcNow());
		if (Seconds > 0.f)
		{
			SetLifeSpan(Seconds);
		}
	}
}
