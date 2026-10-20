#include "Gathering/GatherComponent.h"
#include "Snarehaven.h"
#include "Combat/MeleeAttackComponent.h"
#include "Data/ItemRow.h"
#include "Data/ItemRowLookup.h"
#include "Equipment/EquipmentComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gathering/GatherableActor.h"
#include "TimerManager.h"
#include "SnarehavenCharacter.h"
#include "World/SnareSound.h"

UGatherComponent::UGatherComponent()
{
	// 대상 찾기는 타이머가 돌린다 — 매 프레임 액터를 전수 조사하지 않는다
	// (engines/unreal.md 코드 규칙 2).
	PrimaryComponentTick.bCanEverTick = false;

	// 게스트가 복제 나무를 호스트에게 베어 달라고 부탁하는 RPC 가 이 컴포넌트에 있다 (DESIGN 8.10).
	SetIsReplicatedByDefault(true);
}

void UGatherComponent::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 하이라이트 갱신 주기도 새로 만들지 않고 근접 공격 간격을 빌린다
	// (DESIGN 3.3 "하이라이트 대상 갱신 주기는 공격 간격과 같은 값" — 감독 확정).
	const UMeleeAttackComponent* Melee = FindMeleeAttack();
	const float ScanIntervalSeconds = IsValid(Melee) ? Melee->GetAttackIntervalSeconds() : -1.f;
	if (ScanIntervalSeconds <= 0.f)
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("채집 대상 갱신 주기를 빌릴 곳이 없다 — 버튼을 누를 때만 대상을 찾는다"));
		return;
	}

	World->GetTimerManager().SetTimer(
		TargetScanTimerHandle, this, &UGatherComponent::HandleTargetScan, ScanIntervalSeconds, true);
}

void UGatherComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 타이머를 남겨 두면 파괴된 컴포넌트를 다시 부른다 (engines/unreal.md 코드 규칙 7).
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TargetScanTimerHandle);
	}

	// 하이라이트를 켠 채로 사라지면 채집물이 계속 빛난다.
	SetTarget(nullptr);

	Super::EndPlay(EndPlayReason);
}

void UGatherComponent::HandleTargetScan()
{
	// 숲 나무(폴리지 인스턴스)는 호스트가 사람 곁에만 채집물을 세운다 — 수천 그루에 액터를 다 세우지 않는다.
	// 호스트는 게스트 캐릭터 사본의 이 타이머도 돌리므로 모든 사람 곁이 덮인다.
	const AActor* const Owner = GetOwner();
	if (IsValid(Owner) && Owner->HasAuthority())
	{
		AGatherableActor::SpawnAtInstancedTreesNear(GetWorld(), Owner->GetActorLocation(), GetGatherRangeCm());
	}
	RefreshTarget();
}

UMeleeAttackComponent* UGatherComponent::FindMeleeAttack() const
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return nullptr;
	}

	return Owner->FindComponentByClass<UMeleeAttackComponent>();
}

float UGatherComponent::GetGatherRangeCm() const
{
	const UMeleeAttackComponent* Melee = FindMeleeAttack();
	if (!IsValid(Melee))
	{
		// 사거리를 여기서 지어내지 않는다 — 자동 상호작용 거리는 DESIGN 3.1 확정값 하나뿐이고,
		// 그걸 들고 있는 컴포넌트가 없으면 미정으로 둔다 (DataDriven.md 3 · Verification.md 7).
		UE_LOG(LogSnarehaven, Warning, TEXT("근접 공격 컴포넌트가 없어 채집 사거리를 알 수 없다"));
		return -1.f;
	}

	return Melee->GetAttackRangeCm();
}

AGatherableActor* UGatherComponent::RefreshTarget()
{
	AGatherableActor* NewTarget = FindNearestGatherableInRange();
	SetTarget(NewTarget);
	return NewTarget;
}

bool UGatherComponent::CanGather() const
{
	return IsValid(CurrentTarget) && CurrentTarget->CanGather();
}

bool UGatherComponent::TryGatherNearest()
{
	AGatherableActor* Target = RefreshTarget();
	if (!IsValid(Target))
	{
		return false;
	}

	// 소리는 누른 사람 자리에서 바로 — 게스트도 호스트 응답을 기다리지 않는다 (DESIGN 2.2 소리).
	if (Target->CanGatherWithTool(GetEquippedToolItemID()))
	{
		// 도구를 쓴 채집(필요한 도구·덤 도구)은 든 도구 행의 소리(`UseSoundCue`), 맨손으로 줍는 더미는 줍는 소리다
		// (M16-01 — 이름 분기 없음, M28-09a — 마체테 덤불도 도구질).
		const FItemRow* const ToolRow = FindEquippedToolRow();
		const bool bUsesTool = !GetActingToolItemID(Target->GetRequiredToolItemID(), Target->GetBonusToolItemID(),
			GetEquippedToolItemID()).IsNone();
		const bool bToolSound = bUsesTool && ToolRow != nullptr && !ToolRow->UseSoundCue.IsNone();
		const FName Cue = bToolSound ? ToolRow->UseSoundCue : FName(TEXT("Pickup"));
		const float Volume = bToolSound ? 0.8f : 0.6f;
		// 폰이면 남의 기기에도 그 사람 자리에서 들리게 중계한다 (M16-08).
		if (ASnarehavenCharacter* const Body = Cast<ASnarehavenCharacter>(GetOwner()))
		{
			Body->PlayActionSound(Cue, Target->GetActorLocation(), Volume);
		}
		else
		{
			USnareSoundSubsystem::PlayAt(this, Cue, Target->GetActorLocation(), Volume);
		}
		// 도구로 친 사람 본인 기기만 손이 톡 떤다 (M63-03) — 맨손 줍기는 타격이 아니라 안 떤다.
		// 남의 기기·끔·간격 안은 `PlayStrikeHaptic` 이 거른다.
		if (bToolSound)
		{
			if (ASnarehavenCharacter* const Striker = Cast<ASnarehavenCharacter>(GetOwner()))
			{
				Striker->PlayStrikeHaptic(SnareBodyHaptic::EStrike::Gather);
			}
		}
	}

	// 복제 나무는 호스트 것이다 — 게스트는 직접 베지 않고 부탁만 한다. 숨기·굴림·지급 요청은 호스트가 하고
	// `bGathered` 가 복제돼 내려온다 (지급 판정은 여전히 백엔드 — CLAUDE.md 8).
	const AActor* const Owner = GetOwner();
	if (Target->GetIsReplicated() && IsValid(Owner) && !Owner->HasAuthority())
	{
		Server_GatherTarget(Target);
		return true;
	}

	return GatherTarget(Target);
}

void UGatherComponent::Server_GatherTarget_Implementation(AGatherableActor* Target)
{
	const AActor* const Owner = GetOwner();
	const float RangeCm = GetGatherRangeCm();
	if (!IsValid(Target) || !IsValid(Owner) || RangeCm < 0.f)
	{
		return;
	}

	// 게스트 말을 그대로 믿지 않는다 — 거리·도구를 호스트 자리에서 다시 본다 (대상 찾기와 같은 수평 거리).
	if (FVector::DistSquared2D(Owner->GetActorLocation(), Target->GetActorLocation()) > FMath::Square(RangeCm))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("게스트의 베기 부탁을 거절했다 — 사거리 밖"));
		// 여기서 조용히 돌아서면 게스트 화면엔 아무 일도 안 난 것처럼 보인다 (감독 조작 조사 #9 · M15-15).
		Client_GatherRejected(EGatherRejectReason::TooFar, Target->GetRequiredToolItemID());
		return;
	}

	if (!GatherTarget(Target))
	{
		// 왜 못 벴는지는 호스트 자리에서만 안다 — 아직 남아 있는데 도구가 안 맞나, 누가 먼저 가져갔나.
		// (벴으면 `Target` 이 그 자리에서 사라질 수 있어 실패한 길에서만 다시 본다.)
		const bool bNeedTool = IsValid(Target) && Target->CanGather()
			&& !Target->CanGatherWithTool(GetEquippedToolItemID());
		const FName Required = IsValid(Target) ? Target->GetRequiredToolItemID() : NAME_None;
		UE_LOG(LogSnarehaven, Warning, TEXT("게스트의 베기 부탁을 거절했다 — %s"),
			bNeedTool ? TEXT("도구가 안 맞는다") : TEXT("이미 캔 뒤다"));
		Client_GatherRejected(
			bNeedTool ? EGatherRejectReason::NeedTool : EGatherRejectReason::AlreadyTaken, Required);
	}
}

void UGatherComponent::Client_GatherRejected_Implementation(EGatherRejectReason Reason, FName RequiredToolItemID)
{
	const FText Message = GetRejectMessage(Reason, RequiredToolItemID);
	LastRejectMessage = Message;
	UE_LOG(LogSnarehaven, Log, TEXT("호스트가 채집을 물렸다 — %s"), *Message.ToString());
	OnGatherRejected.Broadcast(Message);
}

FText UGatherComponent::GetRejectMessage(EGatherRejectReason Reason, FName RequiredToolItemID)
{
	switch (Reason)
	{
	case EGatherRejectReason::TooFar:
		return NSLOCTEXT("GatherComponent", "GatherTooFar", "조금 더 가까이 가야 합니다");
	case EGatherRejectReason::AlreadyTaken:
		return NSLOCTEXT("GatherComponent", "GatherAlreadyTaken", "먼저 누른 사람이 가져갔습니다");
	default:
		break;
	}

	return IsMiningTool(RequiredToolItemID)
		? NSLOCTEXT("GatherComponent", "NeedPickaxe", "곡괭이가 있어야 캘 수 있다")
		: NSLOCTEXT("GatherComponent", "NeedHatchet", "손도끼가 있어야 벨 수 있다");
}

FText UGatherComponent::GetNeedToolHint(FName RequiredToolItemID, bool bOwnsFittingTool)
{
	// 파는 곳은 둘 다 잡화점이다 (DESIGN 8.14 사는 목록 손도끼 5 · 8.16 기본 곡괭이 10).
	if (IsMiningTool(RequiredToolItemID))
	{
		return bOwnsFittingTool
			? NSLOCTEXT("GatherComponent", "HintHoldPickaxe", "곡괭이를 들어요 — 도구 버튼")
			: NSLOCTEXT("GatherComponent", "HintBuyPickaxe", "곡괭이가 필요 — 잡화점");
	}
	return bOwnsFittingTool
		? NSLOCTEXT("GatherComponent", "HintHoldHatchet", "손도끼를 들어요 — 도구 버튼")
		: NSLOCTEXT("GatherComponent", "HintBuyHatchet", "손도끼가 필요 — 잡화점");
}

bool UGatherComponent::IsMiningTool(FName RequiredToolItemID)
{
	// 캐는 도구인지는 그 도구 행의 `UseAction`(Mine)이 말한다 — 곡괭이가 한 줄씩 늘 때마다 문구를 늘리지 않으려고.
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
	const FItemRow* const RequiredRow = SnareItemRows::FindByItemID(ItemTable, RequiredToolItemID);
	return RequiredRow != nullptr && RequiredRow->UseAction == EItemAction::Mine;
}

FName UGatherComponent::GetActingToolItemID(FName RequiredToolItemID, FName BonusToolItemID, FName HeldToolItemID)
{
	if (!RequiredToolItemID.IsNone())
	{
		return RequiredToolItemID;
	}
	return !BonusToolItemID.IsNone() && BonusToolItemID == HeldToolItemID ? HeldToolItemID : NAME_None;
}

bool UGatherComponent::GatherTarget(AGatherableActor* Target)
{
	const FName ToolItemID = GetEquippedToolItemID();
	if (!IsValid(Target) || !Target->CanGatherWithTool(ToolItemID))
	{
		return false;
	}

	// 여러 번 쳐야 쓰러지는 나무(DESIGN 8.10) — 마지막 번 전까지는 흔들리기만 하고 지급 요청이 없다.
	// 친 것 자체는 성공이라 true — 휘두르는 동작·소리는 매 번 난다.
	const AActor* const Hitter = GetOwner();
	const bool bBonusTool = !ToolItemID.IsNone() && Target->GetBonusToolItemID() == ToolItemID;
	if (!Target->TakeHit(IsValid(Hitter) ? Hitter->GetActorLocation() : Target->GetActorLocation(), bBonusTool))
	{
		return true;
	}

	// 채집하면 액터가 그 자리에서 사라지므로, 무엇을 몇 개 주기로 되어 있었는지는 **먼저** 읽어 둔다.
	const FName GatherableID = Target->GetGatherableID();
	const FName ItemID = Target->GetGrantItemID();
	const int32 Count = Target->GetGrantCount();
	const FName ExtraItemID = Target->GetExtraItemID();
	const int32 ExtraCount = Target->GetExtraCount();
	const int32 ToolUses = bBonusTool ? 1 : 0;
	LastRequiredToolItemID = GetActingToolItemID(Target->GetRequiredToolItemID(), Target->GetBonusToolItemID(), ToolItemID);

	if (!Target->Gather())
	{
		return false;
	}

	// 사라진 오브젝트는 더 이상 대상이 아니다 — 하이라이트를 바로 다음 것으로 넘긴다.
	RefreshTarget();

	// **여기서 인벤토리를 늘리지 않는다.** 보유 원본은 백엔드다 (DESIGN 4.5 · CLAUDE.md 8).
	OnGathered.Broadcast(GatherableID, ItemID, Count, ToolUses);
	// 덤은 따로 한 줄 — 도구 번은 본 지급에만 센다(전기톱 +1 이 수지까지 두 번 붙지 않게).
	if (!ExtraItemID.IsNone() && ExtraCount > 0)
	{
		OnGathered.Broadcast(GatherableID, ExtraItemID, ExtraCount, 0);
	}
	return true;
}

const FItemRow* UGatherComponent::FindEquippedToolRow() const
{
	const AActor* const Owner = GetOwner();
	const UEquipmentComponent* const Equipment =
		IsValid(Owner) ? Owner->FindComponentByClass<UEquipmentComponent>() : nullptr;
	return IsValid(Equipment) ? Equipment->GetEquippedRow(EEquipSlot::Tool) : nullptr;
}

FName UGatherComponent::GetEquippedToolItemID() const
{
	const AActor* const Owner = GetOwner();
	const UEquipmentComponent* const Equipment =
		IsValid(Owner) ? Owner->FindComponentByClass<UEquipmentComponent>() : nullptr;
	const FItemRow* const ToolRow = IsValid(Equipment) ? Equipment->GetEquippedRow(EEquipSlot::Tool) : nullptr;
	return ToolRow != nullptr ? ToolRow->ItemID : NAME_None;
}

AGatherableActor* UGatherComponent::FindNearestNeedingTool() const
{
	return FindNearestGatherableInRange(false);
}

AGatherableActor* UGatherComponent::FindNearestGatherableInRange(bool bRequireTool) const
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	const float RangeCm = GetGatherRangeCm();
	if (!IsValid(Owner) || World == nullptr || RangeCm < 0.f)
	{
		return nullptr;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	const float RangeSquared = RangeCm * RangeCm;

	// 채집물은 집으면 사라져서 BeginPlay 캐싱이 통하지 않는다. 대신 매 프레임이 아니라
	// 갱신 주기 타이머에서만 부른다 (UMeleeAttackComponent::FindNearestAnimalInRange 와 같은 취지).
	// 손도끼 없이 나무에 불이 들어오면 눌러도 아무 일이 없는 버튼이 된다 — 못 베는 건 하이라이트도 안 한다.
	const FName ToolItemID = GetEquippedToolItemID();
	AGatherableActor* Nearest = nullptr;
	float NearestDistanceSquared = 0.f;
	for (TActorIterator<AGatherableActor> It(World); It; ++It)
	{
		AGatherableActor* Gatherable = *It;
		// 이미 집었거나 줄 것이 미정인 오브젝트는 하이라이트도 하지 않는다 (Verification.md 7).
		// 도구를 따지는지는 부른 쪽이 정한다 — 하이라이트는 못 캐는 것을 비추지 않지만(위 주석),
		// **어떤 도구를 줘야 하나**를 묻는 HUD 는 아직 못 캐는 나무도 봐야 한다 (M15-08).
		if (!IsValid(Gatherable) || (bRequireTool ? !Gatherable->CanGatherWithTool(ToolItemID) : !Gatherable->CanGather()))
		{
			continue;
		}

		// 수평 거리만 잰다 — 캐릭터 위치는 캡슐 중심(땅 위 약 90cm)이고 채집물·나무는 뿌리가 땅이라, 높이까지 재면
		// 사거리 150cm 중 90cm 가 높이에 먹혀 줄기에 막힌 나무엔 영영 안 닿았다 (폰 채집 0회, 루프#410).
		const float DistanceSquared = FVector::DistSquared2D(OwnerLocation, Gatherable->GetActorLocation());
		if (DistanceSquared > RangeSquared)
		{
			continue;
		}

		if (Nearest == nullptr || DistanceSquared < NearestDistanceSquared)
		{
			Nearest = Gatherable;
			NearestDistanceSquared = DistanceSquared;
		}
	}

	return Nearest;
}

void UGatherComponent::SetTarget(AGatherableActor* NewTarget)
{
	if (CurrentTarget == NewTarget)
	{
		return;
	}

	if (IsValid(CurrentTarget))
	{
		CurrentTarget->SetHighlighted(false);
	}

	CurrentTarget = NewTarget;

	if (IsValid(CurrentTarget))
	{
		CurrentTarget->SetHighlighted(true);
	}

	OnGatherTargetChanged.Broadcast(CurrentTarget);
}
