#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RanchPetActor.generated.h"

class UAnimSequence;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMeshComponent;
struct FAnimalRow;
struct FItemRow;

// 목장 칸에 맡긴 펫의 **보이는 몸** (감독 🟡 2026-09-25, DESIGN 3.2 「목장 펫 모습」). 말뚝에 묶여
// 둘레를 서성인다 — 서 있다 걷다를 번갈아 한다. 사냥 동물(`AAnimalCharacter`)이 아니다: 때릴 수도
// 잡을 수도 없고, 복제도 안 된다. 칸의 내용은 여전히 백엔드 캐시(`URanchComponent`)가 정하고 이 몸은
// 그 캐시를 **비추기만** 한다 (CLAUDE.md 8). 세우고 치우는 건 `ARanchSlotActor::ShowPet`.
UCLASS(NotPlaceable)
class SNAREHAVEN_API ARanchPetActor : public AActor
{
	GENERATED_BODY()

public:
	ARanchPetActor();

	/** 행의 몸·클립을 입힌다. 메시가 없는 행이면 false — 부른 쪽이 치운다. */
	bool ApplyRow(const FAnimalRow& Row);

	/**
	 * 말뚝 자리(발밑). 이 둘레 `TetherMinCm`~`TetherRadiusCm` 고리 안에서만 걷고, 지금 고리 위 한 점으로 옮긴다.
	 * `Front`(수평)가 있으면 그쪽 ±`FrontArcDeg` 부채꼴에서만 — 칸 뒤 난간에 몸이 박히지 않게.
	 */
	void SetTetherHome(const FVector& Home, const FVector& Front = FVector::ZeroVector);

	static constexpr float FrontArcDeg = 75.f;

	/** 말뚝 밑돌(폭 약 90cm, DESIGN 3.2 「목장 칸 말뚝 진짜 모습」)에 몸이 박히지 않게 이만큼은 떨어져 선다. */
	static constexpr float TetherMinCm = 55.f;

	/**
	 * 목줄 반경 — 칸 폭 200cm 의 절반보다 작게. 검사용으로 연다.
	 * ponytail: 둘째 줄(140cm 간격)은 이웃 펫 고리가 30cm 겹친다 — 충돌이 없어 스쳐 지날 뿐. 거슬리면 칸 간격으로 반경을 줄인다.
	 */
	static constexpr float TetherRadiusCm = 85.f;

	/**
	 * 꾸미기를 씌운다(DESIGN 10.9) — 행의 `EquipSocketName` 뼈(`head`·`neck`)에, 폭 = 펫 키 × `PetCosmeticWidthRatio`.
	 * nullptr = 벗긴다. 같은 행이면 그대로. 칸 캐시를 비추기만 한다(판정은 서버 `SetPetCosmetic`).
	 */
	void SetCosmetic(const FItemRow* Row);

	/**
	 * 목 굵기(메시 단위 cm, 액터 배율 전) — 기준 자세에서 목 → 머리 축에 직각인 목 단면 상자의 좌우·앞뒤 중 큰 폭.
	 * 목 마디 뼈가 잡은 정점(머리 쪽 빼고)의 축 방향 한가운데 둘레 띠로 잰다. 목걸이 고리를 이만큼 벌린다(`SetCosmetic`).
	 * 정점은 에디터에만 있어 데이터 표 스크립트가 재서 행 `FAnimalRow::PetNeckWidthCm` 에 적는다
	 * (`tools/unreal/create_data_tables.py`). 못 재면 0.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ranch")
	static float MeasureNeckWidthCm(USkeletalMesh* Mesh);

	/** 위 단면의 가운데 — 목 뼈에서 잰 거리(메시 단위 cm, 컴포넌트 축). 행 `FAnimalRow::PetNeckCenterCm` 에 적는다. */
	UFUNCTION(BlueprintCallable, Category = "Ranch")
	static FVector MeasureNeckCenterCm(USkeletalMesh* Mesh);

	/** 위 단면의 앞뒤 높이(목 위쪽 정점 ↔ 멱, 메시 단위 cm) — 고리 위끝이 등선 위로 솟지 않는지 2층 검사용. 못 재면 0. */
	static float MeasureNeckDepthCm(USkeletalMesh* Mesh);

	/**
	 * 머리 뒤쪽 끝 — 머리 뼈 쪽 정점 중 목 → 머리 축에서 목 뼈에 가장 가까운 거리(메시 단위 cm). 목걸이 고리 자리가 이보다
	 * 몸 쪽이어야 얼굴이 아니라 목을 두른다(M40-04 감독 보정, 2층 검사용). 못 재면 0.
	 */
	static float MeasureHeadRearAlongCm(USkeletalMesh* Mesh);

	/** 지금 씌운 꾸미기 ID (없으면 NAME_None) — 검사·중복 적용 막기. */
	FName GetCosmeticItemID() const { return CosmeticItemID; }

	/** 꾸미기 메시 칸 (검사용). */
	UStaticMeshComponent* GetCosmeticMesh() const { return CosmeticMesh; }

	/** 걷는 중인가 (2층 검사용). */
	bool IsWalking() const { return !WalkTarget.IsZero(); }

	/** 지금 몸에 걸어 둔 되풀이 클립 (2층 검사가 「걷는 동안 무슨 클립이 도는가」 를 본다 — M41). */
	UAnimSequence* GetCurrentClip() const { return CurrentClip; }

	/** 지금 클립의 재생 빠르기 — 걷기 칸이 비어 도주 클립을 빌리면 1 보다 작다 (M41-05). */
	float GetCurrentPlayRate() const { return CurrentPlayRate; }

	virtual void Tick(float DeltaSeconds) override;

private:
	/** 클립이 바뀔 때만 갈아 끼운다(되감기 방지). 같은 클립이어도 빠르기는 맞춘다. */
	void PlayLoop(UAnimSequence* Clip, float PlayRate = 1.f);

	/** 목 단면 가운데(XYZ, 목 뼈 기준)와 폭(W). OutDepthCm = 단면 앞뒤 높이. */
	static FVector4 MeasureNeck(USkeletalMesh* Mesh, float* OutDepthCm = nullptr);

	UPROPERTY(VisibleAnywhere, Category = "Ranch")
	TObjectPtr<USkeletalMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Ranch")
	TObjectPtr<UStaticMeshComponent> CosmeticMesh;

	FName CosmeticItemID;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> WalkAnim;

	/** 걷기 칸이 빈 종은 이걸 느리게 빌려 걷는다 (`AAnimalCharacter::PickWalkClip`). */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> FleeAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> CurrentClip;

	float CurrentPlayRate = 1.f;

	/** 행 `PetNeckWidthCm` — 목걸이 고리 폭을 정한다. 0 이면 펫 키 비율로. */
	float NeckWidthCm = 0.f;

	/** 행 `PetNeckCenterCm` — 목걸이 고리를 목 뼈에서 이만큼 옮겨 목 단면 가운데에 둔다. */
	FVector NeckCenterCm = FVector::ZeroVector;

	/** 메시 밑면을 바닥에 맞추는 높이(cm) — 루트가 곧 몸이라 원점이 발밑이 아닐 수 있다. */
	float FootOffsetZ = 0.f;

	FVector TetherHome = FVector::ZeroVector;
	TOptional<float> FrontYaw;
	/** 걸어갈 곳(XY). 영벡터면 서 있다. */
	FVector WalkTarget = FVector::ZeroVector;
	float StandSecondsLeft = 0.f;
};
