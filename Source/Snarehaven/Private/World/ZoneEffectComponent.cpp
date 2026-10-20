#include "World/ZoneEffectComponent.h"
#include "Snarehaven.h"
#include "SnarehavenCharacter.h"
#include "Backend/SnareEntryReject.h"
#include "Data/ZoneRow.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "GameFramework/Pawn.h"
#include "Player/PlayerVitalsComponent.h"
#include "UI/HuntHudWidget.h"
#include "World/SeaActor.h"
#include "World/SnareWeather.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "World/SnareZoneRules.h"
#include "World/WaterRules.h"
#include "World/ZoneTravelComponent.h"

UZoneEffectComponent::UZoneEffectComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 거절된 손님의 되돌리기를 호스트에게 부탁하려면 RPC 가 닿아야 한다.
	SetIsReplicatedByDefault(true);
}

void UZoneEffectComponent::BeginPlay()
{
	Super::BeginPlay();
	ZoneTable = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
}

void UZoneEffectComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateZoneEffects();
}

void UZoneEffectComponent::UpdateZoneEffects()
{
	const AActor* const Owner = GetOwner();
	const UWorld* const World = GetWorld();
	if (Owner == nullptr || World == nullptr)
	{
		return;
	}

	const FVector Location = Owner->GetActorLocation();
	const FZoneRow* const Zone = SnareZones::FindRegionAt(ZoneTable, FName(*UWorld::RemovePIEPrefix(World->GetMapName())), Location,
		ASeaActor::FindSea(this));
	const FName NowID = Zone ? Zone->ZoneID : NAME_None;
	if (NowID != CurrentRegionID)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Zone] %s → %s"), *GetNameSafe(Owner), NowID.IsNone() ? TEXT("칸 밖") : *NowID.ToString());
		CurrentRegionID = NowID;
	}

	if (UPlayerVitalsComponent* const Vitals = Owner->FindComponentByClass<UPlayerVitalsComponent>())
	{
		// 눈 오는 동안 설원 밖 추위 가산 (DESIGN 3.3 「날씨」) — 지붕 아래는 0. 지붕 선은 눈이 올 때만 쏜다.
		const float ZoneDrain = Zone ? Zone->HealthDrainPerSecond : 0.f;
		float Rain = 0.f, Snow = 0.f;
		if (const AGameStateBase* const GameState = World->GetGameState())
		{
			SnareWeather::Amounts(GameState->GetServerWorldTimeSeconds(), Rain, Snow);
		}
		bool bUnderRoof = false;
		if (Snow > 0.f && ZoneDrain <= 0.f)
		{
			RoofCheckTimer -= World->GetDeltaSeconds();
			if (RoofCheckTimer <= 0.f)
			{
				RoofCheckTimer = 0.5f;
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(SnareColdRoof), false, Owner);
				bColdUnderRoof = World->LineTraceSingleByChannel(Hit, Location, Location + FVector(0, 0, 900.f), ECC_Visibility, Params);
			}
			bUnderRoof = bColdUnderRoof;
		}
		Vitals->SetZoneHealthDrain(SnareWeather::ColdDrain(Snow, bUnderRoof, ZoneDrain));
	}

	const bool bOwesFee = Zone && Zone->bChargesEntryFee && !PaidRegionIDs.Contains(Zone->ZoneID);
	if (!bOwesFee)
	{
		LastFreeLocation = Location;
		bHasFreeLocation = true;
		return;
	}

	// 요금은 그 사람의 PlayFab 계정에서 빠진다 — 그 폰을 조종하는 기계만 묻는다.
	const APawn* const Pawn = Cast<APawn>(Owner);
	if (Pawn && Pawn->IsLocallyControlled() && !bFeePending)
	{
		RequestEntryFee(*Zone);
	}
}

void UZoneEffectComponent::RequestEntryFee(const FZoneRow& Zone)
{
	const FString Dest = UZoneTravelComponent::GetFerryDestKey(Zone.ZoneID);
	if (Dest.IsEmpty())
	{
		// 서버 요금표에 이름이 없는 칸 — 받을 길이 없으니 막지도 않는다.
		PaidRegionIDs.Add(Zone.ZoneID);
		return;
	}

	bFeePending = true;
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("dest"), Dest);
	const TWeakObjectPtr<UZoneEffectComponent> WeakThis(this);
	const FName ZoneID = Zone.ZoneID;
	SnareWater::CallServer(this, TEXT("PayFerry"), Args,
		[WeakThis, ZoneID](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UZoneEffectComponent* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bFeePending = false;
			if (!bOk)
			{
				// 못 냈다 — 칸 밖으로 돌려보낸다 (DESIGN 1.3 섬). 잔액은 안 건드린다.
				// 사유는 나룻배 버튼과 같은 표에서 — 화면에 안 띄우면 "왜 튕겼는지" 를 모른다.
				Self->LastMessage = SnareEntryReject::DescribeReply(SnareEntryReject::LoadTable(), bOk, Response);
				UE_LOG(LogSnarehaven, Log, TEXT("[Zone] %s 입장료 거절 — %s: %s"), *ZoneID.ToString(), *Error,
					*Self->LastMessage.ToString());
				if (const ASnarehavenCharacter* const Player = Cast<ASnarehavenCharacter>(Self->GetOwner()))
				{
					if (UHuntHudWidget* const Hud = Player->GetHuntHud())
					{
						Hud->ShowNoticeBanner(Self->LastMessage);
					}
				}
				if (Self->CurrentRegionID == ZoneID)
				{
					Self->PushOut();
				}
				return;
			}
			Self->LastMessage = FText::GetEmpty();
			Self->PaidRegionIDs.Add(ZoneID);
			// 잔액은 서버가 깎았다 — 캐시를 고치지 않고 다시 받는다 (CLAUDE.md 8).
			SnareWater::RefreshPlayerState(Self);
		});
}

void UZoneEffectComponent::PushOut()
{
	AActor* const Owner = GetOwner();
	if (Owner == nullptr || !bHasFreeLocation)
	{
		return;
	}
	if (Owner->HasAuthority())
	{
		Owner->SetActorLocation(LastFreeLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}
	else
	{
		Server_PushOut(LastFreeLocation);
	}
}

void UZoneEffectComponent::Server_PushOut_Implementation(FVector To)
{
	// ponytail: 호스트는 손님이 말한 자리를 믿는다 — 자기 몸을 칸 밖으로 빼는 것뿐이라 재화가 안 움직인다.
	if (AActor* const Owner = GetOwner())
	{
		Owner->SetActorLocation(To, false, nullptr, ETeleportType::TeleportPhysics);
	}
}
