#include "Building/SnareBaseBuildComponent.h"
#include "SnareCheckSwitch.h"
#include "Snarehaven.h"
#include "Algo/AllOf.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Building/SnareBaseSubsystem.h"
#include "Building/SnarePlacedStructureActor.h"
#include "Combat/MeleeAttackComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Data/BaseRuleRow.h"
#include "Data/ItemRow.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Gathering/GatherableActor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/PlayerVitalsComponent.h"
#include "Ranch/RanchGameMode.h"
#include "Ranch/RanchSlotActor.h"
#include "SnarehavenCharacter.h"
#include "SnarehavenGameMode.h"
#include "TimerManager.h"
#include "UI/BuildHudWidget.h"
#include "UI/CraftingScreenWidget.h"
#include "World/SeaActor.h"
#include "World/SnareZoneRules.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 조작감 숫자 (게임 규칙이 아니라 "얼마나 자주 다시 칠하나" — Autonomy 🟢).
	constexpr float PreviewIntervalSeconds = 0.1f;
	/** 회복은 "초당" 이라 1초마다 한 번 (DESIGN 8.10 "+1 HP/초"). 양은 표가 준다. */
	constexpr float FireHealIntervalSeconds = 1.f;
	/** 미리보기는 캐릭터 앞 두 칸 🟡 — 발밑에 두면 몸에 가려 색이 안 보인다. */
	constexpr float PreviewCellsAhead = 2.f;
	/** 놓을 발자국의 가까운 변 ↔ 캐릭터 중심 🟡 — 캡슐 반지름(약 40)에 반 칸 남짓 틈. 칸 크기와 따로 cm 로 둔다. */
	constexpr float PlacementClearanceCm = 100.f;
	/** 소품이 받침을 고르는 반경 🟡 — 몸 앞 점에서 받침 몸 가운데까지. 탁자 긴 변(145)의 절반 + 한 칸 남짓이라 붙어 선 탁자는 잡히고 방 건너편 것은 안 잡힌다. */
	constexpr float TopPickRadiusCm = 250.f;
}

USnareBaseBuildComponent::USnareBaseBuildComponent()
{
	// 미리보기·불 옆 회복은 타이머가 돌린다 (engines/unreal.md 코드 규칙 2).
	PrimaryComponentTick.bCanEverTick = false;
	// 게스트 → 호스트 서버 함수(M15-26)가 이 컴포넌트를 타므로 복제 대상이다. 복제하는 값은 없다.
	SetIsReplicatedByDefault(true);
}

USnareBaseSubsystem* USnareBaseBuildComponent::GetBase() const
{
	const UGameInstance* const GameInstance = GetWorld() != nullptr ? GetWorld()->GetGameInstance() : nullptr;
	return GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBaseSubsystem>() : nullptr;
}

bool USnareBaseBuildComponent::IsLocalOwner() const
{
	const APawn* const Pawn = Cast<APawn>(GetOwner());
	return IsValid(Pawn) && Pawn->IsLocallyControlled();
}

FName USnareBaseBuildComponent::GetTentZone(const UWorld* World)
{
	// 게스트에겐 게임모드가 없다 — 게임 상태가 복제해 주는 게임모드 **클래스**로 본다.
	const AGameStateBase* const GameState = World != nullptr ? World->GetGameState() : nullptr;
	const UClass* const ModeClass = GameState != nullptr ? GameState->GameModeClass.Get() : nullptr;
	if (ModeClass == nullptr)
	{
		return NAME_None;
	}
	if (ModeClass->IsChildOf(ARanchGameMode::StaticClass()))
	{
		return SnareBaseZones::Ranch;
	}
	// 거점 게임모드가 사냥 게임모드의 자식이라 거점을 먼저 본다.
	return ModeClass->IsChildOf(ASnarehavenGameMode::StaticClass()) ? SnareBaseZones::Hunt : NAME_None;
}

bool USnareBaseBuildComponent::IsBaseZone(const UWorld* World)
{
	return !GetTentZone(World).IsNone();
}

bool USnareBaseBuildComponent::IsBlockedByGatherable(const UWorld* World, const FVector& Center, const FVector2D& HalfExtent)
{
	if (World == nullptr)
	{
		return false;
	}
	// ponytail: 맵 채집물 전부를 훑는다(미리보기 한 번에 수백 개) — 느려지면 격자 버킷으로.
	for (TActorIterator<AGatherableActor> It(World); It; ++It)
	{
		const FVector Offset = It->GetActorLocation() - Center;
		if (!It->GetRequiredToolItemID().IsNone()
			&& FMath::Abs(Offset.X) < HalfExtent.X && FMath::Abs(Offset.Y) < HalfExtent.Y)
		{
			return true;
		}
	}
	return false;
}

bool USnareBaseBuildComponent::IsBlockedByRanchSlot(const UWorld* World, const FVector& Center, const FVector2D& HalfExtent)
{
	if (World == nullptr)
	{
		return false;
	}
	const FVector2D FootMin = FVector2D(Center) - HalfExtent;
	const FVector2D FootMax = FVector2D(Center) + HalfExtent;
	for (TActorIterator<ARanchSlotActor> It(World); It; ++It)
	{
		if (It->IsHidden())
		{
			continue;
		}
		// 칸 바닥 = 말뚝 자리 + 칸에 붙은 발판·난간(`setup_ranch_buildings.py` 가 붙인다)의 바닥 넓이.
		FVector2D AreaMin(It->GetActorLocation());
		FVector2D AreaMax = AreaMin;
		TArray<AActor*> Pieces;
		It->GetAttachedActors(Pieces);
		for (const AActor* const Piece : Pieces)
		{
			FVector Origin;
			FVector Extent;
			Piece->GetActorBounds(false, Origin, Extent);
			AreaMin = FVector2D::Min(AreaMin, FVector2D(Origin - Extent));
			AreaMax = FVector2D::Max(AreaMax, FVector2D(Origin + Extent));
		}
		if (FootMin.X < AreaMax.X && AreaMin.X < FootMax.X && FootMin.Y < AreaMax.Y && AreaMin.Y < FootMax.Y)
		{
			return true;
		}
	}
	return false;
}

bool USnareBaseBuildComponent::IsBuildAllowedAt(const UWorld* World, const FVector& Location)
{
	if (!IsBaseZone(World) || SnareZones::IsBuildBlockedAt(World, Location))
	{
		return false;
	}
	// 바다 위·광산 굴 안(같은 맵의 먼 곳 — 섬 밖)은 뭍이 아니다. 바다 없는 맵(검사 월드)은 막지 않는다.
	const ASeaActor* const Sea = ASeaActor::FindSea(World);
	return Sea == nullptr || Sea->CoastDistance(Location) < 0.f;
}

bool USnareBaseBuildComponent::CanBuildHere() const
{
	// 게스트도 놓는다 (M15-14) — 판정·차감은 그대로 백엔드가 한다(CLAUDE.md 8), 여기서 보는 것은
	// "내가 조종하는 몸인가 · 지을 수 있는 땅에 섰는가" 둘뿐이다.
	return IsLocalOwner() && IsBuildAllowedAt(GetWorld(), GetOwner()->GetActorLocation());
}

void USnareBaseBuildComponent::BeginPlay()
{
	Super::BeginPlay();

	// 제작 확정 소리·몸짓 — 조종은 BeginPlay 뒤에 붙기도 해서 듣기는 늘 하고, 내 몸인지는 들을 때 본다.
	if (USnareBaseSubsystem* const CraftBase = GetBase())
	{
		CraftConfirmedHandle = CraftBase->OnCraftConfirmed.AddUObject(this, &USnareBaseBuildComponent::HandleCraftConfirmed);
		BuildConfirmedHandle = CraftBase->OnBuildConfirmed.AddUObject(this, &USnareBaseBuildComponent::HandleBuildConfirmed);
		StructureEditConfirmedHandle = CraftBase->OnStructureEditConfirmed.AddUObject(this,
			&USnareBaseBuildComponent::HandleStructureEditConfirmed);
	}

	UWorld* const World = GetWorld();
	if (World == nullptr || !IsLocalOwner())
	{
		// 남의 복제본은 화면도 회복도 안 한다 — 생존 값은 각자 자기 폰에서 돈다 (M11-03 과 같은 줄).
		return;
	}

	EnsureHud();

	// 불 옆 회복 — 체력은 복제되지 않아 각자 자기 폰에서 채운다.
	World->GetTimerManager().SetTimer(FireHealTimerHandle, this, &USnareBaseBuildComponent::HandleFireHealTick,
		FireHealIntervalSeconds, true);

	// 내 계정의 땅을 세운다 — 거점존이면 거점 텐트, 사냥 섬이면 섬 텐트. 세우는 계기는 서버 성공 응답뿐이다.
	// 게스트도 자기 것을 세운다(M15-14): 복제는 권위가 있는 쪽만 하므로 게스트 것은 자기 화면에만 선다.
	USnareBaseSubsystem* const Base = GetBase();
	if (IsBaseZone(World) && Base != nullptr)
	{
		TentChangedHandle = Base->OnTentChanged.AddUObject(this, &USnareBaseBuildComponent::HandleTentChanged);
		StructurePlacedHandle = Base->OnStructurePlaced.AddUObject(this, &USnareBaseBuildComponent::HandleStructurePlaced);
		StructureRemovedHandle = Base->OnStructureRemoved.AddUObject(this, &USnareBaseBuildComponent::HandleStructureRemoved);
		SpawnWholeMirror();
	}

	// 3층 검사용 스위치 — 제작 화면을 띄운다 (`-SnareCodexScreen` 과 같은 검사 전용 길, 재화는 안 움직인다).
	// `-SnareCraftScreen=Workbench` 면 작업대 자리, `=BoatStorage` 면 배 보관함 판, 값이 없으면 배낭 탭.
	FString CraftPlace;
	if (SnareCheckSwitch::Value(TEXT("SnareCraftScreen="), CraftPlace)
		|| SnareCheckSwitch::Param(TEXT("SnareCraftScreen")))
	{
		if (UCraftingScreenWidget* const Opened = EnsureScreen())
		{
			if (CraftPlace.Equals(TEXT("Workbench"), ESearchCase::IgnoreCase))
			{
				Opened->OpenWorkbenchForTest();
			}
			else if (CraftPlace.Equals(TEXT("BoatStorage"), ESearchCase::IgnoreCase))
			{
				Opened->OpenBoatStorage();
			}
			else
			{
				Opened->OpenBackpack();
			}
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 제작 화면 열기 (-SnareCraftScreen=%s)"), *CraftPlace);
		}
	}

#if !UE_BUILD_SHIPPING
	// `-SnareCraftSoundAt=2/3/4` — 3층 소리 확인 전용 (M52-04). 적은 초마다 작업대(망치)→제련소(쉭)→불(지글) 순으로
	// 제작 확정 처리를 사람 길과 같은 함수로 부른다. 재료·재화는 안 건드린다 (CLAUDE.md 8). 쉼표는 `FParse::Value` 가 끊으므로 `/`.
	FString CraftSoundAt;
	if (SnareCheckSwitch::Value(TEXT("SnareCraftSoundAt="), CraftSoundAt))
	{
		static const ESnareCraftPlace Places[] = { ESnareCraftPlace::Workbench, ESnareCraftPlace::Smelter, ESnareCraftPlace::Fire };
		TArray<FString> Seconds;
		CraftSoundAt.ParseIntoArray(Seconds, TEXT("/"));
		for (int32 Index = 0; Index < Seconds.Num(); ++Index)
		{
			const ESnareCraftPlace Place = Places[Index % UE_ARRAY_COUNT(Places)];
			FTimerHandle Unused;
			World->GetTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this, Place]()
			{
				HandleCraftConfirmed(FName(TEXT("test_craft")), Place);
			}), FCString::Atof(*Seconds[Index]), false);
		}
	}
#endif

	// 3층 검사용 스위치 (M13-33) — 칠한 가구·고른 가구 테두리의 **모습**을 보려는 길. 시험 계정엔 가구가 없고
	// `SetPaint`·`SetSelectedOutline` 은 파이썬으로 안 불린다. 칸 크기는 규칙 표 그대로(새 숫자 없음).
	FString PaintDemoId;
	const FBaseRuleRow* const DemoRules = Base != nullptr ? Base->GetRules() : nullptr;
	if (SnareCheckSwitch::Value(TEXT("SnarePaintDemo="), PaintDemoId) && DemoRules != nullptr)
	{
		const AActor* const Owner = GetOwner();
		const TArray<ASnarePlacedStructureActor*> Demo = SpawnPaintDemo(World, Owner->GetActorLocation(),
			Owner->GetActorForwardVector(), FName(*PaintDemoId), DemoRules->CellSizeCm);
		for (const ASnarePlacedStructureActor* const Shown : Demo)
		{
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 칠하기·테두리 (-SnarePaintDemo=%s) — %s 페인트 %s · 색 %s · 테두리 %s · 자리 %s"),
				*PaintDemoId, *Shown->GetItemID().ToString(), *Shown->GetPaintItemID().ToString(),
				*Shown->GetTint().ToFColor(true).ToHex(), Shown->HasSelectedOutline() ? TEXT("켬") : TEXT("끔"),
				*Shown->GetActorLocation().ToCompactString());
		}
	}

	// 3층 검사용 스위치 (M20-04b) — 전등 불빛의 **모습**을 보려는 길. 시험 계정엔 집이 없어 놓인 전등이 없다.
	// 왼쪽 땅 = 전등 + 태양광 키트(켜짐), 오른쪽 땅 = 전등만(꺼짐). 켜고 끄는 것은 `USnarePowerSubsystem` 그대로다.
	if (SnareCheckSwitch::Param(TEXT("SnarePowerDemo")) && DemoRules != nullptr && DemoRules->HasPowerRules())
	{
		const AActor* const Owner = GetOwner();
		const FVector Forward = Owner->GetActorForwardVector().GetSafeNormal2D();
		const FVector Ahead = Owner->GetActorLocation() + Forward * PreviewCellsAhead * DemoRules->CellSizeCm;
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward) * DemoRules->CellSizeCm;
		// 땅 둘의 불빛이 겹치지 않게 전구 반경만큼 벌린다 — 새 숫자 없음.
		const float Spread = DemoRules->LampLightRadiusCm / DemoRules->CellSizeCm;
		const struct { FName ItemID; const TCHAR* Land; FVector Where; } Demo[] = {
			{ DemoRules->LampItemID, TEXT("power_demo_on"), Ahead - Side * Spread },
			{ DemoRules->SolarKitItemID, TEXT("power_demo_on"), Ahead - Side * (Spread + 1.f) },
			{ DemoRules->LampItemID, TEXT("power_demo_off"), Ahead + Side * Spread },
		};
		for (const auto& Entry : Demo)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.bDeferConstruction = true;
			ASnarePlacedStructureActor* const Shown = World->SpawnActor<ASnarePlacedStructureActor>(
				ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
			if (!IsValid(Shown))
			{
				continue;
			}
			Shown->SetReplicates(false);   // 내 화면에만 — 서버 사본이 없는 가구다.
			Shown->InitStructure(Entry.ItemID, Entry.Land, FString(Entry.Land) + Entry.ItemID.ToString(), FString(), 0,
				DemoRules->CellSizeCm);
			Shown->FinishSpawning(FTransform(ASnarePlacedStructureActor::CellToWorld(World,
				SnareBuildRules::WorldToCell(Entry.Where, DemoRules->CellSizeCm), Shown->GetFootprint(), DemoRules->CellSizeCm)));
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 전력 (-SnarePowerDemo) — %s · 땅 %s · 자리 %s"),
				*Entry.ItemID.ToString(), Entry.Land, *Shown->GetActorLocation().ToCompactString());
		}
	}

	// 3층 검사용 스위치 (M58-05) — 불 옆 온기 소리의 **진짜 길**(`HandleFireHealTick`)을 태우려는 길. 놓기는 서버 왕복이라
	// 오프라인 검사에선 모닥불이 안 선다. 앞 두 칸에 모닥불 하나를 내 화면에만 세운다 (`-SnareHurtPlayer` 와 같이 쓴다).
	if (SnareCheckSwitch::Param(TEXT("SnareFireDemo")) && DemoRules != nullptr)
	{
		const AActor* const Owner = GetOwner();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = true;
		ASnarePlacedStructureActor* const Fire = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
		if (IsValid(Fire))
		{
			Fire->SetReplicates(false);
			Fire->InitStructure(TEXT("item.campfire"), FString(), TEXT("fire_demo"), FString(), 0, DemoRules->CellSizeCm);
			const FVector Where = Owner->GetActorLocation()
				+ Owner->GetActorForwardVector().GetSafeNormal2D() * PreviewCellsAhead * DemoRules->CellSizeCm;
			Fire->FinishSpawning(FTransform(ASnarePlacedStructureActor::CellToWorld(World,
				SnareBuildRules::WorldToCell(Where, DemoRules->CellSizeCm), Fire->GetFootprint(), DemoRules->CellSizeCm)));
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 모닥불 (-SnareFireDemo) — 자리 %s"), *Fire->GetActorLocation().ToCompactString());
		}
	}

	// 3층 검사용 스위치 (M108-01) — 반 칸(50cm) 놓기의 **모습**을 보려는 길. 놓기는 서버 왕복이라 오프라인 검사에선 가구가 안 선다.
	// 탁자 둘을 내 화면에만: 하나는 칸에 딱 맞게, 하나는 바로 옆 칸에서 반 칸(`Half` 0,1) 밀어 틈이 50cm 로 보이게.
	if (SnareCheckSwitch::Param(TEXT("SnareHalfDemo")) && DemoRules != nullptr)
	{
		const AActor* const Owner = GetOwner();
		const FVector Where = Owner->GetActorLocation()
			+ Owner->GetActorForwardVector().GetSafeNormal2D() * PreviewCellsAhead * DemoRules->CellSizeCm;
		const FIntPoint DemoCell = SnareBuildRules::WorldToCell(Where, DemoRules->CellSizeCm);
		for (int32 Index = 0; Index < 2; ++Index)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.bDeferConstruction = true;
			ASnarePlacedStructureActor* const Shown = World->SpawnActor<ASnarePlacedStructureActor>(
				ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
			if (!IsValid(Shown))
			{
				continue;
			}
			Shown->SetReplicates(false);
			Shown->InitStructure(TEXT("item.table_wood"), FString(), FString::Printf(TEXT("half_demo_%d"), Index), FString(), 0,
				DemoRules->CellSizeCm);
			const FIntPoint Size = Shown->GetFootprint();
			const FIntPoint Cell(DemoCell.X, DemoCell.Y + Index * Size.Y);
			const FIntPoint Half(0, Index);
			Shown->FinishSpawning(FTransform(ASnarePlacedStructureActor::CellToWorld(World, Cell, Size, DemoRules->CellSizeCm, Half)));
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 반 칸 놓기 (-SnareHalfDemo) — 탁자 %d · 칸 (%d,%d) · 반 칸 (%d,%d) · 자리 %s"),
				Index, Cell.X, Cell.Y, Half.X, Half.Y, *Shown->GetActorLocation().ToCompactString());
		}
	}

	// 3층 검사용 스위치 (M108-04) — 새 집 가구의 **모습**을 게임 카메라로 보려는 길. 놓기는 서버 왕복이라 오프라인 검사에선 안 선다.
	// `-SnareFurnitureDemo` = 집 가구 다섯(안락의자·소파·둥근 탁자·긴 식탁·1인 침대), `-SnareFurnitureDemo=item.a,item.b` = 그 목록.
	// 몸 앞 칸 줄에 돌림 90(긴 변이 화면 가로, 앉는 쪽이 카메라 쪽)으로 한 칸씩 띄워 내 화면에만 세운다. 서버·재화 안 건드림.
	// 목록을 준 `-SnareFurnitureDemo=…` 는 `Param` 이 못 잡는다(맨 이름 토큰만 본다) — 값으로도 켠다.
	FString List;
	const bool bFurnitureList = SnareCheckSwitch::Value(TEXT("SnareFurnitureDemo="), List, false) && !List.IsEmpty();
	if ((bFurnitureList || SnareCheckSwitch::Param(TEXT("SnareFurnitureDemo"))) && DemoRules != nullptr)
	{
		if (!bFurnitureList)
		{
			List = TEXT("item.armchair,item.sofa,item.table_round,item.table_long,item.bed_single");
		}
		TArray<FString> Ids;
		List.ParseIntoArray(Ids, TEXT(","));
		// 줄 자리 = 몸 앞 몇 칸(`-SnareFurnitureDemoAhead=<칸>`, 음수면 몸 뒤 — 나무·소품이 가리는 자리를 피할 때). 기본은 미리보기 자리.
		float AheadCells = PreviewCellsAhead;
		SnareCheckSwitch::Value(TEXT("SnareFurnitureDemoAhead="), AheadCells);
		const AActor* const Owner = GetOwner();
		const FVector Where = Owner->GetActorLocation()
			+ Owner->GetActorForwardVector().GetSafeNormal2D() * AheadCells * DemoRules->CellSizeCm;
		const FIntPoint Ahead = SnareBuildRules::WorldToCell(Where, DemoRules->CellSizeCm);
		constexpr int32 DemoRotation = 90;
		// `item.rug_square+item.sofa` 처럼 `+` 로 묶은 것은 한 자리에 겹쳐 세운다(M108-05b 러그 위 가구) — 묶음 발자국 가운데에 맞춘다.
		TArray<ASnarePlacedStructureActor*> Shown;
		TArray<int32> GroupOf;
		TArray<FIntPoint> GroupSize;
		int32 Width = 0;
		TArray<FString> Members;
		for (const FString& Entry : Ids)
		{
			Entry.ParseIntoArray(Members, TEXT("+"));
			GroupSize.Add(FIntPoint::ZeroValue);
			for (const FString& Id : Members)
			{
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Params.bDeferConstruction = true;
				ASnarePlacedStructureActor* const Furniture = World->SpawnActor<ASnarePlacedStructureActor>(
					ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
				if (!IsValid(Furniture))
				{
					continue;
				}
				Furniture->SetReplicates(false);
				// 땅 ID 를 줘야 밤 불빛 셈(`USnarePowerSubsystem`)이 이 줄도 센다 — 비전기 조명 밤 캡처(M108-05e).
				Furniture->InitStructure(FName(*Id.TrimStartAndEnd()), TEXT("furniture_demo"), FString::Printf(TEXT("furniture_demo_%d"), Shown.Num()),
					FString(), DemoRotation, DemoRules->CellSizeCm);
				const FIntPoint Size = Furniture->GetFootprint();
				const FIntPoint Rotated = SnareBuildRules::RotatedFootprint(Size.X, Size.Y, DemoRotation);
				GroupSize.Last() = FIntPoint(FMath::Max(GroupSize.Last().X, Rotated.X), FMath::Max(GroupSize.Last().Y, Rotated.Y));
				GroupOf.Add(GroupSize.Num() - 1);
				Shown.Add(Furniture);
			}
			Width += GroupSize.Last().Y + 1;
		}
		// 줄 가운데가 몸 앞에 오게 — 왼쪽 끝 칸부터 한 칸 틈으로 Y 를 늘려 간다.
		int32 NextY = Ahead.Y - (Width - 1) / 2;
		TArray<int32> GroupY;
		for (const FIntPoint& Group : GroupSize)
		{
			GroupY.Add(NextY);
			NextY += Group.Y + 1;
		}
		for (int32 Index = 0; Index < Shown.Num(); ++Index)
		{
			ASnarePlacedStructureActor* const Furniture = Shown[Index];
			const FIntPoint Size = SnareBuildRules::RotatedFootprint(Furniture->GetFootprint().X, Furniture->GetFootprint().Y, DemoRotation);
			const FIntPoint& Group = GroupSize[GroupOf[Index]];
			// 묶음 가운데 = 반 칸 격자로 — 2×2 러그 위 1칸 탁자는 반 칸 밀어 한가운데(세밀 격자로 놓는 것과 같은 자리).
			const FIntPoint Cell(Ahead.X + (Group.X - Size.X) / 2, GroupY[GroupOf[Index]] + (Group.Y - Size.Y) / 2);
			const FIntPoint Half((Group.X - Size.X) % 2, (Group.Y - Size.Y) % 2);
			// 세울 때 준 변환이 InitStructure 의 돌림을 덮지 않게 같은 돌림을 넣는다.
			Furniture->FinishSpawning(FTransform(FRotator(0.f, static_cast<float>(DemoRotation), 0.f),
				ASnarePlacedStructureActor::CellToWorld(World, Cell, Size, DemoRules->CellSizeCm, Half)));
			// `-SnareGeneratorDemo=<초>` (M108-07c) — 이 줄의 발전기에 연료 그만큼(서버 없이 내 화면에만). 0 이면 다 탄 발전기.
			float GeneratorDemoSeconds = 0.f;
			if (Furniture->GetPlaceableFunction() == EPlaceableFunction::Generator
				&& SnareCheckSwitch::Value(TEXT("SnareGeneratorDemo="), GeneratorDemoSeconds))
			{
				Furniture->SetGeneratorFuelSecondsLeft(GeneratorDemoSeconds);
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 발전기 연료 (-SnareGeneratorDemo=%.0f) — %s · 남은 초 %.0f · 돎 %s"),
					GeneratorDemoSeconds, *Furniture->GetStructureId(), Furniture->GetGeneratorFuelSecondsLeft(),
					Furniture->IsGeneratorRunning() ? TEXT("예") : TEXT("아니오"));
			}
			const UStaticMeshComponent* const Body = Furniture->FindComponentByClass<UStaticMeshComponent>();
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 집 가구 (-SnareFurnitureDemo) — %s · 칸 (%d,%d) 반 칸 (%d,%d) · 돌림 %d · 메시 %s · 자리 %s · 몸 밑 z %.1f"),
				*Furniture->GetItemID().ToString(), Cell.X, Cell.Y, Half.X, Half.Y, DemoRotation,
				Body != nullptr && Body->GetStaticMesh() != nullptr ? *Body->GetStaticMesh()->GetName() : TEXT("없음"),
				*Furniture->GetActorLocation().ToCompactString(),
				Body != nullptr ? Body->CalcBounds(Body->GetComponentTransform()).GetBox().Min.Z : 0.f);
			// `-SnareFurnitureDemoTops` (M108-04c) — 받침 가구면 슬롯마다 소품(머그잔·작은 화분·촛대 차례)을 받침 돌림 그대로 올린다.
			// `-SnareFurnitureDemoTops=item.a,item.b` (M108-08b) 면 그 목록 차례 — 목록 값은 `Param` 이 못 잡아 값으로도 켠다(가구 데모와 같다).
			FString TopList;
			const bool bTopList = SnareCheckSwitch::Value(TEXT("SnareFurnitureDemoTops="), TopList, false) && !TopList.IsEmpty();
			if ((!bTopList && !SnareCheckSwitch::Param(TEXT("SnareFurnitureDemoTops"))) || Body == nullptr)
			{
				continue;
			}
			const FBox Top = Body->CalcBounds(Body->GetComponentTransform()).GetBox();
			TArray<FString> TopIDs = { TEXT("item.mug"), TEXT("item.flower_pot"), TEXT("item.candle") };
			if (bTopList)
			{
				TopList.ParseIntoArray(TopIDs, TEXT(","), true);
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.bDeferConstruction = true;
			for (int32 Slot = 0; Slot < Furniture->GetTopSlotCount(); ++Slot)
			{
				FVector At;
				if (!Furniture->GetTopSlotLocation(Slot, At))
				{
					continue;
				}
				ASnarePlacedStructureActor* const Prop = World->SpawnActor<ASnarePlacedStructureActor>(
					ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
				if (!IsValid(Prop))
				{
					continue;
				}
				Prop->SetReplicates(false);
				// 받침과 같은 땅 ID — 전기 소품(라디오)도 줄의 전력 셈에 든다(M108-08c 켜짐/꺼짐 캡처).
				Prop->InitStructure(FName(*TopIDs[Slot % TopIDs.Num()]), Furniture->GetTentId(),
					FString::Printf(TEXT("%s_top_%d"), *Furniture->GetStructureId(), Slot), FString(), DemoRotation, DemoRules->CellSizeCm);
				Prop->SetTopSlot(Furniture->GetStructureId(), Slot);
				Prop->FinishSpawning(FTransform(FRotator(0.f, static_cast<float>(DemoRotation), 0.f), At));
				const UStaticMeshComponent* const PropBody = Prop->GetBodyComponent();
				const FBox PropBox = PropBody != nullptr ? PropBody->CalcBounds(PropBody->GetComponentTransform()).GetBox() : FBox(ForceInit);
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 집 가구 위 소품 (-SnareFurnitureDemoTops) — %s 슬롯 %d/%d · %s · 윗면 z %.1f · 소품 밑 z %.1f · 몸 안 %s"),
					*Furniture->GetItemID().ToString(), Slot, Furniture->GetTopSlotCount(), *Prop->GetItemID().ToString(), Top.Max.Z,
					PropBox.IsValid ? PropBox.Min.Z : -1.f, Top.IsInsideXY(PropBox.GetCenter()) ? TEXT("예") : TEXT("아니오"));
			}
		}
		// `-SnareGeneratorPanel` (M108-07c-2) — 2초 뒤 줄의 첫 발전기 판을 연다. 단추가 켜진 「내 발전기」 판을 보려고 내 화면 사본에만
		// 땅 하나(가구 = 그 발전기)를 넣는다. 서버·재화 안 건드림 — 눌러도 서버가 `not_found` 로 거절한다.
		ASnarePlacedStructureActor* const* const DemoGenerator = Shown.FindByPredicate([](const ASnarePlacedStructureActor* Furniture)
		{
			return Furniture->GetPlaceableFunction() == EPlaceableFunction::Generator;
		});
		if (SnareCheckSwitch::Param(TEXT("SnareGeneratorPanel")) && DemoGenerator != nullptr)
		{
			FSnareTentMirror Land;
			Land.TentId = (*DemoGenerator)->GetTentId();
			Land.PlacedAtUtc = FDateTime::UtcNow().ToIso8601();
			Land.bUpgraded = true;
			FSnareStructureMirror& Entry = Land.Structures.AddDefaulted_GetRef();
			Entry.StructureId = (*DemoGenerator)->GetStructureId();
			Entry.ItemID = (*DemoGenerator)->GetItemID();
			Base->AddTentMirrorForTest(Land);
			TWeakObjectPtr<ASnarePlacedStructureActor> WeakGenerator(*DemoGenerator);
			FTimerHandle PanelHandle;
			World->GetTimerManager().SetTimer(PanelHandle, FTimerDelegate::CreateWeakLambda(this, [this, WeakGenerator]()
			{
				UCraftingScreenWidget* const Opened = WeakGenerator.IsValid() ? EnsureScreen() : nullptr;
				if (Opened != nullptr)
				{
					Opened->OpenFor(WeakGenerator.Get());
				}
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 발전기 판 (-SnareGeneratorPanel) — 열림 %s · 쪽 발전기 %s · 남은 초 %.0f"),
					Opened != nullptr && Opened->IsOpen() ? TEXT("예") : TEXT("아니오"),
					Opened != nullptr && Opened->GetPage() == ESnareBaseScreenPage::Generator ? TEXT("예") : TEXT("아니오"),
					WeakGenerator.IsValid() ? WeakGenerator->GetGeneratorFuelSecondsLeft() : -1.0);
			}), 2.f, false);
		}
		// `-SnareElectricPanel` (M108-08e) — 2초 뒤(전력 셈이 몇 번 돈 뒤) 줄의 첫 전기 가구를 「쓰기」 한 것처럼 —
		// 판은 안 열리고 HUD 짧은 알림 한 줄(켜짐/꺼짐 까닭)을 게임 카메라로 보려는 길. 서버·재화 안 건드림.
		ASnarePlacedStructureActor* const* const DemoElectric = Shown.FindByPredicate([](const ASnarePlacedStructureActor* Furniture)
		{
			return Furniture->GetPowerUse() > 0;
		});
		// `-SnareElectricPanelAgain` (INBOX 지시 10-20 15:40) — 위 첫 「쓰기」 뒤 알림이 떠 있는 0.5초 뒤 같은 가구를 한 번 더 —
		// 작은 가구 판(이름·까닭 줄·「가방에 넣기」)을 게임 카메라로 보려는 길. 누르기만 보고 넣기는 안 누른다.
		const bool bTapAgain = SnareCheckSwitch::Param(TEXT("SnareElectricPanelAgain"));
		if ((SnareCheckSwitch::Param(TEXT("SnareElectricPanel")) || bTapAgain) && DemoElectric != nullptr)
		{
			TWeakObjectPtr<ASnarePlacedStructureActor> WeakElectric(*DemoElectric);
			FTimerHandle PanelHandle;
			World->GetTimerManager().SetTimer(PanelHandle, FTimerDelegate::CreateWeakLambda(this, [this, WeakElectric]()
			{
				UCraftingScreenWidget* const Opened = WeakElectric.IsValid() ? EnsureScreen() : nullptr;
				if (Opened != nullptr)
				{
					Opened->OpenFor(WeakElectric.Get());
				}
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 전기 가구 쓰기 (-SnareElectricPanel) — %s · 판 열림 %s · 알림 「%s」"),
					WeakElectric.IsValid() ? *WeakElectric->GetItemID().ToString() : TEXT("없음"),
					Opened != nullptr && Opened->IsOpen() ? TEXT("예") : TEXT("아니오"),
					Opened != nullptr ? *Opened->GetElectricNoticeText().ToString() : TEXT(""));
			}), 2.f, false);
		}
		if (bTapAgain && DemoElectric != nullptr)
		{
			TWeakObjectPtr<ASnarePlacedStructureActor> WeakElectric(*DemoElectric);
			FTimerHandle AgainHandle;
			World->GetTimerManager().SetTimer(AgainHandle, FTimerDelegate::CreateWeakLambda(this, [this, WeakElectric]()
			{
				UCraftingScreenWidget* const Opened = WeakElectric.IsValid() ? EnsureScreen() : nullptr;
				if (Opened != nullptr)
				{
					Opened->OpenFor(WeakElectric.Get());
				}
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 전기 가구 두 번째 쓰기 (-SnareElectricPanelAgain) — %s · 판 열림 %s · 가구 쪽 %s · 가방에 넣기 %s · 까닭 「%s」"),
					WeakElectric.IsValid() ? *WeakElectric->GetItemID().ToString() : TEXT("없음"),
					Opened != nullptr && Opened->IsOpen() ? TEXT("예") : TEXT("아니오"),
					Opened != nullptr && Opened->GetPage() == ESnareBaseScreenPage::Furniture ? TEXT("예") : TEXT("아니오"),
					Opened != nullptr && Opened->IsPickUpShown() ? TEXT("예") : TEXT("아니오"),
					Opened != nullptr ? *Opened->GetFurnitureStatusText().ToString() : TEXT(""));
			}), 2.5f, false);
		}
	}

	// 3층 검사용 스위치 (M108-03) — 소품이 **받침 슬롯에 올라선 모습**과 놓기 미리보기를 게임 카메라로 보려는 길.
	// 올리기는 서버 왕복이라 오프라인 검사에선 안 선다 — 3초 뒤 내 화면 사본에만 텐트 하나·몸 앞 탁자(긴 변이 화면 가로)·
	// 슬롯 0 머그잔·슬롯 2 촛대를 넣고 진짜 세우기 길(`HandleStructurePlaced`)로 세운 뒤 작은 화분 놓기(`BeginPlacing`)를 켠다 —
	// 미리보기는 남은 슬롯 1 을 골라야 한다. 옆에는 게스트 것 셈 치는 탁자·화분을 호스트 받기 길로 세운다(복제 확인).
	// 서버·재화·인벤토리는 안 건드린다(사본은 로그인 응답이 오면 갈아 끼워진다).
	if (SnareCheckSwitch::Param(TEXT("SnareTopDecorDemo")) && DemoRules != nullptr)
	{
		FTimerHandle DemoHandle;
		World->GetTimerManager().SetTimer(DemoHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			USnareBaseSubsystem* const Base = GetBase();
			const FBaseRuleRow* const Rules = Base != nullptr ? Base->GetRules() : nullptr;
			const AActor* const Owner = GetOwner();
			if (Rules == nullptr || Base->GetItemTable() == nullptr || !IsValid(Owner))
			{
				return;
			}
			const FVector Ahead = Owner->GetActorLocation()
				+ Owner->GetActorForwardVector().GetSafeNormal2D() * PreviewCellsAhead * Rules->CellSizeCm;
			FSnareTentMirror Tent;
			Tent.TentId = TEXT("top_demo");
			Tent.Zone = GetTentZone(GetWorld());
			// 텐트 액터는 세우지 않는다(몸 뒤 칸 사본만) — 탁자를 가리지 않게.
			Tent.Cell = SnareBuildRules::WorldToCell(Owner->GetActorLocation(), Rules->CellSizeCm) - FIntPoint(4, 0);
			Tent.PlacedAtUtc = TEXT("demo");
			FSnareStructureMirror& Table = Tent.Structures.AddDefaulted_GetRef();
			Table.StructureId = TEXT("top_demo_table");
			Table.ItemID = TEXT("item.table_wood");
			Table.Cell = SnareBuildRules::WorldToCell(Ahead, Rules->CellSizeCm);
			// 돌림 90 — 긴 변이 Y(화면 가로), 소품 앞(메시 +Y — 머그 새싹 무늬)이 카메라 쪽.
			Table.Rotation = 90;
			const TCHAR* const TopIDs[] = { TEXT("item.mug"), TEXT("item.candle") };
			const int32 TopSlotsUsed[] = { 0, 2 };
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(TopIDs); ++Index)
			{
				FSnareStructureMirror& Prop = Tent.Structures.AddDefaulted_GetRef();
				Prop.StructureId = FString::Printf(TEXT("top_demo_%d"), Index);
				Prop.ItemID = TopIDs[Index];
				Prop.Cell = Table.Cell;
				Prop.Rotation = Table.Rotation;
				Prop.TopHostId = Table.StructureId;
				Prop.TopSlot = TopSlotsUsed[Index];
			}
			Base->AddTentMirrorForTest(Tent);
			for (const FSnareStructureMirror& Structure : Tent.Structures)
			{
				HandleStructurePlaced(Tent, Structure);
			}
			const ASnarePlacedStructureActor* const TableActor = FindSpawned(Tent.TentId, Table.StructureId);
			const UStaticMeshComponent* const TableBody = TableActor != nullptr ? TableActor->GetBodyComponent() : nullptr;
			const FBox Top = TableBody != nullptr ? TableBody->CalcBounds(TableBody->GetComponentTransform()).GetBox() : FBox(ForceInit);
			for (TActorIterator<ASnarePlacedStructureActor> It(GetWorld()); It; ++It)
			{
				const UStaticMeshComponent* const PropBody = It->IsOnTop() ? It->GetBodyComponent() : nullptr;
				if (It->GetTentId() != Tent.TentId || PropBody == nullptr)
				{
					continue;
				}
				const FBox PropBox = PropBody->CalcBounds(PropBody->GetComponentTransform()).GetBox();
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 소품 (-SnareTopDecorDemo) — %s · 슬롯 %d · 탁자 윗면 z %.1f · 소품 밑 z %.1f · 탁자 몸 안 %s · 자리 %s"),
					*It->GetItemID().ToString(), It->GetTopSlot(), Top.IsValid ? Top.Max.Z : -1.f, PropBox.Min.Z,
					Top.IsValid && Top.IsInsideXY(PropBox.GetCenter()) ? TEXT("예") : TEXT("아니오"),
					*It->GetActorLocation().ToCompactString());
			}
			// 게스트가 놓은 셈 치는 탁자 하나(내 탁자 옆 세 칸) + 그 위 슬롯 1 화분 — 호스트 받기 길(`HostAcceptShared`)로 (M108-03 복제).
			// 같은 슬롯에 또 오면 거절되는지도 남긴다.
			const FString GuestTentId(TEXT("top_demo_guest"));
			const bool bGuestTable = HostAcceptShared(TEXT("item.table_wood"), GuestTentId, TEXT("top_guest_table"),
				Table.Cell + FIntPoint(0, 3), Table.Rotation, TEXT("GUEST_DEMO"), NAME_None);
			const bool bGuestTop = HostAcceptShared(TEXT("item.flower_pot"), GuestTentId, TEXT("top_guest_0"), Table.Cell, 0,
				TEXT("GUEST_DEMO"), NAME_None, FIntPoint::ZeroValue, -1, 0, false, 0, TEXT("top_guest_table"), 1);
			const bool bGuestTwice = HostAcceptShared(TEXT("item.mug"), GuestTentId, TEXT("top_guest_1"), Table.Cell, 0,
				TEXT("GUEST_DEMO"), NAME_None, FIntPoint::ZeroValue, -1, 0, false, 0, TEXT("top_guest_table"), 1);
			const ASnarePlacedStructureActor* const GuestTop = FindSpawned(GuestTentId, TEXT("top_guest_0"));
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 소품 (-SnareTopDecorDemo) — 게스트 탁자 %s · 게스트 화분 슬롯 1 %s · 같은 슬롯 머그잔 %s · 화분 자리 %s"),
				bGuestTable ? TEXT("받음") : TEXT("거절"), bGuestTop ? TEXT("받음") : TEXT("거절"),
				bGuestTwice ? TEXT("받음") : TEXT("거절"), GuestTop != nullptr ? *GuestTop->GetActorLocation().ToCompactString() : TEXT("없음"));
			const bool bBegan = BeginPlacing(TEXT("item.flower_pot"));
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 소품 (-SnareTopDecorDemo) — 화분 놓기 %s · 받침 %s · 슬롯 %d/%d · 판정 %d · 돌림 %d · 자리 %s"),
				bBegan ? TEXT("켬") : TEXT("못 켬"), *PlacementTopHostId, PlacementTopSlot, PlacementTopSlotCount,
				static_cast<int32>(PlacementVerdict), PlacementRotation,
				IsValid(PreviewActor) ? *PreviewActor->GetActorLocation().ToCompactString() : TEXT("없음"));
		}), 3.f, false);
	}

	// 3층 검사용 스위치 (M108-02) — 벽 장식이 **집 안쪽 벽에 걸린 모습**을 게임 카메라로 보려는 길.
	// 걸기는 서버 왕복이라 오프라인 검사에선 안 선다 — 내 화면 사본에만 집 하나와 뒷벽에 걸린 장식 셋(액자·선반·벽시계)을 넣고
	// 진짜 세우기 길(`HandleTentChanged`·`HandleStructurePlaced`)로 세운다. 몸은 앞줄(문 쪽) 가운데 칸 — 이름표가 뒷벽을 안 가린다.
	// `-SnareWallDecorOutside` 면 집이 몸 앞 한 칸 너머(밖에서 덮개 보임), `-SnareWallDecorPreview` 면 벽 선반 놓기(`BeginPlacing`)도 켠다.
	// 서버·재화·인벤토리는 안 건드린다(사본은 로그인 응답이 오면 갈아 끼워진다).
	// `-SnareWallDecorDemo=item.a,item.b` (M108-05c) = 그 목록을 뒷벽 세 칸에 차례로 — 앞 세 개는 높이 120, 다음 세 개는 200(같은 칸 50 간격).
	// 목록 값은 `Param` 이 못 잡는다(가구 데모와 같다) — 값으로도 켠다. 목록이면 게스트 벽시계(액자 위 200)는 자리가 겹쳐 건너뛴다.
	FString WallDemoList;
	const bool bWallDemoList = SnareCheckSwitch::Value(TEXT("SnareWallDecorDemo="), WallDemoList, false) && !WallDemoList.IsEmpty();
	if ((bWallDemoList || SnareCheckSwitch::Param(TEXT("SnareWallDecorDemo"))) && DemoRules != nullptr)
	{
		FTimerHandle DemoHandle;
		World->GetTimerManager().SetTimer(DemoHandle, FTimerDelegate::CreateWeakLambda(this, [this, WallDemoList]()
		{
			USnareBaseSubsystem* const Base = GetBase();
			const FBaseRuleRow* const Rules = Base != nullptr ? Base->GetRules() : nullptr;
			if (Rules == nullptr || Base->GetItemTable() == nullptr)
			{
				return;
			}
			FSnareTentMirror House;
			House.TentId = TEXT("wall_demo");
			House.Zone = GetTentZone(GetWorld());
			const bool bOutside = SnareCheckSwitch::Param(TEXT("SnareWallDecorOutside"));
			House.Cell = SnareBuildRules::WorldToCell(GetOwner()->GetActorLocation(), Rules->CellSizeCm)
				+ (bOutside ? FIntPoint(2, -1) : FIntPoint(0, -1));
			House.PlacedAtUtc = TEXT("demo");
			House.bUpgraded = true;
			const int32 Edge = HouseCellsOf(*Base) - 1;
			// 뒷벽(+X, 변 1) 세 칸에 액자·벽 선반·벽시계 — 방 안 장식이라 앞(메시 +Y)이 카메라 쪽(-X)을 본다.
			TArray<FString> HungIDs;
			if (WallDemoList.IsEmpty())
			{
				HungIDs = { TEXT("item.picture_frame"), TEXT("item.wall_shelf"), TEXT("item.wall_clock") };
			}
			else
			{
				WallDemoList.ParseIntoArray(HungIDs, TEXT(","), true);
				HungIDs.SetNum(FMath::Min(HungIDs.Num(), 2 * (Edge + 1)));
			}
			for (int32 Index = 0; Index < HungIDs.Num(); ++Index)
			{
				FSnareStructureMirror& Hung = House.Structures.AddDefaulted_GetRef();
				Hung.StructureId = FString::Printf(TEXT("wall_demo_%d"), Index);
				Hung.ItemID = FName(*HungIDs[Index]);
				Hung.Cell = House.Cell + FIntPoint(Edge, WallDemoList.IsEmpty() ? Index * Edge / (HungIDs.Num() - 1) : Index % (Edge + 1));
				Hung.WallSide = 1;
				Hung.WallHeightCm = WallDemoList.IsEmpty() ? SnareBuildRules::WallHeightDefault
					: (Index <= Edge ? SnareBuildRules::WallHeightStops[0] : SnareBuildRules::WallHeightStops[2]);
				Hung.Rotation = SnareBuildRules::WallDecorRotation(Hung.WallSide);
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 벽 장식 걸기 — %s · 칸 (%d,%d) · 높이 %d"), *Hung.ItemID.ToString(), Hung.Cell.X, Hung.Cell.Y,
					Hung.WallHeightCm);
			}
			// `-SnareWallDecorFront` (M108-09) — 앞벽(-X, 변 3) 문 양옆 칸에도 화환·지도를 건다. 안에 서면 앞벽과 같이 숨어야 한다.
			const bool bFront = SnareCheckSwitch::Param(TEXT("SnareWallDecorFront"));
			if (bFront)
			{
				const TCHAR* const FrontIDs[] = { TEXT("item.wall_wreath"), TEXT("item.wall_map") };
				for (int32 Index = 0; Index < UE_ARRAY_COUNT(FrontIDs); ++Index)
				{
					FSnareStructureMirror& Hung = House.Structures.AddDefaulted_GetRef();
					Hung.StructureId = FString::Printf(TEXT("wall_demo_front_%d"), Index);
					Hung.ItemID = FrontIDs[Index];
					Hung.Cell = House.Cell + FIntPoint(0, Index * Edge);
					Hung.WallSide = 3;
					Hung.WallHeightCm = SnareBuildRules::WallHeightDefault;
					Hung.Rotation = SnareBuildRules::WallDecorRotation(Hung.WallSide);
				}
			}
			Base->AddTentMirrorForTest(House);
			HandleTentChanged(House);
			for (const FSnareStructureMirror& Hung : House.Structures)
			{
				HandleStructurePlaced(House, Hung);
			}
			// 게스트가 건 셈 치는 벽시계 하나 — 호스트 받기 길(`HostAcceptShared`)로 액자 위 높이 200 에 (M108-02 복제).
			const bool bGuestHung = WallDemoList.IsEmpty() && HostAcceptShared(TEXT("item.wall_clock"), House.TentId, TEXT("wall_demo_guest"),
				House.Cell + FIntPoint(Edge, 0), 0, TEXT("GUEST_DEMO"), NAME_None, FIntPoint::ZeroValue, 1, 200);
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 벽 장식 — 게스트 것 호스트 받기 %s"), bGuestHung ? TEXT("받음") : TEXT("거절"));
			// 게스트 텐트가 옆(+Y)에 먼저 텐트로 오고, 집으로 올린 공유가 다시 온다 — 호스트 화면도 집 색이 되는지.
			const FString GuestHouseId(TEXT("wall_demo_guest_house"));
			const FIntPoint GuestHouseCell = House.Cell + FIntPoint(0, Edge + 2);
			const int32 GuestHouseColor = 3;
			const ASnarePlacedStructureActor* const OwnHouse = FindSpawned(House.TentId, FString());
			const FName TentItemID = OwnHouse != nullptr ? OwnHouse->GetItemID() : NAME_None;
			HostAcceptShared(TentItemID, GuestHouseId, FString(), GuestHouseCell, 0, TEXT("GUEST_DEMO"), NAME_None);
			const ASnarePlacedStructureActor* const GuestHouse = FindSpawned(GuestHouseId, FString());
			const FString TentTint = GuestHouse != nullptr ? GuestHouse->GetTint().ToString() : TEXT("없음");
			HostAcceptShared(TentItemID, GuestHouseId, FString(), GuestHouseCell, 0, TEXT("GUEST_DEMO"), NAME_None,
				FIntPoint::ZeroValue, -1, 0, /*bHouse=*/true, GuestHouseColor);
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 벽 장식 — 게스트 집 올림 받기: 칸 (%d,%d) · 집 %s · 색 번호 %d · 텐트 색 %s → %s"),
				GuestHouseCell.X, GuestHouseCell.Y, GuestHouse != nullptr && GuestHouse->IsHouse() ? TEXT("예") : TEXT("아니오"),
				GuestHouse != nullptr ? GuestHouse->GetHouseColor() : -1, *TentTint,
				GuestHouse != nullptr ? *GuestHouse->GetTint().ToString() : TEXT("없음"));
			const bool bBegan = SnareCheckSwitch::Param(TEXT("SnareWallDecorPreview")) && BeginPlacing(TEXT("item.wall_shelf"));
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 벽 장식 (-SnareWallDecorDemo) — 집 칸 (%d,%d) · 걸린 것 %d · 선반 놓기 %s · 칸 (%d,%d) · 변 %d · 높이 %d · 판정 %d · 돌림 %d · 자리 %s"),
				House.Cell.X, House.Cell.Y, House.Structures.Num(), bBegan ? TEXT("켬") : TEXT("못 켬"),
				PlacementCell.X, PlacementCell.Y, PlacementWallSide, PlacementWallHeightCm, static_cast<int32>(PlacementVerdict),
				PlacementRotation, IsValid(PreviewActor) ? *PreviewActor->GetActorLocation().ToCompactString() : TEXT("없음"));
			// 벽 면과 장식 뒷면 틈 — 집 몸통 사본(복잡 충돌만, HouseWalls 시험과 같은 길)에 장식 가운데에서 바깥으로 쏜다.
			// 틈 < 0 이면 장식이 벽을 뚫은 것.
			ASnarePlacedStructureActor* const ShellOwner = FindSpawned(House.TentId, FString());
			UStaticMeshComponent* Probe = nullptr;
			if (IsValid(ShellOwner) && ShellOwner->GetBodyComponent() != nullptr)
			{
				Probe = NewObject<UStaticMeshComponent>(ShellOwner);
				Probe->SetStaticMesh(ShellOwner->GetBodyComponent()->GetStaticMesh());
				Probe->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				Probe->SetWorldTransform(ShellOwner->GetBodyComponent()->GetComponentTransform());
				Probe->RegisterComponent();
			}
			const FCollisionQueryParams Complex(SCENE_QUERY_STAT(WallDecorDemoGap), true);
			for (TActorIterator<ASnarePlacedStructureActor> It(GetWorld()); It; ++It)
			{
				if (It->GetTentId() != House.TentId || It->IsTent())
				{
					continue;
				}
				FString Gap(TEXT("못 잼"));
				const UStaticMeshComponent* const Decor = It->GetBodyComponent();
				if (Probe != nullptr && Decor != nullptr && It->IsWallHung())
				{
					float OutwardYaw = 0.f;
					SnareBuildRules::WallSlotLocation(FIntPoint::ZeroValue, It->GetWallSide(), 1.f, 0.f, OutwardYaw);
					const FVector Out = FRotator(0.f, OutwardYaw, 0.f).Vector();
					const FBox Box = Decor->CalcBounds(Decor->GetComponentTransform()).GetBox();
					const FVector Extent = Box.GetExtent();
					const float BackFace = FVector::DotProduct(Box.GetCenter(), Out)
						+ FMath::Abs(Out.X) * Extent.X + FMath::Abs(Out.Y) * Extent.Y;
					FHitResult Hit;
					Gap = Probe->LineTraceComponent(Hit, Box.GetCenter(), Box.GetCenter() + Out * 60.f, Complex)
						? FString::Printf(TEXT("%.1f cm"), FVector::DotProduct(Hit.ImpactPoint, Out) - BackFace)
						: FString(TEXT("벽 없음"));
				}
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 벽 장식 — 걸림 %s · 자리 %s · 돌림 %d · 벽 면 틈 %s"),
					*It->GetItemID().ToString(), *It->GetActorLocation().ToCompactString(), It->GetRotation(), *Gap);
			}
			if (Probe != nullptr)
			{
				Probe->DestroyComponent();
			}
			// `-SnareHouseScreen` — 이 집의 「집」 판을 연다: 「가구 n/N」 이 벽 장식을 0.5개로 센 몫인지 (M108-03 한도).
			if (SnareCheckSwitch::Param(TEXT("SnareHouseScreen")))
			{
				UCraftingScreenWidget* const Opened = EnsureScreen();
				if (Opened != nullptr && IsValid(ShellOwner))
				{
					Opened->OpenFor(ShellOwner);
					Opened->ShowPage(ESnareBaseScreenPage::House);
				}
				const FSnareTentMirror* const Shown = Base->FindTent(House.TentId);
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 집 판 (-SnareHouseScreen) — 열림 %s · 사본 %d · 가구 몫 %.1f"),
					Opened != nullptr && Opened->IsOpen() ? TEXT("예") : TEXT("아니오"), Shown != nullptr ? Shown->Structures.Num() : -1,
					Shown != nullptr ? Shown->GetFurnitureLoad() : -1.f);
			}
			// 앞벽 장식 숨김은 집 틱(`PollHouseRoof`)이 정한다 — 1초 뒤 몇 개가 숨었는지 (안에 서면 전부, 밖이면 0).
			if (bFront)
			{
				FTimerHandle FrontHandle;
				GetWorld()->GetTimerManager().SetTimer(FrontHandle, FTimerDelegate::CreateWeakLambda(this, [this, HouseId = House.TentId]()
				{
					int32 Front = 0;
					int32 Hidden = 0;
					int32 OtherHidden = 0;
					const ASnarePlacedStructureActor* const Shell = FindSpawned(HouseId, FString());
					for (TActorIterator<ASnarePlacedStructureActor> It(GetWorld()); It; ++It)
					{
						if (It->GetTentId() != HouseId || It->IsTent() || Shell == nullptr)
						{
							continue;
						}
						const bool bOnFront = Shell->IsOnFrontWall(It->GetActorLocation());
						Front += bOnFront ? 1 : 0;
						Hidden += bOnFront && It->IsFloorViewHidden() ? 1 : 0;
						OtherHidden += !bOnFront && It->IsFloorViewHidden() ? 1 : 0;
					}
					UE_LOG(LogSnarehaven, Log, TEXT("검사용 앞벽 장식 (-SnareWallDecorFront) — 앞벽 %d · 숨음 %d · 다른 벽 숨음 %d · 덮개 %s"),
						Front, Hidden, OtherHidden, Shell != nullptr && Shell->GetHouseRoofComponent()->IsVisible() ? TEXT("보임") : TEXT("숨음"));
				}), 1.f, false);
			}
		}), 3.f, false);
	}

	// 3층 검사용 스위치 (M108-06c) — 2층을 올린 집의 **바깥 모습**(위층 벽·올라간 지붕)을 게임 카메라로 보려는 길.
	// 2층 올리기·계단 놓기는 서버 왕복이라 오프라인 검사에선 안 선다 — 내 화면 사본에만 몸 앞 2칸 너머 2층 집 하나와
	// 오른 줄(+Y, 낮은 끝 문 쪽) 계단 하나를 넣고 진짜 세우기 길(`HandleTentChanged`·`HandleStructurePlaced`)로 세운다.
	// `-SnareHouseFloor2Inside` 면 몸이 집 앞줄 가운데 칸(문 안) — 덮개 숨김으로 1층 방·계단이 보이는지. 서버·재화 안 건드림.
	if (SnareCheckSwitch::Param(TEXT("SnareHouseFloor2Demo")) && DemoRules != nullptr)
	{
		FTimerHandle DemoHandle;
		World->GetTimerManager().SetTimer(DemoHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			USnareBaseSubsystem* const Base = GetBase();
			const FBaseRuleRow* const Rules = Base != nullptr ? Base->GetRules() : nullptr;
			if (Rules == nullptr || Base->GetItemTable() == nullptr)
			{
				return;
			}
			const int32 Edge = HouseCellsOf(*Base) - 1;
			FSnareTentMirror House;
			House.TentId = TEXT("floor2_demo");
			House.Zone = GetTentZone(GetWorld());
			const bool bInside = SnareCheckSwitch::Param(TEXT("SnareHouseFloor2Inside"));
			House.Cell = SnareBuildRules::WorldToCell(GetOwner()->GetActorLocation(), Rules->CellSizeCm)
				+ (bInside ? FIntPoint(0, -Edge / 2) : FIntPoint(2, -Edge / 2));
			House.PlacedAtUtc = TEXT("demo");
			House.bUpgraded = true;
			House.Color = 1;
			House.Floors = 2;
			// 계단 = 오른 줄 전체(서버 6 자리 중 하나 — 돌림 0 이라 긴 변 X, 낮은 끝 -X 가 문 쪽).
			FSnareStructureMirror& Stairs = House.Structures.AddDefaulted_GetRef();
			Stairs.StructureId = TEXT("floor2_demo_stairs");
			Stairs.ItemID = TEXT("item.stairs_wood");
			Stairs.Cell = House.Cell + FIntPoint(0, Edge);
			Stairs.Rotation = 0;
			// 2층 가구 셋 (M108-06f) — 계단 줄(+Y) 밖 2층 여섯 칸 중: 뒤 왼쪽 1인 침대(2×1), 뒤 가운데 안락의자, 앞 왼쪽 화분 나무.
			// 1층에 서면 숨고(`-SnareHouseFloor2Inside`) 2층에 서면 보인다(`-SnareWalkStairs=999`).
			const TPair<const TCHAR*, FIntPoint> Upstairs[] = {
				{ TEXT("item.bed_single"), FIntPoint(1, 0) }, { TEXT("item.armchair"), FIntPoint(2, 1) }, { TEXT("item.potted_tree"), FIntPoint(0, 0) } };
			for (const TPair<const TCHAR*, FIntPoint>& Piece : Upstairs)
			{
				FSnareStructureMirror& Furniture = House.Structures.AddDefaulted_GetRef();
				Furniture.StructureId = FString::Printf(TEXT("floor2_demo_%s"), Piece.Key + 5);
				Furniture.ItemID = Piece.Key;
				Furniture.Cell = House.Cell + Piece.Value;
				Furniture.Rotation = Piece.Value.Y == 1 ? 180 : 0;
				Furniture.Floor = 2;
			}
			Base->AddTentMirrorForTest(House);
			HandleTentChanged(House);
			for (const FSnareStructureMirror& Structure : House.Structures)
			{
				HandleStructurePlaced(House, Structure);
			}
			const ASnarePlacedStructureActor* const Shown = FindSpawned(House.TentId, FString());
			const UInstancedStaticMeshComponent* const Floor = Shown != nullptr ? Shown->GetUpperFloorComponent() : nullptr;
			const UStaticMeshComponent* const Roof = Shown != nullptr ? Shown->GetHouseRoofComponent() : nullptr;
			const ASnarePlacedStructureActor* const ShownStairs = FindSpawned(House.TentId, Stairs.StructureId);
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 2층 집 (-SnareHouseFloor2Demo) — 집 칸 (%d,%d) · 층 %d · 2층 집 %s · 2층 판 %d칸 · 지붕 높이 %.0f · 지붕 메시 %s · 계단 %s · 자리 %s"),
				House.Cell.X, House.Cell.Y, Shown != nullptr ? Shown->GetHouseFloors() : -1,
				Shown != nullptr && Shown->IsTwoStoryHouse() ? TEXT("예") : TEXT("아니오"),
				Floor != nullptr ? Floor->GetInstanceCount() : -1, Roof != nullptr ? Roof->GetRelativeLocation().Z : -1.f,
				Roof != nullptr && Roof->GetStaticMesh() != nullptr ? *Roof->GetStaticMesh()->GetName() : TEXT("없음"),
				ShownStairs != nullptr ? TEXT("섬") : TEXT("없음"),
				Shown != nullptr ? *Shown->GetActorLocation().ToCompactString() : TEXT("없음"));
			for (const FSnareStructureMirror& Structure : House.Structures)
			{
				if (const ASnarePlacedStructureActor* const Piece = Structure.Floor >= 2 ? FindSpawned(House.TentId, Structure.StructureId) : nullptr)
				{
					UE_LOG(LogSnarehaven, Log, TEXT("검사용 2층 가구 %s — 층 %d · 집 바닥 위 %.0f"), *Structure.ItemID.ToString(),
						Piece->GetPlacedFloor(), Shown != nullptr ? Piece->GetActorLocation().Z - Shown->GetActorLocation().Z : -1.f);
				}
			}
			float StopFeetCm = 0.f;
			if (Shown != nullptr && ShownStairs != nullptr && SnareCheckSwitch::Value(TEXT("SnareWalkStairs="), StopFeetCm))
			{
				StartStairsWalkDemo(*ShownStairs, Shown->GetActorLocation(), StopFeetCm);
			}
			// 3층 검사용 (`-SnarePlaceGroundRoom`, M108-06f-4) — `-SnareHouseFloor2Inside` 로 문 안에 선 몸이 방 안쪽(+X)을 보고
			// 의자 놓기를 켠다. 집 1층 안 칸이 텐트 몸 빨강이 아니라 초록인지.
			else if (bInside && Shown != nullptr && SnareCheckSwitch::Param(TEXT("SnarePlaceGroundRoom")))
			{
				BeginUpstairsPlaceDemo(FVector::ForwardVector, TEXT("-SnarePlaceGroundRoom"));
			}
		}), 3.f, false);
	}

	// 3층 검사용 스위치 (`-SnarePlaceTent`) — 배낭 텐트 카드 「놓기」 → 놓기 바 「놓기」와 같은 길을 한 번 탄다.
	// 로그인·가방 조회를 기다려 캡처 지연(6초)의 절반 뒤에 누른다. 자리 판정·차감은 그대로 서버 `PlaceTent` 다.
	if (SnareCheckSwitch::Param(TEXT("SnarePlaceTent")))
	{
		FTimerHandle PressHandle;
		World->GetTimerManager().SetTimer(PressHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			const bool bBegan = BeginPlacing(TEXT("item.tent"));
			const ESnarePlacementVerdict Verdict = PlacementVerdict;
			const FIntPoint Cell = PlacementCell;
			const bool bSent = bBegan && ConfirmPlacement(FString());
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 텐트 놓기 (-SnarePlaceTent) — 존 %s · 시작 %s · 칸 (%d,%d) · 판정 %d · 서버 요청 %s"),
				*GetTentZone(GetWorld()).ToString(), bBegan ? TEXT("예") : TEXT("아니오"), Cell.X, Cell.Y,
				static_cast<int32>(Verdict), bSent ? TEXT("보냄") : TEXT("안 보냄"));
		}), 3.f, false);
	}
}

void USnareBaseBuildComponent::StartStairsWalkDemo(const ASnarePlacedStructureActor& Stairs, const FVector& HouseCenter, float StopFeetCm)
{
	ACharacter* const Walker = Cast<ACharacter>(GetOwner());
	UWorld* const World = GetWorld();
	if (Walker == nullptr || World == nullptr || Stairs.GetCellSizeCm() <= 0.f)
	{
		return;
	}
	// 계단 낮은 끝은 액터 -X(메시·비탈 판과 같다), 꼭대기에서 걸어 나갈 쪽은 집 가운데(계단 줄 옆 2층 판).
	const FVector StairsAt = Stairs.GetActorLocation();
	const FVector Up = Stairs.GetActorForwardVector().GetSafeNormal2D();
	const FVector Out = (HouseCenter - StairsAt).GetSafeNormal2D();
	const float CellCm = Stairs.GetCellSizeCm();
	const float HalfRunCm = Stairs.GetFootprint().X * CellCm * 0.5f;
	const float HalfHeightCm = Walker->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float RadiusCm = Walker->GetCapsuleComponent()->GetScaledCapsuleRadius();
	// 첫 디딤 반 칸 앞 땅 — 계단 액터 루트 = 바닥 높이.
	Walker->TeleportTo(StairsAt - Up * (HalfRunCm + CellCm * 0.5f) + FVector(0.f, 0.f, HalfHeightCm + 5.f), Up.Rotation());
	UE_LOG(LogSnarehaven, Log, TEXT("검사용 계단 오르기 (-SnareWalkStairs=%.0f) — 시작 %s · 오름 방향 %s · 나갈 방향 %s"),
		StopFeetCm, *Walker->GetActorLocation().ToCompactString(), *Up.ToCompactString(), *Out.ToCompactString());

	struct FWalk
	{
		FTimerHandle Handle;
		double StartSeconds = 0.0;
		bool bOut = false;
	};
	const TSharedRef<FWalk> Walk = MakeShared<FWalk>();
	Walk->StartSeconds = World->GetTimeSeconds();
	World->GetTimerManager().SetTimer(Walk->Handle, FTimerDelegate::CreateWeakLambda(Walker,
		[Walker, Walk, StairsAt, Up, Out, CellCm, HalfRunCm, HalfHeightCm, RadiusCm, StopFeetCm]()
	{
		// 이 검사의 시간 상한(초) — 걷기 속도가 바뀌어도 넉넉히. 넘으면 실패 줄을 남기고 손을 뗀다.
		constexpr double GiveUpSeconds = 8.0;
		const FVector At = Walker->GetActorLocation();
		const float FeetCm = static_cast<float>(At.Z - HalfHeightCm - StairsAt.Z);
		const float AlongCm = static_cast<float>(FVector::DotProduct(At - StairsAt, Up));
		const float SideCm = static_cast<float>(FMath::Abs(FVector::DotProduct(At - StairsAt, Out)));
		const double Seconds = Walker->GetWorld()->GetTimeSeconds() - Walk->StartSeconds;
		const bool bClimbStop = StopFeetCm < SnareBuildRules::UpperFloorZCm && FeetCm >= StopFeetCm;
		// 2층 판으로 다 나왔다 = 계단 줄에서 한 칸 옆(옆 판 가운데).
		const bool bOnFloor2 = Walk->bOut && SideCm >= CellCm;
		if (bClimbStop || bOnFloor2 || Seconds > GiveUpSeconds)
		{
			const UCharacterMovementComponent* const Movement = Walker->GetCharacterMovement();
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 계단 오르기 끝 — %s · 발 높이 %.0f · 땅 위 %s · 계단 줄 옆 %.0f · %.1f초"),
				bClimbStop ? TEXT("오르는 중 멈춤") : bOnFloor2 ? TEXT("2층에 섬") : TEXT("시간 넘음(실패)"), FeetCm,
				Movement != nullptr && Movement->IsMovingOnGround() ? TEXT("예") : TEXT("아니오"), SideCm, Seconds);
			Walker->GetWorldTimerManager().ClearTimer(Walk->Handle);
			USnareBaseBuildComponent* const Build = Walker->FindComponentByClass<USnareBaseBuildComponent>();
			if (bOnFloor2 && Build != nullptr && SnareCheckSwitch::Param(TEXT("SnarePlaceUpstairs")))
			{
				Build->BeginUpstairsPlaceDemo(-Up);
			}
			// 3층 검사용 (`-SnareRemoveStairs`, M108-06f-3) — 2층에 선 뒤 계단을 서버 철거 응답과 같은 길(`HandleStructureRemoved`)로
			// 치워 몸이 문 앞 디딤 자리로 옮겨지는지 게임 카메라로 보는 길. 내 화면 사본만 — 서버 요청 없음.
			if (bOnFloor2 && Build != nullptr && SnareCheckSwitch::Param(TEXT("SnareRemoveStairs")))
			{
				Build->HandleStructureRemoved(TEXT("floor2_demo_stairs"));
				FTimerHandle LogHandle;
				Walker->GetWorldTimerManager().SetTimer(LogHandle, FTimerDelegate::CreateWeakLambda(Build, [Build, Walker]()
				{
					const ASnarePlacedStructureActor* const House = Build->FindSpawned(TEXT("floor2_demo"), FString());
					const FVector InHouse = House != nullptr
						? House->GetActorTransform().InverseTransformPosition(ASnarePlacedStructureActor::HouseViewFeet(*Walker)) : FVector::ZeroVector;
					const UCharacterMovementComponent* const Movement = Walker->GetCharacterMovement();
					UE_LOG(LogSnarehaven, Log, TEXT("검사용 계단 치움 (-SnareRemoveStairs) — 계단 %s · 집 공간 X %.0f Y %.0f · 발 높이 %.0f · 땅 위 %s · 2층 판 %d칸"),
						Build->FindSpawned(TEXT("floor2_demo"), TEXT("floor2_demo_stairs")) != nullptr ? TEXT("남음") : TEXT("치움"),
						InHouse.X, InHouse.Y, InHouse.Z, Movement != nullptr && Movement->IsMovingOnGround() ? TEXT("예") : TEXT("아니오"),
						House != nullptr && House->GetUpperFloorComponent() != nullptr ? House->GetUpperFloorComponent()->GetInstanceCount() : -1);
				}), 1.f, false);
			}
			return;
		}
		// 꼭대기 = 몸이 뒤 위층 벽에 닿는 자리(높은 끝 − 반지름) 근처. 거기서 옆 판으로 걸어 나간다.
		if (!Walk->bOut && AlongCm >= HalfRunCm - RadiusCm - 10.f)
		{
			Walk->bOut = true;
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 계단 오르기 — 꼭대기 발 높이 %.0f · %.1f초 → 2층 판 쪽으로"), FeetCm, Seconds);
		}
		Walker->AddMovementInput(Walk->bOut ? Out : Up, 1.f);
	}), 0.01f, true);
}

void USnareBaseBuildComponent::BeginUpstairsPlaceDemo(const FVector& Facing, const TCHAR* SwitchName)
{
	// 3층 검사용 (`-SnarePlaceUpstairs`, M108-06f) — 2층에 선 몸을 집 앞(계단 낮은 끝 쪽)으로 돌리고 나무 의자 놓기를 켠다.
	// 미리보기가 2층 판 높이에 뜨는지 게임 카메라로 보는 길. 누르지는 않는다(서버 요청 없음).
	// `-SnarePlaceGroundRoom`(M108-06f-4) 은 1층 집 안에 선 몸으로 같은 길 — 집 안 칸이 초록인지.
	AActor* const Owner = GetOwner();
	UWorld* const World = GetWorld();
	if (Owner == nullptr || World == nullptr)
	{
		return;
	}
	Owner->SetActorRotation(Facing.Rotation());
	const bool bBegan = BeginPlacing(TEXT("item.chair_wood"));
	FTimerHandle LogHandle;
	World->GetTimerManager().SetTimer(LogHandle, FTimerDelegate::CreateWeakLambda(this, [this, bBegan, SwitchName]()
	{
		const ASnarePlacedStructureActor* const House = FindSpawned(TEXT("floor2_demo"), FString());
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 집 안 놓기 미리보기 (%s) — 시작 %s · 층 %d · 칸 (%d,%d) · 판정 %d · 집 바닥 위 %.0f"),
			SwitchName, bBegan ? TEXT("예") : TEXT("아니오"), PlacementFloor, PlacementCell.X, PlacementCell.Y, static_cast<int32>(PlacementVerdict),
			IsValid(PreviewActor) && House != nullptr ? PreviewActor->GetActorLocation().Z - House->GetActorLocation().Z : -1.f);
	}), 0.5f, false);
}

TArray<ASnarePlacedStructureActor*> USnareBaseBuildComponent::SpawnPaintDemo(UWorld* World, const FVector& Origin,
	const FVector& Forward, FName PaintItemID, float CellSizeCm)
{
	TArray<ASnarePlacedStructureActor*> Spawned;
	if (World == nullptr || CellSizeCm <= 0.f)
	{
		return Spawned;
	}

	// 앞 거리는 놓기 미리보기와 같은 앞 두 칸, 둘은 좌우로 한 칸씩(벤치 폭 1칸) 띄운다 — 새 숫자 없음.
	const FVector Ahead = Origin + Forward.GetSafeNormal2D() * PreviewCellsAhead * CellSizeCm;
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward.GetSafeNormal2D()) * CellSizeCm;
	for (const FVector& Where : { Ahead - Side, Ahead + Side })
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = true;
		ASnarePlacedStructureActor* const Bench = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
		if (!IsValid(Bench))
		{
			continue;
		}
		Bench->SetReplicates(false);   // 내 화면에만 — 서버 사본이 없는 가구다.
		Bench->InitStructure(TEXT("item.bench"), FString(), FString(), FString(), 0, CellSizeCm);
		// 미리보기와 같은 길로 칸에 맞추고 바닥을 쏜다.
		const FTransform OnGround(ASnarePlacedStructureActor::CellToWorld(World,
			SnareBuildRules::WorldToCell(Where, CellSizeCm), Bench->GetFootprint(), CellSizeCm));
		Bench->FinishSpawning(OnGround);
		Spawned.Add(Bench);
	}

	if (Spawned.Num() == 2)
	{
		Spawned[0]->SetPaint(PaintItemID);
		Spawned[1]->SetSelectedOutline(true);
	}
	return Spawned;
}

void USnareBaseBuildComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PreviewTimerHandle);
		World->GetTimerManager().ClearTimer(FireHealTimerHandle);
	}

	if (USnareBaseSubsystem* const Base = GetBase())
	{
		Base->OnTentChanged.Remove(TentChangedHandle);
		Base->OnStructurePlaced.Remove(StructurePlacedHandle);
		Base->OnStructureRemoved.Remove(StructureRemovedHandle);
		Base->OnCraftConfirmed.Remove(CraftConfirmedHandle);
		Base->OnBuildConfirmed.Remove(BuildConfirmedHandle);
		Base->OnStructureEditConfirmed.Remove(StructureEditConfirmedHandle);
	}

	if (IsValid(PreviewActor))
	{
		PreviewActor->Destroy();
	}
	PreviewActor = nullptr;

	// 화면은 컨트롤러 것이라 폰이 사라져도 남는다 — 폰과 함께 치운다.
	if (IsValid(Screen))
	{
		Screen->RemoveFromParent();
	}
	if (IsValid(Hud))
	{
		Hud->RemoveFromParent();
	}

	Super::EndPlay(EndPlayReason);
}

void USnareBaseBuildComponent::EnsureHud()
{
	if (IsValid(Hud))
	{
		return;
	}

	APlayerController* const Controller = Cast<APlayerController>(Cast<APawn>(GetOwner())->GetController());
	if (!IsValid(Controller) || !Controller->IsLocalController())
	{
		return;
	}

	Hud = CreateWidget<UBuildHudWidget>(Controller, UBuildHudWidget::StaticClass());
	if (IsValid(Hud))
	{
		Hud->AddToViewport();
		Hud->BindToBuild(this);
	}
}

UCraftingScreenWidget* USnareBaseBuildComponent::EnsureScreen()
{
	if (IsValid(Screen))
	{
		return Screen;
	}

	const APawn* const Pawn = Cast<APawn>(GetOwner());
	APlayerController* const Controller = IsValid(Pawn) ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!IsValid(Controller) || !Controller->IsLocalController())
	{
		return nullptr;
	}

	Screen = CreateWidget<UCraftingScreenWidget>(Controller, UCraftingScreenWidget::StaticClass());
	if (IsValid(Screen))
	{
		// HUD 버튼·「다음 목표」 띠보다 위 — 화면이 떠 있는 동안 아래 버튼이 눌리지 않고 띠가 판을 가리지 않는다.
		Screen->AddToScreenPanelLayer();
		Screen->Setup(this, GetBase());
	}
	return Screen;
}

void USnareBaseBuildComponent::OpenBackpackCrafting()
{
	if (UCraftingScreenWidget* const Opened = EnsureScreen())
	{
		Opened->OpenBackpack();
	}
}

void USnareBaseBuildComponent::OpenBoatStorage()
{
	if (UCraftingScreenWidget* const Opened = EnsureScreen())
	{
		Opened->OpenBoatStorage();
	}
}

ASnarePlacedStructureActor* USnareBaseBuildComponent::FindNearestStructure() const
{
	const AActor* const Owner = GetOwner();
	const UMeleeAttackComponent* const Melee = IsValid(Owner) ? Owner->FindComponentByClass<UMeleeAttackComponent>() : nullptr;
	const float RangeCm = IsValid(Melee) ? Melee->GetAttackRangeCm() : -1.f;
	if (RangeCm <= 0.f || GetWorld() == nullptr)
	{
		return nullptr;
	}

	ASnarePlacedStructureActor* Nearest = nullptr;
	float NearestDistSq = FMath::Square(RangeCm);
	for (TActorIterator<ASnarePlacedStructureActor> It(GetWorld()); It; ++It)
	{
		if (It->IsPreview())
		{
			continue;
		}
		// 발자국이 큰 텐트는 가운데까지가 멀다 — 가장자리 기준으로 재려고 반 발자국만큼 사거리를 늘린다.
		const float Reach = RangeCm + 0.5f * It->GetFootprint().GetMax() * FMath::Max(1.f, It->GetCellSizeCm());
		const float DistSq = FVector::DistSquared2D(It->GetActorLocation(), Owner->GetActorLocation());
		if (DistSq <= FMath::Square(Reach) && (Nearest == nullptr || DistSq < NearestDistSq))
		{
			Nearest = *It;
			NearestDistSq = DistSq;
		}
	}
	return Nearest;
}

bool USnareBaseBuildComponent::TryInteractNearest()
{
	if (!IsLocalOwner() || IsPlacing())
	{
		return false;
	}

	ASnarePlacedStructureActor* const Target = FindNearestStructure();
	UCraftingScreenWidget* const Opened = IsValid(Target) ? EnsureScreen() : nullptr;
	if (Opened == nullptr)
	{
		return false;
	}

	Opened->OpenFor(Target);
	return true;
}

bool USnareBaseBuildComponent::HasInteractTarget() const
{
	return IsLocalOwner() && !IsPlacing() && IsValid(FindNearestStructure());
}

EPlaceableFunction USnareBaseBuildComponent::GetInteractTargetFunction() const
{
	const ASnarePlacedStructureActor* const Target = HasInteractTarget() ? FindNearestStructure() : nullptr;
	return IsValid(Target) ? Target->GetPlaceableFunction() : EPlaceableFunction::None;
}

bool USnareBaseBuildComponent::TryTapFurniture(const FVector2D& ScreenPos)
{
	const APawn* const Pawn = Cast<APawn>(GetOwner());
	const APlayerController* const Controller = IsValid(Pawn) ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	FVector Origin;
	FVector Direction;
	if (!IsLocalOwner() || IsPlacing() || !IsValid(Controller)
		|| !Controller->DeprojectScreenPositionToWorld(ScreenPos.X, ScreenPos.Y, Origin, Direction))
	{
		return false;
	}

	ASnarePlacedStructureActor* const Hit = FindFurnitureOnRay(GetWorld(), Origin, Direction);
	UCraftingScreenWidget* const Opened = IsValid(Hit) ? EnsureScreen() : nullptr;
	return Opened != nullptr && Opened->TapPaintTarget(Hit);
}

ASnarePlacedStructureActor* USnareBaseBuildComponent::FindFurnitureOnRay(UWorld* World, const FVector& Origin,
	const FVector& Direction)
{
	if (World == nullptr)
	{
		return nullptr;
	}
	// 탑다운 카메라에서 바닥까지보다 넉넉한 길이 — 화면에 보이는 것만 맞는다.
	constexpr float TapRayLengthCm = 100000.f;
	const FVector End = Origin + Direction.GetSafeNormal() * TapRayLengthCm;
	ASnarePlacedStructureActor* Nearest = nullptr;
	float NearestTime = 1.f;
	for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
	{
		// 가구는 텐트 땅 안에 선다 — 텐트 상자가 먼저 맞아 가구를 가리면 안 된다.
		if (It->IsPreview() || It->GetPlaceableFunction() == EPlaceableFunction::Tent)
		{
			continue;
		}
		FVector HitLocation;
		FVector HitNormal;
		float HitTime = 1.f;
		if (FMath::LineExtentBoxIntersection(It->GetComponentsBoundingBox(true), Origin, End, FVector::ZeroVector,
				HitLocation, HitNormal, HitTime)
			&& HitTime < NearestTime)
		{
			Nearest = *It;
			NearestTime = HitTime;
		}
	}
	return Nearest;
}

FText USnareBaseBuildComponent::GetPlaceBlockReason(FName ItemID) const
{
	USnareBaseSubsystem* const Base = GetBase();
	const FBaseRuleRow* const Rules = Base != nullptr ? Base->GetRules() : nullptr;
	const FItemRow* const Row = Base != nullptr ? Base->FindItemRow(ItemID) : nullptr;
	const AActor* const Owner = GetOwner();
	const UWorld* const World = GetWorld();
	if (Rules == nullptr || !Rules->IsComplete() || Row == nullptr || !IsValid(Owner) || World == nullptr)
	{
		return LOCTEXT("PlaceBlockNoRules", "놓기 규칙을 아직 받지 못했습니다");
	}
	if (Row->ItemType != EItemType::Placeable || Row->FootprintX <= 0)
	{
		return LOCTEXT("PlaceBlockNotPlaceable", "놓을 수 있는 물건이 아닙니다");
	}
	const FName Zone = GetTentZone(World);
	if (Zone.IsNone())
	{
		return LOCTEXT("PlaceBlockZone", "여기서는 짓지 못합니다 - 사냥 섬이나 목장에서 놓으세요");
	}
	if (!IsLocalOwner())
	{
		return LOCTEXT("PlaceBlockNotMine", "내 캐릭터로만 놓을 수 있습니다");
	}
	if (!IsBuildAllowedAt(World, Owner->GetActorLocation()))
	{
		return LOCTEXT("PlaceBlockLand", "물 · 늪 · 설원 위에는 놓지 못합니다");
	}
	// 텐트가 아닌 설치물은 내 텐트 땅 안에만 선다 (서버 `PlaceStructure` 와 같은 줄, DESIGN 8.11).
	// 텐트가 하나도 없으면 미리보기가 내내 빨강이라 켜기 전에 까닭을 말한다.
	const bool bNeedsTent = Row->PlaceableFunction != EPlaceableFunction::Tent;
	const bool bHasTent = Base->GetTents().ContainsByPredicate([Zone](const FSnareTentMirror& Tent)
	{
		return Tent.Zone == Zone;
	});
	if (bNeedsTent && !bHasTent)
	{
		return LOCTEXT("PlaceBlockTent", "텐트를 먼저 놓으세요 - 텐트 땅 안에만 놓입니다");
	}
	// 벽 장식은 집 벽에만 걸린다 (서버 `wallHangFail` 의 `not_house`) — 텐트뿐이면 미리보기가 내내 파랑이다.
	const bool bHasHouse = Base->GetTents().ContainsByPredicate([Zone](const FSnareTentMirror& Tent)
	{
		return Tent.Zone == Zone && Tent.bUpgraded;
	});
	if (Row->PlaceableFunction == EPlaceableFunction::WallDecor && !bHasHouse)
	{
		return LOCTEXT("PlaceBlockHouse", "텐트를 집으로 올린 뒤 거세요 - 벽 장식은 집 벽에만 걸립니다");
	}
	// 소품은 받침(탁자·보관함) 위에만 (서버 `topPlaceFail` 의 `not_found`) — 받침이 없으면 미리보기가 내내 파랑이다.
	const bool bHasHost = Base->GetTents().ContainsByPredicate([Zone, Base](const FSnareTentMirror& Tent)
	{
		return Tent.Zone == Zone && Tent.Structures.ContainsByPredicate([Base](const FSnareStructureMirror& Structure)
		{
			const FItemRow* const HostRow = Base->FindItemRow(Structure.ItemID);
			return HostRow != nullptr && HostRow->TopSlots > 0;
		});
	});
	return Row->PlaceableFunction == EPlaceableFunction::TopDecor && !bHasHost
		? LOCTEXT("PlaceBlockTopHost", "탁자나 보관함을 먼저 놓으세요 - 소품은 그 위에만 올립니다") : FText::GetEmpty();
}

bool USnareBaseBuildComponent::BeginPlacing(FName ItemID)
{
	USnareBaseSubsystem* const Base = GetBase();
	const FBaseRuleRow* const Rules = Base != nullptr ? Base->GetRules() : nullptr;
	if (!CanBuildHere() || Rules == nullptr || !Rules->IsComplete() || GetWorld() == nullptr)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("놓기 모드를 켜지 않는다 — 호스트·뭍(바다·늪·설원·광산 밖)·규칙 표가 필요하다 (%s)"), *ItemID.ToString());
		return false;
	}

	CancelPlacement();
	PlacingItemID = ItemID;
	PlacementRotation = 0;
	PlacementTentId.Reset();
	PlacementWallSide = -1;
	PlacementWallHeightCm = SnareBuildRules::WallHeightDefault;
	PlacementTopHostId.Reset();
	PlacementTopSlot = -1;
	PlacementTopSlotCount = 0;
	PlacementFloor = 1;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	PreviewActor = GetWorld()->SpawnActor<ASnarePlacedStructureActor>(ASnarePlacedStructureActor::StaticClass(),
		FTransform::Identity, Params);
	if (IsValid(PreviewActor))
	{
		// 미리보기는 내 화면에만 — 복제하면 게스트에게 "아직 안 놓인 것" 이 보인다.
		PreviewActor->SetReplicates(false);
		PreviewActor->InitStructure(ItemID, FString(), FString(), FString(), 0, Rules->CellSizeCm);
		PreviewActor->SetPreviewVerdict(ESnarePlacementVerdict::Blue);
		PreviewActor->FinishSpawning(FTransform::Identity);
	}

	GetWorld()->GetTimerManager().SetTimer(PreviewTimerHandle, this, &USnareBaseBuildComponent::HandlePreviewTick,
		PreviewIntervalSeconds, true);
	HandlePreviewTick();
	return true;
}

void USnareBaseBuildComponent::RotatePlacement()
{
	if (IsPlacingWallDecor())
	{
		PlacementWallHeightCm = SnareBuildRules::NextWallHeight(PlacementWallHeightCm);
		HandlePreviewTick();
	}
	else if (IsPlacingTopDecor())
	{
		// 소품은 돌림이 받침 것이라 「돌리기」 자리가 「자리」 — 같은 받침의 다음 슬롯.
		PlacementTopSlot = SnareBuildRules::NextTopSlot(PlacementTopSlot, PlacementTopSlotCount);
		HandlePreviewTick();
	}
	else if (IsPlacing())
	{
		PlacementRotation = SnareBuildRules::SnapRotation(PlacementRotation
			+ (bFinePlacement ? SnareBuildRules::FineRotationStepDegrees : 90));
		HandlePreviewTick();
	}
}

bool USnareBaseBuildComponent::IsPlacingWallDecor() const
{
	const USnareBaseSubsystem* const Base = IsPlacing() ? GetBase() : nullptr;
	const FItemRow* const Row = Base != nullptr ? Base->FindItemRow(PlacingItemID) : nullptr;
	return Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::WallDecor;
}

bool USnareBaseBuildComponent::IsPlacingTopDecor() const
{
	const USnareBaseSubsystem* const Base = IsPlacing() ? GetBase() : nullptr;
	const FItemRow* const Row = Base != nullptr ? Base->FindItemRow(PlacingItemID) : nullptr;
	return Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::TopDecor;
}

void USnareBaseBuildComponent::ToggleFinePlacement()
{
	bFinePlacement = !bFinePlacement;
	if (!bFinePlacement)
	{
		PlacementRotation = SnareBuildRules::SnapQuarterRotation(PlacementRotation);
		if (IsPlacing())
		{
			HandlePreviewTick();
		}
	}
	OnPlacementChanged.Broadcast();
}

void USnareBaseBuildComponent::CancelPlacement()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PreviewTimerHandle);
	}
	if (IsValid(PreviewActor))
	{
		PreviewActor->Destroy();
	}
	PreviewActor = nullptr;
	const bool bWasPlacing = IsPlacing();
	PlacingItemID = NAME_None;
	if (bWasPlacing)
	{
		OnPlacementChanged.Broadcast();
	}
}

void USnareBaseBuildComponent::HandlePreviewTick()
{
	USnareBaseSubsystem* const Base = GetBase();
	const FBaseRuleRow* const Rules = Base != nullptr ? Base->GetRules() : nullptr;
	const AActor* const Owner = GetOwner();
	if (!IsPlacing() || Rules == nullptr || !IsValid(Owner))
	{
		return;
	}

	const FItemRow* const Row = Base->FindItemRow(PlacingItemID);
	if (Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::WallDecor)
	{
		UpdateWallPreview(*Base, *Rules);
		return;
	}
	if (Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::TopDecor)
	{
		UpdateTopPreview(*Base, *Rules);
		return;
	}
	const FIntPoint Size = Row != nullptr
		? SnareBuildRules::RotatedFootprint(Row->FootprintX, Row->FootprintY, PlacementRotation) : FIntPoint(1, 1);
	// 발자국 가운데를 바라보는 줄 위, 가까운 변을 몸에서 PlacementClearanceCm 밖에 — 큰 텐트도 몸을 덮지 않는다.
	// 세밀 격자를 켠 가구는 반 칸(50cm) 단위, 텐트는 늘 한 칸 단위.
	const bool bHalfGrid = bFinePlacement && Row != nullptr && Row->PlaceableFunction != EPlaceableFunction::Tent;
	if (bHalfGrid)
	{
		SnareBuildRules::SplitHalfUnits(SnareBuildRules::AnchorAheadHalf(FVector2D(Owner->GetActorLocation()),
			FVector2D(Owner->GetActorForwardVector()), Size, Rules->CellSizeCm, PlacementClearanceCm), PlacementCell, PlacementHalf);
	}
	else
	{
		PlacementCell = SnareBuildRules::AnchorAhead(FVector2D(Owner->GetActorLocation()), FVector2D(Owner->GetActorForwardVector()),
			Size, Rules->CellSizeCm, PlacementClearanceCm);
		PlacementHalf = FIntPoint::ZeroValue;
	}
	FVector Center = ASnarePlacedStructureActor::CellToWorld(GetWorld(), PlacementCell, Size, Rules->CellSizeCm, PlacementHalf);
	// 2층 집 2층에 서 있으면 바닥 가구·러그는 2층 판 위에 뜨고 `floor: 2` 로 묻는다 (M108-06f). 텐트·계단은 늘 1층.
	const FName Zone = GetTentZone(GetWorld());
	const FSnareTentMirror* UpperHouse = nullptr;
	const APawn* const Pawn = Cast<APawn>(Owner);
	PlacementFloor = Row != nullptr && Row->PlaceableFunction != EPlaceableFunction::Tent
		&& Row->PlaceableFunction != EPlaceableFunction::Stairs && Pawn != nullptr
		? PlacementFloorAt(ASnarePlacedStructureActor::HouseViewFeet(*Pawn), Zone, &UpperHouse) : 1;
	if (PlacementFloor == 2 && UpperHouse != nullptr)
	{
		Center.Z = HouseGroundZ(*UpperHouse, Rules->CellSizeCm) + SnareBuildRules::UpperFloorZCm;
	}
	// 늪 칸(DESIGN 1.3)은 거점존 밖과 같은 빨강 — 놓을 자리(발자국 가운데)로 본다(몸이 칸 밖이어도 앞 자리가 늪이면 막힌다).
	PlacementVerdict = Base->EvaluatePlacement(PlacingItemID, PlacementCell, PlacementRotation, IsBuildAllowedAt(GetWorld(), Center),
		Zone, PlacementHalf, PlacementFloor);
	// 섬엔 나무·바위가, 목장엔 동물 칸 발판이 깔려 있다 — 겹치면 다른 물건과 겹친 것과 같은 빨강. 2층 판 위엔 둘 다 없다.
	const FVector2D FootHalf = FVector2D(Size) * Rules->CellSizeCm * 0.5;
	if (PlacementVerdict == ESnarePlacementVerdict::Green && PlacementFloor == 1
		&& (IsBlockedByGatherable(GetWorld(), Center, FootHalf) || IsBlockedByRanchSlot(GetWorld(), Center, FootHalf)))
	{
		PlacementVerdict = ESnarePlacementVerdict::Red;
	}

	if (IsValid(PreviewActor))
	{
		PreviewActor->SetActorLocation(Center);
		PreviewActor->InitStructure(PlacingItemID, FString(), FString(), FString(), PlacementRotation, Rules->CellSizeCm);
		PreviewActor->SetPreviewVerdict(PlacementVerdict);
	}

	OnPlacementChanged.Broadcast();
}

int32 USnareBaseBuildComponent::HouseCellsOf(const USnareBaseSubsystem& Base) const
{
	// 텐트 행은 ID 를 적지 않고 기능(Tent)으로 찾는다 — 집은 텐트 발자국 그대로다.
	for (const TPair<FName, uint8*>& Pair : Base.GetItemTable()->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::Tent)
		{
			return FMath::Max(1, Row->FootprintX);
		}
	}
	return 1;
}

int32 USnareBaseBuildComponent::PlacementFloorAt(const FVector& Feet, FName Zone, const FSnareTentMirror** OutHouse) const
{
	const USnareBaseSubsystem* const Base = GetBase();
	const FBaseRuleRow* const Rules = Base != nullptr ? Base->GetRules() : nullptr;
	if (Rules == nullptr)
	{
		return 1;
	}
	const FIntPoint FeetCell = SnareBuildRules::WorldToCell(Feet, Rules->CellSizeCm);
	const FSnareTentMirror* const House = Base->FindTentOwningCell(FeetCell, Zone);
	if (House == nullptr || !House->bUpgraded || House->Floors != 2)
	{
		return 1;
	}
	// 집 = 텐트 칸이 앵커인 HouseCells×HouseCells (벽 장식·서버 `HOUSE_CELLS` 와 같은 발자국).
	const FIntPoint Offset = FeetCell - House->Cell;
	const int32 HouseCells = HouseCellsOf(*Base);
	if (Offset.X < 0 || Offset.Y < 0 || Offset.X >= HouseCells || Offset.Y >= HouseCells
		|| !SnareBuildRules::IsOnUpperFloor(Feet.Z - HouseGroundZ(*House, Rules->CellSizeCm)))
	{
		return 1;
	}
	if (OutHouse != nullptr)
	{
		*OutHouse = House;
	}
	return 2;
}

float USnareBaseBuildComponent::HouseGroundZ(const FSnareTentMirror& Tent, float CellSizeCm) const
{
	if (const ASnarePlacedStructureActor* const TentActor = FindSpawned(Tent.TentId, FString()))
	{
		return TentActor->GetActorLocation().Z;
	}
	return ASnarePlacedStructureActor::CellToWorld(GetWorld(), Tent.Cell, FIntPoint(1, 1), CellSizeCm).Z;
}

void USnareBaseBuildComponent::UpdateWallPreview(USnareBaseSubsystem& Base, const FBaseRuleRow& Rules)
{
	const AActor* const Owner = GetOwner();
	const FName Zone = GetTentZone(GetWorld());
	const int32 HouseCells = HouseCellsOf(Base);
	// 고를 점 = 몸 앞 PlacementClearanceCm — 바닥 가구 미리보기와 같은 거리. 집 안에서 벽을 보고 서면 그 벽이 잡힌다.
	const FVector2D Point = FVector2D(Owner->GetActorLocation())
		+ FVector2D(Owner->GetActorForwardVector()).GetSafeNormal() * PlacementClearanceCm;

	// 걸 곳 = 이 존의 내 텐트 중 집이 먼저, 그다음 고를 점에 가까운 것 (텐트뿐이면 파랑으로 자리만 보인다).
	const FSnareTentMirror* Best = nullptr;
	float BestDistSq = 0.f;
	for (const FSnareTentMirror& Tent : Base.GetTents())
	{
		if (Tent.PlacedAtUtc.IsEmpty() || Tent.Zone != Zone)
		{
			continue;
		}
		const float DistSq = FVector2D::DistSquared(Point,
			FVector2D(SnareBuildRules::FootprintCenter(Tent.Cell, FIntPoint(HouseCells, HouseCells), Rules.CellSizeCm, 0.f)));
		if (Best == nullptr || (Tent.bUpgraded && !Best->bUpgraded) || (Tent.bUpgraded == Best->bUpgraded && DistSq < BestDistSq))
		{
			Best = &Tent;
			BestDistSq = DistSq;
		}
	}

	FVector Where;
	if (Best != nullptr && SnareBuildRules::PickWallSlot(Point, Best->Cell, HouseCells, Rules.CellSizeCm,
		PlacementCell, PlacementWallSide))
	{
		PlacementTentId = Best->TentId;
		PlacementVerdict = Base.EvaluateWallPlacement(Best->TentId, PlacementCell, PlacementWallSide, PlacementWallHeightCm, HouseCells);
		Where = ASnarePlacedStructureActor::WallToWorld(PlacementCell, PlacementWallSide, Rules.CellSizeCm,
			HouseGroundZ(*Best, Rules.CellSizeCm), PlacementWallHeightCm);
		PlacementRotation = SnareBuildRules::WallDecorRotation(PlacementWallSide);
	}
	else
	{
		// 걸 집이 없다 — 몸 앞 칸에 높이만 띄워 파랑으로 보인다(무엇을 놓는지는 보이게).
		PlacementTentId.Reset();
		PlacementWallSide = -1;
		PlacementVerdict = ESnarePlacementVerdict::Blue;
		PlacementCell = SnareBuildRules::WorldToCell(FVector(Point, 0.f), Rules.CellSizeCm);
		Where = ASnarePlacedStructureActor::CellToWorld(GetWorld(), PlacementCell, FIntPoint(1, 1), Rules.CellSizeCm)
			+ FVector(0.f, 0.f, PlacementWallHeightCm);
	}
	PlacementHalf = FIntPoint::ZeroValue;

	if (IsValid(PreviewActor))
	{
		PreviewActor->SetActorLocation(Where);
		PreviewActor->InitStructure(PlacingItemID, FString(), FString(), FString(), PlacementRotation, Rules.CellSizeCm);
		PreviewActor->SetPreviewVerdict(PlacementVerdict);
	}
	OnPlacementChanged.Broadcast();
}

void USnareBaseBuildComponent::UpdateTopPreview(USnareBaseSubsystem& Base, const FBaseRuleRow& Rules)
{
	const AActor* const Owner = GetOwner();
	const FName Zone = GetTentZone(GetWorld());
	// 고를 점 = 몸 앞 PlacementClearanceCm — 바닥 가구·벽 장식 미리보기와 같은 거리. 탁자 앞에 서서 보면 그 탁자가 잡힌다.
	const FVector2D Point = FVector2D(Owner->GetActorLocation())
		+ FVector2D(Owner->GetActorForwardVector()).GetSafeNormal() * PlacementClearanceCm;

	// 받침 = 이 존 내 텐트 사본에 있는 받침 가구 중 이 화면에 선 것, 몸 가운데가 점에 가장 가까운 것.
	const ASnarePlacedStructureActor* Best = nullptr;
	const FSnareTentMirror* BestTent = nullptr;
	float BestDistSq = FMath::Square(TopPickRadiusCm);
	for (TActorIterator<ASnarePlacedStructureActor> It(GetWorld()); It; ++It)
	{
		const FSnareTentMirror* const Tent = It->IsPreview() || It->GetTopSlotCount() <= 0 ? nullptr : Base.FindTent(It->GetTentId());
		if (Tent == nullptr || Tent->Zone != Zone || Tent->PlacedAtUtc.IsEmpty())
		{
			continue;
		}
		const UStaticMeshComponent* const HostBody = It->GetBodyComponent();
		const FVector Center = HostBody != nullptr ? HostBody->Bounds.Origin : It->GetActorLocation();
		const float DistSq = FVector2D::DistSquared(Point, FVector2D(Center));
		if (DistSq <= BestDistSq)
		{
			Best = *It;
			BestTent = Tent;
			BestDistSq = DistSq;
		}
	}

	FVector Where;
	if (Best != nullptr)
	{
		// 받침이 바뀌었으면 빈 슬롯 중 점에 가까운 것, 다 찼으면 가까운 것(빨강). 같은 받침이면 「자리」로 고른 슬롯 그대로.
		if (Best->GetStructureId() != PlacementTopHostId || PlacementTopSlot < 0 || PlacementTopSlot >= Best->GetTopSlotCount())
		{
			PlacementTopSlot = -1;
			float SlotBest = 0.f;
			bool bSlotFree = false;
			for (int32 Slot = 0; Slot < Best->GetTopSlotCount(); ++Slot)
			{
				FVector At;
				Best->GetTopSlotLocation(Slot, At);
				const bool bFree = Base.EvaluateTopPlacement(BestTent->TentId, Best->GetStructureId(), Slot, Best->GetTopSlotCount())
					!= ESnarePlacementVerdict::Red;
				const float DistSq = FVector2D::DistSquared(Point, FVector2D(At));
				if (PlacementTopSlot < 0 || (bFree && !bSlotFree) || (bFree == bSlotFree && DistSq < SlotBest))
				{
					PlacementTopSlot = Slot;
					SlotBest = DistSq;
					bSlotFree = bFree;
				}
			}
		}
		PlacementTentId = BestTent->TentId;
		PlacementTopHostId = Best->GetStructureId();
		PlacementTopSlotCount = Best->GetTopSlotCount();
		PlacementVerdict = Base.EvaluateTopPlacement(PlacementTentId, PlacementTopHostId, PlacementTopSlot, PlacementTopSlotCount);
		Best->GetTopSlotLocation(PlacementTopSlot, Where);
		PlacementRotation = Best->GetRotation();
		PlacementCell = SnareBuildRules::WorldToCell(Best->GetActorLocation(), Rules.CellSizeCm);
	}
	else
	{
		// 올릴 받침이 없다 — 몸 앞 칸 바닥에 파랑으로 보인다(무엇을 놓는지는 보이게).
		PlacementTentId.Reset();
		PlacementTopHostId.Reset();
		PlacementTopSlot = -1;
		PlacementTopSlotCount = 0;
		PlacementVerdict = ESnarePlacementVerdict::Blue;
		PlacementCell = SnareBuildRules::WorldToCell(FVector(Point, 0.f), Rules.CellSizeCm);
		Where = ASnarePlacedStructureActor::CellToWorld(GetWorld(), PlacementCell, FIntPoint(1, 1), Rules.CellSizeCm);
	}
	PlacementHalf = FIntPoint::ZeroValue;

	if (IsValid(PreviewActor))
	{
		PreviewActor->SetActorLocation(Where);
		PreviewActor->InitStructure(PlacingItemID, FString(), FString(), FString(), PlacementRotation, Rules.CellSizeCm);
		PreviewActor->SetPreviewVerdict(PlacementVerdict);
	}
	OnPlacementChanged.Broadcast();
}

bool USnareBaseBuildComponent::ConfirmPlacement(const FString& Pin)
{
	USnareBaseSubsystem* const Base = GetBase();
	const FBaseRuleRow* const Rules = Base != nullptr ? Base->GetRules() : nullptr;
	const FItemRow* const Row = Base != nullptr ? Base->FindItemRow(PlacingItemID) : nullptr;
	if (!IsPlacing() || Rules == nullptr || Row == nullptr)
	{
		return false;
	}

	HandlePreviewTick();
	if (PlacementVerdict != ESnarePlacementVerdict::Green)
	{
		return false;
	}

	bool bSent = false;
	if (Row->PlaceableFunction == EPlaceableFunction::WallDecor)
	{
		// 벽 장식 — 바닥 칸 대신 집·변·높이를 보낸다 (서버 `wallHangFail` 이 집·벽 칸·간격을 다시 본다).
		bSent = PlacementWallSide >= 0 && !PlacementTentId.IsEmpty()
			&& Base->RequestPlaceStructure(PlacementTentId, PlacingItemID, PlacementCell, PlacementRotation, FString(),
				FIntPoint::ZeroValue, PlacementWallSide, PlacementWallHeightCm);
	}
	else if (Row->PlaceableFunction == EPlaceableFunction::TopDecor)
	{
		// 소품 — 바닥 칸 대신 받침·슬롯을 보낸다 (서버 `topPlaceFail` 이 받침·빈 슬롯을 다시 본다). 칸은 받침 칸(서버가 받침 것으로 덮는다).
		bSent = PlacementTopSlot >= 0 && !PlacementTopHostId.IsEmpty() && !PlacementTentId.IsEmpty()
			&& Base->RequestPlaceStructure(PlacementTentId, PlacingItemID, PlacementCell, PlacementRotation, FString(),
				FIntPoint::ZeroValue, -1, 0, PlacementTopHostId, PlacementTopSlot);
	}
	else if (Row->PlaceableFunction == EPlaceableFunction::Tent)
	{
		bSent = Base->RequestPlaceTent(PlacementCell, GetTentZone(GetWorld()));
	}
	else if (const FSnareTentMirror* const Tent = Base->FindTentOwningCell(PlacementCell, GetTentZone(GetWorld())))
	{
		// PIN 은 보관함만, 표의 자릿수 숫자만. 비우면 주인만 연다 (서버 `PlaceStructure` 와 같은 줄).
		const bool bStorage = Row->PlaceableFunction == EPlaceableFunction::Storage;
		const bool bPinOk = Pin.IsEmpty() || (Pin.Len() == Rules->PinLength
			&& Algo::AllOf(Pin, [](TCHAR Char) { return FChar::IsDigit(Char); }));
		if (!bPinOk)
		{
			return false;
		}
		bSent = Base->RequestPlaceStructure(Tent->TentId, PlacingItemID, PlacementCell, PlacementRotation,
			bStorage ? Pin : FString(), PlacementHalf, -1, 0, FString(), -1, PlacementFloor);
	}

	// 요청이 나가면 미리보기를 걷는다 — 진짜 물건은 서버 응답 뒤에 선다 (CLAUDE.md 8).
	if (bSent)
	{
		CancelPlacement();
		// 내려놓는 몸짓 — 쪼그려 손을 바닥에 댄다(줍기 클립). 물건은 여전히 서버 응답 뒤에 선다 (감독 조작 조사 #4).
		if (ASnarehavenCharacter* const Body = Cast<ASnarehavenCharacter>(GetOwner()))
		{
			Body->PlayItemAction(EItemAction::Gather);
		}
	}
	return bSent;
}

void USnareBaseBuildComponent::HandleFireHealTick()
{
	USnareBaseSubsystem* const Base = GetBase();
	const FBaseRuleRow* const Rules = Base != nullptr ? Base->GetRules() : nullptr;
	const AActor* const Owner = GetOwner();
	UPlayerVitalsComponent* const Vitals = IsValid(Owner) ? Owner->FindComponentByClass<UPlayerVitalsComponent>() : nullptr;
	if (Rules == nullptr || Rules->FireHealRadiusCm <= 0.f || Rules->FireHealPerSecond <= 0.f
		|| !IsValid(Vitals) || Vitals->IsDead() || GetWorld() == nullptr)
	{
		return;
	}

	// 모닥불·정원 화로(`Campfire`)만 — 그릴은 요리만 한다 (DESIGN 8.10).
	for (TActorIterator<ASnarePlacedStructureActor> It(GetWorld()); It; ++It)
	{
		if (!It->IsPreview() && It->GetPlaceableFunction() == EPlaceableFunction::Campfire
			&& FVector::DistSquared2D(It->GetActorLocation(), Owner->GetActorLocation()) <= FMath::Square(Rules->FireHealRadiusCm))
		{
			const bool bWasFull = Vitals->GetHealth() >= Vitals->GetMaxHealth();
			Vitals->Heal(Rules->FireHealPerSecond * FireHealIntervalSeconds);
			// 체력이 실제로 찰 때만 낮은 숨 소리 (M58-03) — 가득이면 무음.
			if (ASnarehavenCharacter* const Body = Cast<ASnarehavenCharacter>(GetOwner()))
			{
				Body->PlayWarmthIfDue(Rules->FireHealPerSecond, bWasFull);
			}
			return;
		}
	}
}

ASnarePlacedStructureActor* USnareBaseBuildComponent::FindSpawned(const FString& TentId, const FString& StructureId) const
{
	for (TActorIterator<ASnarePlacedStructureActor> It(GetWorld()); It; ++It)
	{
		if (!It->IsPreview() && It->GetTentId() == TentId && It->GetStructureId() == StructureId)
		{
			return *It;
		}
	}
	return nullptr;
}

ASnarePlacedStructureActor* USnareBaseBuildComponent::SpawnStructure(FName ItemID, const FString& TentId,
	const FString& StructureId, const FIntPoint& Cell, int32 Rotation, bool bReplicated, const FString& OwnerPlayFabId,
	const FIntPoint& Half, int32 WallSide, int32 WallHeightCm, const FString& TopHostId, int32 TopSlot, int32 Floor)
{
	USnareBaseSubsystem* const Base = GetBase();
	const FBaseRuleRow* const Rules = Base != nullptr ? Base->GetRules() : nullptr;
	const FItemRow* const Row = Base != nullptr ? Base->FindItemRow(ItemID) : nullptr;
	if (Rules == nullptr || Row == nullptr || GetWorld() == nullptr)
	{
		return nullptr;
	}

	if (ASnarePlacedStructureActor* const Existing = FindSpawned(TentId, StructureId))
	{
		return Existing;
	}

	const FIntPoint Size = SnareBuildRules::RotatedFootprint(Row->FootprintX, Row->FootprintY, Rotation);
	FTransform Where(ASnarePlacedStructureActor::CellToWorld(GetWorld(), Cell, Size, Rules->CellSizeCm, Half));
	// 벽 장식은 그 집 벽 칸 변에, 방 안쪽을 보게 (M108-02) — 돌림은 변에서 정해진다.
	// 바닥 = 내 사본의 집, 없으면(호스트가 받은 게스트 것) 이 화면에 선 그 집 액터. 둘 다 없으면 걸 벽이 없어 안 세운다.
	if (WallSide >= 0)
	{
		const FSnareTentMirror* const House = Base->FindTent(TentId);
		const ASnarePlacedStructureActor* const HouseActor = FindSpawned(TentId, FString());
		if (House == nullptr && HouseActor == nullptr)
		{
			return nullptr;
		}
		const float GroundZ = House != nullptr ? HouseGroundZ(*House, Rules->CellSizeCm) : HouseActor->GetActorLocation().Z;
		Where.SetLocation(ASnarePlacedStructureActor::WallToWorld(Cell, WallSide, Rules->CellSizeCm, GroundZ, WallHeightCm));
		Rotation = SnareBuildRules::WallDecorRotation(WallSide);
		Floor = 1;
	}
	// 2층 바닥 가구는 그 집 바닥 + 2층 높이에 (M108-06f) — 2층 판 윗면과 같은 높이. 칸 바닥 쏘기는 판(폰만 막음)을 지나 땅에 맞는다.
	else if (Floor >= 2 && TopHostId.IsEmpty())
	{
		const FSnareTentMirror* const House = Base->FindTent(TentId);
		const ASnarePlacedStructureActor* const HouseActor = FindSpawned(TentId, FString());
		const float GroundZ = House != nullptr ? HouseGroundZ(*House, Rules->CellSizeCm)
			: HouseActor != nullptr ? HouseActor->GetActorLocation().Z : Where.GetLocation().Z;
		Where.SetLocation(FVector(Where.GetLocation().X, Where.GetLocation().Y, GroundZ + SnareBuildRules::UpperFloorZCm));
		Floor = 2;
	}
	else
	{
		Floor = 1;
	}
	// 소품은 이 화면에 선 받침의 윗면 슬롯에, 받침과 같은 돌림으로 (M108-03). 받침이 아직 없으면 올릴 데가 없어 안 세운다.
	if (!TopHostId.IsEmpty())
	{
		const ASnarePlacedStructureActor* const Host = FindSpawned(TentId, TopHostId);
		FVector OnTop;
		if (Host == nullptr || !Host->GetTopSlotLocation(TopSlot, OnTop))
		{
			return nullptr;
		}
		Where.SetLocation(OnTop);
		Rotation = Host->GetRotation();
		Floor = Host->GetPlacedFloor();
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	ASnarePlacedStructureActor* const Spawned = GetWorld()->SpawnActor<ASnarePlacedStructureActor>(
		ASnarePlacedStructureActor::StaticClass(), Where, Params);
	if (!IsValid(Spawned))
	{
		return nullptr;
	}

	const UGameInstance* const GameInstance = GetWorld()->GetGameInstance();
	const USnareBackendSubsystem* const Backend = GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
	Spawned->SetReplicates(bReplicated);
	Spawned->InitStructure(ItemID, TentId, StructureId,
		!OwnerPlayFabId.IsEmpty() ? OwnerPlayFabId : (Backend != nullptr ? Backend->GetPlayFabId() : FString()),
		Rotation, Rules->CellSizeCm);
	if (WallSide >= 0)
	{
		Spawned->SetWallSlot(Cell, WallSide, WallHeightCm);
	}
	if (!TopHostId.IsEmpty())
	{
		Spawned->SetTopSlot(TopHostId, TopSlot);
	}
	Spawned->SetPlacedFloor(Floor);
	Spawned->FinishSpawning(Where);
	return Spawned;
}

void USnareBaseBuildComponent::HandleTentChanged(const FSnareTentMirror& Tent)
{
	USnareBaseSubsystem* const Base = GetBase();
	// 다른 존의 텐트는 이 맵에 세우지 않는다 — 칸 좌표가 다른 월드다.
	if (Base == nullptr || Tent.Zone != GetTentZone(GetWorld()))
	{
		return;
	}

	// 텐트 행은 ID 를 적지 않고 기능(Tent)으로 찾는다.
	FName TentItemID;
	for (const TPair<FName, uint8*>& Pair : Base->GetItemTable()->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::Tent)
		{
			TentItemID = Row->ItemID;
			break;
		}
	}

	// 복제는 권위가 있는 쪽(호스트)만 — 게스트가 client 에서 복제를 켜면 엔진이 거절한다(`SetReplicates` 경고).
	if (ASnarePlacedStructureActor* const TentActor = SpawnStructure(TentItemID, Tent.TentId, FString(), Tent.Cell, 0,
		GetOwner()->HasAuthority()))
	{
		TentActor->SetHouse(Tent.bUpgraded, Tent.Color, Tent.Floors);
		ShareWithHost(TentActor, Tent.Cell, FIntPoint::ZeroValue);
	}
}

void USnareBaseBuildComponent::HandleCraftConfirmed(FName ResultItemID, ESnareCraftPlace Place)
{
	ASnarehavenCharacter* const Body = Cast<ASnarehavenCharacter>(GetOwner());
	if (!IsValid(Body) || !IsLocalOwner())
	{
		return;
	}
	// 소리는 같은 방 사람에게도 (`PlayActionSound` 가 호스트를 거쳐 퍼뜨린다), 몸짓은 놓기와 같은 줍기 클립.
	Body->PlayActionSound(SnareCraftRules::ConfirmCue(Place), Body->GetActorLocation(), SnareCraftRules::ConfirmVolume(Place));
	Body->PlayItemAction(EItemAction::Gather);
	UE_LOG(LogSnarehaven, Log, TEXT("제작 확정 몸짓·소리: %s 자리=%d 소리=%s"),
		*ResultItemID.ToString(), static_cast<int32>(Place), *SnareCraftRules::ConfirmCue(Place).ToString());
}

void USnareBaseBuildComponent::HandleBuildConfirmed(const FString& TentId, const FString& StructureId)
{
	ASnarehavenCharacter* const Body = Cast<ASnarehavenCharacter>(GetOwner());
	if (!IsValid(Body) || !IsLocalOwner())
	{
		return;
	}
	// 선 물건 자리에서 — 다른 존에 세운 것이라 이 맵에 없으면 내 몸 자리. `PlayActionSound` 가 호스트를 거쳐
	// 같은 방 사람에게도 그 자리 소리로 퍼뜨린다 (남이 놓은 것은 그 자리에서 들린다).
	const ASnarePlacedStructureActor* const Placed = FindSpawned(TentId, StructureId);
	LastBuildSoundLocation = Placed != nullptr ? Placed->GetActorLocation() : Body->GetActorLocation();
	Body->PlayActionSound(SnareBuildRules::PlaceConfirmCue(), LastBuildSoundLocation, 1.f);
	UE_LOG(LogSnarehaven, Log, TEXT("설치 확정 소리: 텐트=%s 구조물=%s 자리=%s 소리=%s"), *TentId, *StructureId,
		*LastBuildSoundLocation.ToCompactString(), *SnareBuildRules::PlaceConfirmCue().ToString());
}

void USnareBaseBuildComponent::HandleStructureEditConfirmed(const FString& TentId, const FString& StructureId,
	ESnareStructureEdit Edit)
{
	ASnarehavenCharacter* const Body = Cast<ASnarehavenCharacter>(GetOwner());
	if (!IsValid(Body) || !IsLocalOwner())
	{
		return;
	}
	// 설치 확정과 같은 길 — 그 물건 자리(다른 존 물건이면 내 몸 자리)에서, 호스트를 거쳐 같은 방 사람에게도.
	const ASnarePlacedStructureActor* const Placed = FindSpawned(TentId, StructureId);
	LastBuildSoundLocation = Placed != nullptr ? Placed->GetActorLocation() : Body->GetActorLocation();
	const FName Cue = SnareBuildRules::EditConfirmCue(Edit);
	Body->PlayActionSound(Cue, LastBuildSoundLocation, SnareBuildRules::EditConfirmVolume(Edit));
	// 철거 = 내려치기, 칠하기 = 손 뻗기 (INBOX 2026-10-15 감사 (e) — 전엔 소리만 났다).
	Body->PlayItemAction(Edit == ESnareStructureEdit::Remove ? EItemAction::Smash : EItemAction::Gather);
	UE_LOG(LogSnarehaven, Log, TEXT("철거·칠하기 확정 소리: 텐트=%s 구조물=%s 자리=%s 소리=%s"), *TentId, *StructureId,
		*LastBuildSoundLocation.ToCompactString(), *Cue.ToString());
}

void USnareBaseBuildComponent::HandleStructurePlaced(const FSnareTentMirror& Tent, const FSnareStructureMirror& Structure)
{
	if (Tent.Zone != GetTentZone(GetWorld()))
	{
		return;
	}
	if (ASnarePlacedStructureActor* const Spawned = SpawnStructure(Structure.ItemID, Tent.TentId, Structure.StructureId,
		Structure.Cell, Structure.Rotation, GetOwner()->HasAuthority(), FString(), Structure.Half,
		Structure.WallSide, Structure.WallHeightCm, Structure.TopHostId, Structure.TopSlot, Structure.Floor))
	{
		Spawned->SetPaint(Structure.Color);
		// 발전기 연료 — 서버 `fuelUntilUtc` 까지 남은 초(서버 시계로 잼)를 게임 서버 시계 끝 시각으로 (M108-07c).
		if (Spawned->GetPlaceableFunction() == EPlaceableFunction::Generator)
		{
			if (const USnareBaseSubsystem* const Base = GetBase())
			{
				Spawned->SetGeneratorFuelSecondsLeft(Base->FuelSecondsLeft(Structure.FuelUntilUtc));
			}
		}
		ShareWithHost(Spawned, Structure.Cell, Structure.Half);
	}
}

void USnareBaseBuildComponent::ShareWithHost(const ASnarePlacedStructureActor* Spawned, const FIntPoint& Cell, const FIntPoint& Half) const
{
	// 권위가 있으면(호스트·오프라인) 이미 복제 액터다. 진짜 클라이언트만 호스트에게 알린다.
	if (Spawned == nullptr || GetOwner() == nullptr || GetOwner()->HasAuthority() || GetOwner()->GetNetMode() != NM_Client)
	{
		return;
	}
	// 벽 장식은 걸린 변·높이도 같이 — 호스트가 그 집 벽에 세운다 (M108-02).
	// 텐트는 집 여부·색도 — 집으로 올리면 `HandleTentChanged` 가 다시 불려 여기로 온다(호스트 화면도 집이 된다).
	// 소품은 받침·슬롯도 — 호스트가 그 받침 윗면에 세운다 (M108-03). 받침은 먼저 세워져 먼저 알려진다(Reliable 순서).
	// 집은 층수도 — 2층을 올리면 호스트 화면도 위층 몸이 선다 (M108-06c). 가구는 놓인 층도 — 2층 가구는 호스트 화면에서도 2층에 (M108-06f).
	const_cast<USnareBaseBuildComponent*>(this)->Server_ShareStructure(Spawned->GetItemID(), Spawned->GetTentId(),
		Spawned->GetStructureId(), Cell, Spawned->GetRotation(), Spawned->GetOwnerPlayFabId(), Spawned->GetPaintItemID(), Half,
		Spawned->GetWallSide(), Spawned->GetWallHeightCm(), Spawned->IsHouse(), Spawned->GetHouseColor(),
		Spawned->GetTopHostId(), Spawned->GetTopSlot(), Spawned->GetHouseFloors(), Spawned->GetPlacedFloor(),
		static_cast<float>(Spawned->GetGeneratorFuelSecondsLeft()));
}

bool USnareBaseBuildComponent::HostAcceptShared(FName ItemID, const FString& TentId, const FString& StructureId,
	const FIntPoint& Cell, int32 Rotation, const FString& OwnerPlayFabId, FName PaintItemID, const FIntPoint& Half,
	int32 WallSide, int32 WallHeightCm, bool bHouse, int32 HouseColor, const FString& TopHostId, int32 TopSlot, int32 HouseFloors,
	int32 Floor, float FuelSecondsLeft)
{
	USnareBaseSubsystem* const Base = GetBase();
	const FBaseRuleRow* const Rules = Base != nullptr ? Base->GetRules() : nullptr;
	const FItemRow* const Row = Base != nullptr ? Base->FindItemRow(ItemID) : nullptr;
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority() || Rules == nullptr || Row == nullptr || GetWorld() == nullptr
		|| Row->FootprintX <= 0 || Row->FootprintY <= 0 || TentId.IsEmpty())
	{
		return false;
	}

	// 이미 서 있으면 다시 안 세운다(같은 주소). 텐트는 집 여부·색·층수만 고친다 — 게스트가 나중에 집으로·2층으로 올린 것.
	if (ASnarePlacedStructureActor* const Existing = FindSpawned(TentId, StructureId))
	{
		if (Existing->IsTent() && (Existing->IsHouse() != bHouse || Existing->GetHouseColor() != HouseColor
			|| Existing->GetHouseFloors() != FMath::Max(1, HouseFloors)))
		{
			Existing->SetHouse(bHouse, HouseColor, HouseFloors);
		}
		if (Existing->GetPlaceableFunction() == EPlaceableFunction::Generator)
		{
			Existing->SetGeneratorFuelSecondsLeft(FuelSecondsLeft);   // 게스트가 연료를 넣고 다시 알렸다
		}
		return true;
	}

	// 벽 장식 행은 벽 칸으로만, 벽 칸은 벽 장식 행만 — 바닥에 서거나 가구가 벽에 뜨지 않게.
	// 소품 행도 마찬가지 — 받침으로만, 받침은 소품 행만 (M108-03).
	if ((Row->PlaceableFunction == EPlaceableFunction::WallDecor) != (WallSide >= 0)
		|| (Row->PlaceableFunction == EPlaceableFunction::TopDecor) == TopHostId.IsEmpty())
	{
		return false;
	}
	if (!TopHostId.IsEmpty())
	{
		return HostAcceptSharedTop(ItemID, TentId, StructureId, Cell, OwnerPlayFabId, PaintItemID, TopHostId, TopSlot);
	}
	if (WallSide >= 0)
	{
		return HostAcceptSharedWall(*Base, *Rules, ItemID, TentId, StructureId, Cell, OwnerPlayFabId, PaintItemID,
			WallSide, WallHeightCm);
	}

	// 호스트 기준 겹침 — 선 것들이 덮은 칸과 새 발자국이 하나라도 겹치면 거절.
	// 러그와 바닥 가구는 서로 겹쳐도 된다 — 러그는 러그끼리만 센다(텐트 몸은 둘 다 막는다, DESIGN 8.11 「러그 깔기」).
	const bool bRug = Row->PlaceableFunction == EPlaceableFunction::Rug;
	// 층이 다르면 같은 칸이어도 안 겹친다(서버 `PlaceStructure` 같은 줄) — 2층 가구는 2층 것끼리, 텐트 몸은 1층.
	const int32 PlaceFloor = FMath::Clamp(Floor, 1, 2);
	TSet<FIntPoint> Occupied;
	for (TActorIterator<ASnarePlacedStructureActor> It(GetWorld()); It; ++It)
	{
		// 벽 장식·소품은 바닥 칸을 차지하지 않는다 — 위치가 벽 변·받침 윗면이라 발자국으로 읽으면 엉뚱한 바닥 칸을 막는다.
		// 집 몸도 안 센다 — 집 1층 안은 꾸미는 방이다(서버·미리보기 같은 줄, M108-06f-4). 텐트 몸은 막는다.
		if (It->IsPreview() || It->IsWallHung() || It->IsOnTop() || It->GetPlacedFloor() != PlaceFloor
			|| (It->IsTent() && It->IsHouse()))
		{
			continue;
		}
		if (!It->IsTent() && bRug != (It->GetPlaceableFunction() == EPlaceableFunction::Rug))
		{
			continue;
		}
		const FIntPoint Size = SnareBuildRules::RotatedFootprint(It->GetFootprint().X, It->GetFootprint().Y, It->GetRotation());
		const FVector Loc = It->GetActorLocation();
		// 반 칸(50cm) 단위 앵커 — 세밀 격자로 놓인 가구도 같은 길로 센다.
		const float Unit = Rules->CellSizeCm * 0.5f;
		const FIntPoint Anchor(FMath::RoundToInt(Loc.X / Unit - Size.X), FMath::RoundToInt(Loc.Y / Unit - Size.Y));
		TArray<FIntPoint> Cells;
		SnareBuildRules::FootprintHalfUnits(Anchor, Size, Cells);
		Occupied.Append(Cells);
	}
	const int32 SnappedRotation = SnareBuildRules::SnapRotation(Rotation);
	TArray<FIntPoint> NewCells;
	SnareBuildRules::FootprintHalfUnits(SnareBuildRules::JoinHalfUnits(Cell, Half),
		SnareBuildRules::RotatedFootprint(Row->FootprintX, Row->FootprintY, SnappedRotation), NewCells);
	for (const FIntPoint& NewCell : NewCells)
	{
		if (Occupied.Contains(NewCell))
		{
			return false;
		}
	}

	ASnarePlacedStructureActor* const Spawned = SpawnStructure(ItemID, TentId, StructureId, Cell, SnappedRotation, true, OwnerPlayFabId, Half,
		-1, 0, FString(), -1, PlaceFloor);
	if (Spawned != nullptr && !PaintItemID.IsNone())
	{
		Spawned->SetPaint(PaintItemID);
	}
	if (Spawned != nullptr && Spawned->IsTent() && bHouse)
	{
		Spawned->SetHouse(bHouse, HouseColor, HouseFloors);
	}
	if (Spawned != nullptr && Spawned->GetPlaceableFunction() == EPlaceableFunction::Generator)
	{
		Spawned->SetGeneratorFuelSecondsLeft(FuelSecondsLeft);
	}
	return Spawned != nullptr;
}

bool USnareBaseBuildComponent::HostAcceptSharedWall(const USnareBaseSubsystem& Base, const FBaseRuleRow& Rules,
	FName ItemID, const FString& TentId, const FString& StructureId, const FIntPoint& Cell, const FString& OwnerPlayFabId,
	FName PaintItemID, int32 WallSide, int32 WallHeightCm)
{
	// 걸 집 = 이 화면에 선 그 텐트 액터(게스트 텐트도 `ShareWithHost` 로 먼저 와 있다). 없으면 걸 벽이 없다.
	const ASnarePlacedStructureActor* const HouseActor = FindSpawned(TentId, FString());
	if (HouseActor == nullptr || !HouseActor->IsTent())
	{
		return false;
	}
	// 집 앵커 칸 = 텐트 액터 위치(발자국 가운데)에서 거꾸로 — 겹침 셈과 같은 반 칸 앵커 길.
	const int32 HouseCells = HouseCellsOf(Base);
	const float Unit = Rules.CellSizeCm * 0.5f;
	const FVector HouseLoc = HouseActor->GetActorLocation();
	const FIntPoint HouseCell(FMath::FloorToInt((FMath::RoundToInt(HouseLoc.X / Unit) - HouseCells) * 0.5f),
		FMath::FloorToInt((FMath::RoundToInt(HouseLoc.Y / Unit) - HouseCells) * 0.5f));

	// 같은 집에 걸린 것 — 서버 `wallHangFail` 과 같은 줄(`EvaluateWallHang`)로 벽 칸·높이·간격을 본다.
	// 집인지는 백엔드가 이미 판정했다(집 여부는 텐트 공유로 오지만 순서에 기대지 않는다) — 여기선 벽 칸·겹침만.
	TArray<SnareBuildRules::FWallHung> Hung;
	for (TActorIterator<ASnarePlacedStructureActor> It(GetWorld()); It; ++It)
	{
		if (!It->IsPreview() && It->IsWallHung() && It->GetTentId() == TentId)
		{
			Hung.Add({ It->GetWallCell(), It->GetWallSide(), It->GetWallHeightCm() });
		}
	}
	if (SnareBuildRules::EvaluateWallHang(Cell, WallSide, WallHeightCm, HouseCell, HouseCells, /*bHouse=*/true, Hung)
		!= ESnarePlacementVerdict::Green)
	{
		return false;
	}

	ASnarePlacedStructureActor* const Spawned = SpawnStructure(ItemID, TentId, StructureId, Cell,
		SnareBuildRules::WallDecorRotation(WallSide), true, OwnerPlayFabId, FIntPoint::ZeroValue, WallSide, WallHeightCm);
	if (Spawned != nullptr && !PaintItemID.IsNone())
	{
		Spawned->SetPaint(PaintItemID);
	}
	return Spawned != nullptr;
}

bool USnareBaseBuildComponent::HostAcceptSharedTop(FName ItemID, const FString& TentId, const FString& StructureId,
	const FIntPoint& Cell, const FString& OwnerPlayFabId, FName PaintItemID, const FString& TopHostId, int32 TopSlot)
{
	// 올릴 받침 = 이 화면에 선 그 텐트의 받침 액터(게스트 받침도 `ShareWithHost` 로 먼저 와 있다). 없으면 올릴 데가 없다.
	const ASnarePlacedStructureActor* const Host = FindSpawned(TentId, TopHostId);
	if (Host == nullptr || Host->IsPreview() || Host->GetTopSlotCount() <= 0)
	{
		return false;
	}
	// 같은 받침에 올라간 슬롯 — 서버 `topPlaceFail` 과 같은 줄(`EvaluateTopPlace`)로 슬롯 범위·겹침을 본다.
	TArray<int32> Taken;
	for (TActorIterator<ASnarePlacedStructureActor> It(GetWorld()); It; ++It)
	{
		if (!It->IsPreview() && It->IsOnTop() && It->GetTentId() == TentId && It->GetTopHostId() == TopHostId)
		{
			Taken.Add(It->GetTopSlot());
		}
	}
	if (SnareBuildRules::EvaluateTopPlace(TopSlot, Host->GetTopSlotCount(), Taken) != ESnarePlacementVerdict::Green)
	{
		return false;
	}

	ASnarePlacedStructureActor* const Spawned = SpawnStructure(ItemID, TentId, StructureId, Cell, Host->GetRotation(), true,
		OwnerPlayFabId, FIntPoint::ZeroValue, -1, 0, TopHostId, TopSlot);
	if (Spawned != nullptr && !PaintItemID.IsNone())
	{
		Spawned->SetPaint(PaintItemID);
	}
	return Spawned != nullptr;
}

bool USnareBaseBuildComponent::HostRemoveShared(const FString& StructureId, const FString& OwnerPlayFabId)
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority() || GetWorld() == nullptr || StructureId.IsEmpty())
	{
		return false;
	}
	bool bRemoved = false;
	for (TActorIterator<ASnarePlacedStructureActor> It(GetWorld()); It; ++It)
	{
		if (!It->IsPreview() && It->GetStructureId() == StructureId && It->GetOwnerPlayFabId() == OwnerPlayFabId)
		{
			It->Destroy();
			bRemoved = true;
		}
	}
	return bRemoved;
}

void USnareBaseBuildComponent::Server_ShareStructure_Implementation(FName ItemID, const FString& TentId,
	const FString& StructureId, FIntPoint Cell, int32 Rotation, const FString& OwnerPlayFabId, FName PaintItemID, FIntPoint Half,
	int32 WallSide, int32 WallHeightCm, bool bHouse, int32 HouseColor, const FString& TopHostId, int32 TopSlot, int32 HouseFloors,
	int32 Floor, float FuelSecondsLeft)
{
	if (!HostAcceptShared(ItemID, TentId, StructureId, Cell, Rotation, OwnerPlayFabId, PaintItemID, Half, WallSide, WallHeightCm,
		bHouse, HouseColor, TopHostId, TopSlot, HouseFloors, Floor, FuelSecondsLeft))
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Base] 게스트 설치 공유 거절 %s (%d,%d) 벽 변 %d 받침 %s 슬롯 %d 층 %d — 놓을 수 없는 것이거나 겹침"),
			*ItemID.ToString(), Cell.X, Cell.Y, WallSide, *TopHostId, TopSlot, Floor);
	}
}

void USnareBaseBuildComponent::Server_UnshareStructure_Implementation(const FString& StructureId, const FString& OwnerPlayFabId)
{
	HostRemoveShared(StructureId, OwnerPlayFabId);
}

void USnareBaseBuildComponent::HandleStructureRemoved(const FString& StructureId)
{
	if (GetOwner() != nullptr && !GetOwner()->HasAuthority() && GetOwner()->GetNetMode() == NM_Client && !StructureId.IsEmpty())
	{
		const USnareBackendSubsystem* const Backend = GetWorld() != nullptr && GetWorld()->GetGameInstance() != nullptr
			? GetWorld()->GetGameInstance()->GetSubsystem<USnareBackendSubsystem>() : nullptr;
		Server_UnshareStructure(StructureId, Backend != nullptr ? Backend->GetPlayFabId() : FString());
	}
	for (TActorIterator<ASnarePlacedStructureActor> It(GetWorld()); It; ++It)
	{
		if (!It->IsPreview() && !StructureId.IsEmpty() && It->GetStructureId() == StructureId)
		{
			It->Destroy();
		}
	}
}

void USnareBaseBuildComponent::SpawnWholeMirror()
{
	USnareBaseSubsystem* const Base = GetBase();
	if (Base == nullptr || Base->GetItemTable() == nullptr)
	{
		return;
	}

	for (const FSnareTentMirror& Tent : Base->GetTents())
	{
		// 남의 보관함 사본(텐트 시각이 비어 있다)은 내 땅이 아니다. 다른 존 텐트는 다른 맵에 선다.
		if (Tent.PlacedAtUtc.IsEmpty() || Tent.Zone != GetTentZone(GetWorld()))
		{
			continue;
		}
		HandleTentChanged(Tent);
		// 받침 먼저, 소품은 그 위에 — 소품은 선 받침 윗면에서 자리를 잰다.
		for (const bool bTopPass : { false, true })
		{
			for (const FSnareStructureMirror& Structure : Tent.Structures)
			{
				if (Structure.TopHostId.IsEmpty() != bTopPass)
				{
					HandleStructurePlaced(Tent, Structure);
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
