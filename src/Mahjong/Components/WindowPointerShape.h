#pragma once

#include <BECore/Input/PointerShape.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>

namespace BECore::Mahjong {

    /// The whole output window in scene units, the shape of a pointer target that has no node rect of its own: a press in the bars
    /// beside the view still reaches the target, as a raw mouse event did. Allocates nothing.
    [[nodiscard]] inline PointerShape WholeWindowShape(const Scene& scene, const IRenderer& renderer) {
        const float left = scene.ScreenToSceneX(0.0f);
        const float top = scene.ScreenToSceneY(0.0f);
        const float right = scene.ScreenToSceneX(renderer.GetOutputWidth());
        const float bottom = scene.ScreenToSceneY(renderer.GetOutputHeight());
        return PointerShape::InRect(Rect{left, top, right - left, bottom - top});
    }

}  // namespace BECore::Mahjong
