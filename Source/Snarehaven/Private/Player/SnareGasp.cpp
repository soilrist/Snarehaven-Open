#include "Player/SnareGasp.h"

namespace
{
	// 숨참 소리 (DESIGN 2.2 M56-00) — 크기 0.7 · 되풀이 간격 2.0초.
	constexpr float GaspVolume = 0.7f;
	constexpr float GaspMinGapSeconds = 2.f;
}

SnareSound::FChangeCue SnareGasp::CueFor(bool bInWater, float Stamina, float SecondsSinceLast)
{
	SnareSound::FChangeCue Out;
	if (!bInWater || Stamina > EmptyStamina || SecondsSinceLast < GaspMinGapSeconds)
	{
		return Out;
	}
	Out.Cue = TEXT("Gasp");
	Out.Volume = GaspVolume;
	Out.MinGapSeconds = GaspMinGapSeconds;
	Out.bWorld = true;
	return Out;
}
