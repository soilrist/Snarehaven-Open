#pragma once

#include "CoreMinimal.h"

class UDataTable;

// 마을에 들어서면 화면 위 가운데에 이름 띠가 3초 뜬다 (사람 지시 2026-10-13 「마을 이름」 ③).
// 어느 마을 안인지는 `DT_OutpostTable`(`FOutpostRow`) 하나만 읽는다 — 자리·이름·반지름을 여기 적지 않는다 (DataDriven.md 1).
// 화면 부분(글자 위젯)은 사냥 HUD 가 `USnareNoticeText` 로 들고 있고, 여기는 **언제 띄우나** 만 정한다.
namespace SnareOutpostBanner
{
	/** 띠가 떠 있는 시간(초). DESIGN 지도 절 마을 이름 표 「이름 띠」 줄. */
	inline constexpr float ShowSeconds = 3.f;

	/** 같은 마을에 다시 들어와도 이 시간(초) 안에는 안 띄운다. DESIGN 같은 표. */
	inline constexpr double RepeatGapSeconds = 60.0;

	/**
	 * 지금 자리가 든 마을 — `EnterRadiusCm` 안이고 가운데에 제일 가까운 곳. 높이(Z)는 보지 않는다.
	 * @return 행 이름(거점 id). 어느 마을도 아니면 NAME_None
	 */
	FName FindEntered(const UDataTable* Table, const FVector& Location);

	/** 「들어섰다」 순간과 다시 띄우기 간격을 세는 곳. 시간은 부르는 쪽이 준다(시험이 가짜 시간을 넣는다). */
	struct FTracker
	{
		/** 지금 안에 있는 마을 (밖이면 NAME_None). */
		FName Inside = NAME_None;

		/** 마을마다 마지막으로 띠를 띄운 시각. */
		TMap<FName, double> LastShown;

		/**
		 * 한 번 부른다. 밖에서 안으로 바뀐 순간에만, 그 마을 띠를 최근 `RepeatGapSeconds` 안에 안 띄웠을 때 참.
		 * 마을 안에 계속 서 있으면 거짓. 마을 사이를 곧장 건너가면 새 마을 이름이 뜬다.
		 * @param OutShow 참일 때 띄울 거점 id
		 */
		bool Update(FName NowInside, double NowSeconds, FName& OutShow);
	};
}
