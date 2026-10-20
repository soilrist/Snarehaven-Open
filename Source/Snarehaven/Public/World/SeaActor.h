#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SeaActor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UTexture;
class UStaticMeshComponent;

// 정박한 배 흔들림 (DESIGN 8.9 「항구」 — 레퍼런스 부두의 배는 물 위에서 천천히 오르내리고 기운다). 보기 전용이라 복제하지 않는다.
namespace SnareSea
{
	constexpr float BobPeriodSeconds = 4.f;   // 한 번 오르내리는 데 4초
	constexpr float BobHeightCm = 6.f;        // 작은 배 위아래 폭(±)
	constexpr float BobRollDeg = 1.5f;        // 작은 배 좌우 기울기(±)
	constexpr float BobReferenceCm = 600.f;   // 이 길이보다 긴 배는 길이에 반비례해 덜 흔들린다(대형선은 거의 안 움직인다)

	/** 흔들림 몫 0~1 — 배 길이(cm)가 길수록 작다. 최소 0.2. */
	SNAREHAVEN_API float BobScale(float HullLengthCm);
	/** 시각 Seconds·위상 Phase(라디안)에서 위아래(cm)·기울기(도). Scale 을 곱해 쓴다. */
	SNAREHAVEN_API FVector2f BobOffset(double Seconds, float Phase);
}

// DESIGN 8.9 "맵 끝 = 바다". 섬(정사각 바닥) 둘레를 바다로 채운다 — 한 액터가 다 짓는다.
//   · 수면: 평면 하나(충돌 없음, `MI_Sea_Water` 물결 셰이더 — 반사·굴절 없음, 모바일 가벼움)
//   · 해저: 섬 가장자리에서 계단으로 내려가는 상자(걸어서 들어가고 걸어 나온다)
//   · 바깥 끝: 보이지 않는 벽 4면 (폰을 막는다)
//   · 해안: 밝은 띠·흰 거품 줄은 셰이더가 그린다 — 섬 가운데·반폭(목장) 또는 해안 거리 텍스처(사냥터)를 머티리얼에 넘긴다
//   · 사냥터 지도: `tools/unreal/make_world_map.py` 가 구운 해안 거리장·존 칸을 들고 게임 판정(뭍·바다·존)을 한다
// 폭(해안 → 바깥 끝)은 `DT_WaterActivityTable.SeaWidthCm` 에서 읽는다. 액터 Z 가 곧 수면 높이다 —
// `UWaterTravelComponent` 가 발이 이 높이 아래면 물속으로 친다.
// 계단 깊이는 게임 규칙이 아니라 **레벨 모양** 값이라 여기 칸으로 둔다(레벨에서 고친다).
UCLASS()
class SNAREHAVEN_API ASeaActor : public AActor
{
	GENERATED_BODY()

public:
	ASeaActor();

	virtual void OnConstruction(const FTransform& Transform) override;

	/** 게임에선 생성 스크립트가 다시 안 돈다 — 표 값과 어긋나지 않게 시작할 때 한 번 더 짓는다. */
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 이 태그가 붙은 액터(부두의 배)는 수면에서 흔들린다. */
	static const FName FloatTag;

	/** 수면 높이(월드 Z). */
	float GetSurfaceZ() const { return GetActorLocation().Z; }

	/** 이 점(XY)이 바다 쪽(섬 바깥, 벽 안)인지. */
	bool IsOverSea(const FVector& WorldLocation) const;

	/** 가장 가까운 해안까지 거리(cm). < 0 = 뭍, > 0 = 바다(호수 포함). 거리장이 없으면 정사각 섬(반폭 IslandHalfExtentCm). */
	float CoastDistance(const FVector& WorldLocation) const;

	/** 사냥터 지도 자료(해안 거리장)가 들어 있는가. */
	bool HasField() const { return CoastField.Num() > 0 && CoastField.Num() == CoastFieldSize.X * CoastFieldSize.Y; }

	/** 이 자리의 존 칸 코드 — 아래 4비트 = `FZoneRow::FieldCode`(평원 1·설원 2·숲 3·늪 4), 0x10 = 모래. 칸 밖·자료 없음 = 0. */
	uint8 ZoneCodeAt(const FVector& WorldLocation) const;

	/** From 둘레에서 가장 가까운, 존 코드가 Code 이고 해안에서 MarginCm 이상 안쪽인 뭍 점. 반경 ReachCm 안에 없으면 false. */
	bool FindZonePoint(uint8 Code, const FVector2D& From, float MarginCm, float ReachCm, FVector2D& OutPoint) const;

	/** 거리장이 덮는 월드 XY 사각형. 없으면 무효. */
	FBox2D GetFieldBounds() const;

	/** 월드의 첫 바다. 없으면 nullptr. */
	static ASeaActor* FindSea(const UObject* WorldContext);

	/** 섬 바닥 반폭(cm) — 바닥 평면 크기. 레벨 배치 스크립트가 넣는다. */
	UPROPERTY(EditAnywhere, Category = "Sea")
	float IslandHalfExtentCm = 2500.f;

	// ── 사냥터 지도 자료 (`setup_sea.py` 가 `tools/unreal/data/world_*.bin` 을 넣는다) ─────────────
	// 있으면 IslandHalfExtentCm 은 바다 벽을 재는 바깥 틀일 뿐이고, 해저 계단 대신 땅 메시가 물속으로 비탈진다.
	// 식은 `tools/unreal/islands.py` 와 같다(노드 쌍선형 · 칸 내림).

	/** 거리장 노드 (0,0) 과 존 칸 (0,0) 모서리의 월드 XY(cm). */
	UPROPERTY(EditAnywhere, Category = "Sea|Field")
	FVector2D FieldOriginCm = FVector2D::ZeroVector;

	/** 해안 거리장 노드 간격(cm)과 노드 수(X, Y). */
	UPROPERTY(EditAnywhere, Category = "Sea|Field")
	float CoastCellCm = 200.f;

	UPROPERTY(EditAnywhere, Category = "Sea|Field")
	FIntPoint CoastFieldSize = FIntPoint::ZeroValue;

	/** 해안까지 거리(cm, < 0 = 뭍) — 노드 [i * Y + j]. */
	UPROPERTY(EditAnywhere, Category = "Sea|Field")
	TArray<int16> CoastField;

	/** 존 칸 한 변(cm)과 칸 수(X, Y). */
	UPROPERTY(EditAnywhere, Category = "Sea|Field")
	float ZoneCellCm = 400.f;

	UPROPERTY(EditAnywhere, Category = "Sea|Field")
	FIntPoint ZoneFieldSize = FIntPoint::ZeroValue;

	/** 존 칸 코드 [i * Y + j] — `ZoneCodeAt`. */
	UPROPERTY(EditAnywhere, Category = "Sea|Field")
	TArray<uint8> ZoneField;

	/** 물 셰이더가 해안 띠·거품을 그리는 거리 텍스처(`T_World_Coast`, 1px = 100cm, 값 = 거리/20 + 128). */
	UPROPERTY(EditAnywhere, Category = "Sea|Field")
	TObjectPtr<UTexture> CoastTexture;

	/**
	 * 지도 그림(`T_World_Map`) — 섬 틀 전체(가운데 ± IslandHalfExtentCm, 위 = +X · 오른쪽 = +Y)를 그린 정사각 그림.
	 * 있으면 미니맵·전체 지도가 칸 색·장면 캡처 대신 이 그림을 쓴다 (`GetMapBounds`).
	 */
	UPROPERTY(EditAnywhere, Category = "Sea|Field")
	TObjectPtr<UTexture> MapTexture;

	/** 지도 그림이 덮는 월드 틀(정사각). 그림이 없으면 무효 상자. */
	FBox2D GetMapBounds() const;

	/** 섬 바닥 높이(월드 Z). 해저 첫 계단이 여기서 한 칸 아래다. */
	UPROPERTY(EditAnywhere, Category = "Sea")
	float IslandFloorZ = 0.f;

	/** 해저 계단 한 칸 폭(cm)과 높이(cm). 높이는 캐릭터가 오를 수 있는 45cm 아래여야 한다. */
	UPROPERTY(EditAnywhere, Category = "Sea")
	float ShelfStepWidthCm = 150.f;

	UPROPERTY(EditAnywhere, Category = "Sea")
	float ShelfStepHeightCm = 40.f;

	/** 가장 깊은 해저가 수면에서 얼마 아래인지(cm). 사람 가슴께가 물에 잠기는 깊이. */
	UPROPERTY(EditAnywhere, Category = "Sea")
	float DeepFloorBelowSurfaceCm = 130.f;

	UPROPERTY(EditAnywhere, Category = "Sea")
	TObjectPtr<UMaterialInterface> WaterMaterial;

	/** 마지막으로 지은 바다 폭(cm). 표를 못 읽어 안 지었으면 -1. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Sea")
	float BuiltSeaWidthCm = -1.f;

private:
	void Build();

	UPROPERTY(VisibleAnywhere, Category = "Sea")
	TObjectPtr<UStaticMeshComponent> WaterSurface;

	UPROPERTY(VisibleAnywhere, Category = "Sea")
	TObjectPtr<UInstancedStaticMeshComponent> Seabed;

	UPROPERTY(VisibleAnywhere, Category = "Sea")
	TObjectPtr<UInstancedStaticMeshComponent> OuterWalls;

	struct FFloater
	{
		TWeakObjectPtr<AActor> Actor;
		FTransform Rest;
		float Scale = 1.f;
		float Phase = 0.f;
	};
	TArray<FFloater> Floaters;
};
