#pragma once

#include "CoreMinimal.h"

// M15-22: 화면 한 장 안의 나무·바위 밀도를 숫자로 잰다 — 레퍼런스 시트(`docs/reference/world_environment.md`
// 「배치 밀도」: 화면당 나무 10~20 그루 · 바위 무리 3~5 · 빈 공간 절반 이상)와 맞는지 보는 자다.
// 캡처 픽셀이 아니라 **액터·인스턴스 자리**로 센다(상시 지시 2026-09-23). 게임 쪽 표본 수집은 이 함수를 부른다.
namespace SnareWorldDensity
{
	enum class EKind : uint8
	{
		Other,
		Tree,
		Rock,
	};

	struct FPlacement
	{
		EKind Kind = EKind::Other;
		FVector2D Location = FVector2D::ZeroVector;
	};

	struct FResult
	{
		int32 Trees = 0;
		int32 RockClusters = 0;
		/** 화면 바닥 중 나무·바위 발밑 반경에 안 닿는 몫 (0~1). */
		float EmptyShare = 1.0f;
	};

	/** 시트의 폭 (근거: `world_environment.md` 「배치 밀도」). */
	constexpr int32 SheetTreesMin = 10;
	constexpr int32 SheetTreesMax = 20;
	constexpr int32 SheetRockClustersMin = 3;
	constexpr int32 SheetRockClustersMax = 5;
	constexpr float SheetEmptyShareMin = 0.5f;

	/** 바위끼리 이만큼(cm) 안이면 한 무리다. 🟡 캐릭터 키(약 180cm)의 두 배 남짓 — 붙어 보이는 거리. */
	constexpr float RockClusterGapCm = 400.0f;
	/** 나무·바위 한 개가 바닥을 가리는 반경(cm). 🟡 메시 폭 절반 근사 — 정밀한 발자국이 아니라 자 눈금이다. */
	constexpr float FootprintRadiusCm = 150.0f;

	/** 메시 이름으로 종류를 가른다: `Tree`·`Pine`·`Birch` → 나무, `Rock`·`Boulder` → 바위, 나머지(덤불·풀·그루터기 …) 기타. */
	SNAREHAVEN_API EKind ClassifyMeshName(const FString& MeshName);

	/** `View`(바닥 평면 사각형, cm) 안의 자리만 세어 나무 수·바위 무리 수·빈 공간 몫을 낸다. */
	SNAREHAVEN_API FResult Measure(const TArray<FPlacement>& Placements, const FBox2D& View);

	/** 세 값이 모두 시트의 폭 안인가. */
	SNAREHAVEN_API bool IsWithinSheet(const FResult& Result);

	/**
	 * 실맵 실측 (`-SnareMeasureDensity` 스위치가 부른다). 월드의 정적 메시 액터·인스턴스 메시 자리를 모아
	 * 플레이어 카메라 화면 네 모서리가 바닥(Z=0)에 닿는 사각형으로 `Measure` 를 부르고 숫자를 로그로 찍는다.
	 * 화면이 없으면(헤드리스) 아무것도 안 하고 false.
	 */
	SNAREHAVEN_API bool LogWorldMeasure(APlayerController* Controller);
}
