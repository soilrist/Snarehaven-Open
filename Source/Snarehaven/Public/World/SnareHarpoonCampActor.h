#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SnareHarpoonCampActor.generated.h"

class UStaticMeshComponent;

// 메인 스토리 4장 「회색 작살단」 캠프 (`docs/STORY.md` 4장). 호스트·게스트 각자 자기 화면에 세우는 장식이다 —
// 재화·보상이 없어 권위 2단과 무관하다. 자리는 `SnareStory::CampSpotX/Y`, 도착 판정은 `SnareStory::ArrivalFor(16)`.
// 모양 = 메시 하나(천막 3·우리 3·모닥불·작살 걸이·그물·덫 상자, 시트 `docs/reference/story_harpoon_camp.md`).
UCLASS()
class SNAREHAVEN_API ASnareHarpoonCampActor : public AActor
{
	GENERATED_BODY()

public:
	ASnareHarpoonCampActor();

	/** 게임모드가 사냥 맵에서 한 번 부른다 — 늪 뭍 해안 바닥에 캠프를 세운다. 이미 있으면 세우지 않는다. */
	static ASnareHarpoonCampActor* SpawnAtSpot(UWorld* World);

	/**
	 * 캠프 중심 기준 월드 천막 자리(cm)·바깥 반경 — 메시 원본 `make_harpoon_camp_mesh.py` TENT_SPOTS·TENT_R 와 같되
	 * OBJ 반입이 Y 를 뒤집어 Y 부호가 반대다(10-19 우리가 천막 위에 박혔던 원인).
	 */
	static const TArray<FVector2D>& TentSpots();
	static constexpr float TentRadiusCm = 205.f;

	/** 캠프 안 흩뿌림 바위를 치우는 반경(cm) — 캠프 한가운데 큰 바위가 서 있던 자리(10-19 감독 ③). */
	static constexpr float ClearRocksRadiusCm = 1000.f;

private:
	static void ClearRocksAround(UWorld* World, const FVector2D& Center);

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> CampMesh;
};
