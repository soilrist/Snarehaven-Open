#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SnarePowerSubsystem.generated.h"

struct FBaseRuleRow;

// DESIGN 8.11 「집 2층·전력」: 땅(텐트) 하나마다 놓인 태양광 키트·배터리·전등을 세어 공급 합 ≥ 소모 합이면
// **밤에** 그 땅의 전등을 전부 켜고, 모자라거나 낮이면 전부 끈다. 비전기 조명(행 기능 `FlameLight`, M108-05e)은 전력 없이
// 밤이면 켠다 — 땅 하나 점광은 전등 + 불꽃 합해 `LandLightMax` 까지(전등 먼저, 같은 무리 안은 가구 ID 순).
// 연료 발전기(M108-07c)는 연료가 남은 동안만 공급에 든다 — 끝 시각은 놓인 액터에 복제된 게임 서버 시계 값.
// 전기 가구(행 `PowerUse`, M108-08c)는 소모 합에 들고 공급이 넉넉하면 **낮밤 없이** 켜진 모습(`SetElectricOn`) — 모자라면 전등과 같이 꺼짐.
//
// 전력은 재화가 아니라 보이는 것이다(8.11 「전력 판정」) — 서버 사본으로 세운 놓인 가구 액터를 그대로 센다.
// 호스트가 복제한 액터는 게스트에게도 있고 밤은 서버 시계(`USnareDayNightSubsystem`)라, 복제할 값 없이
// 모든 기기가 같은 답을 낸다. 판정·지급은 하나도 없다(놓기·치우기는 그대로 `PlaceStructure`·`RemoveStructure`).
UCLASS()
class SNAREHAVEN_API USnarePowerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/**
	 * 월드의 놓인 가구를 땅(주인 + 텐트 ID)마다 세어 전등·전기 가구를 켜고 끈다. 2층 검사가 시계를 기다리지 않고 부른다.
	 * @return 켠 전등 + 불꽃 수
	 */
	static int32 ApplyPower(UWorld* World, const FBaseRuleRow& Rules, bool bNight);

	/**
	 * 땅 하나(주인 + 텐트 ID)의 지금 공급 합·소모 합 — `ApplyPower` 와 같은 셈(연료가 남은 발전기만, 전등 × `LampPowerUse` + 전기 가구 `PowerUse`).
	 * 발전기 판(M108-07c-2)의 「이 땅 전력 공급 / 소모」 줄이 읽는다. 전력 줄이 비면 둘 다 0.
	 */
	static void MeasureLand(UWorld* World, const FBaseRuleRow& Rules, const FString& OwnerPlayFabId, const FString& TentId,
		int32& OutSupply, int32& OutUse);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	float SinceLastCheck = 0.f;
	int32 LastLitCount = -1;
};
