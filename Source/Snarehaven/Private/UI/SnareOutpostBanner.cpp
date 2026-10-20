#include "UI/SnareOutpostBanner.h"

#include "Data/OutpostRow.h"
#include "Engine/DataTable.h"

FName SnareOutpostBanner::FindEntered(const UDataTable* Table, const FVector& Location)
{
	if (Table == nullptr)
	{
		return NAME_None;
	}
	FName Best = NAME_None;
	double BestDistSq = TNumericLimits<double>::Max();
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FOutpostRow* const Row = reinterpret_cast<const FOutpostRow*>(Pair.Value);
		if (Row == nullptr || Row->EnterRadiusCm <= 0.f)
		{
			continue;
		}
		const double DistSq = FVector2D::DistSquared(FVector2D(Location.X, Location.Y), FVector2D(Row->XCm, Row->YCm));
		const double Radius = Row->EnterRadiusCm;
		if (DistSq <= Radius * Radius && DistSq < BestDistSq)
		{
			Best = Pair.Key;
			BestDistSq = DistSq;
		}
	}
	return Best;
}

bool SnareOutpostBanner::FTracker::Update(FName NowInside, double NowSeconds, FName& OutShow)
{
	if (NowInside == Inside)
	{
		return false;
	}
	Inside = NowInside;
	if (NowInside.IsNone())
	{
		return false;
	}
	const double* const Last = LastShown.Find(NowInside);
	if (Last != nullptr && NowSeconds - *Last < RepeatGapSeconds)
	{
		return false;
	}
	LastShown.Add(NowInside, NowSeconds);
	OutShow = NowInside;
	return true;
}
