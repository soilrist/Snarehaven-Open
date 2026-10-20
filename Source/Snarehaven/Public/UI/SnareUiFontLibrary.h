#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SnareUiFontLibrary.generated.h"

class UFont;
class UFontFace;

// ROADMAP M13-27: UI 글꼴(`Font_UI_Jua`)의 **대체 글꼴 칸**을 에디터 파이썬이 채우는 입구.
// 복합 글꼴(`FCompositeFont`)의 대체 타입페이스 칸은 `UPROPERTY()` 뿐이라 파이썬이 직접 못 만진다
// (`PyGenUtil::ShouldExportEditorOnlyProperty` 는 `CPF_Edit` 만 내준다). 그래서 여기서 대신 쓴다.
// 부르는 곳: `tools/unreal/setup_ui_font_fallback.py`.
UCLASS()
class SNAREHAVEN_API USnareUiFontLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * `Font` 의 대체 타입페이스를 `FallbackFace` 한 얼굴로 바꾼다. 기본 얼굴(Jua)에 없는 글자는 Slate 가
	 * 이 얼굴로 그린다(`FCompositeFontCache::GetFontDataForCodepoint`). nullptr 이면 대체 칸을 비운다(역검증용).
	 * 저장은 부른 쪽이 한다. 글꼴이 없으면 false.
	 */
	UFUNCTION(BlueprintCallable, Category = "Snarehaven|UI")
	static bool SetFallbackFace(UFont* Font, UFontFace* FallbackFace);
};
