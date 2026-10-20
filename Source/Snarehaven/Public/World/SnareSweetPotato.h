#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"

// DESIGN 8.17 고구마 시세 — 클라이언트가 아는 것은 「오늘이 일요일인가」 하나뿐이다(고구마 장수를 세울지 말지).
// 요일 계산은 서버 `potatoWeekdayOf(day) = (questDay()+4) % 7` 과 같은 식(UTC 날 번호, 0 = 일요일).
// **보이기만 가른다** — 사기·값은 전부 서버 `GetSweetPotato`·`BuySweetPotato` 가 판정한다(CLAUDE.md 8).
namespace SnareSweetPotato
{
	/** 이 UTC 시각의 요일. 0 = 일요일 … 6 = 토요일. */
	SNAREHAVEN_API int32 WeekdayOf(const FDateTime& Utc);

	/** 지금(UTC)이 일요일인가. 3층 캡처용 `-SnareSunday` 가 있으면 늘 참. */
	SNAREHAVEN_API bool IsSundayNow();

	/** 평일 빈 수레 팻말 글자 (DESIGN 8.17 「평일 팻말」). */
	SNAREHAVEN_API FText ClosedSignText();
	/** 일요일 머리 위 이름 — 평일 팻말과 같은 꼴. */
	SNAREHAVEN_API FText OpenSignText();

	/** 일요일에 목장에 들어오면 한 번 띄우는 알림 글자 (DESIGN 8.17 「일요일 알림」). */
	SNAREHAVEN_API FText ArrivalNoticeText();

	/** 출석 달력 판 아래 안내 글자 (DESIGN 8.17 「달력 안내」). */
	SNAREHAVEN_API FText CalendarHintText();

	/** 장수가 파는 묶음 하나의 개수 (서버 `POTATO_BUNDLE`). */
	constexpr int32 BundleSize = 10;
}
