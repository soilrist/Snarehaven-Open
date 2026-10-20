#include "UI/SnareUiFontLibrary.h"

#include "Engine/Font.h"
#include "Engine/FontFace.h"

bool USnareUiFontLibrary::SetFallbackFace(UFont* Font, UFontFace* FallbackFace)
{
	if (!IsValid(Font))
	{
		return false;
	}

	Font->Modify();
	FCompositeFont& Composite = Font->GetMutableInternalCompositeFont();
	Composite.FallbackTypeface.Typeface.Fonts.Reset();
	if (IsValid(FallbackFace))
	{
		// 이름을 기본 타입페이스와 같은 `Default` 로 — 화면 글자가 그 이름으로 찾는다(`SnareUiStyle::ApplyUiStyle`).
		FTypefaceEntry& Entry = Composite.FallbackTypeface.Typeface.Fonts.AddDefaulted_GetRef();
		Entry.Name = TEXT("Default");
		Entry.Font = FFontData(FallbackFace);
	}
#if WITH_EDITORONLY_DATA
	Composite.MakeDirty();   // 열려 있는 에디터의 Slate 글꼴 캐시가 새 칸을 다시 읽게 한다
#endif
	Font->MarkPackageDirty();
	return true;
}
