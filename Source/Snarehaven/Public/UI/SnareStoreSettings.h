#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SnareStoreSettings.generated.h"

/**
 * 스토어 출시 설정 (INBOX 지시 M94 ④). 1.0 은 서버에 젬(`GM`)을 주는 길도, 젬 판매도 없다 —
 * 젬 잔액 칸·젬 뽑기·옷장 상점·상자 열쇠 젬 가격은 영원히 못 누르는 막다른 칸이라 애플 심사 2.1 위험이다.
 * `DefaultGame.ini` 의 `bGemsEnabled=False` 로 화면에서 전부 숨긴다. 서버는 그대로 두고, 젬 IAP 업데이트 때 켜기만 한다.
 */
UCLASS(Config = Game)
class SNAREHAVEN_API USnareStoreSettings : public UObject
{
	GENERATED_BODY()

public:
	/** 꺼지면 젬 글자·젬 버튼·젬 상점 입구를 화면에 만들지 않거나 숨긴다. 코인으로 되는 것은 그대로. */
	UPROPERTY(Config, EditAnywhere, Category = "Store")
	bool bGemsEnabled = true;

	/** 젬 화면을 보여도 되나 — 화면 코드는 설정 객체를 직접 만지지 말고 여기만 부른다. */
	static bool AreGemsEnabled();
};

/** 2층 검사 전용 — 범위가 끝나면 젬 설정을 원래대로 돌린다. 화면 코드는 쓰지 않는다. */
struct FScopedGemsEnabled
{
	explicit FScopedGemsEnabled(bool bEnabled)
		: Previous(GetDefault<USnareStoreSettings>()->bGemsEnabled)
	{
		GetMutableDefault<USnareStoreSettings>()->bGemsEnabled = bEnabled;
	}
	~FScopedGemsEnabled()
	{
		GetMutableDefault<USnareStoreSettings>()->bGemsEnabled = Previous;
	}
	bool Previous;
};
