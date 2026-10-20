#pragma once

#include "CoreMinimal.h"
#include "SnarehavenGameMode.h"
#include "RanchGameMode.generated.h"

// ROADMAP M2-04c: 목장(`L_Ranch`)의 규칙. 사냥 맵과 다른 점은 지금 하나뿐이다 — **사냥 HUD 를 띄우지 않는다**
// (감독 지시 2026-09-19 · DESIGN 3.2 "슬롯 상호작용"). 목장엔 동물이 없어 포획 버튼이 뜰 이유가 없다.
// 이동용 가상 조이스틱은 HUD 위젯이 아니라 엔진 터치 인터페이스(`Config/DefaultInput.ini`)라 그대로 남는다.
//
// 클래스를 따로 둔 이유: 맵 이름 문자열로 가르면 목장 맵이 늘 때마다 조건문이 는다.
// 레벨의 월드 세팅이 이 클래스를 가리키므로, 어떤 이름의 맵이든 "목장 규칙" 을 쓰면 여기로 온다.
// 목장 HUD(Ready 아이콘·미수확 개수)는 M2-06a 에서 이 클래스가 `bShowRanchHud` 로 켠다.
UCLASS()
class SNAREHAVEN_API ARanchGameMode : public ASnarehavenGameMode
{
	GENERATED_BODY()

public:
	ARanchGameMode();

	/** 상인이 설 자리·방향 (캡슐 중심). `-SnareShopOpen` 3층 캡처면 시작점 옆 자리. 검사도 같은 값을 읽는다. */
	static FTransform GetVendorSpawnTransform(bool bShopCheck);

	/** 잡화점·전문 상인 셋·의뢰인을 세운다. `BeginPlay` 가 부르고, 길 막힘 검사도 같은 좌판을 세워 잰다. */
	static void SpawnVendors(UWorld* World);

	/** 나무 자리 `Slot`(0~3) 의 바닥 위치 (DESIGN 8.18 🟡 — 목장 오른쪽 울타리 밖 풀밭). */
	static FVector GetFruitTreeSpot(int32 Slot);

	/**
	 * 과일나무 자리 4칸을 세운다(`AFruitTreeActor`). 서버 답을 받기 전엔 빈 자리 그림이다.
	 * 3층 캡처 `-SnareFruitTrees=gseg` (자리별 한 글자 g 다 자람 · s 묘목 · e 빈 자리) 면 그 모양으로 세운다.
	 */
	static void SpawnFruitTrees(UWorld* World);

protected:
	/** 거점 잡화점 상인을 세운다 (DESIGN 8.14 — 레벨 파일을 안 건드리고 코드로). 권위 쪽만. */
	virtual void BeginPlay() override;
};
