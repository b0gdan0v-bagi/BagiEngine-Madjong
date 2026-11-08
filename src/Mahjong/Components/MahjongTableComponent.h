#pragma once

#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Scene/IComponent.h>
#include <Mahjong/Table/TableLayout.h>

namespace BECore::Mahjong {

    /**
     * @brief Draws the table of the active `MahjongGame`: the felt, the four hands, the open sets, the bonus tiles, the discards and
     *        the wall. It only reads the game; it never changes it.
     *
     * Every frame the component builds a `TableInput` from the model and the session, lays it out (`MakeTableLayout`, for the view size
     * of its scene) and draws the slots with the tile atlas (`Table/TileAtlas.h`, loaded once in `OnAttached`). Nothing is drawn but the
     * felt when the active game is not Mahjong or its match was not started.
     *
     * **No peeking.** While the hand is played only the concealed tiles of the viewer (`TableSession::Viewer()`) are drawn face up, and
     * none at all while `HandoffPending()` (a hot-seat cover). The other seats show backs, their concealed kongs too. When the hand is
     * over (`SessionPhase::HandOver` and `MatchOver`) all four hands are face up. Open sets, bonus tiles and discards are public.
     *
     * Without an atlas texture (the file did not load, or a test did not give one) the tiles are coloured rectangles, so the layout
     * stays visible. The acting seat's hand has a highlight behind it and the discard that waits for a claim has one too.
     */
    class MahjongTableComponent : public IComponent {
        BE_CLASS(MahjongTableComponent)
    public:
        MahjongTableComponent() = default;
        ~MahjongTableComponent() override;

        void OnAttached() override;
        void OnDraw() override;

        /// Draws with `texture` (made from pixels, `IRenderer::CreateTexture()`) instead of the atlas from the file; a test hands one.
        void SetAtlasTexture(IntrusivePtr<ITexture> texture);

        /// The layout of the last `OnDraw` in the view units of the scene (empty before the first draw and while nothing is drawn).
        [[nodiscard]] const TableLayout& GetLayout() const noexcept {
            return _layout;
        }

        /// The attached table, if any; the game assumes a single one.
        static MahjongTableComponent* GetActive() noexcept {
            return s_active;
        }

    private:
        ResourceHandle<ITexture> _atlas;
        TableLayout _layout;
        static MahjongTableComponent* s_active;
    };

}  // namespace BECore::Mahjong
