#pragma once

#include "CoreMinimal.h"

class UButton;
class UTextBlock;
class UWidgetTree;
class UZoneTravelComponent;

// ROADMAP M6-03b: 사냥 HUD·거점 HUD **두 화면**이 포탈 이동 버튼을 같은 글·같은 규칙으로 쓰게 하는 곳.
// `SnareBalanceHud` 와 같은 취지고 같은 순서다 — 부르는 데가 둘이 되는 바로 그 조각에서 나눈다
// (루프#104 가 "두 번째 부르는 데가 생기면" 이라고 미뤄 둔 자리).
//
// **여기에 맵을 여는 길은 없다.** 버튼을 띄울지 말지는 `UZoneTravelComponent::CanTravel()` 하나가
// 정하고, 실제 이동은 호스트의 `AZonePortalActor::TravelToTargetZone()` 뿐이다
// (DESIGN 4.1 권위 표 · M6-02a·b). 화면은 누구를 어디로 보낼지 고르지 않는다.
namespace SnareZoneTravelHud
{
	/** 버튼 높이 바닥 — DESIGN 2 「🟡 누르는 칸 최소 크기」 72 (ROADMAP M38-02). 두 화면이 같은 값을 쓴다. */
	inline constexpr float MinButtonHeight = 72.f;

	/** 버튼을 띄울 상태인가 — 갈 곳이 확정된 포탈이 사거리 안에 있을 때만 참이다. */
	bool IsButtonVisible(const UZoneTravelComponent* Travel);

	/**
	 * 버튼 글자 — `거점존 이동` 꼴. 갈 곳 이름은 표(`DT_ZoneTable`)가 정한다 (DataDriven.md 1).
	 * @return 글자. 고른 포탈이 없거나 꺼져 있으면 빈 FText (그때는 버튼도 접힌다)
	 */
	FText FormatButtonText(const UZoneTravelComponent* Travel);

	/**
	 * 자리표시 버튼 한 벌(버튼 + 가운데 정렬 글자)을 만들어 돌려준다. WBP 에 디자인이 들어오면 안 불린다.
	 * **자리는 여기서 정하지 않는다** — 사냥 화면은 왼쪽 위 장비 버튼 아래, 거점 화면은 칸 목록 아래라
	 * 붙일 데가 다르다 (잔액 줄과 다른 점이다 — 그건 두 화면에서 같은 오른쪽 위였다).
	 * 글자는 최소 높이 `MinButtonHeight` 상자 안에 든다 — 자동 크기 칸(거점 화면)에서도 버튼이 72 아래로 안 준다.
	 * @param OutText 만들어진 글자 위젯 (부르는 쪽이 들고 있다가 `RefreshDisplay` 에서 글자를 갈아 끼운다)
	 */
	UButton* CreateButton(UWidgetTree& Tree, UTextBlock*& OutText);
}
