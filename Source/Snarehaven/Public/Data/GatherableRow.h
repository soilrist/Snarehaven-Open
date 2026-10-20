#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "GatherableRow.generated.h"

// ROADMAP M1-06: 채집 오브젝트 한 종을 설명하는 데이터 한 줄 (DT_GatherableTable 의 행 구조체).
// 종류·주는 아이템·개수를 전부 행에 두는 이유: 채집물이 늘어도 코드가 안 늘어야 한다 (DataDriven.md 1,
// DESIGN 3.3 "오브젝트 종류·드롭 아이템·개수는 코드가 아니라 데이터 행에 둔다").
// 리스폰·도구·개수 범위 칸은 DESIGN 8.10 나무 베기(손도끼로 1~4개, 300초 뒤 다시 자람)가 정해지면서 늘었다.
// 베리 덤불·마른 나무 행은 이 칸들을 0/비움으로 두어 예전 규칙(맨손 1개, 리스폰 없음)을 그대로 지킨다.
/** 채집 더미가 굴리는 후보 한 줄 (DESIGN 13.4 — 버섯 3:3:3:1 처럼 한 더미가 여러 아이템 중 하나를 준다). */
USTRUCT(BlueprintType)
struct FGatherGrantOption
{
	GENERATED_BODY()

	/** 줄 아이템 ID (DT_ItemTable 의 `ItemID`). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	FName ItemID;

	/** 가중치. 0 이하는 안 뽑힌다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	int32 Weight = 0;
};

USTRUCT(BlueprintType)
struct FGatherableRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 세이브·백엔드가 쓰는 고정 ID. 행 이름 `Gatherable_BerryBush` ↔ `gatherable.berry_bush` (DataDriven.md 4). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	FName GatherableID;

	/** 화면에 보이는 이름. 로컬라이제이션을 위해 FString 이 아니라 FText. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	FText DisplayName;

	/** 한 번 채집했을 때 주는 아이템 ID (DT_ItemTable 의 `ItemID`). 비어 있으면 아직 안 채운 행이다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	FName GrantItemID;

	/**
	 * 채울 때마다(처음·다시 날 때) 호스트가 가중치로 하나를 굴려 `GrantItemID` 대신 준다 (DESIGN 13.4 채집 더미).
	 * 비어 있으면 `GrantItemID` 고정이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	TArray<FGatherGrantOption> GrantOptions;

	/** 한 번 채집했을 때 주는 개수. DESIGN 3.3 = 1 (감독 확정). 음수면 미정 취급 (Verification.md 7). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	int32 GrantCount = -1;

	/** 한 번에 주는 최대 개수. 0 이면 `GrantCount` 고정이고, 크면 호스트가 [GrantCount, GrantCountMax] 에서 굴린다 (DESIGN 8.10). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	int32 GrantCountMax = 0;

	/**
	 * 쓰러지기까지 쳐야 하는 번 수 (DESIGN 8.10 나무 3번·향나무 4번). 0·1 이면 한 번에 거둔다.
	 * 2 이상이면 마지막 번 전까지는 흔들리기만 하고 지급이 없으며, 마지막 번에 넘어가는 연출 뒤 사라진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	int32 HitsToFell = 0;

	/** 본 지급과 함께 덤으로 주는 아이템 — 나무를 베면 나무에 더해 수지가 나온다 (DESIGN 8.10). 비어 있으면 덤 없음. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	FName ExtraItemID;

	/** 덤 최대 개수. 호스트가 [0, ExtraCountMax] 에서 굴린다 — 0 이 나오면 그 번은 덤 없음 (DESIGN 8.10). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	int32 ExtraCountMax = 0;

	/** 벤 뒤 다시 자라기까지 초. 0 이면 리스폰 없이 사라진다 (DESIGN 3.3 베리·마른 나무). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	float RespawnSeconds = 0.f;

	/**
	 * 거둔 뒤에도 몸(덤불)은 그대로 서 있고 열매만 없어진다 — 몸 메시의 재질 1번 이후 칸(열매)을 0번(잎) 재질로 덮는다
	 * (레퍼런스 열매 덤불, INBOX 결함 10-06 02:20). 다시 날 때 원래 재질로 돌린다. 끄면 지금처럼 몸이 숨거나 사라진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	bool bKeepBodyWhenGathered = false;

	/** 누른 뒤 겉모습이 바뀌기까지 초 — 손이 가장 멀리 뻗은 때에 열매가 없어지게 (DESIGN 13.4). 0 이면 누른 순간. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	float GatheredVisualDelaySeconds = 0.f;

	/** 도구 칸에 이 아이템을 들고 있어야 채집된다. 비어 있으면 맨손 (DESIGN 8.10 손도끼). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	FName RequiredToolItemID;

	/**
	 * 도구 칸에 이 아이템을 들고 캐면 그 번은 「도구로 얻은 번」으로 서버에 올라가 +1 된다 (DESIGN 13.5 전기톱·마체테).
	 * `RequiredToolItemID` 가 있어도 이 도구로는 캘 수 있다(전기톱은 손도끼 대신). 비어 있으면 보너스 없음.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	FName BonusToolItemID;

	/** `RequiredToolItemID` 대신 들어도 되는 윗급 도구 — 고급 곡괭이로 기본 광맥도 캔다 (DESIGN 8.16). 보너스는 없다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	TArray<FName> AlsoToolItemIDs;

	/**
	 * 맵에 이미 깔린 장식 나무 메시 이름 머리(예: `SM_Nature_Tree_`). 스포너가 이 메시를 쓰는
	 * StaticMeshActor 자리에 채집물을 세우고, 채집물은 그 장식을 제 몸으로 삼는다 — 맵(.umap)을 안 고치고
	 * 이미 있는 나무를 벨 수 있게 하려는 칸이다. 비어 있으면 맵에 직접 깐 채집물이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	FString DecorMeshPrefix;

	/**
	 * 숨긴 줄기 충돌 원기둥 (X = 지름 cm, Y = 높이 cm). 0 이면 없다 — 몸 메시는 충돌이 없어서(채집은 거리 판정)
	 * 맵에 직접 깐 나무(수지 나무)는 이 칸이 있어야 플레이어가 뚫고 지나가지 않는다. 존 소품 나무의
	 * `TrunkCollision` 과 같은 방식·같은 값이다 (M13-15 · DESIGN 13.4).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	FVector2D TrunkCollisionCm = FVector2D::ZeroVector;

	/** 보이는 메시. 진짜 아트가 들어올 때까지 비어 있다 — 그동안은 액터의 자리표시 메시를 그대로 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	TSoftObjectPtr<UStaticMesh> MeshRef;

	/**
	 * 보이는 몸의 월드 높이 cm (DESIGN 3.3 더미 크기 표). 메시 높이에 맞춰 고르게 키운다 — 맵 배치 배율보다 이 칸이 이긴다.
	 * 0 이면 맵 배율 그대로(장식 나무를 넘겨받는 행).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gatherable")
	float BodyHeightCm = 0.f;
};
