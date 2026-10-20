#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ZoneRow.generated.h"

// ROADMAP M6-01: 존 하나와 그 존이 어디로 이어지는지 적은 데이터 한 줄 (DT_ZoneTable 의 행 구조체).
// 근거는 DESIGN 1.3 "존(zone)을 포탈/로딩존으로 잇는 구조" 와 `docs/decisions/005`,
// 지금 이어진 길의 현황은 `docs/WORLD_ZONES.md` 표 A·B 다.
//
// **맵 이름을 코드에 박지 않으려고 만든 표다** (DataDriven.md 1). 지금은 `L_Hunt`·`L_Title` 이
// `ASnarehavenGameMode::HuntLevelName`·`USnareOnlineSubsystem::LobbyLevelName` 처럼 C++ 상수로
// 박혀 있는데, 존이 늘 때마다 상수와 분기가 같이 느는 것을 여기서 끊는다 — 존이 늘면 행이 는다.
// **이 조각은 표만 세운다.** 위 상수들을 표로 갈아끼우는 것은 포탈이 생기는 M6-02 몫이다
// (부팅·로비 흐름까지 같이 건드리면 조각 밖 코드가 된다, PROMPT ③-2).
//
// **이동은 서버가 한다.** 방 안에서 존을 옮기는 길은 `ServerTravel` 하나뿐이고
// (`USnareOnlineSubsystem::MakeHuntServerTravelUrl` 옆), 클라이언트가 이 표를 보고 스스로
// `OpenLevel` 을 부르면 방이 갈라진다 (`docs/WORLD_ZONES.md` 표 B · ROADMAP M6-02 합격 기준).
USTRUCT(BlueprintType)
struct FZoneRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 이 존의 ID. 행 이름 `Zone_Hunt` ↔ `zone.hunt` (DataDriven.md 4 — 한번 정하면 바꾸지 않는다). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
	FName ZoneID;

	/** 이 존이 실제로 쓰는 맵 이름 (`Content/Game/Maps/` 의 `.umap`, 예: `L_Hunt`). 없는 맵을 적으면 2층 검사가 운다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
	FName LevelName;

	/** 화면에 그대로 뜨는 이름 (DESIGN 1.3 의 존 이름 표기). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
	FText DisplayName;

	/** 여기서 갈 수 있는 존들의 `ZoneID`. 같은 표 안에 있는 ID 여야 한다 — 포탈의 갈 곳이 여기서 나온다(M6-02). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
	TArray<FName> ConnectedZoneIDs;

	// ── M12-00: 맵 **안의** 성격 존 (DESIGN 1.3 존 표) ─────────────────────────────────
	// 사용자 방법(ROADMAP M12 머리말): 새 존 시스템 대신 `L_Hunt` 를 넓혀 그 안에 설원·늪·섬 성격을 준다.
	// 그래서 같은 `LevelName` 을 쓰는 행이 여럿이 되고, 그 행들은 **맵 안의 정사각형 한 칸**으로 갈린다.
	// 반변이 0 이면 맵 전체를 뜻하는 옛 행(`zone.hunt`·`zone.ranch`)이다 — 포탈·지금 존 찾기는 그 행만 본다.

	/** 맵 안 정사각형 칸의 가운데 (월드 X·Y, cm). 반변이 0 이면 안 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone|Region")
	FVector2D RegionCenterCm = FVector2D::ZeroVector;

	/** 맵 안 정사각형 칸의 반변(cm). **0 = 맵 전체**(성격 존이 아닌 행). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone|Region")
	float RegionHalfExtentCm = 0.0f;

	/**
	 * 새 지도(2026-09-25) 존 칸 코드 — `ASeaActor::ZoneField` 의 아래 4비트와 같다(평원 1 · 설원 2 · 숲 3 · 늪 4).
	 * 0 이 아니고 바다가 거리장을 들고 있으면 **정사각형 대신 이 코드로** 안/밖을 가른다 — 정사각형은 존을 감싸는 틀이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone|Region")
	int32 FieldCode = 0;

	/** 이 칸 안에 있는 동안 초당 깎이는 체력 (설원). 0 = 안 깎는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone|Region")
	float HealthDrainPerSecond = 0.0f;

	/** 이 칸 안에는 짓지 못한다 (늪). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone|Region")
	bool bBuildBlocked = false;

	/**
	 * 이 칸에 들어가려면 입장료를 낸다 (섬). **요금 숫자는 여기 없다** — 서버 `PayFerry` 의 `FERRY_FARES` 가
	 * 정하고 클라이언트는 요청만 보낸다 (CLAUDE.md 8).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone|Region")
	bool bChargesEntryFee = false;

	/**
	 * 미니맵(M12-03)에서 이 칸을 칠하는 색 (sRGB). 값은 DESIGN 1.3 "미니맵" 표 — 그 칸 바닥색을 탑다운
	 * 캡처에서 뽑은 것이다. 색을 코드에 박지 않으려고 표에 둔다 (칸이 늘면 행이 는다).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone|Region")
	FColor MinimapColor = FColor::Black;
};
