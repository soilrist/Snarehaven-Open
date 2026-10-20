#include "Ranch/RanchDemoFill.h"
#include "Containers/Ticker.h"
#include "Ranch/RanchComponent.h"

#if !UE_BUILD_SHIPPING
void SnareRanchDemoFill::Schedule(URanchComponent* Ranch)
{
	// 칸을 듣는 쪽이 다 묶인 뒤에 알려야 해서 잠깐 기다린다. 실제 계정 값이 아니라 보는 그림일 뿐이다.
	const TWeakObjectPtr<URanchComponent> WeakRanch(Ranch);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakRanch](float)
	{
		URanchComponent* const Station = WeakRanch.Get();
		if (Station == nullptr)
		{
			return false;
		}
		static const TCHAR* const Demo[] = {TEXT("animal.pig"), TEXT("animal.goat"), TEXT("animal.cow"),
			TEXT("animal.peacock"), TEXT("animal.pheasant"), TEXT("animal.fox")};
		TArray<FRanchSlotState> Slots;
		Slots.SetNum(Station->GetSlotCount());
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			Slots[Index].Kind = ERanchSlotKind::AnimalProduct;
			if (Index < UE_ARRAY_COUNT(Demo))
			{
				Slots[Index].AnimalID = FName(Demo[Index]);
				Slots[Index].OccupiedSinceUtc = FDateTime::UtcNow() - FTimespan::FromHours(2);
				Slots[Index].Stock = 2 + Index % 3;
			}
		}
		Station->ApplyBackendSnapshot(Slots);
		return false;
	}), 2.0f);
}
#endif
