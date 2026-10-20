#include "Equipment/SnareGearMemorySubsystem.h"

#include "Snarehaven.h"

void USnareGearMemorySubsystem::RememberSlots(const TArray<FEquippedSlot>& Slots)
{
	RememberedSlots.Reset();
	for (const FEquippedSlot& Entry : Slots)
	{
		// 빈 줄(부위만 있고 행 이름이 없는 것)은 "벗었다" 라서 되걸 것이 없다.
		if (Entry.Slot == EEquipSlot::None || Entry.Slot == EEquipSlot::Head || Entry.ItemRowName.IsNone())
		{
			continue;
		}
		RememberedSlots.Add(Entry);
	}

	UE_LOG(LogSnarehaven, Log, TEXT("[Gear] 맵을 건널 장비 기억 - %d칸"), RememberedSlots.Num());
}

void USnareGearMemorySubsystem::Forget()
{
	RememberedSlots.Reset();
}
