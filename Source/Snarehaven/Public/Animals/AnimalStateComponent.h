#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AnimalStateComponent.generated.h"

struct FAnimalRow;

// 동물 한 마리의 상태. DESIGN 3.1 의 상태 머신 그림 그대로다
// (그림의 `Alert/Fight` 는 한 상태 — 우리는 공격받은 뒤의 전투 상태 하나만 쓴다).
UENUM(BlueprintType)
enum class EAnimalState : uint8
{
	/** 아무도 안 건드린 상태. 체력은 최대치. */
	Idle,
	/** 공격을 받아 체력이 깎이는 중. 임계값보다는 위. */
	Fight,
	/** 체력이 임계값 이하 — 이때만 포획 버튼이 활성화된다 (DESIGN 3.1). */
	Capturable,
	/** 체력 0. 이 순간 재료 드롭이 굴러간다 (M1-04). */
	Dead,
	/** 플레이어가 포획했다. 행의 `PetItemID` 가 펫 아이템 지급 요청이 된다 (M1-05). */
	Captured,
	/**
	 * 올가미 링을 놓쳐 잠깐 달아나는 중 (M21-09b, DESIGN 3.1 「올가미 링 실패 도주」). 포획 버튼이 꺼지고
	 * 반격하지 않는다. 시간이 차면 `Capturable` 로 돌아온다. 맨 끝에 붙인 것은 복제·저장된 앞 값들의 번호를 안 바꾸려고다.
	 */
	Escaping
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAnimalStateChangedSignature, EAnimalState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAnimalHealthChangedSignature, float, NewHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAnimalDeathDropsSignature, const TArray<FName>&, DroppedItemIDs);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAnimalCapturedSignature, FName, PetItemID);

// ROADMAP M1-02: 동물의 체력과 상태 전이를 한 곳에서 판정한다.
// 체력·임계값 같은 숫자는 여기 없다 — `DT_AnimalTable` 의 행을 받아 채운다 (DataDriven.md 3).
// 권위: 동물 HP·포획 판정과 **재료 드롭**까지가 호스트 몫이다 (CLAUDE.md 8 · DESIGN 4.1 권위 표).
// 그래서 죽을 때 드롭 목록을 굴리는 것도 여기서 한다 (M1-04). 다만 **그 재료를 인벤토리에 넣지는 않는다** —
// 인벤토리 원본은 백엔드라 지급은 요청만 쌓인다 (UInventoryComponent::QueuePendingGrant · DESIGN 4.5).
// 재화(스네어 코인)는 드롭 목록에 절대 들어가지 않는다 (DESIGN 3.1·4.8).
// 멀티(M3-02): **체력·상태는 호스트만 바꾸고, 게스트는 복제로 받아 본다.** 게스트에서 `ApplyDamage`·`TryCapture`
// 를 불러도 아무 일도 일어나지 않는다 (아래 `HasAnimalAuthority`). 게스트가 때리려면 자기 폰의
// `UMeleeAttackComponent`·`UCaptureComponent` 가 호스트에 요청을 보내야 한다 (DESIGN 4.1 권위 표).
// 복제하는 것은 `CurrentHP`·`CurrentState`·`bVariant`(개체마다 굴린 변종 여부, M21-07)·`BossHPMultiplier`(우두머리 배수, M21-07f) 넷뿐이다 — 나머지(최대 체력·임계값·이름·펫 ID)는 게스트도
// 같은 `DT_AnimalTable` 행을 읽어 채우므로 회선에 실을 이유가 없다 (DataDriven.md 1).
// 드롭 목록(`DeathDrops`)도 복제하지 않는다 — 재료 지급은 백엔드가 판정하고 호스트만 요청을 만든다 (CLAUDE.md 8).
UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API UAnimalStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAnimalStateComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * 이 동물의 판정을 여기서 해도 되는지 (호스트인지). 액터에 안 붙은 컴포넌트는 네트워크 역할이
	 * 없으므로 권위로 본다 — 2층 검사가 컴포넌트만 만들어 쓰는 길이다 (Verification.md 6).
	 */
	UFUNCTION(BlueprintPure, Category = "Animal")
	bool HasAnimalAuthority() const;

	/**
	 * 데이터 테이블 행으로 체력·임계값을 채우고 Idle 로 되돌린다.
	 * 행의 숫자가 미정(음수)이면 채우지 않고 경고만 남긴다 (Verification.md 7).
	 * @param Row DT_AnimalTable 의 한 줄
	 * @return 숫자가 성해서 실제로 초기화됐으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Animal")
	bool InitializeFromRow(const FAnimalRow& Row);

	/**
	 * 체력을 깎고 상태를 다시 판정한다. Dead·Captured 이후에는 아무 일도 하지 않는다.
	 * @param Amount 깎을 체력 (0 이하면 무시)
	 * @param PristineChanceMul 이 타로 죽으면 최상급 드롭 확률에 곱할 배율 — 때린 사람의 사냥 모자 ×1.3 (DESIGN 13.5). 없으면 1
	 * @return 실제로 체력이 깎였으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Animal")
	bool ApplyDamage(float Amount, float PristineChanceMul = 1.f);

	/** 막타가 넘긴 최상급 배율 (M65 시험이 읽는다). 막타 전에는 1. */
	float GetPendingPristineChanceMul() const { return PendingPristineChanceMul; }

	/**
	 * 드롭 목록에서 `Count` 개를 뽑는다 (DESIGN 3.1 「최상급 드롭 확률」, M22). 한 개마다 `PristineChance` 로
	 * 최상급(`item.pristine_`)을, 아니면 나머지에서 균등하게. 목록에 최상급이 없으면 전부 균등, 확률이 음수(미정)면
	 * 최상급을 뺀 나머지에서만 뽑는다. 난수를 밖에서 받는 것은 2층이 시드를 고정해 비율을 재려고다.
	 */
	static TArray<FName> RollDropItems(const TArray<FName>& Items, int32 Count, float PristineChance, FRandomStream& Stream);

	/** 최상급 드롭 ID 인가 — 접두어 `item.pristine_` (create_data_tables.py `ANIMAL_SPECIES` 이름 규칙). */
	static bool IsPristineItem(FName ItemID);

	/**
	 * 포획을 시도한다. DESIGN 3.1 대로 성공률은 확률이 아니라 상태다 —
	 * Capturable 일 때만 성공하고, 그 외 상태에서는 아무 일도 없다.
	 * @return 포획했으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Animal")
	bool TryCapture();

	/**
	 * 싸움을 풀고 Idle 로 돌린다 (M5-07d3 — 추격을 포기하고 제자리로 돌아간 동물). 체력은 그대로다
	 * (DESIGN 3.1 "추격 포기"). 호스트에서 Fight 일 때만 바뀐다.
	 * @return 상태를 바꿨으면 true
	 */
	bool ReturnToIdle();

	/**
	 * 올가미 링 실패 도주를 건다 (M21-09b) — 호스트에서 `Capturable` 일 때만. HP 는 그대로다.
	 * @param Seconds 도주 시간 (`DT_PlayerCombatTable` 의 `CaptureEscapeSeconds`). 0 이하면 아무 일도 없다
	 * @return Escaping 으로 바꿨으면 true
	 */
	bool BeginEscape(float Seconds);

	/**
	 * 도주 시간을 흘린다 (호스트 — 동물 틱이 부른다). 다 차면 `Capturable` 로 돌아온다.
	 * 2층 검사는 월드 시계 없이 이 함수로 시간을 민다.
	 * @return 이번 호출로 도주가 끝났으면 true
	 */
	bool AdvanceEscape(float DeltaSeconds);

	/** 남은 도주 시간(초). 도주 중이 아니면 0. 호스트만 안다 — 게스트는 상태만 복제로 본다. */
	float GetEscapeSecondsLeft() const { return EscapeSecondsLeft; }

	UFUNCTION(BlueprintPure, Category = "Animal")
	EAnimalState GetState() const { return CurrentState; }

	UFUNCTION(BlueprintPure, Category = "Animal")
	float GetCurrentHP() const { return CurrentHP; }

	/** 최대 체력 — 우두머리면 행 값 × 우두머리 배수 (M21-07f). HUD 체력 막대도 이 값을 본다. */
	UFUNCTION(BlueprintPure, Category = "Animal")
	float GetMaxHP() const { return BossHPMultiplier > 0.f ? MaxHP * BossHPMultiplier : MaxHP; }

	/** 포획 가능해지는 체력(절대값). 아직 초기화 전이면 음수 (미정 표시). */
	UFUNCTION(BlueprintPure, Category = "Animal")
	float GetCaptureThresholdHP() const;

	/**
	 * 죽을 때 굴린 드롭 목록. 같은 재료가 여러 번 나올 수 있어 ID 가 중복된다.
	 * 아직 안 죽었으면 빈 배열이고, 한 번 죽은 동물은 다시 굴리지 않는다.
	 */
	UFUNCTION(BlueprintPure, Category = "Animal")
	const TArray<FName>& GetDeathDrops() const { return DeathDrops; }

	/**
	 * 포획했을 때 되는 펫 아이템 ID. `DT_AnimalTable` 행에서 온다 (M1-05).
	 * 행에 안 적혀 있으면 NAME_None — 그러면 포획해도 펫 아이템 요청을 만들지 않는다.
	 */
	UFUNCTION(BlueprintPure, Category = "Animal")
	FName GetPetItemID() const { return bVariant ? VariantPetItemID : PetItemID; }

	/**
	 * 이 종의 고정 ID(`FAnimalRow::AnimalID`, `animal.fox`). 사냥 처치를 서버 `ClaimLoot {kills}` 로 알릴 때 쓴다
	 * (지시 감독 10-20 19:30 ① — 드롭은 서버가 이 ID 로 굴린다). 행에 없으면 NAME_None.
	 */
	UFUNCTION(BlueprintPure, Category = "Animal")
	FName GetAnimalID() const { return AnimalID; }

	/**
	 * 변종 굴림 한 번 (M21-07, DESIGN 10.6 「변종 확률」). 확률이 0 이하(미정 포함)면 늘 false.
	 * 난수를 밖에서 받는 것은 2층이 시드를 고정해 1만 번 비율을 재려고다 (`RollDropItems` 와 같은 꼴).
	 */
	static bool RollVariant(float Chance, FRandomStream& Stream);

	/**
	 * 이 개체를 변종으로 정한다 — 호스트만, 행에 변종 펫 ID 가 있을 때만. 복제로 게스트도 같은 값을 본다.
	 * @return 실제로 변종이 됐으면 true
	 */
	bool SetVariant(bool bNewVariant);

	/** 스폰 때 행의 확률로 굴려 변종 여부를 정한다 (호스트). 변종이 됐으면 true. */
	bool RollVariantOnSpawn(FRandomStream& Stream);

	/** 색 변종인가 (M21-07). 호스트가 스폰 때 정하고 복제된다. */
	UFUNCTION(BlueprintPure, Category = "Animal")
	bool IsVariant() const { return bVariant; }

	/**
	 * 이 개체를 주간 우두머리로 만든다 (M21-07f, DESIGN 10.6 「우두머리 HP」) — 호스트만, 초기화 뒤 처치 전에만.
	 * 최대 체력이 행 값 × `HPMultiplier` 가 되고 체력을 가득 채운다. **포획 불가** — 임계값이 0 이 되어
	 * Capturable 을 건너뛰고 Fight → Dead 로만 간다. 배수는 복제돼 게스트 체력 막대도 같은 최대치를 본다.
	 * @param HPMultiplier 위험구역 행의 `BossHPMultiplier` (0 이하면 거절)
	 * @return 실제로 우두머리가 됐으면 true
	 */
	bool MakeBoss(float HPMultiplier);

	/** 주간 우두머리인가 (M21-07f). 호스트가 스폰 때 정하고 복제된다. */
	UFUNCTION(BlueprintPure, Category = "Animal")
	bool IsBoss() const { return BossHPMultiplier > 0.f; }

	/**
	 * 이 동물에 실제로 데미지를 넣은 사람과 그 시각을 남긴다 (M21-06 「같이 잡기」 — DESIGN 3.4).
	 * 같은 사람은 한 줄 — 마지막 명중 시각만 고친다. 호스트에서만 의미가 있다(데미지가 호스트에서만 들어간다).
	 * @param Hitter 때린 사람의 몸
	 * @param WorldSeconds 그 순간의 월드 시각(`GetTimeSeconds`)
	 */
	void RecordHitter(AActor* Hitter, double WorldSeconds);

	/**
	 * 마지막 명중이 `WindowSeconds` 안인 사람들 — 막타를 친 사람이 드롭을 나눌 대상을 고른다.
	 * 사라진 몸(나간 사람)은 뺀다. 순서는 처음 때린 순.
	 */
	TArray<AActor*> GetHittersWithin(double NowSeconds, float WindowSeconds) const;

	/**
	 * 화면에 보이는 동물 이름. `DT_AnimalTable` 행의 `DisplayName` 을 그대로 들고 있다 (M1-08 HUD 가 읽는다).
	 * 초기화 전이면 빈 FText — HUD 는 그때 이름 칸을 비워 둔다.
	 */
	UFUNCTION(BlueprintPure, Category = "Animal")
	const FText& GetDisplayName() const { return DisplayName; }

	/**
	 * 이름 카드·포획 배너에 쓰는 이름 — 변종이면 `변종 ○○`, 아니면 `GetDisplayName` 그대로 (M21-07a2, DESIGN 10.6 「변종 겉모습」).
	 */
	UFUNCTION(BlueprintPure, Category = "Animal")
	FText GetCardName() const;

	/** 더 이상 때리거나 포획할 수 없는 상태인지 (Dead·Captured). */
	UFUNCTION(BlueprintPure, Category = "Animal")
	bool IsFinished() const { return CurrentState == EAnimalState::Dead || CurrentState == EAnimalState::Captured; }

	/** 상태가 바뀔 때. HUD 의 포획 버튼 활성화(M1-08)가 이걸 듣는다. */
	UPROPERTY(BlueprintAssignable, Category = "Animal")
	FOnAnimalStateChangedSignature OnStateChanged;

	/** 체력이 바뀔 때. 동물 HP 바(M1-08)가 이걸 듣는다. */
	UPROPERTY(BlueprintAssignable, Category = "Animal")
	FOnAnimalHealthChangedSignature OnHealthChanged;

	/** 죽어서 드롭이 정해졌을 때. 죽인 쪽이 이걸 듣고 백엔드 지급 요청을 쌓는다 (M1-04). */
	UPROPERTY(BlueprintAssignable, Category = "Animal")
	FOnAnimalDeathDropsSignature OnDeathDrops;

	/** 포획에 성공했을 때 한 번. 포획한 쪽이 이걸 듣고 펫 등록 요청을 쌓는다 (M1-05 · DESIGN 4.1). */
	UPROPERTY(BlueprintAssignable, Category = "Animal")
	FOnAnimalCapturedSignature OnCaptured;

	/**
	 * 복제 콜백 — 게스트에서 체력이 도착했을 때 엔진이 부른다. 직접 부르는 곳은 2층 검사뿐이다.
	 * 하는 일은 `OnHealthChanged` 를 그대로 울리는 것 하나 — 게스트의 HP 바가 호스트와 같이 움직인다.
	 */
	UFUNCTION()
	void OnRep_CurrentHP();

	/**
	 * 복제 콜백 — 게스트에서 상태가 도착했을 때. `OnStateChanged` 만 울린다.
	 * **`OnDeathDrops`·`OnCaptured` 는 울리지 않는다** — 드롭 목록과 펫 지급 요청은 호스트가 만들고
	 * 백엔드가 판정한다. 게스트가 빈 배열로 지급 요청을 쌓으면 그게 곧 재화 버그다 (CLAUDE.md 8 · DESIGN 4.8).
	 */
	UFUNCTION()
	void OnRep_CurrentState();

private:
	void SetState(EAnimalState NewState);

	/** 행의 드롭 목록·개수 범위로 이번 사체의 드롭을 정한다. 숫자가 미정이거나 목록이 비면 빈 결과. */
	void RollDeathDrops(float PristineChanceMul);

	// 상태·체력은 이 컴포넌트만 바꾼다. 밖에서 직접 쓰면 전이 규칙을 건너뛸 수 있어서
	// 읽기 함수만 열어 둔다 (engines/unreal.md 코드 규칙 1).
	UPROPERTY(ReplicatedUsing = OnRep_CurrentState, VisibleAnywhere, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	EAnimalState CurrentState = EAnimalState::Idle;

	/** 현재 체력. 초기화 전에는 음수 = 미정 (Verification.md 7). */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentHP, VisibleAnywhere, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	float CurrentHP = -1.f;

	/** 최대 체력. DT_AnimalTable 에서 온다 — 코드 기본값은 "아직 안 채움". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	float MaxHP = -1.f;

	/** 포획 임계값(최대 체력의 %). DT_AnimalTable 에서 온다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	float CaptureThresholdPct = -1.f;

	// 아래 셋도 전부 DT_AnimalTable 행에서 온다 (DESIGN 3.1 "재료 아이템 1~3개").
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	TArray<FName> DeathDropItems;

	/** 사망 드롭 개수 범위. 음수면 미정 취급이라 드롭이 켜지지 않는다 (Verification.md 7). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	int32 DeathDropCountMin = -1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	int32 DeathDropCountMax = -1;

	/** 드롭 한 개마다 최상급 확률 (DESIGN 3.1, M22). 음수면 미정 — 최상급을 굴리지 않는다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	float PristineDropChance = -1.f;

	/** 남은 도주 시간 (M21-09b). 복제하지 않는다 — 끝내는 것은 호스트이고 게스트는 상태만 본다. */
	float EscapeSecondsLeft = 0.f;

	/** 막타를 친 사람의 최상급 배율 — `ApplyDamage` 가 받아 `SetState(Dead)` → `RollDeathDrops` 로 넘긴다. */
	float PendingPristineChanceMul = 1.f;

	/** 화면 표시 이름. 코드에 박지 않고 행에서 받아 둔다 (DataDriven.md 1). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	FText DisplayName;

	/** 종 고정 ID. DT_AnimalTable 행에서 온다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	FName AnimalID;

	/** 포획하면 되는 펫 아이템 ID. DT_AnimalTable 행에서 온다 — 코드에서 만들지 않는다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	FName PetItemID;

	/** 변종이면 포획 때 이 펫이 간다 (M21-07). 행에서 온다 — 비면 변종이 서지 않는다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	FName VariantPetItemID;

	/** 스폰 때 변종이 될 확률. 행에서 온다 — 음수면 미정이라 굴리지 않는다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	float VariantChance = -1.f;

	/**
	 * 색 변종인가. 호스트가 굴려 정하고 게스트에 복제한다 — 표에서 읽을 수 없는 개체마다의 값이라
	 * 회선에 싣는다(포획 펫 ID 가 게스트 HUD·몸 색에도 필요하다, 색은 M21-07a2).
	 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	bool bVariant = false;

	/** 우두머리 체력 배수 (M21-07f). 0 = 우두머리 아님. 게스트도 최대 체력을 맞추려고 복제한다 — 표에 없는 개체 값이다. */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	float BossHPMultiplier = 0.f;

	/** 명중한 사람과 마지막 명중 시각 (M21-06). 복제하지 않는다 — 드롭을 나누는 것은 호스트뿐이다. */
	struct FRecentHitter
	{
		TWeakObjectPtr<AActor> Actor;
		double LastHitSeconds = 0.0;
	};
	TArray<FRecentHitter> RecentHitters;

	/** 이번 사체에서 실제로 나온 드롭. 죽는 순간 한 번만 정해진다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	TArray<FName> DeathDrops;
};
