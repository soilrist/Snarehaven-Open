#include "Player/SnareFaceExpression.h"

#include "Player/SnareLowHealth.h"

ESnareFaceExpression SnareFaceExpression::For(float Health, float MaxHealth, bool bDowned, float SecondsSincePoison,
	float SecondsSinceAte)
{
	if (bDowned)
	{
		return ESnareFaceExpression::Down;
	}
	const bool bLowHealth = MaxHealth > 0.f && Health / MaxHealth <= SnareLowHealth::Threshold;
	const bool bPoisoned = SecondsSincePoison >= 0.f && SecondsSincePoison <= PoisonHurtSeconds;
	if (bLowHealth || bPoisoned)
	{
		return ESnareFaceExpression::Hurt;
	}
	if (SecondsSinceAte >= 0.f && SecondsSinceAte <= HappySeconds)
	{
		return ESnareFaceExpression::Happy;
	}
	return ESnareFaceExpression::Neutral;
}

const TCHAR* SnareFaceExpression::NameOf(ESnareFaceExpression Expression)
{
	switch (Expression)
	{
	case ESnareFaceExpression::Hurt: return TEXT("Hurt");
	case ESnareFaceExpression::Down: return TEXT("Down");
	case ESnareFaceExpression::Happy: return TEXT("Happy");
	default: return TEXT("Neutral");
	}
}

bool SnareFaceExpression::IsValidCheckDelay(float Seconds)
{
	return FMath::IsFinite(Seconds) && Seconds > 0.f;
}
