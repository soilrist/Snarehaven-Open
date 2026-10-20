#include "Player/MountComponent.h"
#include "SnareCheckSwitch.h"
#include "Snarehaven.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/AnimalRow.h"
#include "Animation/AnimSequence.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "SnarehavenCharacter.h"
#include "UObject/ConstructorHelpers.h"
#include "World/SnareSound.h"
#include "World/WaterRules.h"
#include "World/WaterTravelComponent.h"

namespace
{
	// 탈 종의 몸은 동물 표에서 `AnimalID` 로 찾는다 (DESIGN 8.3).
	const TCHAR* const AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");

	// 종이 표에 없거나 비었을 때(3층 `-SnareMountNow`) 빌리는 몸 — 늑대.
	const FName PlaceholderMountRow(TEXT("Animal_Wolf"));

	// `-SnareMountNow` 캡처가 얹는 안장 — 예전 모습(안장 있음) 그대로.
	const FName CaptureSaddle(TEXT("item.saddle_hunting"));

	// 안장 (`saddle_make.py` → `import_mount_saddle.py`). 원점 = 몸판 밑면, 앉는 자리 윗면 = 그 위 7cm(`SEAT_TOP`).
	const TCHAR* const SaddlePath = TEXT("/Game/Game/Characters/Equipment/Saddle/SM_Mount_Saddle.SM_Mount_Saddle");
	constexpr float SaddleSeatCm = 7.f;

	// 등뼈 → 등털 꼭대기 = 몸 키의 10% (늑대 원본 Blender 실측: 몸 가운데 1/3 에서 뼈 0.310 · 털 0.377 / 키 0.665).
	// ponytail: 늑대 한 종 값을 8종 모두에 쓴다. 종마다 안장이 뜨거나 묻히면 행마다 재거나 안장 소켓으로.
	constexpr float FurAboveBackBoneOfBodyHeight = 0.10f;
}

UMountComponent::UMountComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SaddleAsset(SaddlePath);
	SaddleMesh = SaddleAsset.Object;
}

void UMountComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMountComponent, bMounted);
	DOREPLIFETIME(UMountComponent, MountSpeciesId);
	DOREPLIFETIME(UMountComponent, MountSaddleId);
}

void UMountComponent::BeginPlay()
{
	Super::BeginPlay();
	WaterTable = LoadObject<UDataTable>(nullptr, SnareWater::TablePath);
	CachedRow = SnareWater::FindRow(WaterTable);
	AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	// `-SnareMountNow` — 3층 캡처 전용. 보유 판정을 건너뛰고 호스트가 태운다(백엔드는 안 건드린다, `-SnareCodexDemo` 와 같은 자리표시).
	if (GetOwner() && GetOwner()->HasAuthority() && SnareCheckSwitch::Param(TEXT("SnareMountNow")))
	{
		MountSaddleId = CaptureSaddle;
		SetMountedOnAuthority(true, /*bQuiet=*/true);
	}
#if !UE_BUILD_SHIPPING
	// `-SnareMountToggleAt=3/6` — 3층 소리 확인 전용 (M47-06). 적은 초마다 버튼과 같은 `RequestMounted` 로 타고↔내린다 —
	// `-SnareMountNow` 와 달리 플레이어가 한 바꿈이라 `[Sound] 탈것` 이 난다. 보유 판정은 `-SnareMountNow` 처럼 건너뛴다.
	FString ToggleList;
	UWorld* const World = GetWorld();
	if (World && GetOwner() && GetOwner()->HasAuthority() && SnareCheckSwitch::Value(TEXT("SnareMountToggleAt="), ToggleList))
	{
		TArray<FString> Seconds;
		ToggleList.ParseIntoArray(Seconds, TEXT("/")); // 쉼표는 `FParse::Value` 가 값 끝으로 읽는다
		for (const FString& Second : Seconds)
		{
			FTimerHandle Unused;
			World->GetTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 탈것 바꿈 (-SnareMountToggleAt) — %s"), bMounted ? TEXT("내림") : TEXT("탐"));
				RequestMounted(!bMounted, NAME_None, CaptureSaddle);
			}), FCString::Atof(*Second), false);
		}
	}
#endif
}

void UMountComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// 타고 물에 들어가면 호스트가 내린다 — 탈것으로 바다를 건너지 않는다(8.9 수영·배가 이긴다).
	if (bMounted && GetOwner() && GetOwner()->HasAuthority() && IsOnWater())
	{
		SetMountedOnAuthority(false);
	}
	if (bMounted && MountBody)
	{
		UpdateMountBodyAnim();
	}
}

void UMountComponent::UpdateMountBodyAnim()
{
	const ASnarehavenCharacter* const Character = Cast<ASnarehavenCharacter>(GetOwner());
	if (!Character || !MountRow || !CachedRow)
	{
		return;
	}
	// 대기/걷기: 사람 몸과 같은 두 문턱(들어감 30 · 나옴 10cm/s) — 두 몸이 서로 다른 순간에 멈춰 보이지 않게.
	const float Speed = Character->GetVelocity().Size2D();
	if (Speed >= Character->GetMoveAnimEnterSpeed())
	{
		bMountMoving = true;
	}
	else if (Speed <= Character->GetMoveAnimExitSpeed())
	{
		bMountMoving = false;
	}
	// 클립 공간 속도(표 실측) × 탈것 몸 배율 = 그 클립이 화면에서 밟는 속도 (DESIGN 8.3 "탈것 몸 클립").
	const float BodyScale = static_cast<float>(MountBody->GetRelativeScale3D().X);
	const float WalkClipSpeed = CachedRow->MountWalkClipSpeedCmPerSecond * BodyScale;
	const float RunClipSpeed = CachedRow->MountRunClipSpeedCmPerSecond * BodyScale;
	// 걷기↔달리기는 두 클립 속도의 기하평균에서 가른다 — 어느 속도에서든 재생 속도가 1 에 더 가까운 클립을 튼다.
	// (최고 속도 × 0.9 로 가르면 탈것 속도 780 에서 걷기 클립이 최대 7배로 돌았다.)
	const bool bRun = bMountMoving && Speed >= FMath::Sqrt(WalkClipSpeed * RunClipSpeed);
	// 비어 있는 칸은 아래 단계로 떨어진다: 달리기 → 걷기 → 대기.
	UAnimSequence* const Idle = MountRow->IdleAnimRef.LoadSynchronous();
	UAnimSequence* const Walk = bMountMoving ? MountRow->WalkAnimRef.LoadSynchronous() : nullptr;
	UAnimSequence* const Run = bRun ? MountRow->FleeAnimRef.LoadSynchronous() : nullptr;
	UAnimSequence* const Want = Run ? Run : (Walk ? Walk : Idle);
	if (Want && Want != MountAnim)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Mount] 탈것 클립 전환 %s -> %s (속도 %.0f · 걷기 클립 %.0f · 달리기 클립 %.0f cm/s)"),
			*GetNameSafe(MountAnim), *Want->GetName(), Speed, WalkClipSpeed, RunClipSpeed);
		MountAnim = Want;
		MountBody->PlayAnimation(Want, true);
	}
	// 발이 안 미끄러지게 재생 속도 = 실제 속도 ÷ 클립이 밟는 속도 (사람 몸 `UpdateBodyAnimation` 과 같은 규칙). 대기는 1배.
	const float ClipSpeed = Want == Run ? RunClipSpeed : (Want == Walk ? WalkClipSpeed : 0.f);
	MountBody->SetPlayRate(ClipSpeed > KINDA_SMALL_NUMBER ? Speed / ClipSpeed : 1.f);
}

float UMountComponent::GetMoveSpeedMultiplier() const
{
	return bMounted && CachedRow ? CachedRow->MountSpeedMul : 1.f;
}

bool UMountComponent::IsOnWater() const
{
	const UWaterTravelComponent* const Water = GetOwner() ? GetOwner()->FindComponentByClass<UWaterTravelComponent>() : nullptr;
	return Water && (Water->IsInWater() || Water->GetRiddenBoat() != nullptr);
}

FString UMountComponent::GetMountBlockCode(const FSnarePlayerSnapshot& Snapshot) const
{
	if (CachedRow == nullptr)
	{
		return TEXT("mount_off");
	}
	if (IsOnWater())
	{
		return TEXT("mount_water");
	}
	if (!Snapshot.HasOwner())
	{
		return TEXT("mount_no_codex");
	}
	// 결정 009: 종 하나라도 가졌으면 탄다. 종은 서버 `ExchangeCollar` 만 더한다(클라가 세지 않는다).
	return Snapshot.Mounts.Num() > 0 ? FString() : TEXT("mount_locked");
}

FName UMountComponent::GetRideSpecies(const FSnarePlayerSnapshot& Snapshot) const
{
	if (Snapshot.Mounts.Contains(ChosenSpecies))
	{
		return ChosenSpecies;
	}
	return Snapshot.Mounts.Num() > 0 ? Snapshot.Mounts[0] : NAME_None;
}

const FAnimalRow* UMountComponent::FindSpeciesRow(FName SpeciesId) const
{
	if (AnimalTable && !SpeciesId.IsNone())
	{
		for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
		{
			const FAnimalRow* const Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
			if (Row->AnimalID == SpeciesId)
			{
				return Row;
			}
		}
	}
	return nullptr;
}

FText UMountComponent::GetSpeciesName(FName SpeciesId) const
{
	const FAnimalRow* const Row = FindSpeciesRow(SpeciesId);
	return Row ? Row->DisplayName : FText::FromName(SpeciesId);
}

void UMountComponent::CycleMountSpecies(const FSnarePlayerSnapshot& Snapshot)
{
	if (Snapshot.Mounts.Num() == 0)
	{
		return;
	}
	const int32 Index = Snapshot.Mounts.IndexOfByKey(GetRideSpecies(Snapshot));
	ChosenSpecies = Snapshot.Mounts[(Index + 1) % Snapshot.Mounts.Num()];
	UE_LOG(LogSnarehaven, Log, TEXT("[Mount] 탈 종 고름: %s"), *ChosenSpecies.ToString());
}

void UMountComponent::ToggleMount()
{
	if (bMounted)
	{
		RequestMounted(false);
		return;
	}
	const USnareBackendSubsystem* const Backend = SnareWater::FindBackend(this);
	ToggleMountWith(Backend ? Backend->GetPlayerSnapshot() : FSnarePlayerSnapshot());
}

void UMountComponent::ToggleMountWith(const FSnarePlayerSnapshot& Snapshot)
{
	if (bMounted)
	{
		RequestMounted(false);
		return;
	}
	LastMessage = GetMountBlockCode(Snapshot);
	if (LastMessage.IsEmpty())
	{
		const FName Species = GetRideSpecies(Snapshot);
		RequestMounted(true, Species, Snapshot.MountSaddles.FindRef(Species));
	}
	else
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Mount] %s 탈것 거절: %s"), *GetNameSafe(GetOwner()), *LastMessage);
	}
}

void UMountComponent::RequestMounted(bool bNewMounted, FName SpeciesId, FName SaddleId)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (bNewMounted)
		{
			MountSpeciesId = SpeciesId;
			MountSaddleId = SaddleId;
		}
		SetMountedOnAuthority(bNewMounted);
	}
	else
	{
		Server_SetMounted(bNewMounted, SpeciesId, SaddleId);
	}
}

void UMountComponent::Server_SetMounted_Implementation(bool bNewMounted, FName SpeciesId, FName SaddleId)
{
	// ponytail: 호스트는 손님의 "이 종을 가졌다·이 안장을 달았다" 를 믿는다(손님 스냅샷은 손님 기계에만 있다 — 배 띄우기와 같은 규칙).
	// 모습만 바뀌고(효과 없음) 재화·인벤토리는 안 움직인다. 위조가 문제되면 호스트가 손님 PlayFabId 로 서버 `GetMounts` 를 되묻는다.
	if (bNewMounted && IsOnWater())
	{
		return;
	}
	RequestMounted(bNewMounted, SpeciesId, SaddleId);
}

void UMountComponent::SetMountedOnAuthority(bool bNewMounted, bool bQuiet)
{
	if (bMounted == bNewMounted)
	{
		return;
	}
	bMounted = bNewMounted;
	UE_LOG(LogSnarehaven, Log, TEXT("[Mount] %s %s (속도 배율 %.2f)"),
		*GetNameSafe(GetOwner()), bMounted ? TEXT("탐") : TEXT("내림"), GetMoveSpeedMultiplier());
	// 물에 들어가 호스트가 내린 것도 실제로 내린 것이라 소리를 낸다(무음 길 = 거절·같은 상태·캡처 태움뿐).
	if (!bQuiet)
	{
		RequestMountCue();
	}
	OnRep_Mounted();
}

void UMountComponent::RequestMountCue()
{
	const SnareSound::FChangeCue Cue = SnareSound::MountCue(bMounted);
	// 같은 사람이 표의 간격 안에 또 타고 내리면 첫 한 번만 — 버튼을 연타해도 삐걱이 겹쳐 쌓이지 않는다.
	const UWorld* const World = GetWorld();
	const double Now = World != nullptr ? World->GetTimeSeconds() : 0.0;
	if (Now - LastMountCueSeconds < Cue.MinGapSeconds)
	{
		return;
	}
	LastMountCueSeconds = Now;
	++MountCueCount;
	LastMountCue = Cue.Cue;
	UE_LOG(LogSnarehaven, Log, TEXT("[Sound] 탈것 %s — %s · 크기 %.2f · 모든 기기"), *Cue.Cue.ToString(),
		*GetNameSafe(GetOwner()), Cue.Volume);
	Multicast_PlayMountCue(Cue.Cue, Cue.Volume);
}

void UMountComponent::Multicast_PlayMountCue_Implementation(FName Cue, float Volume)
{
	if (const AActor* const Owner = GetOwner())
	{
		USnareSoundSubsystem::PlayAt(this, Cue, Owner->GetActorLocation(), Volume);
	}
}

void UMountComponent::OnRep_Mounted()
{
	if (bMounted)
	{
		LastMessage.Reset();
	}
	ApplyMountBody();
}

void UMountComponent::ApplyMountBody()
{
	ACharacter* const Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* const Rider = Character ? Character->GetMesh() : nullptr;
	const UCapsuleComponent* const Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!Rider || !Capsule)
	{
		return;
	}
	// 탄 사람 발 자리 = 캡슐 바닥 (`ASnarehavenCharacter::ApplyPlayerBody` 와 같은 규칙). 타 있으면 여기에 늑대 키를 더한다.
	const float FootZ = -Capsule->GetUnscaledCapsuleHalfHeight();

	const FAnimalRow* Row = FindSpeciesRow(MountSpeciesId);
	Row = Row || !AnimalTable ? Row : AnimalTable->FindRow<FAnimalRow>(PlaceholderMountRow, TEXT("Mount"));
	USkeletalMesh* const Mesh = bMounted && Row && CachedRow ? Row->MeshRef.LoadSynchronous() : nullptr;
	if (!Mesh)
	{
		if (MountBody)
		{
			MountBody->DestroyComponent();
			MountBody = nullptr;
		}
		if (Saddle)
		{
			Saddle->DestroyComponent();
			Saddle = nullptr;
		}
		MountAnim = nullptr;
		MountRow = nullptr;
		bMountMoving = false;
		Rider->SetRelativeLocation(FVector(0.f, 0.f, FootZ));
		return;
	}
	if (!MountBody)
	{
		MountBody = NewObject<USkeletalMeshComponent>(GetOwner(), TEXT("MountBody"));
		MountBody->SetupAttachment(Character->GetCapsuleComponent());
		MountBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MountBody->SetSkeletalMesh(Mesh);
		MountBody->RegisterComponent();
		MountAnim = Row->IdleAnimRef.LoadSynchronous();
		if (MountAnim)
		{
			MountBody->PlayAnimation(MountAnim, true);
		}
	}
	MountRow = Row;
	// 🟡 안장 셋은 아직 모습이 같다(같은 `SM_Mount_Saddle`) — 색 바꾸기는 남은 일. 안장을 안 단 종은 맨등.
	if (Saddle && MountSaddleId.IsNone())
	{
		Saddle->DestroyComponent();
		Saddle = nullptr;
	}
	if (!Saddle && SaddleMesh && !MountSaddleId.IsNone())
	{
		Saddle = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("MountSaddle"));
		Saddle->SetupAttachment(Character->GetCapsuleComponent());
		Saddle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Saddle->SetStaticMesh(SaddleMesh);
		Saddle->RegisterComponent();
	}

	// 크기: 탈것 몸 길이 = 탄 사람 몸 너비 × 표 배율 (DESIGN 8.3 "탄 모습 크기"). 둘 다 메시 bounds 에서 잰다.
	// 사람 너비는 발자국의 **좁은 쪽** — 메시 bounds 는 팔 벌린 T 자세라 넓은 쪽은 팔 길이다(첫 시도 168cm → 늑대 5m).
	// 탄 사람 몸이 아직 없으면(자리표시 실린더) 캡슐 지름으로 잰다.
	const USkeletalMesh* const RiderMesh = Rider->GetSkeletalMeshAsset();
	const FVector RiderExtent = RiderMesh ? RiderMesh->GetBounds().BoxExtent * Rider->GetRelativeScale3D().X : FVector::ZeroVector;
	const float RiderWidth = RiderMesh ? 2.f * FMath::Min(RiderExtent.X, RiderExtent.Y) : 2.f * Capsule->GetUnscaledCapsuleRadius();
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const float MountLength = 2.f * FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y);
	const float Scale = MountLength > KINDA_SMALL_NUMBER ? RiderWidth * CachedRow->MountLengthPerRiderWidth / MountLength : 1.f;

	// 방향은 `AAnimalCharacter::ApplyBodyFromRow` 와 같은 -90 보정. 몸 가운데를 캡슐 축에, 발을 캡슐 바닥에 댄다.
	const FRotator MeshYaw(0.f, -90.f, 0.f);
	const FVector Center = MeshYaw.RotateVector(FVector(Bounds.Origin.X, Bounds.Origin.Y, 0.f)) * Scale;
	MountBody->SetRelativeScale3D(FVector(Scale));
	MountBody->SetRelativeRotation(MeshYaw);
	MountBody->SetRelativeLocation(FVector(-Center.X, -Center.Y, FootZ - (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale));

	// 앉는 자리는 몸 가운데에서 꼬리 쪽으로 몸 길이 × 표 값만큼 물린다 (DESIGN 8.3 "앉는 자리"). 60° 카메라에서 앉은 사람
	// 머리는 앞으로 투영돼 늑대 머리·어깨를 덮는다 — 뒤로 물리면 앞 1/3 이 드러난다(3층 캡처 `m606b_mount_ride`).
	// 메시 -90 보정 뒤 앞(+X)은 메시 +Y = 긴 축이라, 메시 공간에서 뒤로 = 긴 축 − 쪽이다.
	const float SeatBackMesh = CachedRow->MountSeatBackOfLength * MountLength;
	const float SeatBack = SeatBackMesh * Scale;

	// 등 높이 = 앉는 자리 앞뒤로 몸 길이 1/3 폭에 든 뼈 중 가장 높은 것(참조 자세). bounds 꼭대기는 머리·귀라 너무 높다 —
	// 카메라가 720cm 로 가까워 사람을 높이 올릴수록 화면에서 커져 늑대를 덮는다(3층 두 번째 캡처).
	// 뼈는 몸속이라 털 두께(몸 키의 10%)를 더한 곳이 등 윗면 = 안장 밑면. 탄 사람은 안장 앉는 자리(+7cm)에 앉는다.
	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
	const TArray<FTransform>& LocalPose = Skeleton.GetRefBonePose();
	TArray<FTransform> ComponentPose;
	ComponentPose.SetNum(LocalPose.Num());
	const int32 LongAxis = Bounds.BoxExtent.X > Bounds.BoxExtent.Y ? 0 : 1;
	const float MiddleHalf = Bounds.BoxExtent[LongAxis] / 3.f;
	float BackZ = Bounds.Origin.Z + Bounds.BoxExtent.Z;
	bool bFoundBack = false;
	for (int32 Bone = 0; Bone < LocalPose.Num(); ++Bone)
	{
		const int32 Parent = Skeleton.GetParentIndex(Bone);
		ComponentPose[Bone] = Parent == INDEX_NONE ? LocalPose[Bone] : LocalPose[Bone] * ComponentPose[Parent];
		const FVector At = ComponentPose[Bone].GetLocation();
		if (FMath::Abs(At[LongAxis] - (Bounds.Origin[LongAxis] - SeatBackMesh)) <= MiddleHalf)
		{
			BackZ = bFoundBack ? FMath::Max(BackZ, static_cast<float>(At.Z)) : static_cast<float>(At.Z);
			bFoundBack = true;
		}
	}
	const float BodyHeight = 2.f * Bounds.BoxExtent.Z * Scale;
	const float BackHeight = (BackZ - (Bounds.Origin.Z - Bounds.BoxExtent.Z)) * Scale + FurAboveBackBoneOfBodyHeight * BodyHeight;
	if (Saddle)
	{
		Saddle->SetRelativeLocation(FVector(-SeatBack, 0.f, FootZ + BackHeight));
	}
	const float SeatHeight = BackHeight + (Saddle ? SaddleSeatCm : 0.f);
	Rider->SetRelativeLocation(FVector(-SeatBack, 0.f, FootZ + SeatHeight));
	UE_LOG(LogSnarehaven, Log, TEXT("[Mount] 탄 모습: %s(%s) 배율 %.2f · 길이 %.0fcm (사람 너비 %.0fcm) · 등 %.0fcm (몸 키 %.0fcm) · 안장 %s · 앉는 높이 %.0fcm · 뒤로 %.0fcm"),
		*MountSpeciesId.ToString(), *Row->AnimalID.ToString(), Scale, MountLength * Scale, RiderWidth, BackHeight, BodyHeight, Saddle ? *MountSaddleId.ToString() : TEXT("없음"), SeatHeight, SeatBack);
}
