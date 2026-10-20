#include "World/LootChestActor.h"
#include "SnareCheckSwitch.h"

#include "Snarehaven.h"
#include "Combat/MeleeAttackComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/BackpackComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UI/ChestScreenWidget.h"
#include "UObject/ConstructorHelpers.h"
#include "World/ZoneTravelComponent.h"

namespace
{
	// 서버 `CHEST_COUNT` = DESIGN 8.10 「8.1 옆 2개」. 번호가 서버 기록 칸(`chests[1..2]`)과 짝이다.
	constexpr int32 ChestCount = 2;
	/** 상자 월드 폭 (DESIGN 8.11 「가구·상자 진짜 메시 크기」 `LootChestActor` 행). */
	constexpr float ChestWidthCm = 90.f;
	/** 금고 받침: 지름·높이(cm)와 금고 상자 확대 배율 — 엔진 기본 원기둥(지름 100·높이 100)을 눌러 쓴다. */
	constexpr float VaultPlinthDiameterCm = 170.f;
	constexpr float VaultPlinthHeightCm = 30.f;
	constexpr float VaultChestScale = 1.4f;
	/** 금고 폐허 둘레(cm): 담 조각 고리 반지름·기둥 고리 반지름·바닥 돌판 반지름. 담 사이 틈이 넓어 걸어 들어갈 길을 막지 않는다. */
	constexpr float VaultWallRingCm = 520.f;
	constexpr float VaultPillarRingCm = 430.f;
	constexpr float VaultSlabRingCm = 230.f;
}

ALootChestActor::ALootChestActor()
{
	// 거리 검사는 타이머가 한다 (engines/unreal.md 코드 규칙 2).
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	RootComponent = Body;
	// 동선을 막지 않게 충돌은 끈다 (상인과 같다).
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);
	// 상자 메시 (M13-37) — 받침점이 바닥이라 옮기지 않고, 수평 긴 변을 표 폭에 맞추는 한 배율만 준다.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ChestMeshAsset(
		TEXT("/Game/Game/Environment/Props/LootChest/SM_Prop_LootChest_01.SM_Prop_LootChest_01"));
	if (ChestMeshAsset.Succeeded())
	{
		Body->SetStaticMesh(ChestMeshAsset.Object);
		const FVector Size = ChestMeshAsset.Object->GetBoundingBox().GetSize();
		Body->SetRelativeScale3D(FVector(ChestWidthCm / FMath::Max(KINDA_SMALL_NUMBER, FMath::Max(Size.X, Size.Y))));
	}

	VaultPlinth = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VaultPlinth"));
	VaultPlinth->SetupAttachment(Body);
	VaultPlinth->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VaultPlinth->SetGenerateOverlapEvents(false);
	VaultPlinth->SetVisibility(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderAsset.Succeeded())
	{
		VaultPlinth->SetStaticMesh(CylinderAsset.Object);
	}
}

void ALootChestActor::ApplyVaultLook()
{
	if (VaultRuinIndex <= 0 || VaultPlinth == nullptr || VaultPlinth->GetStaticMesh() == nullptr)
	{
		return;
	}
	// 상자 크기는 Body 배율에 곱해진다 — 받침은 부모 배율을 되돌려 월드 크기를 맞춘다.
	const FVector ChestScale = Body->GetRelativeScale3D();
	Body->SetRelativeScale3D(ChestScale * VaultChestScale);
	const FVector Parent = Body->GetRelativeScale3D();
	VaultPlinth->SetRelativeScale3D(FVector(
		VaultPlinthDiameterCm / 100.f / Parent.X, VaultPlinthDiameterCm / 100.f / Parent.Y, VaultPlinthHeightCm / 100.f / Parent.Z));
	// 원기둥 가운데가 원점이라 윗면이 상자 바닥(0)에 오도록 반높이만큼 내린다.
	VaultPlinth->SetRelativeLocation(FVector(0.f, 0.f, -VaultPlinthHeightCm * 0.5f / Parent.Z));
	VaultPlinth->SetVisibility(true);
	UMaterialInterface* const Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (UMaterialInstanceDynamic* const Mid = UMaterialInstanceDynamic::Create(Base, this))
	{
		Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.02f, 0.45f, 0.42f));
		VaultPlinth->SetMaterial(0, Mid);
	}
	BuildVaultRuin();
}

void ALootChestActor::AddRuinPiece(UStaticMesh* Mesh, const FVector& WorldOffset, const FRotator& Rot, const FVector& SizeCm,
	const FLinearColor& Color, bool bBlock)
{
	UStaticMeshComponent* const Piece = NewObject<UStaticMeshComponent>(this);
	Piece->SetStaticMesh(Mesh);
	Piece->SetupAttachment(RootComponent);
	Piece->SetCollisionEnabled(bBlock ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (bBlock)
	{
		Piece->SetCollisionProfileName(TEXT("BlockAll"));
	}
	Piece->SetGenerateOverlapEvents(false);
	Piece->RegisterComponent();
	// 부모(상자 몸통) 배율과 상관없이 월드 크기로 맞춘다.
	Piece->SetWorldTransform(FTransform(Rot, GetActorLocation() + WorldOffset, SizeCm / 100.f));
	UMaterialInterface* const Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (UMaterialInstanceDynamic* const Mid = UMaterialInstanceDynamic::Create(Base, this))
	{
		Mid->SetVectorParameterValue(TEXT("Color"), Color);
		Piece->SetMaterial(0, Mid);
	}
}

void ALootChestActor::BuildVaultRuin()
{
	UStaticMesh* const Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* const Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cube == nullptr || Cylinder == nullptr)
	{
		return;
	}
	const FLinearColor Stone(0.12f, 0.12f, 0.13f);
	const FLinearColor StoneLight(0.2f, 0.2f, 0.21f);
	const FLinearColor Gold(0.8f, 0.5f, 0.05f);
	const float Seed = static_cast<float>(VaultRuinIndex) * 37.f;

	// 무너진 담: 네 방향 사이사이에 낮은 조각(키가 제각각, 한쪽으로 기움). 담 사이 틈이 넓다.
	for (int32 I = 0; I < 4; ++I)
	{
		const float Deg = Seed + 45.f + 90.f * I;
		const float Rad = FMath::DegreesToRadians(Deg);
		const float Height = 70.f + 55.f * FMath::Frac(FMath::Sin(Deg) * 43.f + 1.f);
		const float Length = 200.f + 70.f * (I % 2);
		AddRuinPiece(Cube, FVector(FMath::Cos(Rad), FMath::Sin(Rad), 0.f) * VaultWallRingCm + FVector(0.f, 0.f, Height * 0.5f - 15.f),
			FRotator(I % 2 ? 6.f : -4.f, Deg + 90.f, 0.f), FVector(Length, 60.f, Height), I % 2 ? Stone : StoneLight, true);
	}
	// 부러진 기둥 2개: 하나는 서 있고 하나는 누워 있다.
	{
		const float Rad = FMath::DegreesToRadians(Seed + 10.f);
		AddRuinPiece(Cylinder, FVector(FMath::Cos(Rad), FMath::Sin(Rad), 0.f) * VaultPillarRingCm + FVector(0.f, 0.f, 120.f),
			FRotator::ZeroRotator, FVector(70.f, 70.f, 260.f), StoneLight, true);
		const float Rad2 = FMath::DegreesToRadians(Seed + 200.f);
		AddRuinPiece(Cylinder, FVector(FMath::Cos(Rad2), FMath::Sin(Rad2), 0.f) * VaultPillarRingCm + FVector(0.f, 0.f, 30.f),
			FRotator(90.f, Seed, 0.f), FVector(60.f, 60.f, 200.f), Stone, false);
	}
	// 바닥 돌판 3장: 금고 앞뒤로 깔린 납작한 돌.
	for (int32 I = 0; I < 3; ++I)
	{
		const float Deg = Seed + 120.f * I + 20.f;
		const float Rad = FMath::DegreesToRadians(Deg);
		AddRuinPiece(Cube, FVector(FMath::Cos(Rad), FMath::Sin(Rad), 0.f) * VaultSlabRingCm + FVector(0.f, 0.f, -8.f),
			FRotator(0.f, Deg + 15.f, 0.f), FVector(160.f, 130.f, 24.f), StoneLight, false);
	}
	// 금고 자물쇠: 카메라 쪽(남, -Y) 금빛 판 하나만. 철띠는 검은 판으로 튀어나와 뺐다 — 폐허·청록 받침이 일반 상자와 가른다.
	constexpr float BandCm = 3.f;
	Body->UpdateBounds();
	const FVector Ext = Body->Bounds.BoxExtent;
	AddRuinPiece(Cube, FVector(0.f, -(Ext.Y + BandCm), Ext.Z), FRotator::ZeroRotator, FVector(26.f, 8.f, 34.f), Gold, false);
}

int32 ALootChestActor::SpawnBesideZone(UWorld* World, const FVector& ZoneCenter, float ZoneRadiusCm)
{
	if (World == nullptr || World->GetNetMode() == NM_Client || ZoneRadiusCm <= 0.f)
	{
		return 0;
	}
	for (TActorIterator<ALootChestActor> It(World); It; ++It)
	{
		if (It->GetChestIndex() > 0)
		{
			return 0;
		}
	}

	int32 Spawned = 0;
	for (int32 Index = 1; Index <= ChestCount; ++Index)
	{
		// 1 은 서쪽(-Y), 2 는 동쪽(+Y) 경계 위 — 구역 크기는 8.1 행 값을 그대로 빌린다(새 거리 숫자 0).
		const float Side = Index == 1 ? -1.f : 1.f;
		const FTransform Where(ZoneCenter + FVector(0.f, Side * ZoneRadiusCm, 0.f));
		ALootChestActor* const Chest = World->SpawnActorDeferred<ALootChestActor>(
			StaticClass(), Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Chest != nullptr)
		{
			Chest->SetChestIndex(Index);
			Chest->FinishSpawning(Where);
			++Spawned;
		}
	}
	UE_LOG(LogSnarehaven, Log, TEXT("[Chest] 위험구역 옆 상자 %d개 - 가운데 %s 반지름 %.0fcm"),
		Spawned, *ZoneCenter.ToString(), ZoneRadiusCm);
	return Spawned;
}

bool ALootChestActor::SpawnVault(UWorld* World, const FVector& Where, int32 RuinIndex)
{
	if (World == nullptr || World->GetNetMode() == NM_Client || RuinIndex <= 0)
	{
		return false;
	}
	for (TActorIterator<ALootChestActor> It(World); It; ++It)
	{
		if (It->GetVaultRuinIndex() == RuinIndex)
		{
			return false;
		}
	}
	const FTransform Transform(Where);
	ALootChestActor* const Vault = World->SpawnActorDeferred<ALootChestActor>(
		StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Vault == nullptr)
	{
		return false;
	}
	Vault->SetVaultRuinIndex(RuinIndex);
	Vault->FinishSpawning(Transform);
	UE_LOG(LogSnarehaven, Log, TEXT("[Chest] 금고 폐허 %d - %s"), RuinIndex, *Where.ToString());
	return true;
}

void ALootChestActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ALootChestActor, ChestIndex);
	DOREPLIFETIME(ALootChestActor, VaultRuinIndex);
}

void ALootChestActor::BeginPlay()
{
	Super::BeginPlay();

	ApplyVaultLook();

	// 화면은 사람이 보는 기기에서만. 헤드리스 서버엔 뷰포트가 없다.
	if (GetNetMode() != NM_DedicatedServer)
	{
		StartProximityScan();
	}
}

void ALootChestActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ProximityTimerHandle);
	}
	if (IsValid(ChestScreen))
	{
		ChestScreen->RemoveFromParent();
	}
	Super::EndPlay(EndPlayReason);
}

void ALootChestActor::StartProximityScan()
{
	UWorld* const World = GetWorld();
	APawn* const Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	const UMeleeAttackComponent* const Melee = IsValid(Pawn) ? Pawn->FindComponentByClass<UMeleeAttackComponent>() : nullptr;
	const float IntervalSeconds = IsValid(Melee) ? Melee->GetAttackIntervalSeconds() : -1.f;
	if (World == nullptr)
	{
		return;
	}
	if (IntervalSeconds <= 0.f)
	{
		// 폰(과 그 간격 값)이 붙기 전이다 — 주기를 지어내지 않고 다음 틱에 다시 본다.
		World->GetTimerManager().SetTimerForNextTick(this, &ALootChestActor::StartProximityScan);
		return;
	}
	World->GetTimerManager().SetTimer(
		ProximityTimerHandle, this, &ALootChestActor::HandleProximityScan, IntervalSeconds, true, 0.f);
}

void ALootChestActor::HandleProximityScan()
{
	UpdateProximity(UGameplayStatics::GetPlayerPawn(this, 0));
}

bool ALootChestActor::UpdateProximity(APawn* LocalPawn)
{
	const UZoneTravelComponent* const Travel = IsValid(LocalPawn) ? LocalPawn->FindComponentByClass<UZoneTravelComponent>() : nullptr;
	const float RangeCm = IsValid(Travel) ? Travel->GetTravelRangeCm() : -1.f;
	const bool bInRange = RangeCm >= 0.f
		&& FVector::DistSquared(LocalPawn->GetActorLocation(), GetActorLocation()) <= FMath::Square(RangeCm);

	if (!bInRange)
	{
		if (IsValid(ChestScreen) && ChestScreen->IsInViewport())
		{
			ChestScreen->RemoveFromParent();
		}
		// 끝난 보물(열었다·사라졌다)의 상자는 결과를 다 보고 떠날 때 치운다.
		const UBackpackComponent* const Backpack = IsValid(LocalPawn) ? LocalPawn->FindComponentByClass<UBackpackComponent>() : nullptr;
		if (!TreasureID.IsEmpty() && IsValid(Backpack) && Backpack->GetActiveTreasure().TreasureID != TreasureID)
		{
			Destroy();
		}
		return false;
	}

	APlayerController* const Controller = Cast<APlayerController>(LocalPawn->GetController());
	if (!IsValid(ChestScreen) && IsValid(Controller))
	{
		ChestScreen = CreateWidget<UChestScreenWidget>(Controller, UChestScreenWidget::StaticClass());
		if (IsValid(ChestScreen))
		{
			UBackpackComponent* const Backpack = LocalPawn->FindComponentByClass<UBackpackComponent>();
			if (VaultRuinIndex > 0)
			{
				ChestScreen->BindToVault(Backpack, VaultRuinIndex);
			}
			else
			{
				ChestScreen->BindToChest(Backpack, ChestIndex, TreasureID);
			}
		}
	}
	if (IsValid(ChestScreen) && !ChestScreen->IsInViewport())
	{
		ChestScreen->AddToViewport();
		UE_LOG(LogSnarehaven, Log, TEXT("[Chest] 상자 앞 (번호 %d, 금고 %d, 보물 %s) — 상자 화면을 올린다"),
			ChestIndex, VaultRuinIndex, TreasureID.IsEmpty() ? TEXT("-") : *TreasureID);
		// 3층 캡처용 (`-SnareChestOpen`) — 다가가자마자 한 번 연다. 굴림·지급은 그대로 서버다.
		if (SnareCheckSwitch::Param(TEXT("SnareChestOpen")))
		{
			ChestScreen->PressOpen();
		}
		// 3층 캡처용 (`-SnareChestDisclosure`) — 열기 전에 확률 고지 표를 보인다.
		if (SnareCheckSwitch::Param(TEXT("SnareChestDisclosure")))
		{
			ChestScreen->SetOpen(true);
			ChestScreen->SetDisclosureShown(true);
		}
	}
	return true;
}
