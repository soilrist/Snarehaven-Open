#include "World/SnareWeather.h"

namespace SnareWeather
{
	ESnareWeather ForSlot(int64 Slot)
	{
		// SplitMix64 — 슬롯 번호를 섞어 0~99 칸으로 (표준 난수기는 기기마다 다를 수 있어 쓰지 않는다).
		uint64 Z = static_cast<uint64>(Slot) + 0x9E3779B97F4A7C15ULL;
		Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ULL;
		Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBULL;
		Z ^= Z >> 31;
		const int32 Roll = static_cast<int32>(Z % 100ULL);
		if (Roll < ClearPercent) return ESnareWeather::Clear;
		if (Roll < ClearPercent + RainPercent) return ESnareWeather::Rain;
		return ESnareWeather::Snow;
	}

	ESnareWeather At(double Seconds)
	{
		return ForSlot(static_cast<int64>(FMath::FloorToDouble(Seconds / SlotSeconds)));
	}

	void Amounts(double Seconds, float& OutRain, float& OutSnow)
	{
		const double SlotPos = FMath::FloorToDouble(Seconds / SlotSeconds);
		const int64 Slot = static_cast<int64>(SlotPos);
		const float In = FMath::SmoothStep(0.f, 1.f, static_cast<float>(FMath::Clamp((Seconds - SlotPos * SlotSeconds) / BlendSeconds, 0.0, 1.0)));
		OutRain = 0.f;
		OutSnow = 0.f;
		const ESnareWeather Now = ForSlot(Slot);
		const ESnareWeather Before = ForSlot(Slot - 1);
		if (Now == ESnareWeather::Rain) OutRain += In;
		if (Now == ESnareWeather::Snow) OutSnow += In;
		if (Before == ESnareWeather::Rain) OutRain += 1.f - In;
		if (Before == ESnareWeather::Snow) OutSnow += 1.f - In;
		OutRain = FMath::Min(OutRain, 1.f);
		OutSnow = FMath::Min(OutSnow, 1.f);
	}

	float ColdDrain(float SnowAmount, bool bUnderRoof, float ZoneDrainPerSecond)
	{
		if (ZoneDrainPerSecond > 0.f || bUnderRoof)
		{
			return FMath::Max(ZoneDrainPerSecond, 0.f);
		}
		return SnowColdPerSecond * FMath::Clamp(SnowAmount, 0.f, 1.f);
	}
}
