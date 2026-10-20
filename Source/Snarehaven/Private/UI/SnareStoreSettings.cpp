#include "UI/SnareStoreSettings.h"

bool USnareStoreSettings::AreGemsEnabled()
{
	return GetDefault<USnareStoreSettings>()->bGemsEnabled;
}
