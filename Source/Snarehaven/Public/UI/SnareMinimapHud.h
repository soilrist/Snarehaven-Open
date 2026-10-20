#pragma once

#include "CoreMinimal.h"

class ADangerZoneActor;
class ASeaActor;
class UButton;
class UCanvasPanel;
class UDataTable;
class UImage;
class UTextBlock;
class UWidgetTree;
struct FZoneRow;

// ROADMAP M12-03: 사냥 HUD 왼쪽 위 모서리의 **미니맵** — 맵 안 존 칸(DESIGN 1.3)의 경계와 플레이어 점,
// 그 아래 지금 존 이름 (레퍼런스 레퍼런스의 "모서리 작은 지도 + 존 이름").
// 칸 자리·크기·색은 **전부 `DT_ZoneTable` 에서 읽는다** — 칸이 늘면 표 행이 늘고 여기는 그대로다.
// `SnareBalanceHud` 와 같은 짜임이다: 자리표시 트리를 세우는 코드와 좌표 계산을 한 곳에 둬서
// HUD 와 2층 검사가 같은 함수를 쓴다.
//
// 방향: 카메라가 yaw 0 으로 고정이라(`ASnarehavenCharacter` 의 CameraBoom) 화면 위 = 월드 +X,
// 화면 오른쪽 = 월드 +Y 다. 미니맵도 같은 방향으로 그린다 — 지도와 화면이 돌아가 있으면 헷갈린다.
namespace SnareMinimapHud
{
	/** 자리표시 배치 값 (Autonomy.md 🟡 — 화면 배치라 게임 규칙 숫자가 아니다. DESIGN 1.3 "미니맵" 표에 근거). */
	constexpr float MapSize = 240.f;      // 지도 긴 변
	constexpr float FrameBorder = 6.f;    // 지도 둘레 테두리 (진갈색)
	constexpr float CellGap = 4.f;        // 칸 사이 선 두께 — 테두리 색이 비쳐 칸 경계가 된다
	constexpr float DotSize = 18.f;       // 플레이어 점 지름
	constexpr float ZoneNameHeight = 36.f;
	constexpr float ZoneNameGap = 8.f;
	constexpr float EventDotSize = 14.f;  // 월드 이벤트 표식 지름 (DESIGN 8.1b "추락 지점 표식")

	/** 그 맵의 칸 행(반변 > 0)만 표 순서대로. 표가 없거나 칸이 없으면 빈 배열. */
	SNAREHAVEN_API TArray<const FZoneRow*> GetRegionRows(const UDataTable* Table, FName LevelName);

	/** 칸들을 전부 덮는 월드 X·Y 사각형. 칸이 없으면 무효(`bIsValid == false`). */
	SNAREHAVEN_API FBox2D GetMapBounds(const TArray<const FZoneRow*>& Rows);

	/**
	 * 월드 위치 → 지도 안 좌표(왼쪽 위가 (0,0), 단위는 화면 units). 긴 변이 `InMapSize` 에 맞게 줄인다.
	 * 칸 밖(바다)이면 지도 가장자리에 붙인다 — 점이 지도 밖으로 나가 다른 버튼을 덮지 않게.
	 */
	SNAREHAVEN_API FVector2D WorldToMap(const FBox2D& Bounds, const FVector& Location, float InMapSize = MapSize);

	/** 지도 그림 크기(가로, 세로). 칸이 없으면 0. */
	SNAREHAVEN_API FVector2D GetMapExtent(const FBox2D& Bounds, float InMapSize = MapSize);

	/** 존 이름 줄 글자. 칸 밖이면 `-` (모르는 것을 지어내지 않는다 — Verification.md 7). */
	SNAREHAVEN_API FText FormatZoneName(const FZoneRow* Row);

	/** 월드 이벤트 남은 시간 줄 (`추락 지점 9:41`). 초는 올림 — 0:00 은 사라지는 그 순간뿐이다. */
	SNAREHAVEN_API FText FormatEventRemaining(float Seconds);

	/** 보물 남은 시간 줄 (`보물 9:41`, DESIGN 13.8). 초는 올림. */
	SNAREHAVEN_API FText FormatTreasureRemaining(double Seconds);

	/**
	 * 위성 통신기 좌표 줄 (DESIGN 13.5 — `북 12 · 동 -34`). 월드 +X = 북(지도 위), +Y = 동(지도 오른쪽), 단위 m 반올림.
	 */
	SNAREHAVEN_API FText FormatCoordinates(const FVector& Location);

	/** `AddPlaceholder` 가 세운 것. 칸이 없으면 전부 nullptr·0 이고 아무것도 안 세운다. */
	struct FPlaceholder
	{
		UImage* PlayerDot = nullptr;
		UButton* TouchButton = nullptr;       // 지도 그림 전체를 덮는 투명 누름 칸 — 누르면 큰 지도 (INBOX 결함 10-11 23:05)
		UTextBlock* ZoneNameText = nullptr;
		UImage* EventDot = nullptr;           // 월드 이벤트 표식 (M12-07) — 이벤트가 없으면 접혀 있다
		UTextBlock* EventText = nullptr;      // 존 이름 아래 남은 시간 줄 — 이벤트가 없으면 접혀 있다
		UTextBlock* CoordText = nullptr;      // 그 아래 좌표 줄 (13.5 위성 통신기) — 들고 있을 때만 보인다
		UImage* StoryDot = nullptr;           // 스토리 목표 표식 — 지금 단계의 상대가 이 맵에 있을 때만
		UImage* TreasureDot = nullptr;        // 보물 표식 (13.8, M13-12) — 찾는 보물이 이 맵에 있을 때만
		UTextBlock* TreasureText = nullptr;   // 그 아래 보물 남은 시간 줄 — 찾는 보물이 있을 때만
		FBox2D Bounds = FBox2D(ForceInit);
		int32 CellCount = 0;
		float Height = 0.f;   // 왼쪽 위에서 차지한 세로 길이 — 그 아래 기둥을 이만큼 내린다
	};

	/** 칸 위젯 이름 (`MinimapCell_zone.snow`). 2층 검사가 이 함수로 찾는다. */
	SNAREHAVEN_API FName GetCellWidgetName(const FZoneRow& Row);

	/**
	 * 자리표시 트리 왼쪽 위(`TopLeft`)에 지도 한 장과 존 이름 줄을 얹는다. WBP 에 디자인이 들어오면 안 불린다.
	 * 칸이 없는 맵(목장·검사 월드)이면 아무것도 세우지 않는다. `Sea` 가 지도 그림을 들고 있으면 칸 대신 그 그림 한 장.
	 */
	SNAREHAVEN_API FPlaceholder AddPlaceholder(UWidgetTree& Tree, UCanvasPanel& Root,
		const TArray<const FZoneRow*>& Rows, const FVector2D& TopLeft, const ASeaActor* Sea = nullptr);

	/** 점(플레이어·이벤트)을 월드 위치에 맞춰 옮긴다. 점의 **가운데**가 `WorldToMap` 자리에 온다. */
	SNAREHAVEN_API void MoveDot(UImage& Dot, const FBox2D& Bounds, const FVector& Location);

	/**
	 * 이벤트 표식과 남은 시간 줄을 이벤트 구역에 맞춘다. `Event` 가 없으면(안 떴거나 사라졌으면) 둘 다 접는다.
	 * 남은 시간은 구역의 **서버 시계 값**(`GetLifetimeRemaining`)이다 — 여기서 세지 않는다.
	 */
	/** 좌표 줄을 켜고 끈다. 켜져 있으면 `Location` 좌표로 글자를 바꾼다(같으면 그대로). */
	SNAREHAVEN_API void UpdateCoordinates(UTextBlock* Text, bool bShown, const FVector& Location);

	SNAREHAVEN_API void UpdateEventMarker(UImage* Dot, UTextBlock* Text, const FBox2D& Bounds, const ADangerZoneActor* Event);

	/** 스토리 목표 표식을 `Location` 에 맞춘다. nullptr 이면(상대가 이 맵에 없음) 접는다. */
	SNAREHAVEN_API void UpdateStoryMarker(UImage* Dot, const FBox2D& Bounds, const FVector* Location);

	/**
	 * 보물 표식과 남은 시간 줄. `RemainingSeconds <= 0` 이면 둘 다 접는다. `Location` 이 nullptr 이면
	 * (보물 칸이 다른 맵) 줄만 보인다. 남은 시간은 서버가 준 `expiresUtc` 에서 잰다 — 여기서 정하지 않는다.
	 */
	SNAREHAVEN_API void UpdateTreasureMarker(UImage* Dot, UTextBlock* Text, const FBox2D& Bounds,
		const FVector* Location, double RemainingSeconds);
}
