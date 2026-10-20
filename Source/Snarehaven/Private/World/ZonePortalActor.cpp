#include "World/ZonePortalActor.h"
#include "SnareCheckSwitch.h"
#include "Snarehaven.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Online/SnareOnlineSubsystem.h"
#include "World/SnareZoneRules.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/CollisionProfile.h"

namespace
{
	// DESIGN 8.14 「잡화점·선착장 진짜 모습」 부두 행들.
	constexpr float PierLongSideCm = 600.f;      // 긴 변 (뭍 200 · 바다 위 400)
	constexpr float BoardingCellCm = 200.f;      // 배 타는 칸 = 바다 쪽 끝 200cm
	constexpr float PlankAboveFloorCm = 15.f;    // 판자 윗면 = 뭍 바닥 +15
	constexpr float PivotBelowFloorCm = 118.f;   // 받침점 = 뭍 바닥 −118 (DESIGN 「부두 높이」 🟡 M13-41 엔진 실측)
	constexpr float FerryArriveBackCm = 400.f;   // 섬 배 내리는 자리 = 배 타는 칸에서 뭍 쪽 400 (뭍 위 판자 가운데, DESIGN 8.14)
}

AZonePortalActor::AZonePortalActor()
{
	// 포탈은 밟는 순간에만 일할 것이 있다 — 매 프레임 볼 것이 없다 (engines/unreal.md 코드 규칙 2).
	PrimaryActorTick.bCanEverTick = false;
	// 나룻배 도착 소리를 모든 기기에 보내려면 복제돼야 한다 (`Multicast_PlayCrossingCue`, DESIGN 8.10).
	bReplicates = true;

	// 액터 자리 = **배 타는 칸 가운데의 판자 윗면**, 앞(+X) = 바다 쪽. 버튼(M6-02b)은 폰 중심과 이 점의
	// 거리로 뜨니, 판자 위 폰(중심이 캡슐 반높이 위)이 바다 쪽 끝 200cm 근처에 서야 사거리에 든다.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent);
	// 부두 위를 걷는다 — 메시가 복잡 충돌을 단순 충돌로 쓴다(`import_equipment_weapons.py` PROPS) 판자 모양대로 밟힌다.
	Body->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Body->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PierMeshAsset(
		TEXT("/Game/Game/Environment/Props/FerryPier/SM_Prop_FerryPier_01.SM_Prop_FerryPier_01"));
	if (PierMeshAsset.Succeeded())
	{
		Body->SetStaticMesh(PierMeshAsset.Object);
	}
	FitPierBody();

	// C++ 전용 프로젝트라 블루프린트 없이 기본 표를 주려면 생성자 경로 참조뿐이다
	// (AGatherableActor·AAnimalCharacter 와 같은 방식).
	static ConstructorHelpers::FObjectFinder<UDataTable> ZoneTableAsset(
		TEXT("/Game/Game/Data/World/DT_ZoneTable.DT_ZoneTable"));
	if (ZoneTableAsset.Succeeded())
	{
		ZoneTable = ZoneTableAsset.Object;
	}
}

void AZonePortalActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// DESIGN 8.14 「부두 크기·높이 적용 시점」(M13-41): "부두 배율·위치를 생성자만이 아니라 OnConstruction 에서도
	// 다시 맞춘다" — 맵에 저장된 `Body` 배율 1·위치 (0,0,0) 이 생성자 값을 덮어 부두가 100cm 로 보였다.
	FitPierBody();
}

void AZonePortalActor::FitPierBody()
{
	const UStaticMesh* const Mesh = IsValid(Body) ? Body->GetStaticMesh() : nullptr;
	if (Mesh == nullptr)
	{
		return;
	}
	// 메시 긴 변이 X, 종 기둥 쪽(+X)이 바다 끝이다 — 돌리지 않는다. 긴 변을 표 값에 맞추는 한 배율.
	const FBox Box = Mesh->GetBoundingBox();
	const float Scale = PierLongSideCm / FMath::Max(KINDA_SMALL_NUMBER, Box.GetSize().X);
	Body->SetRelativeScale3D(FVector(Scale));
	// 바다 끝을 이 점 앞 `BoardingCellCm / 2` 에, 받침점을 판자 윗면 아래로(바닥 +15 → 바닥 −118) 내린다.
	Body->SetRelativeLocationAndRotation(FVector(BoardingCellCm * 0.5f - Box.Max.X * Scale, -Box.GetCenter().Y * Scale,
		-(PlankAboveFloorCm + PivotBelowFloorCm)), FRotator::ZeroRotator);
}

void AZonePortalActor::BeginPlay()
{
	Super::BeginPlay();

	InitializeFromTable();

#if !UE_BUILD_SHIPPING
	// `-SnareCrossSoundAt=2/3/4` — 3층 소리 확인 전용 (M53-05). 첫째 초 = 존 포털, 둘째 초 = 나룻배, 셋째 초 = 광산 문
	// (`AMineDoorActor` 가 읽음). 맵 이동·순간이동 없이 확정 뒤 소리 함수만 사람 길과 같게 부른다. 쉼표는 `FParse::Value` 가 끊으므로 `/`.
	FString CrossAt;
	if (HasAuthority() && SnareCheckSwitch::Value(TEXT("SnareCrossSoundAt="), CrossAt))
	{
		TArray<FString> Seconds;
		CrossAt.ParseIntoArray(Seconds, TEXT("/"));
		for (int32 Index = 0; Index < FMath::Min(Seconds.Num(), 2); ++Index)
		{
			FTimerHandle Unused;
			GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this, Index]()
			{
				PlayCrossingCue(Index == 0 ? SnareSound::EZoneCrossing::Portal : SnareSound::EZoneCrossing::Ferry, true, GetActorLocation());
			}), FCString::Atof(*Seconds[Index]), false);
		}
	}
#endif
}

void AZonePortalActor::SetZoneData(UDataTable* Table, FName InSourceZone, FName InTargetZone)
{
	ZoneTable = Table;
	SourceZoneID = InSourceZone;
	TargetZoneID = InTargetZone;
}

TMap<FName, FZoneRow> AZonePortalActor::ReadZoneRows(const UDataTable* Table)
{
	TMap<FName, FZoneRow> Rows;
	if (!IsValid(Table))
	{
		return Rows;
	}

	for (const FName& RowName : Table->GetRowNames())
	{
		if (const FZoneRow* Row = Table->FindRow<FZoneRow>(RowName, TEXT("AZonePortalActor::ReadZoneRows")))
		{
			Rows.Add(Row->ZoneID.IsNone() ? RowName : Row->ZoneID, *Row);
		}
	}

	return Rows;
}

FName AZonePortalActor::FindCurrentZoneID(const UWorld* const World)
{
	if (!IsValid(World))
	{
		return NAME_None;
	}

	// 생성자가 이미 물고 있는 표를 기본값 객체에서 빌린다 — 맵 이름을 코드에 박지 않으려고 만든 표라
	// (`FZoneRow` 머리말) 되짚는 쪽도 같은 표를 봐야 한다.
	const AZonePortalActor* const Defaults = GetDefault<AZonePortalActor>();
	const TMap<FName, FZoneRow> Rows = ReadZoneRows(Defaults != nullptr ? Defaults->ZoneTable : nullptr);

	// PIE 는 맵 이름 앞에 `UEDPIE_0_` 를 붙인다 — 붙은 채로 맞대면 에디터에서만 조용히 빈 값이 된다.
	const FString MapName = UWorld::RemovePIEPrefix(World->GetMapName());
	for (const TPair<FName, FZoneRow>& Entry : Rows)
	{
		// 맵 안 성격 존(M12-00 설원·늪·섬)은 같은 맵 이름을 나눠 쓴다 — 맵 전체 행만 "지금 존" 이다.
		if (Entry.Value.LevelName.ToString() == MapName && !SnareZones::IsRegionRow(Entry.Value))
		{
			return Entry.Key;
		}
	}

	return NAME_None;
}

const FZoneRow* AZonePortalActor::ResolveTargetZone(const TMap<FName, FZoneRow>& Rows,
	const FName InSourceZoneID, const FName InTargetZoneID, FString& OutReason)
{
	OutReason.Reset();

	const FZoneRow* const Source = Rows.Find(InSourceZoneID);
	if (Source == nullptr)
	{
		OutReason = FString::Printf(TEXT("포탈이 선 존 %s 가 표에 없다"), *InSourceZoneID.ToString());
		return nullptr;
	}

	const FZoneRow* const Target = Rows.Find(InTargetZoneID);
	if (Target == nullptr)
	{
		OutReason = FString::Printf(TEXT("갈 존 %s 가 표에 없다"), *InTargetZoneID.ToString());
		return nullptr;
	}

	// 표가 이어 두지 않은 존으로는 못 보낸다 — 포탈이 아무 데나 열리면 표를 세운 뜻이 없다
	// (ROADMAP M6-02 합격 기준 "포탈이 표에 있는 존으로만 보낸다").
	if (!Source->ConnectedZoneIDs.Contains(InTargetZoneID))
	{
		OutReason = FString::Printf(TEXT("%s 에서 %s 로 이어진 길이 표에 없다"),
			*InSourceZoneID.ToString(), *InTargetZoneID.ToString());
		return nullptr;
	}

	// 맵 이름이 빈 행으로 떠나면 "이동 중" 에서 멈춘다. 이름이 실제 맵인지까지는 표 검사가 본다
	// (`Snarehaven.Zone.Table.RowsPointSomewhereReal`).
	if (Target->LevelName.IsNone())
	{
		OutReason = FString::Printf(TEXT("갈 존 %s 의 맵 이름이 비었다"), *InTargetZoneID.ToString());
		return nullptr;
	}

	return Target;
}

bool AZonePortalActor::InitializeFromTable()
{
	TargetLevelName = NAME_None;
	TargetDisplayName = FText::GetEmpty();

	if (IsIslandFerry())
	{
		TargetDisplayName = FerryDisplayName;
		UE_LOG(LogSnarehaven, Log, TEXT("[Ferry] 섬 배 %s → %s (%s)"), *GetName(), *FerryTo->GetName(), *FerryDisplayName.ToString());
		return true;
	}

	if (!IsValid(ZoneTable) || SourceZoneID.IsNone() || TargetZoneID.IsNone())
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("%s: 존 표·존 ID 가 지정되지 않았다 — 갈 곳 없는 포탈로 둔다"), *GetName());
		return false;
	}

	FString Reason;
	const TMap<FName, FZoneRow> Rows = ReadZoneRows(ZoneTable);
	const FZoneRow* const Target = ResolveTargetZone(Rows, SourceZoneID, TargetZoneID, Reason);
	if (Target == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 포탈을 켜지 않는다 — %s"), *GetName(), *Reason);
		return false;
	}

	TargetLevelName = Target->LevelName;
	TargetDisplayName = Target->DisplayName;

	UE_LOG(LogSnarehaven, Log, TEXT("[Zone] 포탈 %s: %s → %s (맵 %s)"),
		*GetName(), *SourceZoneID.ToString(), *TargetZoneID.ToString(), *TargetLevelName.ToString());
	return true;
}

bool AZonePortalActor::TravelToTargetZone()
{
	if (!CanTravel())
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 갈 곳이 확정되지 않아 떠나지 않는다"), *GetName());
		return false;
	}

	// **클라이언트는 스스로 맵을 열지 않는다** — 여기서 `OpenLevel` 을 부르면 게스트 혼자 다른 맵에
	// 서서 방이 갈라진다 (`docs/WORLD_ZONES.md` 표 B). 게스트가 서버에 부탁하는 길은 M6-02b 몫.
	if (!HasAuthority())
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("%s: 클라이언트가 존을 직접 옮기지 않는다 — 서버가 옮긴다"), *GetName());
		return false;
	}

	UWorld* const World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	const FString TravelUrl = USnareOnlineSubsystem::MakeZoneServerTravelUrl(TargetLevelName);
	UE_LOG(LogSnarehaven, Log, TEXT("[Zone] 서버가 존을 옮긴다 — %s"), *TravelUrl);
	if (!World->ServerTravel(TravelUrl))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Zone] ServerTravel 이 거절됐다 — %s"), *TravelUrl);
		return false;
	}

	// M8-06: **떠나기가 받아들여진 뒤에만** 보낸다 — 거절된 길에서 보내면 일어나지 않은 이동이 세어진다.
	// 위의 `HasAuthority()` 문지기 덕에 여기까지는 호스트만 온다. 게스트도 보내면 한 번의 이동이
	// 방 인원 수만큼 세어진다 (DESIGN 4.1 권위 2단 · `docs/ANALYTICS.md` 표 A).
	UGameInstance* const GameInstance = UGameplayStatics::GetGameInstance(this);
	if (USnareBackendSubsystem* const Backend =
		IsValid(GameInstance) ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr)
	{
		// 존 ID 는 포탈이 이미 들고 있다 — 맵 이름(`TargetLevelName`)이 아니라 표의 ID 를 싣는다.
		TMap<FName, FString> Payload;
		Payload.Add(TEXT("from_zone_id"), SourceZoneID.ToString());
		Payload.Add(TEXT("to_zone_id"), TargetZoneID.ToString());
		Backend->WriteGameEvent(TEXT("zone_travelled"), Payload);
	}

	PlayCrossingCue(SnareSound::EZoneCrossing::Portal, true, GetActorLocation());
	return true;
}

bool AZonePortalActor::FerryTraveller(AActor* const Traveller)
{
	if (!HasAuthority() || !IsIslandFerry() || !IsValid(Traveller))
	{
		return false;
	}

	// 건너편 부두의 뭍 쪽 판자에 내린다 — 배 타는 칸(사거리 안)이 아니라서 내리자마자 되돌아가는 버튼이 안 뜬다.
	const FVector Inland = -FerryTo->GetActorForwardVector();
	const FVector Arrive = FerryTo->GetActorLocation() + Inland * FerryArriveBackCm
		+ FVector(0.f, 0.f, Traveller->GetSimpleCollisionHalfHeight());
	const bool bMoved = Traveller->TeleportTo(Arrive, Inland.Rotation());
	UE_LOG(LogSnarehaven, Log, TEXT("[Ferry] %s 섬 배 %s → %s 도착 %s (%s)"), *Traveller->GetName(), *GetName(),
		*FerryTo->GetName(), *Arrive.ToCompactString(), bMoved ? TEXT("옮김") : TEXT("막힘"));
	PlayCrossingCue(SnareSound::EZoneCrossing::Ferry, bMoved, Arrive);
	return bMoved;
}

void AZonePortalActor::PlayCrossingCue(SnareSound::EZoneCrossing Crossing, bool bConfirmed, const FVector& Where)
{
	const SnareSound::FChangeCue Cue = SnareSound::ZoneCue(Crossing, bConfirmed);
	if (Cue.Cue.IsNone())
	{
		return;
	}
	++CrossingCueCount;
	LastCrossingCue = Cue.Cue;
	UE_LOG(LogSnarehaven, Log, TEXT("[Sound] 건너기 %s — 크기 %.2f · %s"), *Cue.Cue.ToString(), Cue.Volume,
		Cue.bWorld ? TEXT("모든 기기") : TEXT("호스트 기기"));
	if (Cue.bWorld)
	{
		++CrossingMulticastCount;
		Multicast_PlayCrossingCue(Cue.Cue, Cue.Volume, Where);
		return;
	}
	USnareSoundSubsystem::Play2D(this, Cue.Cue, Cue.Volume);
}

void AZonePortalActor::Multicast_PlayCrossingCue_Implementation(FName Cue, float Volume, FVector Where)
{
	USnareSoundSubsystem::PlayAt(this, Cue, Where, Volume);
}

void AZonePortalActor::SetHighlighted(bool bHighlighted)
{
	if (!IsValid(Body))
	{
		return;
	}

	// 커스텀 뎁스 스텐실만 켠다 — 실제 외곽선은 포스트프로세스 몫이다
	// (AGatherableActor::SetHighlighted 와 같은 방식).
	Body->SetRenderCustomDepth(bHighlighted);
}
