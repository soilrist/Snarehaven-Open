#pragma once

#include "CoreMinimal.h"
#include "World/ShopVendorActor.h"
#include "SnareHookNpcActor.generated.h"

// 메인 스토리 4장 회색 작살단 두목 「갈고리」 (`docs/STORY.md` 3절 인물 표). 작살단 캠프(`ASnareHarpoonCampActor`)가
// 모닥불 앞에 하나 세운다 — 캠프처럼 각자 자기 화면의 장식이라 복제하지 않는다.
// 몸·모자·머리 위 「!」「?」·돌아보기는 상인 것을 빌리고(STORY.md 「새 모델 최소」), 좌판·화면은 없다 — 파는 것도 여는 판도 없다.
// 말 걸기(18·20단계 대사)는 상황 버튼이 아니라 곁에 서면 뜬다(`SnareStory::ArrivalFor(20)` = 갈고리 곁, 11단계 모래와 같은 결).
UCLASS()
class SNAREHAVEN_API ASnareHookNpcActor : public AShopVendorActor
{
	GENERATED_BODY()

public:
	ASnareHookNpcActor();

	/** 캠프 중심 바닥(`CampGround`)에서 카메라 쪽(−X)으로 이만큼 — 모닥불(가운데)과 우리(−Y)·천막(+Y) 사이 빈 자리. */
	static constexpr float CampOffsetXCm = -220.f;

	/** 캠프가 부른다 — 모닥불 앞에 갈고리를 세운다. 이미 있으면 그것을 돌려준다. */
	static ASnareHookNpcActor* SpawnAtCamp(UWorld* World, const FVector& CampGround);

protected:
	virtual void ApplyKindLook() override;
	/** 열 판이 없다 — 곁에 와도 화면을 올리지 않는다. */
	virtual UUserWidget* CreateScreen(APlayerController* Controller) override;
};
