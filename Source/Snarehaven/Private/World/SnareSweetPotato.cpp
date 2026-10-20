#include "World/SnareSweetPotato.h"
#include "SnareCheckSwitch.h"

#include "Misc/CommandLine.h"

int32 SnareSweetPotato::WeekdayOf(const FDateTime& Utc)
{
	const int64 Day = Utc.ToUnixTimestamp() / 86400;
	return static_cast<int32>((Day + 4) % 7);
}

bool SnareSweetPotato::IsSundayNow()
{
	return SnareCheckSwitch::Param(TEXT("SnareSunday")) || WeekdayOf(FDateTime::UtcNow()) == 0;
}

FText SnareSweetPotato::ClosedSignText()
{
	return NSLOCTEXT("SnareSweetPotato", "ClosedSign", "고구마 장수 달순 · 일요일에 와요");
}

FText SnareSweetPotato::OpenSignText()
{
	return NSLOCTEXT("SnareSweetPotato", "OpenSign", "고구마 장수 달순");
}

FText SnareSweetPotato::ArrivalNoticeText()
{
	return NSLOCTEXT("SnareSweetPotato", "ArrivalNotice", "고구마 장수가 왔어요! 장터 줄 끝으로 가 보세요");
}

FText SnareSweetPotato::CalendarHintText()
{
	return NSLOCTEXT("SnareSweetPotato", "CalendarHint", "일요일 09시~월요일 09시, 목장 장터 줄 끝에 고구마 장수가 와요");
}
