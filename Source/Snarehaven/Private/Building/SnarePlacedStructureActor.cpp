#include "Building/SnarePlacedStructureActor.h"
#include "Snarehaven.h"
#include "SnareCheckSwitch.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "UI/SnareUiStyle.h"
#include "World/SnareHouseRoof.h"
#include "World/SnareSound.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 자리표시 모양 (게임 규칙 숫자가 아니라 "메시 대기 동안 얼마나 크게 보일까" — Autonomy 🟢 임시 그래픽).
	// 엔진 큐브는 한 변 100cm 다.
	constexpr float CubeSizeCm = 100.f;
	constexpr float FurnitureHeightCm = 60.f;
	constexpr float TentHeightCm = 220.f;
	/** 미리보기가 바닥과 겹쳐 깜빡이지 않게 살짝 띄운다. */
	constexpr float PreviewLiftCm = 2.f;
	/** 바닥 찾기 — 위아래로 이만큼 쏜다. */
	constexpr float GroundTraceHalfCm = 5000.f;
	/** 러그 띄움 상한 — 바위 하나에 걸려 러그가 공중에 뜨지 않게 (DESIGN 8.11 「러그 깔기」 🟡 루프#1389). */
	constexpr float RugMaxLiftCm = 20.f;
	/** 계단 비탈 판 두께 — 보이지 않는 충돌이라 캡슐이 판을 뚫지 않을 만큼만 (모양 값, 규칙 숫자 아님). */
	constexpr float StairRampThicknessCm = 10.f;
	constexpr float FireLightIntensity = 3000.f;
	constexpr float FireLightRadiusCm = 400.f;
	/** 전등 점광 높이 = 메시 키 × 이 비율 — 전구 알 가운데(`make_power_meshes.py` 갓 밑 102cm − 1.5 / 키 124 ≈ 0.81).
	 *  바닥(루트)에 두면 빛이 땅을 스치듯 비춰 기둥 발치에 점만 맺힌다 (감독 보정 10-20 13:10, `lh1404_gen_on`). */
	constexpr float LampBulbHeightRatio = 0.8f;
	/** 발전기 공회전 고리 (DESIGN 8.11 「연료 발전기 숫자」 소리 — 안쪽 300 = 집 한 방, 바깥 1500 = 텐트 땅 지름 남짓). */
	const FName GeneratorLoopCue(TEXT("Generator_Loop"));
	constexpr float GeneratorLoopVolume = 0.5f;
	constexpr float GeneratorHearFullRadiusCm = 300.f;
	constexpr float GeneratorHearMaxRadiusCm = 1500.f;
	/** 도는 동안 다시 보는 간격(초) — 연료가 다 타면 이만큼 안에 소리가 멎는다(전력 셈 쪽 1초 판과 같은 눈높이). */
	constexpr float GeneratorSoundPollSeconds = 1.f;
	/** 도는 발전기 몸 떨림 (DESIGN 8.11 「연료 발전기 숫자」 떨림 🟡 루프#1408) — 진폭 cm · 빠르기 Hz(공회전 소리 맥 25Hz 와 같게). */
	constexpr float GeneratorShakeAmplitudeCm = 1.f;
	constexpr float GeneratorShakeHz = 25.f;

	/** 색 칸 이름 — 기본 도형 재질, 그리고 가구 재질(`import_equipment_weapons.py` 가 끼우는 곱하기 칸). */
	const FName TintParameter(TEXT("Color"));
	/** 오두막 슬롯 재질 색 칸 (`setup_ranch_buildings.py` 의 `MI_Building_Cottage*`) · 집 색을 입히는 덮개 슬롯. */
	const FName HouseColorParameter(TEXT("DiffuseColor"));
	const FName HouseRoofSlot(TEXT("Roof"));
	/** 집 덮개 숨김을 다시 보는 간격(초) — 문턱을 넘고 이만큼 안에 숨는다. */
	constexpr float HouseRoofPollSeconds = 0.2f;

	/** 고른 가구 테두리 두께 (DESIGN 8.12 "고른 가구 테두리 두께"). */
	constexpr float SelectedOutlineThicknessCm = 5.f;
	/** `tools/unreal/create_outline_material.py` 가 만드는 칸 이름. */
	const FName OutlineColorParameter(TEXT("OutlineColor"));
	const FName OutlineThicknessParameter(TEXT("OutlineThicknessCm"));
	/** 껍질 밑면을 바닥 위로 띄우는 높이 — 바닥 아래로 부풀면 위에서 본 앞·옆 띠가 바닥에 가린다 (M13-34). */
	const FName OutlineGroundLiftParameter(TEXT("OutlineGroundLiftCm"));

	// 월드 환경 시트(docs/reference/world_environment.md) 에서 고른 색 🟡. UI 팔레트(SnareUiStyle)는 버튼 색이라 쓰지 않는다.
	FLinearColor FromHex(const TCHAR* Hex)
	{
		return FLinearColor(FColor::FromHex(Hex));
	}
}

ASnarePlacedStructureActor::ASnarePlacedStructureActor()
{
	// 놓인 뒤 움직이지 않는다 — 매 프레임 볼 것이 없다 (engines/unreal.md 코드 규칙 2).
	// 도는 발전기 몸 떨림·켜진 선풍기 날개만 예외 — 틱은 꺼 두고 그동안만 켠다 (M108-07d-2 · 08d-1).
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	bAlwaysRelevant = true;

	// 루트는 바닥 한 점, 몸은 그 위로 반 높이 — 서버 칸에는 높이가 없어 바닥을 기준으로 세운다.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent);
	// 채집물과 같은 이유 — 상호작용은 거리로 판정한다. 막는 충돌은 메시가 설 때 함께 정한다.
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);

	// 행에 메시가 없을 때의 자리표시 — 큐브에 색 칸이 있는 기본 도형 재질.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeAsset.Succeeded())
	{
		CubeMesh = CubeAsset.Object;
		Body->SetStaticMesh(CubeMesh);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TintAsset(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (TintAsset.Succeeded())
	{
		CubeMaterial = TintAsset.Object;
		Body->SetMaterial(0, CubeMaterial);
	}
	// 집 몸통·덮개 (M108-02). 덮개는 상호작용·충돌 없이 보이기만 — 몸과 같은 이유.
	HouseRoof = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HouseRoof"));
	HouseRoof->SetupAttachment(RootComponent);
	HouseRoof->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HouseRoof->SetGenerateOverlapEvents(false);
	HouseRoof->SetVisibility(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HouseAsset(
		TEXT("/Game/Game/Environment/Buildings/SM_Building_TentHouse_01.SM_Building_TentHouse_01"));
	if (HouseAsset.Succeeded())
	{
		HouseMesh = HouseAsset.Object;
	}
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HouseRoofAsset(
		TEXT("/Game/Game/Environment/Buildings/SM_Building_TentHouseRoof_01.SM_Building_TentHouseRoof_01"));
	if (HouseRoofAsset.Succeeded())
	{
		HouseRoofMesh = HouseRoofAsset.Object;
		HouseRoof->SetStaticMesh(HouseRoofMesh);
	}
	// 2층 집 (M108-06c) — 1층 앞벽은 바닥에, 위층 벽·2층 바닥 판은 2층 바닥 높이에. 위층 덮개는 `HouseRoof` 가 메시를 갈아 끼운다.
	// 위층 벽·바닥 판만 막는다: 폰만(카메라·총알·놓기 바닥 찾기는 지나간다). 종류를 움직이는 것으로 둬 바닥 찾기(`CellToWorld`·
	// `RugGroundLiftCm` — 움직이지 않는 것만 쏜다)가 2층 판에 맞아 1층 가구가 판 위에 서지 않게 한다. 켜기는 2층일 때만(`RefreshVisual`).
	HouseFront = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HouseFront"));
	HouseFront->SetupAttachment(RootComponent);
	HouseFront->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HouseFront->SetGenerateOverlapEvents(false);
	HouseFront->SetVisibility(false);
	HouseUpper = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HouseUpper"));
	UpperFloor = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("UpperFloor"));
	for (UStaticMeshComponent* const Upper : { HouseUpper.Get(), static_cast<UStaticMeshComponent*>(UpperFloor.Get()) })
	{
		Upper->SetupAttachment(RootComponent);
		Upper->SetRelativeLocation(FVector(0.f, 0.f, SnareBuildRules::UpperFloorZCm));
		Upper->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Upper->SetCollisionObjectType(ECC_WorldDynamic);
		Upper->SetCollisionResponseToAllChannels(ECR_Ignore);
		Upper->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Upper->SetGenerateOverlapEvents(false);
		Upper->SetVisibility(false);
	}
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HouseFrontAsset(
		TEXT("/Game/Game/Environment/Buildings/SM_Building_TentHouseFront_01.SM_Building_TentHouseFront_01"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HouseUpperAsset(
		TEXT("/Game/Game/Environment/Buildings/SM_Building_TentHouseUpper_01.SM_Building_TentHouseUpper_01"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HouseUpperRoofAsset(
		TEXT("/Game/Game/Environment/Buildings/SM_Building_TentHouseUpperRoof_01.SM_Building_TentHouseUpperRoof_01"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> UpperFloorAsset(
		TEXT("/Game/Game/Environment/Buildings/SM_Building_TentHouseFloor2_01.SM_Building_TentHouseFloor2_01"));
	if (HouseFrontAsset.Succeeded() && HouseUpperAsset.Succeeded() && HouseUpperRoofAsset.Succeeded() && UpperFloorAsset.Succeeded())
	{
		HouseFrontMesh = HouseFrontAsset.Object;
		HouseUpperMesh = HouseUpperAsset.Object;
		HouseUpperRoofMesh = HouseUpperRoofAsset.Object;
		UpperFloorMesh = UpperFloorAsset.Object;
		HouseFront->SetStaticMesh(HouseFrontMesh);
		HouseUpper->SetStaticMesh(HouseUpperMesh);
		UpperFloor->SetStaticMesh(UpperFloorMesh);
	}
	// 계단 비탈 (M108-06d) — 위층 판과 같은 충돌 종류(폰만, 움직이는 것). 켜기·눕히기는 계단 행일 때 `RefreshStairRamp`.
	StairRamp = CreateDefaultSubobject<UBoxComponent>(TEXT("StairRamp"));
	StairRamp->SetupAttachment(RootComponent);
	StairRamp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StairRamp->SetCollisionObjectType(ECC_WorldDynamic);
	StairRamp->SetCollisionResponseToAllChannels(ECR_Ignore);
	StairRamp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	StairRamp->SetGenerateOverlapEvents(false);
	StairRamp->SetHiddenInGame(true);
	// 모바일 렌더러라 커스텀 뎁스 후처리 대신 오버레이 머티리얼(뒤집은 껍질)로 테두리를 그린다 (M13-24).
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> OutlineAsset(
		TEXT("/Game/Game/VFX/Outline/M_Outline_Selected.M_Outline_Selected"));
	if (OutlineAsset.Succeeded())
	{
		OutlineMaterial = OutlineAsset.Object;
	}

	// 선풍기 날개 (M108-08d-1) — 몸에 붙어 몸 배율·돌림·떨림을 같이 받는다. 보이기만, 충돌 없음.
	Spinner = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Spinner"));
	Spinner->SetupAttachment(Body);
	Spinner->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Spinner->SetGenerateOverlapEvents(false);
	Spinner->SetVisibility(false);

	FireLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FireLight"));
	FireLight->SetupAttachment(RootComponent);
	FireLight->SetIntensity(FireLightIntensity);
	FireLight->SetAttenuationRadius(FireLightRadiusCm);
	FireLight->SetLightColor(FromHex(TEXT("F0895A")));
	FireLight->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableAsset(
		TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (ItemTableAsset.Succeeded())
	{
		ItemTable = ItemTableAsset.Object;
	}
}

void ASnarePlacedStructureActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASnarePlacedStructureActor, ItemID);
	DOREPLIFETIME(ASnarePlacedStructureActor, TentId);
	DOREPLIFETIME(ASnarePlacedStructureActor, StructureId);
	DOREPLIFETIME(ASnarePlacedStructureActor, OwnerPlayFabId);
	DOREPLIFETIME(ASnarePlacedStructureActor, Rotation);
	DOREPLIFETIME(ASnarePlacedStructureActor, bHouse);
	DOREPLIFETIME(ASnarePlacedStructureActor, HouseColor);
	DOREPLIFETIME(ASnarePlacedStructureActor, HouseFloors);
	DOREPLIFETIME(ASnarePlacedStructureActor, PaintItemID);
	DOREPLIFETIME(ASnarePlacedStructureActor, PlacedFloor);
	DOREPLIFETIME(ASnarePlacedStructureActor, GeneratorFuelEndServerSeconds);
	DOREPLIFETIME(ASnarePlacedStructureActor, CellSizeCm);
}

void ASnarePlacedStructureActor::BeginPlay()
{
	Super::BeginPlay();
	RefreshVisual();
}

void ASnarePlacedStructureActor::Destroyed()
{
	// 치운 계단 자리는 2층 바닥 구멍도 메운다 — 그 전에 그 집 2층에 선 폰을 문 앞으로 옮긴다(M108-06f-3, 갇힘 없음).
	// 옮기기는 호스트 판정이라 클라(복제된 계단이 지워질 때·게스트 화면 사본)에선 안 한다. 이 함수는 치울 때만 불린다 —
	// 맵을 내릴 때는 안 불려 다 같이 사라지는 중에 집을 다시 깔지 않는다.
	UWorld* const World = GetWorld();
	if (World != nullptr && !bPreview && PlaceableFunction == EPlaceableFunction::Stairs && GetNetMode() != NM_Client)
	{
		for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
		{
			if (*It == this || !It->IsTwoStoryHouse() || It->CellSizeCm <= 0.f)
			{
				continue;
			}
			const FVector InHouse = It->GetActorTransform().InverseTransformPosition(GetActorLocation());
			if (FMath::Abs(InHouse.X) < It->Footprint.X * It->CellSizeCm * 0.5f && FMath::Abs(InHouse.Y) < It->Footprint.Y * It->CellSizeCm * 0.5f)
			{
				const int32 Moved = It->MoveUpperFloorPawnsToDoor();
				UE_LOG(LogSnarehaven, Log, TEXT("[Base] 계단 %s 치움 — 집 %s 2층에 있던 몸 %d 을 문 앞으로"), *StructureId, *It->TentId, Moved);
			}
		}
	}
	NotifyHousesOfStairs(this);
	Super::Destroyed();
}

void ASnarePlacedStructureActor::InitStructure(FName InItemID, const FString& InTentId, const FString& InStructureId,
	const FString& InOwnerPlayFabId, int32 InRotation, float InCellSizeCm)
{
	CellSizeCm = InCellSizeCm;
	ItemID = InItemID;
	TentId = InTentId;
	StructureId = InStructureId;
	OwnerPlayFabId = InOwnerPlayFabId;
	Rotation = SnareBuildRules::SnapRotation(InRotation);
	RefreshVisual();
}

void ASnarePlacedStructureActor::SetWallSlot(const FIntPoint& InCell, int32 InSide, int32 InHeightCm)
{
	WallCell = InCell;
	WallSide = InSide;
	WallHeightCm = InHeightCm;
}

void ASnarePlacedStructureActor::SetTopSlot(const FString& InHostId, int32 InSlot)
{
	TopHostId = InHostId;
	TopSlot = InSlot;
}

bool ASnarePlacedStructureActor::GetTopSlotLocation(int32 Slot, FVector& OutLocation) const
{
	const UStaticMesh* const Mesh = Body->GetStaticMesh();
	if (TopSlotCount <= 0 || Slot < 0 || Slot >= TopSlotCount || Mesh == nullptr)
	{
		return false;
	}
	const FBox Bounds = Mesh->GetBoundingBox();
	const FVector Size = Bounds.GetSize();
	const FVector Along = Size.Y > Size.X ? FVector(0.f, Size.Y, 0.f) : FVector(Size.X, 0.f, 0.f);
	const FVector Local = FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Max.Z)
		+ Along * SnareBuildRules::TopSlotAlong(Slot, TopSlotCount);
	OutLocation = Body->GetComponentTransform().TransformPosition(Local);
	return true;
}

void ASnarePlacedStructureActor::SetHouse(bool bInHouse, int32 InColor, int32 InFloors)
{
	bHouse = bInHouse;
	HouseColor = InColor;
	HouseFloors = FMath::Max(1, InFloors);
	RefreshVisual();
}

void ASnarePlacedStructureActor::SetPaint(FName InPaintItemID)
{
	PaintItemID = InPaintItemID;
	RefreshVisual();
}

void ASnarePlacedStructureActor::SetPreviewVerdict(ESnarePlacementVerdict Verdict)
{
	bPreview = true;
	PreviewVerdict = Verdict;
	RefreshVisual();
}

void ASnarePlacedStructureActor::SetSelectedOutline(bool bOn)
{
	if (bOn && !IsValid(OutlineInstance) && IsValid(OutlineMaterial))
	{
		OutlineInstance = UMaterialInstanceDynamic::Create(OutlineMaterial, this);
		OutlineInstance->SetVectorParameterValue(OutlineColorParameter, SnareUiStyle::UiButtonHighlight());
		OutlineInstance->SetScalarParameterValue(OutlineThicknessParameter, SelectedOutlineThicknessCm);
		// 미리보기와 같은 이유(바닥과 겹쳐 깜빡임)로 같은 높이만큼 띄운다.
		OutlineInstance->SetScalarParameterValue(OutlineGroundLiftParameter, PreviewLiftCm);
	}
	Body->SetOverlayMaterial(bOn ? OutlineInstance : nullptr);
}

bool ASnarePlacedStructureActor::HasSelectedOutline() const
{
	return Body->GetOverlayMaterial() != nullptr;
}

void ASnarePlacedStructureActor::SetLampLit(bool bLit, float Candela, float RadiusCm, const FLinearColor& Color)
{
	if (bPreview)
	{
		return;
	}
	bLampLit = bLit;
	if (bLit)
	{
		// 불 행이 아닌 전등에만 온다 — 같은 점광을 전구 줄 값으로 바꿔 쓴다(모바일 점광 수를 늘리지 않게 하나로).
		FireLight->SetIntensityUnits(ELightUnits::Candelas);
		FireLight->SetIntensity(Candela);
		FireLight->SetAttenuationRadius(RadiusCm);
		FireLight->SetLightColor(Color);   // 기본 = DESIGN 3.3 「밤 불빛」 전구 주황, 불꽃은 모닥불 색
		FireLight->SetCastShadows(false);
	}
	RefreshFireLight();
}

void ASnarePlacedStructureActor::SetElectricOn(bool bOn)
{
	if (bElectricOn != bOn)
	{
		bElectricOn = bOn;
		RefreshVisual();
	}
}

void ASnarePlacedStructureActor::SetFloorViewHidden(bool bHide)
{
	if (bFloorViewHidden == bHide)
	{
		return;
	}
	bFloorViewHidden = bHide;
	Body->SetVisibility(!bHide);
	Spinner->SetVisibility(!bHide && Spinner->GetStaticMesh() != nullptr);
	RefreshFireLight();
}

void ASnarePlacedStructureActor::RefreshFireLight()
{
	FireLight->SetVisibility((IsFireFunction() || bLampLit) && !bPreview && !bFloorViewHidden);
}

bool ASnarePlacedStructureActor::IsFireFunction() const
{
	return PlaceableFunction == EPlaceableFunction::Campfire || PlaceableFunction == EPlaceableFunction::Grill
		|| PlaceableFunction == EPlaceableFunction::Smelter;   // 제련소 화구도 불빛
}

bool ASnarePlacedStructureActor::IsLampLightVisible() const
{
	return FireLight->IsVisible();
}

double ASnarePlacedStructureActor::ServerNowSeconds(const UWorld* World)
{
	const AGameStateBase* const GameState = World != nullptr ? World->GetGameState() : nullptr;
	return GameState != nullptr ? GameState->GetServerWorldTimeSeconds() : (World != nullptr ? World->GetTimeSeconds() : 0.0);
}

void ASnarePlacedStructureActor::SetGeneratorFuelSecondsLeft(double SecondsLeft)
{
	GeneratorFuelEndServerSeconds = SecondsLeft > 0.0 ? ServerNowSeconds(GetWorld()) + SecondsLeft : 0.0;
	RefreshGeneratorSound();
}

void ASnarePlacedStructureActor::OnRep_GeneratorFuel()
{
	RefreshGeneratorSound();
}

void ASnarePlacedStructureActor::RefreshGeneratorSound()
{
	const bool bWant = IsGeneratorRunning() && !bPreview;
	if (bWant != bGeneratorSoundOn)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Sound] 발전기 %s 공회전 %s — 연료 %.0f초"), *StructureId, bWant ? TEXT("시작") : TEXT("멈춤"),
			GetGeneratorFuelSecondsLeft());
	}
	bGeneratorSoundOn = bWant;
	// 몸 떨림도 같은 조건 — 소리 장치가 없는 기기에서도 떤다. 멈추면 놓인 자리로 돌려놓는다.
	if (bWant != bGeneratorShaking)
	{
		bGeneratorShaking = bWant;
		RefreshTickEnabled();
		GeneratorShakePhase = 0.f;
		Body->SetRelativeLocation(BodyRestLocation);
	}
	if (bWant && !IsValid(GeneratorLoop))
	{
		GeneratorLoop = USnareSoundSubsystem::SpawnLoopAttached(this, GeneratorLoopCue, RootComponent, GeneratorLoopVolume,
			GeneratorHearFullRadiusCm, GeneratorHearMaxRadiusCm);
	}
	else if (!bWant && IsValid(GeneratorLoop))
	{
		GeneratorLoop->Stop();
		GeneratorLoop->DestroyComponent();
		GeneratorLoop = nullptr;
	}
	UWorld* const World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	FTimerManager& Timers = World->GetTimerManager();
	if (bWant && !Timers.IsTimerActive(GeneratorSoundTimer))
	{
		Timers.SetTimer(GeneratorSoundTimer, this, &ASnarePlacedStructureActor::RefreshGeneratorSound, GeneratorSoundPollSeconds, true);
	}
	else if (!bWant)
	{
		Timers.ClearTimer(GeneratorSoundTimer);
	}
}

void ASnarePlacedStructureActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickGeneratorShake(DeltaSeconds);
	TickElectricSpin(DeltaSeconds);
}

void ASnarePlacedStructureActor::RefreshTickEnabled()
{
	SetActorTickEnabled(bGeneratorShaking || bSpinning);
}

void ASnarePlacedStructureActor::RefreshSpinner(const FItemRow* Row, bool bBodyIsRowMesh)
{
	UStaticMesh* const SpinMesh = Row != nullptr && bBodyIsRowMesh ? Row->PlacedSpinMeshRef.LoadSynchronous() : nullptr;
	if (Spinner->GetStaticMesh() != SpinMesh)
	{
		Spinner->SetStaticMesh(SpinMesh);
		Spinner->EmptyOverrideMaterials();
	}
	Spinner->SetVisibility(SpinMesh != nullptr && !bFloorViewHidden);
	if (SpinMesh != nullptr)
	{
		// 몸 메시 좌표 그대로 — 몸에 붙어 있어 배율·돌림은 몸 것을 받는다. 젖힘은 몸 X 축, 돌림은 그 안쪽 자기 Y 축.
		Spinner->SetRelativeLocation(Row->PlacedSpinPivotCm);
		SpinTilt = FQuat(FVector::XAxisVector, FMath::DegreesToRadians(Row->PlacedSpinTiltDeg));
		SpinTurnsPerSecond = FMath::Max(0.f, Row->PlacedSpinTurnsPerSecond);
		Spinner->SetRelativeRotation(SpinTilt * FQuat(FVector::YAxisVector, FMath::DegreesToRadians(SpinAngleDeg)));
	}
	// 켜짐 모습(전력 셈 또는 `-SnareElectricOnDemo`)일 때만 돈다 — 미리보기는 켜짐 모습이 없어 안 돈다.
	const bool bWant = SpinMesh != nullptr && bShowingElectricOnMesh && SpinTurnsPerSecond > 0.f;
	if (bWant != bSpinning)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Build] %s 날개 %s — 각 %.0f°"), *StructureId, bWant ? TEXT("돎") : TEXT("멈춤"), SpinAngleDeg);
		bSpinning = bWant;
		bSpinLoggedTurn = false;
		RefreshTickEnabled();
	}
}

void ASnarePlacedStructureActor::RefreshElectricLoop(const FItemRow* Row)
{
	// 켜짐 모습(전력 셈 또는 `-SnareElectricOnDemo`)일 때만 — 미리보기는 켜짐 모습이 없어 안 튼다. 각 기기가 스스로 정한다(소리는 복제 안 함).
	const bool bWant = Row != nullptr && !Row->PlacedOnLoopCue.IsNone() && bShowingElectricOnMesh;
	if (bWant != bElectricLoopOn)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Sound] %s 켜짐 고리 %s %s"), *StructureId, Row != nullptr ? *Row->PlacedOnLoopCue.ToString() : TEXT("-"),
			bWant ? TEXT("시작") : TEXT("멈춤"));
		bElectricLoopOn = bWant;
	}
	if (bWant && !IsValid(ElectricLoop))
	{
		ElectricLoop = USnareSoundSubsystem::SpawnLoopAttached(this, Row->PlacedOnLoopCue, RootComponent, Row->PlacedOnLoopVolume,
			Row->PlacedOnLoopInnerCm, Row->PlacedOnLoopOuterCm);
	}
	else if (!bWant && IsValid(ElectricLoop))
	{
		ElectricLoop->Stop();
		ElectricLoop->DestroyComponent();
		ElectricLoop = nullptr;
	}
}

bool ASnarePlacedStructureActor::IsElectricLoopPlaying() const
{
	return IsValid(ElectricLoop) && ElectricLoop->IsPlaying();
}

void ASnarePlacedStructureActor::TickElectricSpin(float DeltaSeconds)
{
	if (!bSpinning)
	{
		return;
	}
	const float Next = SpinAngleDeg + 360.f * SpinTurnsPerSecond * DeltaSeconds;
	if (Next >= 360.f && !bSpinLoggedTurn)
	{
		// 틱이 실제로 도는지 게임 로그로 보는 한 줄 — 돌기 시작한 뒤 첫 바퀴만.
		UE_LOG(LogSnarehaven, Log, TEXT("[Build] %s 날개 한 바퀴 돎"), *StructureId);
		bSpinLoggedTurn = true;
	}
	SpinAngleDeg = FMath::Fmod(Next, 360.f);
	Spinner->SetRelativeRotation(SpinTilt * FQuat(FVector::YAxisVector, FMath::DegreesToRadians(SpinAngleDeg)));
}

void ASnarePlacedStructureActor::TickGeneratorShake(float DeltaSeconds)
{
	if (!bGeneratorShaking)
	{
		return;
	}
	// 원을 그리며 떤다 — 한 축 사인은 프레임이 0 에 걸리면 안 보이지만 원은 진폭이 늘 그대로다(전기톱 떨림과 같은 까닭).
	GeneratorShakePhase = FMath::Fmod(GeneratorShakePhase + UE_TWO_PI * GeneratorShakeHz * DeltaSeconds, UE_TWO_PI);
	Body->SetRelativeLocation(BodyRestLocation
		+ FVector(FMath::Cos(GeneratorShakePhase), FMath::Sin(GeneratorShakePhase), 0.f) * GeneratorShakeAmplitudeCm);
}

FVector ASnarePlacedStructureActor::GetBodyOffsetFromRest() const
{
	return Body->GetRelativeLocation() - BodyRestLocation;
}

FName ASnarePlacedStructureActor::GetGeneratorLoopCue()
{
	return GeneratorLoopCue;
}

bool ASnarePlacedStructureActor::IsGeneratorSoundPlaying() const
{
	return IsValid(GeneratorLoop) && GeneratorLoop->IsPlaying();
}

bool ASnarePlacedStructureActor::IsGeneratorRunning() const
{
	return PlaceableFunction == EPlaceableFunction::Generator && GetGeneratorFuelSecondsLeft() > 0.0;
}

double ASnarePlacedStructureActor::GetGeneratorFuelSecondsLeft() const
{
	return GeneratorFuelEndServerSeconds > 0.0 ? FMath::Max(GeneratorFuelEndServerSeconds - ServerNowSeconds(GetWorld()), 0.0) : 0.0;
}

void ASnarePlacedStructureActor::UpdateHouseRoof(const FVector& Feet)
{
	const bool bShell = IsTent() && bHouse && !bPreview && IsValid(HouseRoofMesh);
	const bool bInside = bShell && SnareHouseRoof::IsUnderRoof(HouseRoof->Bounds.GetBox(), Feet);
	const bool bShown = bShell && !bInside;
	if (HouseRoof->IsVisible() != bShown)
	{
		HouseRoof->SetVisibility(bShown);
	}
	// 2층 집이면 1층 앞벽·위층은 층에 따라 — 1층에 서면 덮개와 같이 다 숨어 방이 보이고, 2층에 서면 위층 덮개(지붕·위층
	// 앞벽)만 숨어 2층 판·위층 뒷·옆벽이 남는다. 루트 = 집 바닥이라 발 높이는 루트에서 잰다.
	const bool bUpstairs = bInside && SnareBuildRules::IsOnUpperFloor(static_cast<float>(Feet.Z - GetActorLocation().Z));
	const bool bUpperShown = (bShown || bUpstairs) && IsTwoStoryHouse();
	for (UStaticMeshComponent* const Part : { HouseFront.Get(), HouseUpper.Get(), static_cast<UStaticMeshComponent*>(UpperFloor.Get()) })
	{
		if (Part->IsVisible() != bUpperShown)
		{
			Part->SetVisibility(bUpperShown);
		}
	}
	// 2층 가구는 2층 판과 같이 — 1층 안에 서면 숨어 1층 방이 가려지지 않는다. 같은 텐트 주소의 2층 가구만.
	// 2층 집이 아니게 돼도 한 번 더 돌아 숨긴 것을 되돌린다.
	UWorld* const World = GetWorld();
	if (World == nullptr || TentId.IsEmpty() || !IsTent())
	{
		return;
	}
	const bool bHideUpstairs = bInside && !bUpstairs && IsTwoStoryHouse();
	for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
	{
		if (*It == this || It->bPreview || It->IsTent() || It->TentId != TentId)
		{
			continue;
		}
		const bool bUpperPiece = It->PlacedFloor >= 2;
		// 앞벽 장식(M108-09)은 그 층 앞벽과 같이 — 안에 서면 앞벽이 숨는데 장식만 남으면 허공에 뒷면을 보이며 방을 가린다.
		// 게스트 화면엔 벽 변이 없어(복제 안 함) 액터 자리로 가른다: 집 공간에서 앞(-X) 벽 면 위.
		const bool bFrontDecor = bInside && It->PlaceableFunction == EPlaceableFunction::WallDecor
			&& bUpperPiece == bUpstairs && IsOnFrontWall(It->GetActorLocation());
		It->SetFloorViewHidden((bUpperPiece && bHideUpstairs) || bFrontDecor);
	}
}

bool ASnarePlacedStructureActor::IsOnFrontWall(const FVector& WorldLocation) const
{
	if (CellSizeCm <= 0.f)
	{
		return false;
	}
	// 벽 장식 액터 자리 = 벽 칸 변 가운데(`SnareBuildRules::WallSlotLocation`) — 앞벽 면에서 칸 1/4 안이면 앞벽 것.
	// 바닥 가구 가운데는 앞 줄이어도 면에서 반 칸 안쪽이라 섞이지 않는다.
	const FVector InHouse = GetActorTransform().InverseTransformPosition(WorldLocation);
	return FMath::Abs(InHouse.X + Footprint.X * CellSizeCm * 0.5f) <= CellSizeCm * 0.25f
		&& FMath::Abs(InHouse.Y) < Footprint.Y * CellSizeCm * 0.5f;
}

bool ASnarePlacedStructureActor::IsTwoStoryHouse() const
{
	return IsTent() && bHouse && HouseFloors >= 2 && !bPreview && IsValid(HouseMesh) && IsValid(HouseRoofMesh)
		&& IsValid(HouseFrontMesh) && IsValid(HouseUpperMesh) && IsValid(HouseUpperRoofMesh) && IsValid(UpperFloorMesh);
}

void ASnarePlacedStructureActor::RefreshUpperFloor(const AActor* IgnoreStairs)
{
	UpperFloor->ClearInstances();
	UWorld* const World = GetWorld();
	if (World == nullptr || !IsTwoStoryHouse() || CellSizeCm <= 0.f)
	{
		return;
	}
	// 판 한 칸 메시는 칸 한 변(100)으로 만들었다 — 칸 크기가 바뀌어도 칸에 맞게 늘린다.
	const float TileScale = CellSizeCm / FMath::Max(KINDA_SMALL_NUMBER, UpperFloorMesh->GetBoundingBox().GetSize().X);
	for (int32 X = 0; X < Footprint.X; ++X)
	{
		for (int32 Y = 0; Y < Footprint.Y; ++Y)
		{
			// 칸 가운데(액터 공간, 판 높이) — 이 점이 계단 발자국 안이면 구멍. 계단 돌림은 액터 돌림이라 계단 공간으로 옮겨 잰다.
			const FVector Local((X - (Footprint.X - 1) * 0.5f) * CellSizeCm, (Y - (Footprint.Y - 1) * 0.5f) * CellSizeCm, 0.f);
			const FVector At = GetActorTransform().TransformPosition(Local);
			bool bHole = false;
			for (TActorIterator<ASnarePlacedStructureActor> It(World); It && !bHole; ++It)
			{
				if (*It == this || *It == IgnoreStairs || It->bPreview || It->PlaceableFunction != EPlaceableFunction::Stairs
					|| It->CellSizeCm <= 0.f)
				{
					continue;
				}
				const FVector InStairs = It->GetActorTransform().InverseTransformPosition(At);
				bHole = FMath::Abs(InStairs.X) < It->Footprint.X * It->CellSizeCm * 0.5f
					&& FMath::Abs(InStairs.Y) < It->Footprint.Y * It->CellSizeCm * 0.5f;
			}
			if (!bHole)
			{
				UpperFloor->AddInstance(FTransform(FRotator::ZeroRotator, Local, FVector(TileScale, TileScale, 1.f)));
			}
		}
	}
}

int32 ASnarePlacedStructureActor::MoveUpperFloorPawnsToDoor()
{
	UWorld* const World = GetWorld();
	if (World == nullptr || !IsTwoStoryHouse() || CellSizeCm <= 0.f)
	{
		return 0;
	}
	const FTransform& House = GetActorTransform();
	const FVector Door = House.TransformPosition(FVector(SnareBuildRules::HouseDoorStepX(Footprint.X, CellSizeCm), 0.f, 0.f));
	// 문 밖을 보고 선다 — 나온 쪽으로 걸어가면 된다.
	const FRotator Facing = (-GetActorForwardVector()).GetSafeNormal2D().Rotation();
	int32 Moved = 0;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* const Pawn = *It;
		const FVector InHouse = House.InverseTransformPosition(HouseViewFeet(*Pawn));
		if (FMath::Abs(InHouse.X) >= Footprint.X * CellSizeCm * 0.5f || FMath::Abs(InHouse.Y) >= Footprint.Y * CellSizeCm * 0.5f
			|| !SnareBuildRules::IsOnUpperFloor(static_cast<float>(InHouse.Z)))
		{
			continue;
		}
		// 발을 문 앞 땅(집 바닥 높이)에 — 엔진이 겹치면 가까운 빈자리를 찾고, 그래도 안 되면 그대로 놓는다(위에 남지 않게).
		const FVector Target = Door + FVector(0.f, 0.f, Pawn->GetSimpleCollisionHalfHeight() + 5.f);
		if (!Pawn->TeleportTo(Target, Facing))
		{
			Pawn->TeleportTo(Target, Facing, false, true);
		}
		++Moved;
	}
	return Moved;
}

void ASnarePlacedStructureActor::NotifyHousesOfStairs(const AActor* IgnoreStairs) const
{
	UWorld* const World = GetWorld();
	if (World == nullptr || bPreview || PlaceableFunction != EPlaceableFunction::Stairs)
	{
		return;
	}
	for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
	{
		if (It->IsTent() && It->bHouse && It->HouseFloors >= 2)
		{
			It->RefreshUpperFloor(IgnoreStairs);
		}
	}
}

void ASnarePlacedStructureActor::RefreshStairRamp()
{
	const bool bRamp = PlaceableFunction == EPlaceableFunction::Stairs && !bPreview && CellSizeCm > 0.f;
	StairRamp->SetCollisionEnabled(bRamp ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (!bRamp)
	{
		return;
	}
	// 액터 공간에서 윗면 = 낮은 끝 (-Run/2, 바닥) → 높은 끝 (+Run/2, 2층 바닥) 선. 판 가운데는 그 선 가운데에서 윗면 법선
	// (-sin, 0, cos) 반대로 반 두께 — 윗면이 디딤 모서리 선과 맞아 몸이 디딤 위에 선다. 폭은 발자국 짧은 변 그대로.
	const float RunCm = Footprint.X * CellSizeCm;
	const float SlopeDegrees = SnareBuildRules::StairsSlopeDegrees(RunCm);
	const float SlopeRadians = FMath::DegreesToRadians(SlopeDegrees);
	const float HalfThicknessCm = StairRampThicknessCm * 0.5f;
	const float LengthCm = FMath::Sqrt(FMath::Square(RunCm) + FMath::Square(SnareBuildRules::UpperFloorZCm));
	StairRamp->SetBoxExtent(FVector(LengthCm * 0.5f, Footprint.Y * CellSizeCm * 0.5f, HalfThicknessCm));
	StairRamp->SetRelativeRotation(FRotator(SlopeDegrees, 0.f, 0.f));
	StairRamp->SetRelativeLocation(FVector(HalfThicknessCm * FMath::Sin(SlopeRadians), 0.f,
		SnareBuildRules::UpperFloorZCm * 0.5f - HalfThicknessCm * FMath::Cos(SlopeRadians)));
}

void ASnarePlacedStructureActor::PollHouseRoof()
{
	// 지붕은 이 기기에서 보는 사람 기준 — 로컬 플레이어 하나만 본다(거점 오두막 `USnareHouseRoofSubsystem` 과 같다).
	const APlayerController* const Controller = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	const APawn* const Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	// 폰이 없으면(로딩·관전) 덮개를 덮은 채로 — 아주 먼 점을 발로 친다.
	UpdateHouseRoof(Pawn != nullptr ? HouseViewFeet(*Pawn) : FVector(WORLD_MAX));
}

FVector ASnarePlacedStructureActor::HouseViewFeet(const APawn& Pawn)
{
	return Pawn.GetActorLocation() - FVector(0.f, 0.f, Pawn.GetSimpleCollisionHalfHeight());
}

void ASnarePlacedStructureActor::OnRep_Structure()
{
	// 복제로 온 진짜가 서면, 게스트가 응답 직후 자기 화면에만 세워 둔 같은 주소의 사본은 치운다 (M15-26).
	if (!bPreview && !TentId.IsEmpty() && GetWorld() != nullptr)
	{
		for (TActorIterator<ASnarePlacedStructureActor> It(GetWorld()); It; ++It)
		{
			if (*It != this && !It->bPreview && !It->GetIsReplicated() && It->TentId == TentId && It->StructureId == StructureId)
			{
				It->Destroy();
			}
		}
	}
	RefreshVisual();
}

FLinearColor ASnarePlacedStructureActor::VerdictColor(ESnarePlacementVerdict Verdict)
{
	switch (Verdict)
	{
	case ESnarePlacementVerdict::Green: return FromHex(TEXT("7BBF5A"));
	case ESnarePlacementVerdict::Red: return FromHex(TEXT("E8514F"));
	default: return FromHex(TEXT("3E7BD6"));
	}
}

FLinearColor ASnarePlacedStructureActor::HouseColorAt(int32 ColorIndex)
{
	// 집 색 다섯 (서버 `color` 0..4). 순서는 월드 환경 시트의 지붕 색 줄 순서 🟡.
	static const TCHAR* const Colors[] = { TEXT("E8514F"), TEXT("3E7BD6"), TEXT("7BBF5A"), TEXT("F5D33B"), TEXT("F2E4C9") };
	return FromHex(Colors[FMath::Clamp(ColorIndex, 0, static_cast<int32>(UE_ARRAY_COUNT(Colors)) - 1)]);
}

FVector ASnarePlacedStructureActor::CellToWorld(UWorld* World, const FIntPoint& AnchorCell, const FIntPoint& RotatedSize,
	float InCellSizeCm, const FIntPoint& Half)
{
	FVector Location = SnareBuildRules::FootprintCenterHalf(SnareBuildRules::JoinHalfUnits(AnchorCell, Half), RotatedSize, InCellSizeCm, 0.f);
	if (World != nullptr)
	{
		// 칸에는 높이가 없다 — 서버는 x,y 만 저장한다. 다시 세울 때도 같은 곳에 서도록 바닥을 쏴서 찾는다.
		FHitResult Hit;
		// 채널이 아니라 **물체 종류**로 쏜다 — 채널로 쏘면 서 있는 캐릭터 캡슐에 맞아 가구가 머리 위에 선다.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SnareStructureGround), false);
		if (World->LineTraceSingleByObjectType(Hit, Location + FVector(0.f, 0.f, GroundTraceHalfCm),
			Location - FVector(0.f, 0.f, GroundTraceHalfCm), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			Location.Z = Hit.ImpactPoint.Z;
		}
	}
	return Location;
}

float ASnarePlacedStructureActor::RugGroundLiftCm() const
{
	UWorld* const World = GetWorld();
	if (World == nullptr || CellSizeCm <= 0.f)
	{
		return 0.f;
	}
	const FVector Root = GetActorLocation();
	const FVector Half(Footprint.X * CellSizeCm * 0.5f, Footprint.Y * CellSizeCm * 0.5f, 0.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SnareRugGround), false, this);
	float Highest = Root.Z;
	for (const FVector2D Corner : { FVector2D(0.f, 0.f), FVector2D(-1.f, -1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, -1.f), FVector2D(1.f, 1.f) })
	{
		// 귀는 몸 돌림대로 — 발자국은 돌리기 전 크기라 액터 회전으로 돌린다. 끝에서 5cm 안쪽(옆 칸 턱을 안 잡게).
		const FVector At = Root + GetActorRotation().RotateVector(FVector(Corner.X * (Half.X - 5.f), Corner.Y * (Half.Y - 5.f), 0.f));
		FHitResult Hit;
		if (World->LineTraceSingleByObjectType(Hit, At + FVector(0.f, 0.f, GroundTraceHalfCm), At - FVector(0.f, 0.f, GroundTraceHalfCm),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			Highest = FMath::Max(Highest, Hit.ImpactPoint.Z);
		}
	}
	return FMath::Min(Highest - Root.Z, RugMaxLiftCm);
}

FVector ASnarePlacedStructureActor::WallToWorld(const FIntPoint& Cell, int32 Side, float InCellSizeCm, float GroundZ, int32 HeightCm)
{
	float OutwardYaw = 0.f;
	return SnareBuildRules::WallSlotLocation(Cell, Side, InCellSizeCm, GroundZ + HeightCm, OutwardYaw);
}

void ASnarePlacedStructureActor::RefreshVisual()
{
	auto FindRow = [this](FName Id) -> const FItemRow*
	{
		if (IsValid(ItemTable) && !Id.IsNone())
		{
			for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
			{
				const FItemRow* const Candidate = reinterpret_cast<const FItemRow*>(Pair.Value);
				if (Candidate != nullptr && Candidate->ItemID == Id)
				{
					return Candidate;
				}
			}
		}
		return nullptr;
	};
	const FItemRow* const Row = FindRow(ItemID);
	PlaceableFunction = Row != nullptr ? Row->PlaceableFunction : EPlaceableFunction::None;
	Footprint = FIntPoint(Row != nullptr ? FMath::Max(1, Row->FootprintX) : 1, Row != nullptr ? FMath::Max(1, Row->FootprintY) : 1);
	TopSlotCount = Row != nullptr ? FMath::Max(0, Row->TopSlots) : 0;
	PowerUse = Row != nullptr ? FMath::Max(0, Row->PowerUse) : 0;

	// 발자국은 회전 전 크기로 두고 액터를 돌린다 — 몸은 그 안에서 발자국 X 방향으로 눕는다.
	SetActorRotation(FRotator(0.f, static_cast<float>(Rotation), 0.f));
	const float LiftCm = bPreview ? PreviewLiftCm : PlaceableFunction == EPlaceableFunction::Rug ? RugGroundLiftCm() : 0.f;

	// 행 메시 (M13-37 · DESIGN 8.11 「가구·상자 진짜 메시 크기」). 없으면 큐브 자리표시 그대로.
	UStaticMesh* const RowMesh = Row != nullptr && Row->PlacedMeshWidthCm > 0.f ? Row->PlacedMeshRef.LoadSynchronous() : nullptr;
	// 전기 가구 켜짐 짝 (M108-08b-2) — 끼우는 것만 바꾸고, 자리·배율은 아래에서 꺼진 메시(RowMesh)로 잰다.
	const bool bWantOnMesh = RowMesh != nullptr && !bPreview && !Row->PlacedOnMeshRef.IsNull()
		&& (bElectricOn || SnareCheckSwitch::Param(TEXT("SnareElectricOnDemo")));
	UStaticMesh* const OnMesh = bWantOnMesh ? Row->PlacedOnMeshRef.LoadSynchronous() : nullptr;
	bShowingElectricOnMesh = OnMesh != nullptr;
	// 집이 된 텐트는 오두막 몸통 (M108-02) — 텐트 메시엔 벽이 없어 벽 장식이 지붕 위 공중에 떴다.
	const bool bHouseShell = IsTent() && bHouse && !bPreview && IsValid(HouseMesh) && IsValid(HouseRoofMesh);
	UStaticMesh* const WantMesh = bHouseShell ? HouseMesh.Get() : OnMesh != nullptr ? OnMesh : RowMesh != nullptr ? RowMesh : CubeMesh.Get();
	if (Body->GetStaticMesh() != WantMesh)
	{
		// 메시가 바뀌면 칠하기 사본도 새 메시 재질에서 다시 만든다. 테두리(오버레이)는 컴포넌트 칸이라 그대로 남는다.
		Body->SetStaticMesh(WantMesh);
		Body->EmptyOverrideMaterials();
		if (RowMesh == nullptr)
		{
			Body->SetMaterial(0, CubeMaterial);
		}
		TintMaterial = nullptr;
	}
	// 2층 집 (M108-06c) — 덮개를 1층 앞벽 + 위층 덮개(2층 바닥 높이)로 가르고, 위층 벽·바닥 판을 세운다.
	const bool bTwoStory = IsTwoStoryHouse();
	UStaticMesh* const WantRoof = bTwoStory ? HouseUpperRoofMesh.Get() : HouseRoofMesh.Get();
	if (HouseRoof->GetStaticMesh() != WantRoof)
	{
		// 지붕 슬롯 번호가 메시마다 달라 집 색 사본은 새 메시에서 다시 만든다.
		HouseRoof->SetStaticMesh(WantRoof);
		HouseRoof->EmptyOverrideMaterials();
		HouseRoofTint = nullptr;
	}
	HouseRoof->SetRelativeLocation(FVector(0.f, 0.f, bTwoStory ? SnareBuildRules::UpperFloorZCm : 0.f));
	for (UStaticMeshComponent* const Upper : { HouseUpper.Get(), static_cast<UStaticMeshComponent*>(UpperFloor.Get()) })
	{
		Upper->SetCollisionEnabled(bTwoStory ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
	RefreshUpperFloor();
	RefreshStairRamp();
	if (bHouseShell)
	{
		// 3×3 칸 크기로 만든 메시라 배율 1, 원점 = 발자국 가운데 바닥. 앞(문)은 액터 돌림을 따른다.
		Body->SetRelativeRotation(FRotator::ZeroRotator);
		Body->SetRelativeScale3D(FVector::OneVector);
		Body->SetRelativeLocation(FVector::ZeroVector);
	}
	else if (RowMesh != nullptr)
	{
		// 모자와 같은 한 배율 — 수평 긴 변 = 표 폭, 높이는 메시 비율대로. 긴 변이 Y 면 90° 돌려 발자국 X 로 눕힌다.
		const FBox Bounds = RowMesh->GetBoundingBox();
		const FVector Size = Bounds.GetSize();
		const bool bLongY = Size.Y > Size.X;
		const float Scale = Row->PlacedMeshWidthCm / FMath::Max(KINDA_SMALL_NUMBER, FMath::Max(Size.X, Size.Y));
		const FRotator Turn(0.f, bLongY ? 90.f : 0.f, 0.f);
		// 메시 가운데를 발자국 가운데로, 메시 밑면을 바닥(루트)으로. 벽 장식은 뒷면(메시 -Y 끝)을 루트로 — 루트가 벽 면이라
		// 가운데를 맞추면 두께 절반이 벽에 묻힌다 (감독 결함 10-20 08:10, 선반 8.3cm).
		const bool bBackToRoot = PlaceableFunction == EPlaceableFunction::WallDecor;
		const FVector Center = Turn.RotateVector(
			FVector(Bounds.GetCenter().X, bBackToRoot ? Bounds.Min.Y : Bounds.GetCenter().Y, 0.f) * Scale);
		Body->SetRelativeRotation(Turn);
		Body->SetRelativeScale3D(FVector(Scale));
		Body->SetRelativeLocation(FVector(-Center.X, -Center.Y, -Bounds.Min.Z * Scale + LiftCm));
		// 비전기 조명(M108-05e) — 점광을 불꽃 높이로(메시 꼭대기에서 높이 10% 아래). 불 행(모닥불·화로)은 옛 자리(루트) 그대로,
		// 그 밖에 불이 켜지는 것은 전등뿐이라 전구 높이로 (`SetLampLit` 은 전력 셈이 전등 ID 에만 부른다).
		const float MeshHeightCm = (Bounds.Max.Z - Bounds.Min.Z) * Scale;
		if (PlaceableFunction == EPlaceableFunction::FlameLight)
		{
			FireLight->SetRelativeLocation(FVector(0.f, 0.f, MeshHeightCm * 0.9f + LiftCm));
		}
		else if (!IsFireFunction())
		{
			FireLight->SetRelativeLocation(FVector(0.f, 0.f, MeshHeightCm * LampBulbHeightRatio + LiftCm));
		}
	}
	else
	{
		const float HeightCm = IsTent() ? TentHeightCm : FurnitureHeightCm;
		// 칸 크기는 호스트가 규칙 표에서 읽어 복제로 넘긴다 (아직 안 왔으면 큐브 한 변).
		const float CellCm = CellSizeCm > 0.f ? CellSizeCm : CubeSizeCm;
		Body->SetRelativeRotation(FRotator::ZeroRotator);
		Body->SetRelativeScale3D(FVector(Footprint.X * CellCm / CubeSizeCm, Footprint.Y * CellCm / CubeSizeCm, HeightCm / CubeSizeCm));
		// 큐브 중심이 원점이라 바닥에서 반 높이만큼 올려 그린다.
		Body->SetRelativeLocation(FVector(0.f, 0.f, HeightCm * 0.5f + LiftCm));
	}
	// 메시는 텍스처가 제 색이라 "제 색" 자리는 흰색(곱하기 1)이다 — 칠한 색·집 색·판정 색만 곱한다.
	auto OwnColor = [bTextured = RowMesh != nullptr](const TCHAR* Hex) { return bTextured ? FLinearColor::White : FromHex(Hex); };

	// 떨림은 이 자리 위에 얹는다 — 몸을 다시 세웠으니 떨던 걸음도 처음부터.
	BodyRestLocation = Body->GetRelativeLocation();
	GeneratorShakePhase = 0.f;
	RefreshSpinner(Row, RowMesh != nullptr && !bHouseShell);
	RefreshElectricLoop(Row);

	RefreshFireLight();
	RefreshGeneratorSound();

	UWorld* const World = GetWorld();
	if (bHouseShell)
	{
		PollHouseRoof();
		if (World != nullptr && !World->GetTimerManager().IsTimerActive(HouseRoofTimer))
		{
			World->GetTimerManager().SetTimer(HouseRoofTimer, this, &ASnarePlacedStructureActor::PollHouseRoof, HouseRoofPollSeconds, true);
		}
	}
	else
	{
		HouseRoof->SetVisibility(false);
		for (UStaticMeshComponent* const Part : { HouseFront.Get(), HouseUpper.Get(), static_cast<UStaticMeshComponent*>(UpperFloor.Get()) })
		{
			Part->SetVisibility(false);
		}
		if (World != nullptr)
		{
			World->GetTimerManager().ClearTimer(HouseRoofTimer);
		}
	}

	if (bPreview)
	{
		ApplyTint(VerdictColor(PreviewVerdict));
	}
	else if (bHouseShell)
	{
		// 집 색은 덮개 지붕에 — 통나무 벽·마루는 오두막 제 색.
		Tint = HouseColorAt(HouseColor);
		const int32 RoofSlot = HouseRoof->GetMaterialIndex(HouseRoofSlot);
		if (!IsValid(HouseRoofTint) && RoofSlot != INDEX_NONE)
		{
			HouseRoofTint = HouseRoof->CreateDynamicMaterialInstance(RoofSlot);
		}
		if (IsValid(HouseRoofTint))
		{
			HouseRoofTint->SetVectorParameterValue(HouseColorParameter, Tint);
		}
	}
	else if (IsTent())
	{
		// 텐트는 캔버스색, 집이 되면 고른 색(집 메시가 없을 때).
		ApplyTint(bHouse ? HouseColorAt(HouseColor) : OwnColor(TEXT("F2E4C9")));
	}
	else if (IsFireFunction())
	{
		ApplyTint(OwnColor(TEXT("F0895A")));
	}
	else
	{
		// 칠한 가구는 페인트 행의 색 (DESIGN 13.8), 아니면 나무색.
		const FItemRow* const Paint = FindRow(PaintItemID);
		ApplyTint(Paint != nullptr && !Paint->PaintColorHex.IsEmpty() ? FromHex(*Paint->PaintColorHex) : OwnColor(TEXT("D8C29E")));
	}
	// 계단이 서면 그 집 2층 바닥에 구멍을 낸다 (M108-06c).
	NotifyHousesOfStairs(nullptr);
}

void ASnarePlacedStructureActor::ApplyTint(const FLinearColor& Color)
{
	Tint = Color;
	if (!IsValid(TintMaterial))
	{
		TintMaterial = Body->CreateDynamicMaterialInstance(0);
	}
	if (IsValid(TintMaterial))
	{
		TintMaterial->SetVectorParameterValue(TintParameter, Color);
	}
	// 날개도 같은 색 — 미리보기 판정 색이 날개만 빠지지 않게. 날개 메시가 바뀌면 사본도 새로 (`RefreshSpinner` 가 덮어쓰기를 비움).
	if (Spinner->GetStaticMesh() != nullptr)
	{
		UMaterialInstanceDynamic* const SpinTint = Cast<UMaterialInstanceDynamic>(Spinner->GetMaterial(0));
		UMaterialInstanceDynamic* const Use = SpinTint != nullptr ? SpinTint : Spinner->CreateDynamicMaterialInstance(0);
		if (Use != nullptr)
		{
			Use->SetVectorParameterValue(TintParameter, Color);
		}
	}
}
