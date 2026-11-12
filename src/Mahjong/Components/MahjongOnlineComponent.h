#pragma once

#include <BECore/Scene/IComponent.h>
#include <Mahjong/Online/OnlineScreens.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }

    namespace SDLEvents {
        struct KeyDownEvent;
    }

    namespace SceneEvents {
        struct SceneUpdateEvent;
    }

}  // namespace BECore

namespace BECore::Mahjong {

    class MahjongGame;

    /**
     * @brief The online panel: the nodes `Online` and `OnlineEntry` (`config/nodes/MahjongOnline.xml`, `MahjongOnlineEntry.xml`) show what this component publishes and emit the
     *        actions it answers. The pages, the fields and the pad are `OnlineScreens` (pure); the client is `MahjongGame::GetOnline()`. This component only shows the one and
     *        feeds the other.
     *
     * **Frame.** Every `SceneUpdateEvent` (the active game only, whether or not the panel is open) calls `OnlineClient::Update()`, `MahjongGame::SyncOnlineTable()` (the room's table, which closes the
     * panel when it goes live) and `OnlineScreens::Sync`, so a room is kept alive and a reconnect runs behind the table. The save flush stays the HUD's.
     *
     * **Open.** `MahjongGame::IsOnlineOpen()` is the owner of the state: the `Online` button of `Setup` (`MahjongOnline`, only while `Setup` is open, never in the browser build)
     * sets it and this component opens the pages; `Start()` and `Stop()` of the game clear it and the pages close. `OnlineClose` hides the panel and leaves the room as it is. The
     * canvas `Online` shows the page `Menu`, `Host`, `Join` or `Lobby`, the canvas `OnlineEntry` the page `Entry` (the first one is hidden meanwhile: the second has a dim of its own).
     *
     * **Input.** The actions of the panel (`MahjongOnlineHost` ... `MahjongOnlineLeave`) are ignored while it is closed, and each asks `OnlineScreens` again what is on offer, so a
     * stale click does nothing. On the `Entry` page the keyboard types too (`SDLEvents::KeyDownEvent`: Latin letters, digits, space, Backspace (may repeat), Enter = Done); Escape
     * goes one page back, and in the `Lobby` does nothing. The name the player typed is handed to `MahjongGame::SetOnlineName` once they leave the field or send a form.
     *
     * **Variables** (set only when `OnlineScreens::Version()`, `OnlineClient::Version()`, the version of `UIStrings` or the open state moved; the elements likewise): `onlineNotice`,
     * `onlineStatus`, `onlineName`, `onlineCode`, `onlineRoom`, `onlineSeat0..3`, `onlineHands`, `onlineLevel`, `onlineHints` (`allowed` | `off`), `onlineHostLine`, `onlineEntryValue` and
     * `padKey0..47` (while the `Entry` page is up and the pad, its field or the language moved). The token of a session is never published.
     */
    class MahjongOnlineComponent : public IComponent {
        BE_CLASS(MahjongOnlineComponent)
    public:
        MahjongOnlineComponent() = default;
        ~MahjongOnlineComponent() override;

        void OnAttached() override;
        void OnDraw() override;

        /// The pages the panel shows (read-only, for tests).
        [[nodiscard]] const OnlineScreens& Screens() const noexcept {
            return _screens;
        }

        /// The attached component, if any; the game assumes a single one.
        static MahjongOnlineComponent* GetActive() noexcept {
            return s_active;
        }

    private:
        void OnAction(const InputEvents::ActionEvent& event);
        void OnKeyDown(const SDLEvents::KeyDownEvent& event);
        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);

        /// Opens or closes the pages after the flag of the game.
        void Follow(MahjongGame& game);
        void OpenPages(MahjongGame& game);
        /// Hands the name the player typed to the game, which saves it.
        void StoreName(MahjongGame& game) const;
        /// Shows or hides the canvases and the page, and sets the variables, when something moved since the last time.
        void Publish(MahjongGame* game);
        void PublishVariables(const OnlineClient& client, const UIStrings& strings);
        void PublishPages(const OnlineClient& client);
        void PublishPad(const UIStrings& strings);
        void SetItem(size_t item, bool visible);
        void SetPadKey(size_t key, bool visible);
        void SetPadRow(size_t row, bool visible);

        /// The elements the pages show among themselves (`Item` in the source file).
        static constexpr size_t kItemCount = 32;

        OnlineScreens _screens;
        PoolString _seatVars[kSeatCount];
        PoolString _padVars[TextEntry::kPadMaxKeys];
        PoolString _padKeyNames[TextEntry::kPadMaxKeys];
        PoolString _padRowNames[TextEntry::kPadRowCount];
        PoolString _itemNames[kItemCount];
        // What the scene shows now: -1 not known yet, else 0 / 1
        int8_t _items[kItemCount];
        int8_t _padKeys[TextEntry::kPadMaxKeys];
        int8_t _padRows[TextEntry::kPadRowCount];
        bool _onlineShown = false;
        bool _entryShown = false;
        bool _setupButtonChecked = false;
        bool _closeRequested = false;  // Escape closed the Menu: the game's flag follows at the next `Follow`
        const MahjongGame* _publishedFor = nullptr;
        uint64_t _publishedScreens = 0;
        uint64_t _publishedClient = 0;
        uint64_t _publishedStrings = 0;
        bool _published = false;
        bool _padPublished = false;
        OnlineField _padField = OnlineField::Name;
        uint64_t _padStrings = 0;
        static MahjongOnlineComponent* s_active;
    };

}  // namespace BECore::Mahjong
