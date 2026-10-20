#pragma once

#include "Animals/AnimalStateComponent.h"
#include "CoreMinimal.h"
#include "Data/AnimalRow.h"
#include "GameFramework/Character.h"
#include "AnimalCharacter.generated.h"

class UAnimSequence;
class UDataTable;
class ASnarehavenCharacter;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;

// ROADMAP M1-02: 사냥·포획 대상 동물 액터. 종류별 차이는 코드가 아니라
// `DT_AnimalTable` 의 행 하나로 정해진다 (DataDriven.md 1 — 동물이 늘어도 코드는 안 는다).
// 보이는 몸은 행의 `MeshRef` 가 채워져 있으면 그 스켈레탈 메시, 비어 있으면 엔진 기본 실린더다
// (M1-07 에서 돼지가 들어왔다 — 종이 늘어도 코드가 아니라 행만 는다).
UCLASS()
class SNAREHAVEN_API AAnimalCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AAnimalCharacter();

	/**
	 * `AnimalTable` 의 `AnimalRowName` 행을 읽어 상태 컴포넌트를 채운다.
	 * BeginPlay 가 부르고, 2층 검사는 테이블·행을 직접 넣고 부른다.
	 * @return 행을 찾아 초기화까지 성공했으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Animal")
	bool InitializeFromTable();

	/**
	 * 볼 데이터를 바꾼다. 스폰 직후(M1-03)나 검사에서 종류를 지정할 때 쓴다.
	 * @param Table   동물 데이터 테이블 (FAnimalRow 행 구조체)
	 * @param RowName 그 안의 행 이름 (예: `Animal_Pig`)
	 */
	UFUNCTION(BlueprintCallable, Category = "Animal")
	void SetAnimalData(UDataTable* Table, FName RowName);

	/** 체력·상태 전이를 판정하는 컴포넌트. */
	UFUNCTION(BlueprintPure, Category = "Animal")
	UAnimalStateComponent* GetAnimalState() const { return AnimalState; }

	/**
	 * 이 동물이 보는 `DT_AnimalTable`. 동물을 **세우는 쪽**(M6-04b `ADangerZoneActor`)이 기본값(CDO)에서
	 * 빌려 가려고 열어 둔다 — 표 경로가 이 파일 생성자 한 곳에만 남는다 (DataDriven.md 1).
	 */
	UFUNCTION(BlueprintPure, Category = "Animal")
	UDataTable* GetAnimalTable() const { return AnimalTable; }

	/** 이 개체가 보는 행 이름. 세우는 쪽·치우는 쪽이 "무슨 종인가" 를 물을 때 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Animal")
	FName GetAnimalRowName() const { return AnimalRowName; }

	/**
	 * **검사 스위치 `-SnareStageAnimals` 가 붙었는가.** 붙으면 자리표시 종(`FAnimalRow::bPlaceholderSpecies`
	 * = 돼지·소·양)도 기본 플레이처럼 선다 — 그 셋을 쓰는 검사가 아직 여럿이라 켤 길을 남긴다
	 * (사용자 지시 2026-09-23 16:55 ②). **스위치 없는 일반 플레이에는 한 마리도 서지 않는다.**
	 * 종을 세우는 쪽 둘(`AAnimalSpreadActor` · `ASnarehavenGameMode`)이 같은 답을 쓰게 여기 한 곳에 둔다.
	 */
	static bool ShouldStandPlaceholderSpecies();

	/**
	 * **검사 스위치 `-SnareFreezeAnimals` 가 붙었는가.** 붙으면 동물이 걷지도 도망치지도 않고 선 자리에 선다
	 * (맞기 전에 달아나 스토어 스크린샷에 못 담던 장면용, 오프라인 검사 전용). 스위치 없는 일반 플레이는 그대로다.
	 */
	static bool ShouldFreezeAnimals();

	/**
	 * **검사 스위치 `-SnareHuntHerd` 가 붙었는가.** 붙으면 폰(`UWaterTravelComponent`)이 동물 3~4마리를 선 자리 앞쪽 호로
	 * 옮겨 한 화면에 모은다 (스토어 사냥·포획 스크린샷용, `-SnareFreezeAnimals` 와 함께, 오프라인 검사 전용).
	 * 동물을 옮길 뿐 재화·배낭·목장 칸은 건드리지 않는다. 스위치 없는 일반 플레이는 그대로다.
	 */
	static bool ShouldHuntHerd();

	/**
	 * `-SnareHuntHerd` 가 `Index` 번째 동물을 놓을 자리 (높이는 `Origin` 그대로). 0번은 때릴 수 있게 사거리 `ReachCm` 안쪽
	 * 옆자리, 나머지는 앞쪽 360~430cm 호 — 플레이어 몸에 가리지 않는다.
	 * @param Origin  선 폰 위치
	 * @param Forward 폰이 보는 쪽 (수평이 아니어도 수평으로 쓴다)
	 * @param Index   0 부터
	 * @param ReachCm 폰 공격 사거리, 0 이하면 200 으로 본다
	 */
	static FVector HuntHerdSpot(const FVector& Origin, const FVector& Forward, int32 Index, float ReachCm);

	/**
	 * 그 행이 자리표시 종인가. 표가 없거나 행이 없으면 false — 모르는 행을 치우지는 않는다.
	 * @param Table   볼 표 (`DT_AnimalTable`)
	 * @param RowName 행 이름
	 */
	static bool IsPlaceholderSpeciesRow(const UDataTable* Table, FName RowName);

	/** 자리표시 몸통(보이는 실린더). 3층 캡처에 무엇이 찍히는지 2층에서 확인하려고 꺼내 본다. */
	UFUNCTION(BlueprintPure, Category = "Placeholder")
	UStaticMeshComponent* GetPlaceholderBody() const { return PlaceholderBody; }

	/**
	 * 행에 적힌 진짜 메시·Idle 애니를 몸에 올린다. 올렸으면 자리표시 실린더를 숨긴다.
	 * `InitializeFromTable` 이 부르고, 2층 검사는 행을 직접 만들어 부른다.
	 * @return 스켈레탈 메시를 실제로 올렸으면 true (행이 비어 있으면 false — 자리표시 그대로)
	 */
	UFUNCTION(BlueprintCallable, Category = "Animal")
	bool ApplyBodyFromRow(const FAnimalRow& Row);

	/**
	 * 행의 `MoveSpeed` 를 도주 속도로 건다 (M1-11b). `InitializeFromTable` 이 부르고,
	 * 2층 검사는 행을 직접 만들어 부른다 (`ApplyBodyFromRow` 와 같은 방식).
	 * 행의 값이 미정(음수)이면 걸지 않는다 — 그 종은 안 움직인다 (Verification.md 7).
	 */
	UFUNCTION(BlueprintCallable, Category = "Animal")
	void ApplyMoveSpeedFromRow(const FAnimalRow& Row);

	/**
	 * 이번 프레임에 도망갈 방향 (단위 벡터). 도망갈 상황이 아니면 영벡터다 —
	 * Fight 가 아니거나, 속도가 미정이거나, 도망칠 상대가 없을 때.
	 * `Tick` 이 이걸 그대로 `AddMovementInput` 에 넣는다.
	 * 도주 한계(DESIGN 3.1) 밖에서는 바깥으로 나가는 성분이 깎여 경계를 따라 도는 방향이 나온다.
	 */
	UFUNCTION(BlueprintPure, Category = "Animal")
	FVector GetFleeDirection() const;

	/**
	 * 싸움이 시작된 자리에서 도주 한계(DESIGN 3.1 「동물 도주 이탈 한계」)만큼 멀어졌나.
	 * 2층 검사가 "한계 안에 남았다" 를 이 함수가 아니라 실제 좌표로 보므로, 여기서는 로그·방향 꺾기에만 쓴다.
	 */
	UFUNCTION(BlueprintPure, Category = "Animal")
	bool IsBeyondFleeLeash() const;

	/**
	 * 지금 경계를 도는 중인가. 한계에 한 번 닿으면 **확실히 안으로 들어올 때까지** 참이다 —
	 * 한계 선 하나로 갈리면 경계에 붙어 도는 동안 매 걸음 안팎이 뒤집힌다 (`UpdateFleeOrbit`).
	 */
	UFUNCTION(BlueprintPure, Category = "Animal")
	bool IsOrbitingLeash() const;

	/**
	 * 막힘을 뺀 **본래** 도주 방향 — 사람 반대쪽(경계 밖에서는 접선). `GetFleeDirection` 은
	 * 여기에 우회를 얹어 돌려준다. 막힘 판정 자체가 이 방향을 봐야 해서 둘로 나눠 뒀다.
	 */
	FVector GetBaseFleeDirection() const;

	/**
	 * **앞이 막혀 제자리에 굳었는지** 보고, 그렇다면 잠시 옆으로 도는 방향을 래치한다. `Tick` 이
	 * 방향을 고르기 전에 매 프레임 부른다. 한 걸음에 실제로 간 거리가 갈 수 있었던 거리의 일부에도
	 * 못 미치는 상태가 잠깐 이어지면 막힌 것으로 본다 — 속도는 매 프레임 되돌려지므로 자리로만 안다.
	 */
	void UpdateBlockedDetour(float DeltaSeconds);

	/**
	 * 막혔을 때 꺾을 쪽(단위 벡터). 가려던 쪽에 가까운 각도부터 **캡슐을 실제로 쓸어 보고** 열린 첫 쪽을
	 * 고른다. 빠져나갈 틈이 아예 없으면 영벡터 — 그때는 본래 도주 방향 그대로 민다.
	 */
	FVector ChooseBlockedDetourDirection(const FVector& Base) const;

	/** 그 쪽으로 한 몸 너비만큼 지나갈 틈이 있나(캡슐 쓸기). `ChooseBlockedDetourDirection` 만 쓴다. */
	bool IsDetourOpen(const FVector& Direction) const;

	/**
	 * 평소(Idle) 배회 입력 — 서 있다 걷다를 번갈아 한다(DESIGN 3.1 🟡 「동물 배회」). 걷는 중이면 크기
	 * 0.5 인 방향, 서 있거나 Idle 이 아니면 영벡터. `Tick` 이 도주·추격 입력에 더한다. 호스트만 부른다.
	 */
	FVector UpdateWander(float DeltaSeconds);

	/** 배회로 걸을 쪽 — 집 반경 안·물가 밖·앞이 열린 쪽 가운데 무작위. 없으면 집 쪽이나 영벡터. */
	FVector ChooseWanderDirection(float StrideCm) const;

	/** 지금 배회로 걷는 중인가 (2층 검사용 · PIE 끼임 검사가 파이썬에서 부른다). */
	UFUNCTION(BlueprintPure, Category = "Animal")
	bool IsWanderWalking() const { return !WanderDirection.IsNearlyZero(); }

	/** 배회 반경(cm) — 처음 선 자리에서 이만큼 안만 돈다. 스포너가 「동물이 설 땅」 넓이를 이 값으로 잰다. */
	static float GetWanderRadiusCm();

	/** 배회 집을 잊는다 — 다음 Idle 틱에 지금 자리를 새 집으로 잡는다(지붕에서 땅으로 옮긴 뒤). */
	void ResetWanderHome() { bWanderHomeSet = false; WanderDirection = FVector::ZeroVector; }

	/** 경계 원의 접선(반시계 단위 벡터). 경계 밖에서 도는 방향이 이 벡터의 ±다. */
	FVector GetLeashTangent() const;

	/** 지금 실린 속도를 보고 도는 쪽을 고른다(+1·-1). `FleeOrbitSign` 이 비었을 때만 쓰인다. */
	int8 ChooseFleeOrbitSign() const;

	/** 경계 밖으로 나간 순간 도는 쪽을 한 번 정하고, 안으로 돌아오면 지운다. `Tick` 이 매 프레임 부른다. */
	void UpdateFleeOrbit();

	/**
	 * 도주 한계 밖에서 속도가 선을 넘지 않게 한다. 입력이 접선이면 **크기를 지킨 채 방향만 돌리고**,
	 * 그 밖에는 바깥 성분만 깎는다 — 깎기만 하면 경계에 닿는 순간 속도가 통째로 사라져 동물이 선다.
	 * 걸음 간격을 받는 이유는 넘친 거리를 한 걸음에 되돌리는 안쪽 속도를 재기 위해서다.
	 */
	void ApplyLeashToVelocity(const FVector& FleeInput, float DeltaSeconds);

	/**
	 * **지금 몸에 돌아야 할 되풀이 클립을 고른다** (M5-07d). 고르는 순서는 하나뿐이다:
	 * 움직이지 않으면 대기 · 도망가는 중이면 도주(`FleeAnimRef`) · 그 밖에 움직이면 걷기(`WalkAnimRef`).
	 * **걷기 칸이 비었으면 도주 클립을 느리게** 쓰고(M41-04 — 새 4종은 걷기 칸이 없다, DESIGN 8.7), 그것도 없으면 대기다.
	 * 죽음·포획 뒤에는 nullptr 을 돌려준다 — 그때 돌던 죽음 클립을 되풀이 클립이 덮으면 안 된다.
	 */
	UFUNCTION(BlueprintPure, Category = "Animal")
	UAnimSequence* ChooseLocomotionAnim() const;

	/** `ChooseLocomotionAnim` 이 고른 클립의 재생 빠르기. 걷기 대신 도주 클립을 빌릴 때만 1 보다 작다 (M41-04). */
	UFUNCTION(BlueprintPure, Category = "Animal")
	float ChooseLocomotionPlayRate() const;

	/**
	 * **걷는 동안 틀 클립 하나를 고른다** — 걷기 → 도주 느리게 → 대기 순서로 떨어진다 (M41-04).
	 * 종 이름이 아니라 칸이 비었는지로만 가른다(DataDriven 3). 도주 클립을 빌리면 `OutPlayRate` 는
	 * 걷기 속도 ÷ 도주 속도(배회 배율)다 — 다리가 몸이 나아가는 빠르기만큼만 움직이게.
	 * 목장 펫(`ARanchPetActor`)도 같은 순서를 쓰라고 정적 함수로 둔다 (M41-05).
	 */
	static UAnimSequence* PickWalkClip(UAnimSequence* Walk, UAnimSequence* Flee, UAnimSequence* Idle, float& OutPlayRate);

	/**
	 * 고른 클립을 실제로 건다. `Tick` 이 매 프레임 부르지만 **클립이 바뀔 때만** 갈아 끼운다 —
	 * 매 프레임 다시 걸면 프레임 0 으로 되감겨 제자리걸음이 된다 (플레이어 `PlayBodyAnimation` 과 같은 함정).
	 * 2층 검사는 월드 틱 없이 이걸 직접 부른다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animal")
	void UpdateLocomotionAnimation();

	/** 지금 몸에 걸려 있는 클립. 2층 검사가 "무슨 클립이 도는가" 를 이걸로 본다. */
	UFUNCTION(BlueprintPure, Category = "Animal")
	UAnimSequence* GetCurrentBodyAnim() const { return CurrentBodyAnim; }

	/** 지금 몸에 걸린 클립의 재생 빠르기. 2층 검사가 "도주 클립을 느리게 빌렸나" 를 이걸로 본다 (M41-04). */
	UFUNCTION(BlueprintPure, Category = "Animal")
	float GetCurrentBodyPlayRate() const { return CurrentBodyPlayRate; }

	/**
	 * 대기 ↔ 이동 클립이 갈리는 속도 문턱 둘(cm/s). 2층 검사가 "클립이 깜빡이는가" 를 볼 때
	 * 숫자를 제 파일에 베껴 적지 않고 여기서 읽는다 — 튜닝값이 바뀌어도 검사가 안 깨진다.
	 */
	float GetMoveAnimEnterSpeed() const { return MoveAnimEnterSpeedCmPerSecond; }
	float GetMoveAnimExitSpeed() const { return MoveAnimExitSpeedCmPerSecond; }

	/**
	 * 행의 공격 칸(`BehaviorKind`·`AttackDamage`·`AttackIntervalSec`·`AttackRangeCm`·`AggroRadiusCm`)을
	 * 읽는다 (M5-07d2 · DESIGN 3.1 "동물 공격"). `InitializeFromTable` 이 부르고, 2층 검사는 행을 직접 만들어 부른다.
	 */
	void ApplyAttackFromRow(const FAnimalRow& Row);

	/** 이 종이 한 번이라도 칠 수 있나 — 도주 종이거나 데미지·간격·사거리 칸이 비었으면(0·미정) false. */
	bool CanEverAttack() const;

	/**
	 * 행의 소리 칸(`HitSoundCue`·`DeathSoundCue`·`AggroSoundCue`·`SoundPitch`)을 읽는다 (M43-03 · DESIGN 8.7 「동물 소리」).
	 * `InitializeFromTable` 이 부르고, 2층 검사는 행을 직접 만들어 부른다.
	 */
	void ApplySoundFromRow(const FAnimalRow& Row);

	/** 이 동물이 지금까지 낸 소리 요청 수 (소리 이름별). 2층 검사가 "한 번 맞힘 = 한 번 울음" 을 이 값으로 본다. */
	int32 GetVoiceRequestCount(FName Cue) const { return VoiceRequestCounts.FindRef(Cue); }

	/**
	 * 지금 공격할 때인가 (갈래별 조건 — DESIGN 3.1 · 8.7 행동 열). 궁지 반격 = 도주 한계에 닿아 경계를 도는 중,
	 * 맞으면 반격 = 싸움 중, 선공 = 싸움 중이거나 반경 안의 사람을 쫓는 중. Capturable 부터는 늘 false.
	 */
	bool WantsToAttackNow() const;

	/**
	 * 사거리 안의 상대를 한 대 친다 — **호스트에서만** (DESIGN 4.1 권위 표). 체력은 상대 기기의
	 * `UPlayerVitalsComponent` 를 거쳐 깎인다 (`ASnarehavenCharacter::Client_ReceiveAnimalHit`).
	 * 간격은 부르는 쪽(`Tick`)이 잰다. @return 실제로 쳤으면 true
	 */
	bool TryAttackOnAuthority(ASnarehavenCharacter* Target);

	/** 쫓는 쪽(반격·선공)이 이번 프레임에 다가갈 방향. 쫓을 때가 아니거나 이미 사거리 안이면 영벡터다. */
	FVector GetChaseDirection() const;

	/** 선공 종이 반경 안의 사람을 보고 쫓는 중인가 (아직 맞지 않았어도). */
	bool IsAggroed() const { return bAggroed; }

	/** 추격을 포기하고 싸움 시작 자리로 돌아가는 중인가 (DESIGN 3.1 "추격 포기"). */
	bool IsReturningHome() const { return bReturningHome; }

	/** 싸움(또는 쫓기) 시작 자리 — 도주 한계와 추격 포기가 같은 이 점을 잰다. */
	FVector GetFightHome() const { return FleeHome; }

	/** 지금까지 실제로 친 횟수. 2층 검사가 "한 번도 안 쳤다" 를 이 값으로 본다. */
	int32 GetLandedAttackCount() const { return LandedAttackCount; }

	/**
	 * 행에서 읽어 둔 공격·피격 클립. 공격 클립은 `TryAttackOnAuthority` 가 한 번 틀고(M5-07d2),
	 * 피격 클립은 플레이어가 맞힐 때마다 `Multicast_PlayHitReaction` 이 한 번 튼다(M21-02).
	 */
	UFUNCTION(BlueprintPure, Category = "Animal")
	UAnimSequence* GetAttackAnim() const { return AttackAnim; }

	UFUNCTION(BlueprintPure, Category = "Animal")
	UAnimSequence* GetHitAnim() const { return HitAnim; }

	/**
	 * Fight 상태면 매 프레임 플레이어 반대 방향으로 민다 (M1-11b · DESIGN 3.1).
	 * `AActor::Tick` 과 같은 public 이다 — 2층 검사가 월드 틱 없이 한 프레임씩 직접 돌린다.
	 */
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * 맞은 순간의 그림을 **모든 기기에서** 한 번 (M21-02 · DESIGN 3.1 「때리는 손맛」) — 피격 클립 · 흰 깜빡임 ·
	 * 털·먼지 파편. 판정은 호스트가 끝냈고 이건 그림뿐이라 놓쳐도 된다(Unreliable). 파편 수는 호스트가 굴려
	 * 보내므로 모두 같은 수를 본다. 죽었거나 포획됐으면 클립만 안 튼다(죽는 자세가 이긴다).
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayHitReaction(float FlashSeconds, uint8 DebrisCount);

	/**
	 * 호스트에서 `Offset` 만큼을 `Seconds` 동안 고르게 민다 (DESIGN 3.1 「넉백 거리·시간」).
	 * 싸움(Fight) 중일 때만 민다 — Capturable·죽음은 즉시 정지가 규칙이다. 위치는 이동 복제로 게스트에 간다.
	 * @return 실제로 밀기 시작했으면 true
	 */
	bool StartKnockback(const FVector& Offset, float Seconds);

	/** 넉백이 도는 중인가. 2층 검사가 본다. */
	bool IsKnockbackActive() const { return KnockbackSecondsLeft > 0.f; }

	/** 지금 흰 깜빡임이 걸려 있나 (몸 메시 오버레이). 2층 검사가 본다. */
	bool IsHitFlashActive() const;

	/** 지금 변종 색 겹(M21-07a2)이 몸에 걸려 있나. 흰 깜빡임 중엔 false. 2층 검사가 본다. */
	bool IsVariantLookActive() const;

	/** 지금 이야기 종(안개뿔) 흰 겹이 몸에 걸려 있나. 흰 깜빡임 중엔 false. 2층 검사가 본다. */
	bool IsStoryLookActive() const;

	/** 2층 검사 전용 — 행 없이 이야기 종 겉모습을 켠다. */
	void SetStoryLookForTest(bool bOn) { bStoryLook = bOn; }

	/** 지금 떠 있는 피격 파편 수. 2층 검사가 본다. */
	int32 GetHitDebrisCount() const;

protected:

	/** 상태 알림을 여기서 건다 — 몸(메시)이 없는 종도 포획하면 사라져야 한다 (M1-10e2). */
	virtual void PostInitializeComponents() override;

	virtual void BeginPlay() override;

	/**
	 * 상태가 바뀌었을 때 몸에 걸린 클립을 갈아 끼운다 (M1-09).
	 * 애님 블루프린트가 없어서 전이를 C++ 이 한다 — DESIGN 3.1 이 정한 (c) 길이다.
	 */
	UFUNCTION()
	void HandleStateChanged(EAnimalState NewState);

	/** 공격 클립을 모두의 화면에서 한 번 튼다 — 판정은 호스트가 끝냈고 이건 그림뿐이라 놓쳐도 된다. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayAttackAnim();

	/** 선공 종이 대상을 처음 잡은 순간 덤빔 소리를 모두의 기기에서 한 번 (M43-04) — 연출이라 놓쳐도 된다. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayAggroVoice();

	// 어떤 동물인지는 인스턴스마다 다를 수 있어 EditAnywhere (engines/unreal.md 코드 규칙 1).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	TObjectPtr<UDataTable> AnimalTable;

	/** 볼 행 이름. MVP 동물은 돼지 1종이라 기본값을 그 행에 맞춰 둔다 (DESIGN 7 MVP 범위). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animal")
	FName AnimalRowName = TEXT("Animal_Pig");

private:
	/** `ChooseLocomotionAnim`·`ChooseLocomotionPlayRate` 가 같이 쓰는 한 곳 — 클립과 빠르기를 함께 고른다. */
	UAnimSequence* ChooseLocomotionClip(float& OutPlayRate) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimalStateComponent> AnimalState;

	// ACharacter 의 콜리전 캡슐은 렌더되지 않아서, 이게 없으면 3층 캡처에 동물이 아예 안 찍힌다
	// (플레이어가 M0-06 에서 그랬다). M1-07 에서 돼지 스켈레탈 메시가 들어오면 이 자리를 대체한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Placeholder", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	/** 도주 속도(cm/s). 행에서 온다 — 코드 기본값은 "아직 안 채움" (DataDriven.md 3). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	float MoveSpeed = -1.f;

	// 도망칠 상대. Fight 에 들어가는 순간 한 번만 찾아 둔다 — 매 프레임 월드를 훑으면
	// 동물 수 × 액터 수만큼 일이 는다(4~8인 PVE·모바일). 상대가 사라지면 약한 참조가 알아서 빈다.
	TWeakObjectPtr<AActor> FleeFrom;

	// 도주 한계를 재는 기준점 = **싸움이 시작된 자리**. `FleeFrom` 과 같은 순간에 같이 찍는다 —
	// BeginPlay 자리를 쓰지 않는 이유는 위험구역 스포너가 세운 동물도, 검사가 직접 스폰한 동물도
	// BeginPlay 를 안 거칠 수 있어서다(2층 검사 월드가 그렇다).
	FVector FleeHome = FVector::ZeroVector;

	// 한계에 닿았다는 로그는 한 번만 — 틱마다 찍으면 3층 로그가 그 한 줄로 덮인다.
	bool bFleeLeashReported = false;

	// 경계를 도는 쪽(+1 = 반시계 · -1 = 시계 · 0 = 아직 경계 밖이 아니다). 경계에 닿은 순간 한 번만
	// 정하고 안으로 돌아올 때까지 유지한다 — 매 프레임 다시 고르면 사람과 가장 먼 점 언저리에서
	// 좌우로 왔다갔다 하며 속도가 죽고 클립이 깜빡인다 (감독 PIE 결함 2026-09-24).
	int8 FleeOrbitSign = 0;

	// 막힘을 재는 값들. 지난 프레임 자리와 견줘 **실제로 간 거리**를 보고(`UpdateBlockedDetour`),
	// 못 간 시간이 쌓이면 잠시 옆으로 도는 방향을 래치한다. 우회 방향을 들고 있는 이유는
	// 매 프레임 다시 고르면 막은 것 앞에서 좌우로 떨기만 하기 때문이다.
	FVector LastTickLocation = FVector::ZeroVector;
	float BlockedSeconds = 0.f;
	float FreeSeconds = 0.f;
	float BlockedDetourSeconds = 0.f;
	FVector BlockedDetourDirection = FVector::ZeroVector;

	// 배회 상태. 집 = Idle 로 처음 선 자리(스폰 자리). 방향이 영벡터면 서 있는 중이다.
	FVector WanderHome = FVector::ZeroVector;
	FVector WanderDirection = FVector::ZeroVector;
	float WanderSecondsLeft = 0.f;
	bool bWanderHomeSet = false;

	// 행에서 읽어 둔 클립들. 상태가 바뀔 때마다 표를 다시 뒤지지 않으려고 액터가 들고 있는다.
	UPROPERTY()
	TObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY()
	TObjectPtr<UAnimSequence> DeathAnim;

	UPROPERTY()
	TObjectPtr<UAnimSequence> WalkAnim;

	UPROPERTY()
	TObjectPtr<UAnimSequence> FleeAnim;

	UPROPERTY()
	TObjectPtr<UAnimSequence> AttackAnim;

	UPROPERTY()
	TObjectPtr<UAnimSequence> HitAnim;

	// 지금 걸려 있는 클립. 같은 클립을 매 프레임 다시 걸면 프레임 0 으로 되감겨 걸음이 멈칫한다
	// — 플레이어에서 사람이 실제로 본 증상이다 (`ASnarehavenCharacter::PlayBodyAnimation`, 루프#139).
	UPROPERTY()
	TObjectPtr<UAnimSequence> CurrentBodyAnim;

	// 지금 걸린 클립의 재생 빠르기 — 같은 도주 클립이 걷기(느리게)와 도주(1배)를 겸하므로 클립만으로는 못 가른다.
	float CurrentBodyPlayRate = 1.f;

	// 대기 ↔ 이동 클립이 갈리는 속도 문턱 둘(cm/s). 하나로 가르면 가속·마찰 때문에 속도가 그 하나를
	// 여러 번 넘나들고 그때마다 클립이 되감겨 멈칫한다 — 플레이어가 같은 함정에 빠졌었다(루프#139).
	// **밸런스 숫자가 아니라 애니메이션 튜닝값이라 DESIGN §6 에 안 적는다**(플레이어 쪽 같은 칸의 판단
	// 그대로): 동물이 나아가는 속도·거리는 하나도 안 바뀌고 어느 클립을 보여 줄지만 정한다.
	// 값은 플레이어의 30/10 을 **속도 비율 그대로 옮긴 것**이다 — 플레이어 걷기 실측 약 156cm/s 의
	// 1/5·1/15 였으니, 동물 도주 속도 45cm/s(DESIGN 3.1)에서는 9 와 3 이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	float MoveAnimEnterSpeedCmPerSecond = 9.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animal", meta = (AllowPrivateAccess = "true"))
	float MoveAnimExitSpeedCmPerSecond = 3.f;

	// 두 문턱 사이에서는 지금 상태를 그대로 둔다 — 그 "지금 상태" 가 이 칸이다.
	bool bBodyMoving = false;

	/**
	 * 호스트 틱에서 공격을 돌린다 — 선공 감지 · 추격 포기 · 되돌아가기 · 간격 재기 · 한 대 (M5-07d2·d3).
	 * 이동 방향은 여기서 안 정한다(`GetChaseDirection`) — 여기는 상태 칸만 바꾼다.
	 */
	void UpdateAttack(float DeltaSeconds);

	/** 쫓는 갈래(맞으면 반격·선공)인가. 궁지 반격은 도망가다 돌아서는 것이라 쫓지 않는다. */
	bool IsChaserKind() const;

	// 공격 칸 (M5-07d2 · DESIGN 3.1 "동물 공격"). 행에서 온다 — 코드 기본값은 "아직 안 채움"(-1)이고
	// 그러면 치지 않는다 (DataDriven.md 3 · Verification.md 7).
	EAnimalBehaviorKind BehaviorKind = EAnimalBehaviorKind::Flee;
	float AttackDamage = -1.f;
	float AttackIntervalSec = -1.f;
	float AttackRangeCm = -1.f;
	float AggroRadiusCm = -1.f;

	/** 죽거나 포획된 뒤 사라지기까지 초 (DESIGN 3.1 「시체 수명」, M22). 행에서 온다 — 0 이하(미정)면 치우지 않는다. */
	float CorpseLifeSeconds = -1.f;

	// 다음 한 대까지 남은 시간. 0 이면 사거리에 드는 순간 바로 친다 — 첫 대는 기다리지 않는다.
	float AttackCooldownSeconds = 0.f;

	// 선공 종이 반경 안의 사람을 보고 쫓기 시작했다(아직 Idle 이어도). 추격을 포기하면 풀린다.
	bool bAggroed = false;

	// 추격을 포기하고 `FleeHome` 으로 걸어 돌아가는 중. 도착하면 풀리고, 그동안은 새로 쫓지 않는다.
	bool bReturningHome = false;

	// 공격·피격 클립이 도는 남은 시간 — 그동안은 이동 클립이 덮지 않는다.
	float BodyActionSecondsLeft = 0.f;

	// ── 맞은 순간 (M21-02) ──
	/** 넉백을 한 프레임 민다 (호스트 틱). */
	void UpdateKnockback(float DeltaSeconds);

	/** 파편을 한 프레임 움직이고 수명이 다하면 치운다 (모든 기기 틱). */
	void UpdateHitDebris(float DeltaSeconds);

	/** 흰 깜빡임을 뗀다 (타이머). */
	void ClearHitFlash();

	/** 깜빡임을 걸 몸 — 진짜 메시가 있으면 그것, 없으면 자리표시. */
	UMeshComponent* GetFlashBody() const;

	// ── 색 변종 겉모습 (M21-07a2) ──
	/** 깜빡임이 없을 때 몸에 걸려 있어야 할 겹 — 변종이면 변종 색, 아니면 nullptr. 변종 색은 처음 쓸 때 만든다. */
	UMaterialInterface* GetRestingOverlay();

	/** 복제된 `bVariant` 와 몸 겹을 맞춘다 (모든 기기 틱). 깜빡임 중이면 건드리지 않는다 — 끝날 때 `ClearHitFlash` 가 되돌린다. */
	void UpdateVariantLook();

	// `M_HitFlash` 동적 인스턴스 한 겹 — 변종만 만든다(사냥터 동물 대부분은 안 든다).
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> VariantLookMaterial;

	// 이야기 종 행(`bStorySpecies`)이면 참 — 평소 겹이 변종 금빛 대신 안개 흰색이 된다. 행을 읽을 때 정한다.
	bool bStoryLook = false;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StoryLookMaterial;

	FVector KnockbackVelocity = FVector::ZeroVector;
	float KnockbackSecondsLeft = 0.f;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> HitFlashMaterial;

	FTimerHandle HitFlashTimerHandle;

	// 파편은 **처음 맞을 때 한 번** 만든다 — 사냥터 동물 101마리 전부에 미리 붙이면 맞지도 않은 동물이 컴포넌트를 든다.
	// 인스턴스 메시 하나라 6~10개를 그려도 그리기 호출은 1번이다 (DESIGN 4.2).
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> HitDebris;

	UPROPERTY()
	TObjectPtr<UStaticMesh> HitDebrisMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> HitDebrisBaseMaterial;

	TArray<FVector> HitDebrisVelocities;
	float HitDebrisSecondsLeft = 0.f;

	int32 LandedAttackCount = 0;

	/**
	 * 동물 자리에서 행 소리를 낸다 — 이름이 비었거나 같은 소리를 최소 간격 안에 또 부르면 안 낸다(DESIGN 8.7).
	 * 모든 기기가 제 손으로 부른다(멀티캐스트·상태 복제 위) — 권위와 무관한 연출.
	 * @return 실제로 소리를 요청했으면 true
	 */
	bool PlayVoice(FName Cue, float Volume);

	// 소리 칸 (M43-02 표). 비면 무음.
	FName HitSoundCue;
	FName DeathSoundCue;
	FName AggroSoundCue;
	float SoundPitch = 1.f;

	/** 소리 이름 → 마지막으로 낸 월드 시각(초). 같은 동물 같은 소리 최소 간격을 잰다. */
	TMap<FName, double> LastVoiceSeconds;

	TMap<FName, int32> VoiceRequestCounts;
};
