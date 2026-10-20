#pragma once

#include "CoreMinimal.h"
#include "Equipment/EquipmentComponent.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "SnareGearMemorySubsystem.generated.h"

// ROADMAP M15-10 (INBOX 2026-09-25 ④) — **맵을 건너는 장비 기억**.
//
// 왜 필요한가: 존을 옮기면(사냥 섬 ↔ 거점) 폰이 통째로 새로 스폰돼 손에 든 도구·등에 멘 배낭이 사라졌다.
// 모자만 살아남았는데, 그건 서버가 기록하기 때문이다(`GetBaseState` 의 `hat` → `UBackpackComponent::ApplyBackendHat`).
// 도구·배낭은 **겉모습일 뿐이라 서버를 안 탄다**(DESIGN 3.5 · CLAUDE.md 8) — 그래서 기억도 이 기기 안에 둔다.
//
// 게임 인스턴스에 두는 이유는 `USnareBaseSubsystem` 과 같다 — 맵이 바뀌어도 게임 인스턴스는 살아 있다.
// **재화·인벤토리가 아니다**: 여기 든 것은 "무엇을 걸치고 있었나" 뿐이고, 되걸 때도 사람이 누르는 길과
// 같은 `UEquipmentComponent::EquipItem` 을 탄다(게스트면 그 안에서 호스트에게 요청이 올라간다).
UCLASS()
class SNAREHAVEN_API USnareGearMemorySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * 지금 걸친 것을 기억한다 — 맵을 떠나는 순간(`ASnarehavenCharacter::EndPlay`) 부른다.
	 * **모자(`EEquipSlot::Head`)는 빼고** 담는다: 모자는 서버가 기록·복원하므로 여기서도 되걸면
	 * 서버가 "안 썼다"고 한 모자를 화면이 혼자 다시 씌운다.
	 * @param Slots 지금 부위별 장착 목록 (`UEquipmentComponent::GetEquippedSlots`)
	 */
	void RememberSlots(const TArray<FEquippedSlot>& Slots);

	/** 기억한 목록 (한 번도 기억한 적 없으면 빈 배열). */
	const TArray<FEquippedSlot>& GetRememberedSlots() const { return RememberedSlots; }

	/** 기억을 지운다 — 계정이 바뀌거나(로그아웃) 검사가 처음 상태를 만들 때. */
	void Forget();

private:
	/** 마지막으로 걸치고 있던 것. 모자는 안 든다 (위 `RememberSlots` 설명). */
	UPROPERTY()
	TArray<FEquippedSlot> RememberedSlots;
};
