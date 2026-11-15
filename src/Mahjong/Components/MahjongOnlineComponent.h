#pragma once

#include <BECore/Scene/IComponent.h>
#include <Mahjong/Online/OnlineScreens.h>

namespace BECore {

    namespace ClipboardEvents {
        struct PasteEvent;
    }

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
     *        feeds the other. Every callback runs on the main thread (scene and clipboard events). The panel as a whole: `src/Mahjong/Online/CLAUDE.md`, "The screens".
     *
     * **Frame.** Every `SceneUpdateEvent` of the active game, whether or not the panel is open, calls `OnlineClient::Update()`, `MahjongGame::SyncOnlineTable()`
     * (which closes the panel when the room's table goes live) and `OnlineScreens::Sync`, so a room is kept alive and a reconnect runs behind the table.
     *
     * **Open.** `MahjongGame::IsOnlineOpen()` owns the state; this component opens and closes the pages after it. `OnlineClose` hides the panel and leaves the room as it
     * is. `Online` is hidden while `OnlineEntry` is up: the second has a dim of its own.
     *
     * **Input.** Each action asks `OnlineScreens` again what is on offer, so a stale click does nothing. A key held with Ctrl or Cmd types nothing (the paste shortcut
     * reaches here as a `v`). The name the player typed goes to `MahjongGame::SetOnlineName` once they leave the field or send a form.
     *
     * **The nickname.** The same pad types the tester's nickname (`OpenNicknameEntry`): a `TextEntry` of its own, no `OnlineScreens` page. The pad goes at the next frame
     * after `Done` or Escape, because the HUD hears the same Escape and must find it still up; it also goes, with nothing stored, when `Options` closes under it.
     *
     * **Clipboard.** A paste read cannot be cancelled and is answered from a later `Update()`, so its callback holds a `weak_ptr` of `_alive` and finds the component
     * again through `GetActive()`: a scene torn down with a read pending leaves nothing to call.
     *
     * **Variables** are set only when `OnlineScreens::Version()`, `OnlineClient::Version()`, the version of `UIStrings` or the open state moved; the elements likewise.
     * A host's name is a value of a text, never the text. The token of a session is never published.
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

        /// Opens the pad on the tester's nickname (the row of `Options`) with @p current typed; false unless `Options` is open and the pad is not
        /// already up. `Done` and Escape hand the text to `MahjongUploadComponent::SetNickname`.
        bool OpenNicknameEntry(StringView current);
        /// Whether the pad is up for the nickname (until the frame after `Done` or Escape, so the HUD, which hears the same Escape, leaves `Options` open).
        [[nodiscard]] bool IsNicknameEntryOpen() const noexcept {
            return _nicknameActive;
        }

        static MahjongOnlineComponent* GetActive() noexcept {
            return s_active;
        }

    private:
        void OnAction(const InputEvents::ActionEvent& event);
        void OnKeyDown(const SDLEvents::KeyDownEvent& event);
        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);
        void OnPaste(const ClipboardEvents::PasteEvent& event);
        void OnClipboardAnswer(const ClipboardText& answer);
        void RequestPaste();

        void OnNicknameKey(MahjongGame& game, const SDLEvents::KeyDownEvent& event);
        void OnNicknameAction(MahjongGame& game, StringView action, StringView value);
        void FinishNickname();
        void PublishNickname(const UIStrings& strings);

        void Follow(MahjongGame& game);
        void OpenPages(MahjongGame& game);
        void StoreName(MahjongGame& game) const;
        void Publish(MahjongGame* game);
        void PublishVariables(const OnlineClient& client, const UIStrings& strings);
        void PublishPages(const OnlineClient& client);
        void PublishPad(const UIStrings& strings);
        void PublishRooms(const OnlineClient& client, const UIStrings& strings);
        void SetItem(size_t item, bool visible);
        void SetPadKey(size_t key, bool visible);
        void SetPadRow(size_t row, bool visible);

        /// The elements the pages show among themselves (`Item` in the source file).
        static constexpr size_t kItemCount = 46;

        OnlineScreens _screens;
        /// Alive while the component is attached; a read of the clipboard that is still pending holds a `weak_ptr` of it.
        SharedPtr<int> _alive;
        PoolString _seatVars[kSeatCount];
        static constexpr size_t kListRowCount = static_cast<size_t>(OnlineScreens::kRoomsPerPage);
        PoolString _listVars[kListRowCount];
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
        TextEntry _nicknameEntry{TextEntry::Kind::Name};
        Vector<String> _nicknamePad;
        uint64_t _nicknameStrings = 0;
        bool _nicknameActive = false;
        bool _nicknameClosing = false;    // `Done` or Escape stored it: the pad goes at the next `Follow`
        bool _nicknameDirty = false;      // the text moved since it was published
        bool _nicknamePublished = false;  // the keys and the value are on the scene
        static MahjongOnlineComponent* s_active;
    };

}  // namespace BECore::Mahjong
