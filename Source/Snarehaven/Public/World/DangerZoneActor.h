#pragma once

#include "CoreMinimal.h"
#include "Data/DangerZoneRow.h"
#include "GameFramework/Actor.h"
#include "DangerZoneActor.generated.h"

class AAnimalCharacter;
class UDataTable;
class UMaterialInterface;
class UStaticMesh;
struct FZoneRow;

// ROADMAP M6-04b: 위험구역(던전) 스포너. 맵에 하나 놓고 `DT_DangerZoneTable` 의 행 이름만 적으면
// **반지름·마리수·재생성 주기는 전부 그 행이 정한다** (DataDriven.md 1 — 구역이 늘어도 코드는 안 는다).
// 근거는 `docs/decisions/005`-3 · DESIGN 1.3 과 숫자를 확정한 DESIGN 8.1 표.
//
// **이 파일에 설계 숫자는 한 개도 없다.** 500cm·5마리·300초는 행에만 있고, 행이 미정(음수)이면
// 구역이 아예 안 켜진다 (Verification.md 7 — 미정 숫자로 게임을 돌리지 않는다).
//
// **스폰·재생성은 서버만 한다** (CLAUDE.md 8 · DESIGN 4.1 권위 2단 표 — 동물 HP·포획·재료 드롭까지가
// 호스트 몫이다). 게스트가 스스로 동물을 세우면 호스트에 없는 동물이 보이고, 때려도 아무 일이
// 일어나지 않는다. 그래서 `RefillZone()` 은 `HasAuthority()` 가 아니면 아무것도 하지 않는다 —
// 복제는 엔진이 한다 (`AAnimalCharacter` 는 M3-02 부터 복제된다).
UCLASS()
class SNAREHAVEN_API ADangerZoneActor : public AActor
{
	GENERATED_BODY()

public:
	ADangerZoneActor();

	/**
	 * `DangerZoneTable` 에서 `DangerZoneRowName` 행을 읽어 구역 수치를 확정한다.
	 * BeginPlay 가 부르고, 2층 검사는 표·행을 직접 넣고 부른다.
	 * @return 행을 찾았고 숫자가 전부 성해서 구역이 켜졌으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "DangerZone")
	bool InitializeFromTable();

	/**
	 * 볼 표와 행 이름을 바꾼다. 맵에 놓을 때 구역을 지정하거나 검사에서 바꿔 끼울 때 쓴다.
	 * @param Table   위험구역 표 (FDangerZoneRow 행 구조체)
	 * @param RowName 그 안의 행 이름 (예: `Danger_HuntField`)
	 */
	UFUNCTION(BlueprintCallable, Category = "DangerZone")
	void SetDangerZoneData(UDataTable* Table, FName RowName);

	/**
	 * 표를 거치지 않고 수치를 직접 먹인다 — **검사 전용 길**이다 (Verification.md 6).
	 * 2층이 이걸로 설계값과 다른 마리수를 넣어 "코드에 박힌 수가 아니라 행을 따르는지" 를 본다.
	 * @param Row 쓸 구역 한 줄
	 * @return 숫자가 성해서 구역이 켜졌으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "DangerZone")
	bool SetDangerZoneRow(const FDangerZoneRow& Row);

	/** 구역이 켜졌는지. 행이 없거나 숫자가 미정(음수)이면 false — 동물을 한 마리도 세우지 않는다. */
	UFUNCTION(BlueprintPure, Category = "DangerZone")
	bool IsZoneActive() const { return bZoneActive; }

	/** 볼 행 이름. */
	FName GetDangerZoneRowName() const { return DangerZoneRowName; }

	/** 확정된 구역 수치 (행 사본). 켜지기 전이면 기본값 = 전부 미정이다. */
	UFUNCTION(BlueprintPure, Category = "DangerZone")
	const FDangerZoneRow& GetZoneRow() const { return ZoneRow; }

	/**
	 * 지금 이 구역에 **서 있는**(아직 안 죽고 안 잡힌) 동물 수.
	 * 죽거나 포획된 동물은 액터가 남아 있어도(숨겨질 뿐 `Destroy` 하지 않는다 —
	 * `AAnimalCharacter::HandleStateChanged`) 세지 않는다.
	 */
	UFUNCTION(BlueprintPure, Category = "DangerZone")
	int32 GetLiveAnimalCount() const;

	/**
	 * 모자란 만큼 동물을 세운다. **서버에서만** 동작하고, 구역이 꺼져 있으면 아무것도 하지 않는다.
	 * BeginPlay 가 한 번 부르고 그 뒤로는 재생성 타이머가 부른다.
	 * @return 이번에 새로 세운 마리 수
	 */
	UFUNCTION(BlueprintCallable, Category = "DangerZone")
	int32 RefillZone();

	/**
	 * 이 구역이 우두머리를 세우는가 (M21-07f, DESIGN 10.6) — 행의 `BossHPMultiplier`·`BossBodyScale` 이 둘 다 0 보다 클 때.
	 * 맞으면 `RefillZone` 이 정원 밖으로 **한 마리 더**(구역 가운데, 몸 배율·체력 배수)를 세운다. 재생성도 같은 타이머다.
	 */
	bool HasBoss() const { return bZoneActive && ZoneRow.BossHPMultiplier > 0.f && ZoneRow.BossBodyScale > 0.f; }

	/** 지금 서 있는 우두머리. 없거나 쓰러졌으면 nullptr. */
	AAnimalCharacter* GetStandingBoss() const;

	/**
	 * 몇 번째 동물을 구역 어디에 세울지 — 구역 중심 기준 상대 위치.
	 * 반지름 안쪽으로 고르게 퍼지게 나선으로 돌린다. **거리·각도에 상수를 두지 않는다**:
	 * 거리는 `(Index+1)/Count × RadiusCm`, 각도는 한 바퀴를 `Count` 로 나눈 값이다.
	 * @param RadiusCm 구역 반지름 (행 값)
	 * @param Index    0 부터 `Count-1`
	 * @param Count    구역 안 동물 수 (행 값)
	 */
	static FVector MakeSpawnOffset(float RadiusCm, int32 Index, int32 Count);

	/**
	 * 선공종(`Aggressive`)은 제 서식 칸(`HabitatZoneID`)에서만 선다 — DESIGN 8.7 "선공종은 설원·숲만, 평원은 안전"
	 * (#0204). 다른 종은 어느 칸이든 true. 종 행을 못 찾으면 false — 모르는 종을 아무 데나 세우지 않는다.
	 */
	static bool CanStandInRegion(const UDataTable* AnimalTable, FName AnimalRowName, const FZoneRow& Region);

	/**
	 * 구역 중심을 행 `ZoneID` 의 칸으로 맞춘다 — 칸 행인데 자리가 칸 밖이면 칸 가운데(X·Y)로, Z 는 그대로.
	 * 맵 전체 행(`zone.hunt`)이거나 이미 칸 안이면 그대로다. 배치 스크립트가 어디 놓았든 표가 이긴다
	 * (`AAnimalSpreadActor::InitializeFromTable` 과 같은 규칙 · #0204).
	 */
	static FVector FitToZone(const FZoneRow& Zone, const FVector& Location);

	// ---- M12-07 랜덤 월드 이벤트 — 같은 구역 코드에 **수명만** 더한다 (DESIGN 8.1b) ----

	/**
	 * 행의 `LifetimeSeconds` 만큼 뒤에 `ExpireZone()` 을 부르는 타이머를 건다. **서버만.**
	 * BeginPlay 가 부르고, 2층 검사는 BeginPlay 없이 직접 부른다.
	 * @return 수명 있는 구역이라 타이머를 걸었으면 true (상시 구역·게스트는 false)
	 */
	bool StartLifetime();

	/**
	 * 수명이 몇 초 남았는지 — **서버 시계**로 잰다: 복제된 종료 시각 − `GetServerWorldTimeSeconds()`.
	 * 게스트는 스스로 세지 않고 이 값을 읽는다 (미니맵 남은 시간 줄). 수명 없는 구역이면 음수.
	 */
	float GetLifetimeRemaining() const;

	/** 지금 떠 있는 월드 이벤트 구역(수명 있는 켜진 구역) 하나. 없으면 nullptr. 미니맵이 틱마다 부른다. */
	static const ADangerZoneActor* FindWorldEvent(const UWorld* World);

	/** 수명 끝: 아직 서 있는 동물을 치우고 구역 액터를 없앤다. **서버만** — 사라짐은 복제로 간다. */
	void ExpireZone();

	/**
	 * 이 행을 `AppearIntervalSeconds` 마다 맵 어딘가에 세우는 반복 타이머를 월드에 건다. 서버만.
	 * @return 건 타이머 (행이 이벤트가 아니거나 게스트면 무효 핸들)
	 */
	static FTimerHandle ScheduleWorldEvent(UWorld* World, UDataTable* Table, FName RowName);

	/**
	 * 이 행의 이벤트 구역 하나를 지금 세운다. 자리는 이 맵의 성격 존 칸 중 무작위 한 곳 안, 바닥 위.
	 * @return 세운 구역 (자리를 못 찾았거나 게스트면 nullptr)
	 */
	static ADangerZoneActor* SpawnWorldEvent(UWorld* World, UDataTable* Table, FName RowName);

	/**
	 * GameMode BeginPlay 가 부르는 입구: 기본 표에서 이 맵(행 `ZoneID` 의 레벨)에 속한 이벤트 행마다
	 * `ScheduleWorldEvent`. `-SnareWorldEventNow` 면 기다리지 않고 한 번 바로 세운다(3층 캡처용).
	 * @return 건 타이머 수
	 */
	static int32 ScheduleWorldEvents(UWorld* World);

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 어느 구역인지는 맵에 놓인 구역마다 다르다 (engines/unreal.md 코드 규칙 1).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DangerZone")
	TObjectPtr<UDataTable> DangerZoneTable;

	/** 볼 행 이름. 기본값을 두지 않는다 — 구역이 늘면 맵에서 골라 적는다. */
	// 이벤트 구역은 서버가 세워 복제하므로 게스트도 이 이름으로 행을 읽어 알림을 띄운다 (M12-07).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "DangerZone")
	FName DangerZoneRowName;

private:
	/** 재생성 타이머가 부르는 자리. 하는 일은 `RefillZone()` 하나다. */
	void HandleRespawnTick();

	/** 한 마리를 세운다. 종은 행의 `AnimalRowName`, 동물 표는 `AAnimalCharacter` 기본값(CDO)에서 빌린다. */
	AAnimalCharacter* SpawnOneAnimal(int32 Index);

	/** 위 둘이 같이 쓰는 몸통 — 자리(배율 포함)에 행 종 한 마리를 세운다. */
	AAnimalCharacter* SpawnAnimalAt(const FTransform& SpawnTransform);

	/** 우두머리 한 마리를 구역 가운데에 세운다 (M21-07f). 서버만. */
	AAnimalCharacter* SpawnBoss();

	// 표에서 읽어 둔 수치. 세울 때마다 표를 다시 뒤지지 않으려고 들고 있는다 (AZonePortalActor 와 같은 방식).
	UPROPERTY()
	FDangerZoneRow ZoneRow;

	bool bZoneActive = false;

	// 이 구역이 세운 동물들. 죽거나 잡힌 것도 남는다 — 액터가 사라지지 않기 때문에
	// 빈자리 판정은 `GetLiveAnimalCount()` 가 상태로 한다.
	UPROPERTY()
	TArray<TObjectPtr<AAnimalCharacter>> SpawnedAnimals;

	// 우두머리는 정원(슬롯) 밖 한 자리다 — 쓰러지면 다음 채움 때 다시 선다(보상은 서버가 주 1회로 막는다).
	UPROPERTY()
	TObjectPtr<AAnimalCharacter> BossAnimal;

	FTimerHandle RespawnTimerHandle;
	FTimerHandle LifetimeTimerHandle;

	// 수명 끝 시각(서버 월드 시계, 초). 0 = 수명 없음. 게스트는 이 값으로 남은 시간을 잰다 (M12-07).
	UPROPERTY(Replicated)
	double ExpireServerSeconds = 0.0;

	/** 추락 지점 잔해 더미 + 반지름 링을 이 구역에 붙인다. 호스트·게스트 각자 BeginPlay 에서 (복제 안 함). */
	void BuildCrashSiteVisuals();

	// 추락 지점 표식에 쓰는 기존 에셋 (DESIGN 8.1b "추락 지점 표식") — 새 에셋을 만들지 않는다.
	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> DebrisMeshes;

	UPROPERTY()
	TObjectPtr<UStaticMesh> RingMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> RingMaterial;

	/** 이 기기의 로컬 플레이어 HUD 에 "월드 이벤트 등장" 줄을 띄운다 (호스트·게스트 공통). */
	void NotifyWorldEventAppeared() const;
};
