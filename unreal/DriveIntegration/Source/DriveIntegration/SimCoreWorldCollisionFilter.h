#pragma once
#include "Components/PrimitiveComponent.h"
#include "Debug/DebugDrawComponent.h"

namespace SimCoreWorldQuery {
// Gameplay debugger draw components inherit query defaults and expose enormous
// render bounds, but have no collision geometry. Do not generalize this to
// unknown primitives: a debug component with a real body must still be queried.
inline bool IsRenderOnlyDebugComponent(UPrimitiveComponent* Component)
{
    return Component && Component->IsA<UDebugDrawComponent>()
        && !Component->GetBodySetup() && !Component->BodyInstance.IsValidBodyInstance();
}
}
