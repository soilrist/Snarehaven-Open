#pragma once

#include "CoreMinimal.h"
#include "World/ShopVendorActor.h"
#include "Backend/SnareStory.h"
#include "QuestGiverActor.generated.h"

class UWidgetComponent;

// DESIGN 8.14 「의뢰」 (🟡 감독 2026-09-25) — 레퍼런스 마을의 의뢰 NPC 자리. 거점(`L_Ranch`)에 `ARanchGameMode` 가
// 상인 옆에 세운다. 몸·근접 검사는 상인과 같고(좌판만 없다), 다가오면 의뢰 화면(`UQuestScreenWidget`)을 올린다.
// 의뢰 목록·차감·보상은 전부 서버(`GetQuests`/`CompleteQuest`) 몫이다.
UCLASS()
class SNAREHAVEN_API AQuestGiverActor : public AShopVendorActor
{
	GENERATED_BODY()

public:
	AQuestGiverActor();

	/** 3층 캡처용 — `-SnareQuestOpen` 이거나 `-SnareQuestOpen=<탭>` 이면 true (`-이름` 만 찾는 도우미는 `=` 붙은 형태를 못 찾는다). 출시 빌드는 늘 false. */
	static bool IsOpenSwitchSet();

protected:
	virtual void ApplyKindLook() override;
	virtual UUserWidget* CreateScreen(APlayerController* Controller) override;
	virtual void CollapseScreen() override;
	virtual void ExpandScreen() override;

};
