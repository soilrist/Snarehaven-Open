#pragma once

#include "CoreMinimal.h"
#include "ItemAction.generated.h"

/**
 * 아이템을 쓸 때 한 번 트는 몸 동작 (DESIGN 8.8 「아이템 사용 모션」) + HUD 이모트 (DESIGN 2.2 「이모트」).
 * RPC 로 남의 화면에 보내려고 UENUM 이다. 둘 다 그림뿐이라 같은 길을 탄다.
 */
UENUM(BlueprintType)
enum class EItemAction : uint8
{
	Eat,     // 먹기·마시기 — `A_Player_Eat`
	Gather,  // 맨손 줍기(열매·버섯·돌) — `A_Player_Gather`
	Chop,    // 손도끼 나무 베기 — `A_Player_Chop`(허리 높이 수평 휘두르기, 없으면 공격 클립)
	Wave,    // 이모트 인사 — `A_Player_Wave` 한 번
	Cheer,   // 이모트 만세 — `A_Player_Cheer` 한 번
	Dance,   // 이모트 춤 — `A_Player_Dance` 움직일 때까지 되풀이
	Sit,     // 이모트 앉기 — 배 앉기 클립(`A_Player_BoatSit`, 바닥에 앉은 자세) 움직일 때까지 되풀이
	Mine,    // 곡괭이 광석 캐기 — 공격 클립(머리 위에서 내리찍기) 한 바퀴. 복제되는 값이라 끝에 붙인다
	None,    // 쓸 때 몸 동작이 없다(표 칸 `UseAction` 의 기본값). 복제되는 값이라 끝에 붙인다 — 맨손 줍기는 `Gather`
	Smash,   // 손도끼 내려치기 — 공격 클립(머리 위에서 앞 바닥으로). `Mine` 과 같은 클립이지만 곡괭이 문구·판정은 타지 않는다. 복제되는 값이라 끝에 붙인다
	Lasso,   // 올가미 던져 당기기(포획 누름) — `A_Player_FishCast`(한 팔 머리 위→앞으로 던졌다 당겨 옴 0.47초) 재사용. 복제되는 값이라 끝에 붙인다
};
