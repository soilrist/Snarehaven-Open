#include "Ranch/RanchGameMode.h"
#include "SnareCheckSwitch.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "SnarehavenCharacter.h"
#include "Misc/CommandLine.h"
#include "World/FruitTreeActor.h"
#include "World/FruitTreeSync.h"
#include "World/QuestGiverActor.h"
#include "World/ShopVendorActor.h"

namespace
{
	// 🟡 상인 자리 (DESIGN 8.14 "잡화점 자리" 줄) — 시작점(0,0) 옆, 목장 칸 줄(x=250) 밖이라 칸 버튼과 안 겹친다.
	// 높이는 뺀 바닥 자리 — 상인 액터는 몸 캡슐 중심에 서니 플레이어 캡슐 반높이를 더한다(M13-39).
	const FVector2D VendorLocation(0.f, 600.f);
	// 3층 캡처(`-SnareShopOpen`)는 걷지 않고 상점을 찍어야 해서 시작점 바로 옆(상호작용 150cm 안)에 세운다.
	const FVector2D VendorCheckLocation(120.f, 0.f);
	// 상인 방향 (DESIGN 8.14 「상인·좌판 방향」 yaw 180) — 카메라 팔 yaw 0 의 반대.
	constexpr float VendorYawDegrees = 180.f;
	/** 의뢰인은 상인에서 +Y 로 이만큼 — 좌판 긴 변 260 의 반 + 캡슐·틈. 🟡 화면 배치 값. */
	constexpr float QuestGiverOffsetYCm = 260.f;

	// 🟡 과일나무 자리 (DESIGN 8.18) — 목장 울타리 **안** 빈 풀밭 4곳(기본 카메라에 다 들어옴, 길·좌판·우물과 안 겹침).
	// X 가 커지는 순서(slot 0 이 앞쪽). 잎 지름 200 이라 서로 250 넘게 떨어뜨린다.
	const FVector2D FruitTreeSpots[] = {FVector2D(-207.f, 527.f), FVector2D(150.f, -650.f), FVector2D(470.f, 150.f), FVector2D(520.f, 560.f)};

	/**
	 * 🟡 전문 상인 셋 (DESIGN 8.14 「가게 여럿」) — 앞 울타리(x −300) 밖 장터 줄. 좌판 긴 변 260 이 Y 로 눕고
	 * 상인 사이 400 이라 좌판끼리 안 닿는다. 대문 앞 길(y 0)은 비운다.
	 */
	struct FMarketStall
	{
		EShopKind Kind;
		FVector2D Spot;
	};
	const FMarketStall MarketStalls[] = {
		{EShopKind::Farming, FVector2D(-800.f, -1050.f)},
		{EShopKind::Fishing, FVector2D(-800.f, -650.f)},
		{EShopKind::Clothes, FVector2D(-800.f, 650.f)},
		{EShopKind::SweetPotato, FVector2D(-800.f, -1450.f)},   // DESIGN 8.17 — 농부 상인(y −1050)에서 400
	};
}

ARanchGameMode::ARanchGameMode()
{
	// 목장엔 사냥할 동물이 없다 — 포획 버튼·동물 HP 바를 띄우지 않는다 (감독 지시 2026-09-19).
	bShowHuntHud = false;

	// 대신 목장 칸의 Ready 아이콘·미수확 개수를 보여주는 HUD 를 띄운다 (ROADMAP M2-06a · DESIGN 3.2).
	bShowRanchHud = true;
}

void ARanchGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 게임 모드는 권위 쪽에만 있다 — 상인은 복제되어 게스트 화면에도 선다.
	SpawnVendors(GetWorld());
	SpawnFruitTrees(GetWorld());
}

FVector ARanchGameMode::GetFruitTreeSpot(const int32 Slot)
{
	if (SnareCheckSwitch::Param(TEXT("SnareFruitTreesNear")))
	{
		// 3층 근접 캡처 — 시작점 바로 앞(−X)에 한 줄로 세워 `-SnareCameraDistance` 로 가까이서 본다.
		return FVector(-60.f, -420.f + 280.f * Slot, 0.f);
	}
	const FVector2D Spot = FruitTreeSpots[FMath::Clamp(Slot, 0, SnareFruitTree::SlotCount - 1)];
	return FVector(Spot.X, Spot.Y, 0.f);
}

void ARanchGameMode::SpawnFruitTrees(UWorld* World)
{
	if (World == nullptr)
	{
		return;
	}
	FString Look;
	SnareCheckSwitch::Value(TEXT("SnareFruitTrees="), Look);
	for (int32 Slot = 0; Slot < SnareFruitTree::SlotCount; ++Slot)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AFruitTreeActor* const Tree = World->SpawnActor<AFruitTreeActor>(AFruitTreeActor::StaticClass(),
			FTransform(FRotator(0.f, Slot * 70.f, 0.f), GetFruitTreeSpot(Slot)), Params);
		if (Tree == nullptr)
		{
			continue;
		}
		Tree->SlotIndex = Slot;
		SnareFruitTree::FTreeView View;
		View.Slot = Slot;
		const FString Letter = Look.Mid(Slot, 1);
		// g = 다 자람(열매 3) · h = 다 따 낸 나무(열매 0) · s = 묘목 · 그 밖 = 빈 자리
		View.Stage = (Letter == TEXT("g") || Letter == TEXT("h")) ? SnareFruitTree::EStage::Grown
			: Letter == TEXT("s") ? SnareFruitTree::EStage::Sapling : SnareFruitTree::EStage::Empty;
		static const TCHAR* const Cycle[] = {TEXT("item.pear"), TEXT("item.peach"), TEXT("item.cherry"), TEXT("item.orange")};
		View.Fruit = Cycle[Slot % 4];
		View.Fruits = SnareFruitTree::VisibleFruits(View.Stage, Letter == TEXT("h") ? 0 : 3);
		Tree->ApplyView(View);
	}
	// 캡처용 모양(`-SnareFruitTrees=`)을 골랐으면 서버 답으로 덮지 않는다. 아니면 서버에 묻는다 — 답이 오기 전엔 빈 자리 그림.
	if (Look.IsEmpty())
	{
		FruitTreeSync::Refresh(World);
	}
}

void ARanchGameMode::SpawnVendors(UWorld* World)
{
	if (World == nullptr)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// 3층 캡처 `-SnareShopOpen -SnareVendor=Fishing` 이면 그 상인이 캡처 자리에 선다(없으면 잡화상).
	const bool bShopCheck = SnareCheckSwitch::Param(TEXT("SnareShopOpen"));
	FString CheckVendor;
	SnareCheckSwitch::Value(TEXT("SnareVendor="), CheckVendor);
	const float HalfHeight = GetVendorSpawnTransform(false).GetLocation().Z;
	for (const FMarketStall& Stall : MarketStalls)
	{
		const bool bHere = bShopCheck && CheckVendor == StaticEnum<EShopKind>()->GetNameStringByValue(static_cast<int64>(Stall.Kind));
		const FTransform Spot = bHere ? GetVendorSpawnTransform(true)
			: FTransform(FRotator(0.f, VendorYawDegrees, 0.f), FVector(Stall.Spot, HalfHeight));
		AShopVendorActor* const Vendor = World->SpawnActorDeferred<AShopVendorActor>(AShopVendorActor::StaticClass(), Spot,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Vendor != nullptr)
		{
			Vendor->SetShopKind(Stall.Kind);
			Vendor->FinishSpawning(Spot);
		}
	}
	World->SpawnActor<AShopVendorActor>(AShopVendorActor::StaticClass(),
		GetVendorSpawnTransform(bShopCheck && CheckVendor.IsEmpty()), Params);
	// 의뢰인은 상인 옆 (DESIGN 8.14 「의뢰」). `-SnareQuestOpen` 3층 캡처면 상인 캡처 자리에 대신 선다.
	const bool bQuestCheck = AQuestGiverActor::IsOpenSwitchSet();
	FTransform QuestSpot = GetVendorSpawnTransform(bQuestCheck);
	QuestSpot.AddToTranslation(bQuestCheck ? FVector::ZeroVector : FVector(0.f, QuestGiverOffsetYCm, 0.f));
	World->SpawnActor<AQuestGiverActor>(AQuestGiverActor::StaticClass(), QuestSpot, Params);
}

FTransform ARanchGameMode::GetVendorSpawnTransform(const bool bShopCheck)
{
	const FVector2D Spot = bShopCheck ? VendorCheckLocation : VendorLocation;
	const float HalfHeight = GetDefault<ASnarehavenCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	// 상인 앞(액터 +X)이 게임 카메라 쪽(세계 −X)을 본다 — DESIGN 8.14 「상인·좌판 방향」(M13-41): "상인·좌판이
	// 게임 카메라 쪽(세계 −X, yaw 180)을 본다 — 첫 PlayerStart 를 보지 않는다". 카메라 팔은 yaw 0 고정
	// (`ASnarehavenCharacter::ApplyCameraSettings`)이라 그 반대쪽이다.
	return FTransform(FRotator(0.f, VendorYawDegrees, 0.f), FVector(Spot, HalfHeight));
}
