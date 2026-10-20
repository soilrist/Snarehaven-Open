#pragma once

#include "CoreMinimal.h"

class URanchComponent;

#if !UE_BUILD_SHIPPING
// 3층 캡처 `-SnareRanchDemo` 전용 — 로그인 없이 동물 칸 6곳을 채운 모양을 띄운다 (스토어 스크린샷 ① 목장).
// 시간·티커를 쓰는 이 코드는 `RanchSyncComponent.cpp` 밖에 둔다 — 그 파일은 `Ranch.Slot.NoClientSideTimer` 가 훑는다.
namespace SnareRanchDemoFill
{
	void Schedule(URanchComponent* Ranch);
}
#endif
