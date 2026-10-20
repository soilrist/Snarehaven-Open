#include "Animals/AnimalStateComponent.h"
#include "Snarehaven.h"
#include "Data/AnimalRow.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

UAnimalStateComponent::UAnimalStateComponent()
{
	// 상태는 공격·포획 입력이 올 때만 바뀐다 — 매 프레임 볼 것이 없다.
	PrimaryComponentTick.bCanEverTick = false;

	// M3-02: 게스트가 같은 동물의 체력·상태를 보려면 컴포넌트가 복제 대상이어야 한다.
	SetIsReplicatedByDefault(true);
}

void UAnimalStateComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 회선에 싣는 것은 호스트만 아는 두 값뿐 — 나머지는 게스트도 데이터 테이블에서 같은 값을 읽는다.
	DOREPLIFETIME(UAnimalStateComponent, CurrentHP);
	DOREPLIFETIME(UAnimalStateComponent, CurrentState);
	DOREPLIFETIME(UAnimalStateComponent, bVariant);
	DOREPLIFETIME(UAnimalStateComponent, BossHPMultiplier);
}

bool UAnimalStateComponent::HasAnimalAuthority() const
{
	const AActor* Owner = GetOwner();
	// 액터에 안 붙은 컴포넌트는 네트워크 역할 자체가 없다 (2층 검사가 만드는 것) — 권위로 본다.
	return Owner == nullptr || Owner->HasAuthority();
}

void UAnimalStateComponent::OnRep_CurrentHP()
{
	OnHealthChanged.Broadcast(CurrentHP);
}

void UAnimalStateComponent::OnRep_CurrentState()
{
	OnStateChanged.Broadcast(CurrentState);
}

bool UAnimalStateComponent::InitializeFromRow(const FAnimalRow& Row)
{
	// 미정 숫자(-1)로 상태 머신을 돌리면 "체력 0 이하 = 즉사" 로 조용히 오작동한다.
	// 그래서 채우지 않고 경고만 남긴다 (Verification.md 7).
	if (Row.MaxHP <= 0.f || Row.CaptureThresholdPct < 0.f)
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("동물 %s 의 숫자가 아직 미정이다 (MaxHP=%.1f, CaptureThresholdPct=%.1f) — 상태 머신을 켜지 않는다"),
			*Row.AnimalID.ToString(), Row.MaxHP, Row.CaptureThresholdPct);
		return false;
	}

	DisplayName = Row.DisplayName;
	MaxHP = Row.MaxHP;
	CaptureThresholdPct = Row.CaptureThresholdPct;
	DeathDropItems = Row.DeathDropItems;
	DeathDropCountMin = Row.DeathDropCountMin;
	DeathDropCountMax = Row.DeathDropCountMax;
	PristineDropChance = Row.PristineDropChance;
	AnimalID = Row.AnimalID;
	PetItemID = Row.PetItemID;
	VariantPetItemID = Row.VariantPetItemID;
	VariantChance = Row.VariantChance;
	// 변종은 스폰 때 따로 굴린다(`RollVariantOnSpawn`) — 다시 초기화하면 일반으로 돌아간다.
	bVariant = false;
	// 우두머리도 스폰 때 따로 정한다(`MakeBoss`). 게스트는 지우지 않는다 — BeginPlay 전에 복제로 온 값이다.
	if (HasAnimalAuthority())
	{
		BossHPMultiplier = 0.f;
	}
	// 다시 초기화하면 지난 사체의 드롭은 지운다 (스폰 재활용 대비).
	DeathDrops.Reset();
	RecentHitters.Reset();
	CurrentHP = GetMaxHP();   // 게스트가 우두머리를 늦게 받아도 막대가 가득으로 시작한다(호스트는 배수 0 이라 행 값)
	// 델리게이트를 듣는 쪽(HUD)이 초기 체력을 알 수 있게 SetState 전에 알린다.
	OnHealthChanged.Broadcast(CurrentHP);
	SetState(EAnimalState::Idle);
	return true;
}

void UAnimalStateComponent::RecordHitter(AActor* Hitter, double WorldSeconds)
{
	if (!IsValid(Hitter))
	{
		return;
	}

	for (FRecentHitter& Entry : RecentHitters)
	{
		if (Entry.Actor.Get() == Hitter)
		{
			Entry.LastHitSeconds = WorldSeconds;
			return;
		}
	}

	FRecentHitter& Added = RecentHitters.AddDefaulted_GetRef();
	Added.Actor = Hitter;
	Added.LastHitSeconds = WorldSeconds;
}

TArray<AActor*> UAnimalStateComponent::GetHittersWithin(double NowSeconds, float WindowSeconds) const
{
	TArray<AActor*> Result;
	for (const FRecentHitter& Entry : RecentHitters)
	{
		AActor* const Actor = Entry.Actor.Get();
		if (IsValid(Actor) && NowSeconds - Entry.LastHitSeconds <= WindowSeconds)
		{
			Result.Add(Actor);
		}
	}
	return Result;
}

float UAnimalStateComponent::GetCaptureThresholdHP() const
{
	if (MaxHP <= 0.f || CaptureThresholdPct < 0.f)
	{
		return -1.f;
	}

	// 우두머리는 포획 불가 (DESIGN 10.6) — 임계 0 이면 `ApplyDamage` 가 Dead 를 먼저 보고 Capturable 로 안 간다.
	if (IsBoss())
	{
		return 0.f;
	}

	return MaxHP * CaptureThresholdPct / 100.f;
}

bool UAnimalStateComponent::ApplyDamage(float Amount, float PristineChanceMul)
{
	// 체력 판정은 호스트 몫이다 (DESIGN 4.1 권위 표). 게스트가 자기 화면의 동물을 깎으면
	// 곧 복제로 덮여 되살아나 보인다 — 조용히 틀리느니 아예 하지 않는다.
	if (!HasAnimalAuthority())
	{
		return false;
	}

	// 죽었거나 포획된 동물은 더 깎이지 않는다 (DESIGN 3.1 — Dead·Captured 는 끝 상태).
	if (IsFinished())
	{
		return false;
	}

	if (Amount <= 0.f)
	{
		return false;
	}

	if (MaxHP <= 0.f)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("초기화 안 된 동물에 데미지가 들어왔다 — 무시한다"));
		return false;
	}

	CurrentHP = FMath::Max(CurrentHP - Amount, 0.f);
	OnHealthChanged.Broadcast(CurrentHP);

	if (CurrentHP <= 0.f)
	{
		PendingPristineChanceMul = PristineChanceMul;
		SetState(EAnimalState::Dead);
	}
	else if (CurrentState == EAnimalState::Escaping)
	{
		// 도주 중에 맞아도 도주는 그대로다 — 남은 시간이 차야 Capturable 로 선다 (명세 snare-ring 2).
	}
	else if (CurrentHP <= GetCaptureThresholdHP())
	{
		// 임계 이하에서 계속 때려도 Capturable 을 유지한다 — 플레이어가 포획/사냥을 고르는 구간이다.
		SetState(EAnimalState::Capturable);
	}
	else
	{
		SetState(EAnimalState::Fight);
	}

	return true;
}

bool UAnimalStateComponent::TryCapture()
{
	// 포획 판정도 호스트 몫이다 (DESIGN 4.1) — 게스트는 `UCaptureComponent` 가 요청만 보낸다.
	if (!HasAnimalAuthority())
	{
		return false;
	}

	// 확률 롤이 없는 것은 의도다 (DESIGN 3.1 "포획 성공률 100%") — 조건은 상태 하나뿐(우두머리는 그 상태에 못 간다).
	if (CurrentState != EAnimalState::Capturable || IsBoss())
	{
		return false;
	}

	SetState(EAnimalState::Captured);
	return true;
}

bool UAnimalStateComponent::ReturnToIdle()
{
	if (!HasAnimalAuthority() || CurrentState != EAnimalState::Fight)
	{
		return false;
	}

	SetState(EAnimalState::Idle);
	return true;
}

bool UAnimalStateComponent::BeginEscape(float Seconds)
{
	// 도주를 거는 것도 포획 판정의 일부라 호스트 몫이다 (DESIGN 4.1).
	if (!HasAnimalAuthority() || CurrentState != EAnimalState::Capturable || Seconds <= 0.f)
	{
		return false;
	}

	EscapeSecondsLeft = Seconds;
	SetState(EAnimalState::Escaping);
	return true;
}

bool UAnimalStateComponent::AdvanceEscape(float DeltaSeconds)
{
	if (!HasAnimalAuthority() || CurrentState != EAnimalState::Escaping)
	{
		return false;
	}

	EscapeSecondsLeft = FMath::Max(EscapeSecondsLeft - DeltaSeconds, 0.f);
	if (EscapeSecondsLeft > 0.f)
	{
		return false;
	}

	// HP 는 도주 전 그대로(또는 도주 중 맞아 더 낮다)라 늘 임계 이하 — 다시 대기한다 (DESIGN 3.1).
	SetState(EAnimalState::Capturable);
	return true;
}

void UAnimalStateComponent::SetState(EAnimalState NewState)
{
	if (CurrentState == NewState)
	{
		return;
	}

	CurrentState = NewState;
	if (CurrentState != EAnimalState::Escaping)
	{
		EscapeSecondsLeft = 0.f;
	}

	// 상태 변경을 알리기 **전에** 드롭을 정한다 — OnStateChanged 를 듣고 바로 GetDeathDrops 를
	// 보는 쪽(죽인 플레이어·HUD)이 빈 배열을 보지 않게.
	if (CurrentState == EAnimalState::Dead)
	{
		RollDeathDrops(PendingPristineChanceMul);
	}

	OnStateChanged.Broadcast(CurrentState);

	if (CurrentState == EAnimalState::Dead)
	{
		OnDeathDrops.Broadcast(DeathDrops);
	}

	// 포획은 재료 드롭과 달리 굴릴 것이 없다 — 행에 적힌 펫 아이템 하나가 그대로 간다 (DESIGN 3.1·3.2).
	// 변종이면 변종 펫이다(M21-07, DESIGN 10.6) — 굴림은 스폰 때 이미 끝났다.
	// 행에 안 적혀 있으면(NAME_None) 알리지 않는다 — 듣는 쪽이 빈 ID 로 지급 요청을 만들지 않게.
	if (CurrentState == EAnimalState::Captured)
	{
		const FName CapturedPetID = GetPetItemID();
		if (CapturedPetID.IsNone())
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("%s: 펫 아이템 ID 가 행에 없어 포획 지급 요청을 만들지 않는다"), *GetName());
		}
		else
		{
			OnCaptured.Broadcast(CapturedPetID);
		}
	}
}

bool UAnimalStateComponent::RollVariant(float Chance, FRandomStream& Stream)
{
	return Chance > 0.f && Stream.FRand() < Chance;
}

bool UAnimalStateComponent::SetVariant(bool bNewVariant)
{
	// 변종 여부는 호스트가 정한다 — 게스트가 바꾸면 포획 펫 ID 가 호스트와 갈린다 (DESIGN 4.1).
	if (!HasAnimalAuthority() || (bNewVariant && VariantPetItemID.IsNone()))
	{
		return false;
	}

	bVariant = bNewVariant;
	return true;
}

bool UAnimalStateComponent::RollVariantOnSpawn(FRandomStream& Stream)
{
	if (!HasAnimalAuthority() || VariantPetItemID.IsNone())
	{
		return false;
	}

	if (VariantChance < 0.f)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 변종 확률이 미정이다 — 굴리지 않는다"), *GetName());
		return false;
	}

	// DESIGN 10.6 「변종 확률」 — 동물이 설 때마다 한 번, 종 상관없이 같은 값.
	return RollVariant(VariantChance, Stream) && SetVariant(true);
}

bool UAnimalStateComponent::MakeBoss(float HPMultiplier)
{
	if (!HasAnimalAuthority() || HPMultiplier <= 0.f || MaxHP <= 0.f || IsFinished())
	{
		return false;
	}

	BossHPMultiplier = HPMultiplier;
	// 우두머리는 펫이 아니다 — 스폰 굴림으로 변종이 됐어도 금빛 겹·`변종` 이름을 걷는다.
	bVariant = false;
	CurrentHP = GetMaxHP();
	OnHealthChanged.Broadcast(CurrentHP);
	return true;
}

FText UAnimalStateComponent::GetCardName() const
{
	if (DisplayName.IsEmpty())
	{
		return DisplayName;
	}
	// DESIGN 10.6 「우두머리 자리·종」 — 이름 카드 `우두머리`.
	if (IsBoss())
	{
		return FText::Format(LOCTEXT("AnimalBossCardName", "우두머리 {0}"), DisplayName);
	}
	if (!bVariant)
	{
		return DisplayName;
	}
	return FText::Format(LOCTEXT("AnimalVariantCardName", "변종 {0}"), DisplayName);
}

bool UAnimalStateComponent::IsPristineItem(FName ItemID)
{
	return ItemID.ToString().StartsWith(TEXT("item.pristine_"));
}

TArray<FName> UAnimalStateComponent::RollDropItems(const TArray<FName>& Items, int32 Count, float PristineChance, FRandomStream& Stream)
{
	TArray<FName> Pristine;
	TArray<FName> Plain;
	for (const FName& Item : Items)
	{
		(IsPristineItem(Item) ? Pristine : Plain).Add(Item);
	}

	TArray<FName> Result;
	Result.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		// 최상급이 없는 목록(늑대)은 예전처럼 전부 균등 — 확률 칸을 안 쓴다.
		if (Pristine.Num() == 0)
		{
			Result.Add(Plain[Stream.RandRange(0, Plain.Num() - 1)]);
			continue;
		}
		// 확률이 미정(음수)이면 최상급을 굴리지 않는다 — 판매가 20 짜리를 추측으로 뿌리면 경제가 무너진다(M22).
		const bool bPristine = Plain.Num() == 0 || (PristineChance > 0.f && Stream.FRand() < PristineChance);
		const TArray<FName>& Pool = bPristine ? Pristine : Plain;
		Result.Add(Pool[Stream.RandRange(0, Pool.Num() - 1)]);
	}
	return Result;
}

void UAnimalStateComponent::RollDeathDrops(float PristineChanceMul)
{
	DeathDrops.Reset();

	if (DeathDropItems.Num() == 0)
	{
		// 드롭 목록이 빈 동물도 있을 수 있다 (아직 안 채운 행) — 조용히 넘어가되 흔적은 남긴다.
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 드롭 목록이 비어 있어 사망 드롭이 없다"), *GetName());
		return;
	}

	// 개수 범위가 미정(음수)이거나 뒤집혀 있으면 추측해서 굴리지 않는다 (DataDriven.md 3 · Verification.md 7).
	if (DeathDropCountMin < 0 || DeathDropCountMax < DeathDropCountMin)
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("%s: 사망 드롭 개수 범위가 미정이다 (%d~%d) — 드롭을 굴리지 않는다"),
			*GetName(), DeathDropCountMin, DeathDropCountMax);
		return;
	}

	if (PristineDropChance < 0.f && DeathDropItems.ContainsByPredicate(&UAnimalStateComponent::IsPristineItem))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 최상급 드롭 확률이 미정이다 — 최상급을 빼고 굴린다"), *GetName());
	}

	// DESIGN 3.1 "재료 아이템 1~3개" — 개수는 행의 범위에서, 종류는 「최상급 드롭 확률」(M22)대로 고른다.
	// 예전엔 목록 전체에서 균등이라 판매가 20 짜리 최상급이 드롭의 1/3 이었다(경제 붕괴, 감독 M22).
	FRandomStream Stream(FMath::Rand());
	const int32 DropCount = Stream.RandRange(DeathDropCountMin, DeathDropCountMax);
	DeathDrops = RollDropItems(DeathDropItems, DropCount, PristineDropChance * PristineChanceMul, Stream);
}

#undef LOCTEXT_NAMESPACE
