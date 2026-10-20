#include "Player/SnareBodyHaptic.h"

SnareBodyHaptic::FCue SnareBodyHaptic::CueFor(EEvent Event, float SecondsSinceLast)
{
	if (SecondsSinceLast < MinGapSeconds)
	{
		return FCue();
	}
	FCue Cue;
	switch (Event)
	{
	case EEvent::Hurt:
		Cue.Intensity = HurtIntensity;
		Cue.Seconds = HurtSeconds;
		break;
	case EEvent::Down:
		Cue.Intensity = DownIntensity;
		Cue.Seconds = DownSeconds;
		break;
	case EEvent::Heartbeat:
		Cue.Intensity = HeartbeatIntensity;
		Cue.Seconds = HeartbeatSeconds;
		break;
	default:
		break;
	}
	return Cue;
}

SnareBodyHaptic::FCue SnareBodyHaptic::CueFor(EGun Gun, float SecondsSinceLastShot)
{
	if (SecondsSinceLastShot < ShotMinGapSeconds)
	{
		return FCue();
	}
	FCue Cue;
	switch (Gun)
	{
	case EGun::Pistol:
		Cue.Intensity = PistolShotIntensity;
		Cue.Seconds = PistolShotSeconds;
		break;
	case EGun::Rifle:
		Cue.Intensity = RifleShotIntensity;
		Cue.Seconds = RifleShotSeconds;
		break;
	default:
		break;
	}
	return Cue;
}

SnareBodyHaptic::FCue SnareBodyHaptic::CueFor(EStrike Strike, float SecondsSinceLastStrike)
{
	if (SecondsSinceLastStrike < StrikeMinGapSeconds)
	{
		return FCue();
	}
	FCue Cue;
	switch (Strike)
	{
	case EStrike::Melee:
		Cue.Intensity = MeleeStrikeIntensity;
		Cue.Seconds = MeleeStrikeSeconds;
		break;
	case EStrike::Gather:
		Cue.Intensity = GatherStrikeIntensity;
		Cue.Seconds = GatherStrikeSeconds;
		break;
	default:
		break;
	}
	return Cue;
}

SnareBodyHaptic::FCue SnareBodyHaptic::CueFor(EEat Eat, float SecondsSinceLastEat)
{
	if (SecondsSinceLastEat < EatMinGapSeconds)
	{
		return FCue();
	}
	FCue Cue;
	switch (Eat)
	{
	case EEat::Food:
		Cue.Intensity = EatIntensity;
		Cue.Seconds = EatSeconds;
		break;
	case EEat::Poison:
		Cue.Intensity = PoisonIntensity;
		Cue.Seconds = PoisonSeconds;
		break;
	default:
		break;
	}
	return Cue;
}
