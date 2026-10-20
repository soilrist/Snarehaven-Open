#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "SnareViewFade.generated.h"

// 가림 페이드 — 내려다보는 카메라와 내 캐릭터 사이를 막는 나무 잎·껍질을 디더로 뚫는다.
// 계산은 머티리얼(`M_Nature_MatteFade`, `create_view_fade_material.py`)이 픽셀마다 하고, 여기서는
// 카메라·캐릭터 자리를 머티리얼 파라미터 모음(`MPC_ViewFade`)에 매 프레임 써 준다 — 나무 인스턴스가 몇 천 개여도 액터를 안 돈다.
// 아래 `Coverage` 는 그 머티리얼 HLSL 과 같은 식이라 2층 시험이 가림 판정을 단정한다.
namespace SnareViewFade
{
	/** 카메라→캐릭터 선에서 이 반지름(cm) 안의 면이 뚫린다 — 몸 폭(캡슐 반지름 ≈45)의 5배 남짓, 잎덩이 한 겹이 몸을 다 덮는 폭 */
	constexpr float RadiusCm = 250.f;
	/** 캐릭터 가슴 높이(cm) — 선이 발이 아니라 몸 한가운데를 향하게 */
	constexpr float AimHeightCm = 90.f;
	/** 캐릭터보다 이만큼(cm) 카메라 쪽에 있어야 가림으로 친다 — 발치 땅·풀이 안 뚫린다. 집 뒤 벽 한 겹(두께·기울기로 선 위 Along ≈ 115)도 뚫리도록 40 */
	constexpr float AheadStartCm = 40.f;
	/** AheadStartCm 에서 이만큼(cm) 더 가야 완전히 뚫린다 — 선 따라 부드럽게 */
	constexpr float AheadRampCm = 80.f;
	/** 집 안에 서면 지붕을 통째로 숨긴다 — 지붕 발자국(XY)을 이만큼(cm) 줄인 안쪽이 「안」 */
	constexpr float RoofInsideMarginCm = 30.f;
	/** 선에서 반지름의 이 몫 안쪽은 최대로 뚫리고 바깥 가장자리까지 부드럽게 닫힌다 */
	constexpr float EdgeSoftFraction = 0.4f;
	/** 가장 많이 뚫릴 때 픽셀 몫 — 1 이 아니라서 나무가 아주 사라지지는 않고 어른거린다 */
	constexpr float MaxCoverage = 0.85f;
	/** 켜지고 꺼질 때 걸리는 시간(초) — 0.2 안에 원래대로 */
	constexpr float FadeSeconds = 0.2f;

	/** 점 Point 에서 가림 몫 0~1 (1×MaxCoverage 까지). Strength 는 0~1 켜짐 정도. */
	SNAREHAVEN_API float Coverage(const FVector& Camera, const FVector& Target, const FVector& Point, float Strength);

	/** 폰 XY 가 지붕 발자국 안인지 — Min/Max 는 지붕 액터 경계 XY */
	SNAREHAVEN_API bool IsInsideRoofFootprint(const FVector2D& Min, const FVector2D& Max, const FVector& Pawn);

	/** 켜짐 정도를 FadeSeconds 에 0↔1 을 오가게 목표로 한 걸음 */
	SNAREHAVEN_API float StepStrength(float Current, bool bWant, float DeltaSeconds);

	/** 뚫을지 — 보는 사람이 있고, 스토어 촬영(`-SnareStoreShot=`, 검사 전용)이 아닐 때만 */
	SNAREHAVEN_API bool WantFade(bool bHasViewer, bool bStoreShot);
}

UCLASS()
class SNAREHAVEN_API USnareViewFadeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void UpdateRoofs(const APawn* Pawn);

	float Strength = 0.f;
	bool bRoofsCached = false;
	TArray<TWeakObjectPtr<AActor>> RoofActors;
};
