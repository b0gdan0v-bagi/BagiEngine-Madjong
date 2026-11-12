#pragma once

#include <Mahjong/Online/OnlineClient.h>
#include <Mahjong/Online/TextEntry.h>

namespace BECore::Mahjong {

    /// The pages of the online panel. `Closed`: the panel is not shown.
    CORE_ENUM(OnlinePage, uint8_t, Closed,
              Menu,   // Host a room, Join a room, Return to the room of a kept session
              Host,   // the form of a new room: name, hands, level, Create
              Join,   // the form of a room to enter: name, code, Enter
              Entry,  // the pad that edits one field of the form the player came from
              Lobby   // the room: the code, the seats, Start, Leave (also while connecting and reconnecting before the game has started)
    )

    /// The field the `Entry` page edits.
    enum class OnlineField : uint8_t { Name, Code };

    /**
     * @brief What the pages of the online panel show and do: the page, the two fields, the host's options and the pad. Pure, no scene: the component only shows it.
     *
     * The commands take the `OnlineClient` (which lives in `MahjongGame`, not here) and turn the player's choices into its commands; `Sync` follows the client after every
     * `OnlineClient::Update()` and moves the page by the stage: Connecting, Lobby and Reconnecting show the `Lobby` page and a started game (`Game`) closes the panel (the table is the scene's); an
     * `Idle` client after a request that failed returns to the form it was sent from, after a room (gone, replaced, left) to the `Menu`. An error is the notice of the client (or of a field that
     * cannot be sent), shown once and cleared by the next move of the player.
     *
     * Not here: the token. A session goes through the client only (`Resume` takes it from the caller).
     *
     * `Version()` grows with every change a screen shows; a frame in which nothing moved changes nothing and allocates nothing. One thread.
     */
    class OnlineScreens {
    public:
        OnlineScreens();

        [[nodiscard]] uint64_t Version() const {
            return _version;
        }

        // ---- What a screen reads ----

        [[nodiscard]] OnlinePage Page() const {
            return _page;
        }

        [[nodiscard]] bool IsOpen() const {
            return _page != OnlinePage::Closed;
        }

        [[nodiscard]] OnlineField Focus() const {
            return _focus;
        }

        [[nodiscard]] const TextEntry& Name() const {
            return _name;
        }

        [[nodiscard]] const TextEntry& Code() const {
            return _code;
        }

        /// The field the `Entry` page edits (the focused one).
        [[nodiscard]] const TextEntry& Focused() const {
            return _focus == OnlineField::Name ? _name : _code;
        }

        /// The keys of the pad of the focused field in the language of the last `Refresh`; empty outside the `Entry` page.
        [[nodiscard]] const eastl::vector<eastl::string>& Pad() const {
            return _pad;
        }

        /// The player has typed the name (or an earlier run stored it), so it is theirs to keep and no longer the default of the language.
        [[nodiscard]] bool NameIsOwn() const {
            return _nameOwn;
        }

        [[nodiscard]] const OnlineRoomOptions& Options() const {
            return _options;
        }

        /// The form can be sent: its fields are complete.
        [[nodiscard]] bool CanCreate() const {
            return _name.IsComplete();
        }
        [[nodiscard]] bool CanEnter() const {
            return _name.IsComplete() && _code.IsComplete();
        }

        /// The Menu offers `Return`: the client is idle and a session is kept (by the client, or by an earlier run: `SetStoredRoom`).
        [[nodiscard]] bool CanReturn() const {
            return _canReturn;
        }

        /// The code of the room `Return` would enter; empty with `CanReturn()` false.
        [[nodiscard]] const eastl::string& ReturnRoom() const {
            return _returnRoom;
        }

        /// The notice to show: a key of `OnlineWords.h` and its argument; the key is empty when there is none.
        [[nodiscard]] eastl::string_view NoticeKey() const {
            return _noticeKey;
        }
        [[nodiscard]] const eastl::string& NoticeArg() const {
            return _noticeArg;
        }

        /// The key of the status line ("Connecting...", "The connection was lost. Reconnecting..."); empty when there is none.
        [[nodiscard]] eastl::string_view StatusKey() const {
            return _statusKey;
        }

        // ---- What the game gives ----

        /// The name stored by an earlier run; a name the field cannot hold changes nothing. Until the player edits the name it is the default one of the current language.
        void SetStoredName(eastl::string_view name);

        /// The code of the room of a session an earlier run kept (empty: none). The game stores what the client keeps, so it is dropped when the client's session moves
        /// (`SessionRevision()`: a new room, a `Leave`, a room that is gone).
        void SetStoredRoom(eastl::string_view room);

        /// Follows the language: the pad of the focused field and the default name are made again when the language moved. The component calls it before it publishes.
        void Refresh(const UIStrings& strings);

        // ---- The player's moves ----

        /// Opens the panel: the `Lobby` page while the client is in a room (or connecting), else the `Menu`. The notice is cleared unless @p keepNotice (a room
        /// that has just ended says why on the `Menu`).
        void Open(const OnlineClient& client, bool keepNotice = false);
        void Close();

        /// Escape: an `Entry` page to its form, a form to the `Menu`, the `Menu` closes the panel. The `Lobby` does nothing (leaving is a button: a stray key must not give up a
        /// seat). True when something changed.
        bool Back();

        /// The `Menu`'s two choices.
        bool ShowHost();
        bool ShowJoin();

        /// Opens the `Entry` page for @p field of the form on show (the `Code` of the `Join` form only).
        bool FocusField(OnlineField field);
        /// Types a character into the focused field of the `Entry` page: the key @p index of `Pad()`, or one UTF-8 character of the keyboard.
        bool PressKey(size_t index);
        bool Type(eastl::string_view character);
        bool Backspace();
        /// The `Entry` page back to its form.
        bool Done();

        /// The host options, `Host` form only: 8 or 16 hands, level 1..10, whether the players of the room may use hints and the coach.
        bool SetHands(int hands);
        bool SetLevel(int level);
        bool SetHintsAllowed(bool allowed);

        /// Sends the form. Fields that are not complete send nothing and say why in the notice (`bad_name`, `unknown_room`).
        bool Create(OnlineClient& client, eastl::string_view url);
        bool Enter(OnlineClient& client, eastl::string_view url);
        /// `Menu` only, with `CanReturn()`: comes back into @p session (the client's own `SessionToKeep()`, or the one the game stored).
        bool Resume(OnlineClient& client, const RoomSession& session);
        /// `Lobby` only: takes @p seat when the lobby offers it (`CanChoose`), asks the host's start, leaves the room (or the attempt to enter one).
        bool Sit(OnlineClient& client, int seat);
        bool Start(OnlineClient& client);
        bool Leave(OnlineClient& client);

        /// Follows the client after its `Update()` (and after each command above, which calls it itself).
        void Sync(const OnlineClient& client);

    private:
        template <typename T>
        void Assign(T& field, const T& value) {
            if (!(field == value)) {
                field = value;
                Bump();
            }
        }

        void Bump() {
            ++_version;
        }

        void SetPage(OnlinePage page);
        void ClearNotice();
        void SetLocalNotice(eastl::string_view key);
        void RebuildPad(const UIStrings& strings);
        void ClearPad();
        [[nodiscard]] TextEntry& FocusedEntry() {
            return _focus == OnlineField::Name ? _name : _code;
        }
        /// A form is a page the `Entry` page can be opened from.
        [[nodiscard]] bool IsForm(OnlinePage page) const {
            return page == OnlinePage::Host || page == OnlinePage::Join;
        }

        OnlinePage _page = OnlinePage::Closed;
        /// The page a request was sent from: where a failed one returns.
        OnlinePage _origin = OnlinePage::Menu;
        /// The form an `Entry` page returns to.
        OnlinePage _form = OnlinePage::Host;
        OnlineField _focus = OnlineField::Name;
        TextEntry _name{TextEntry::Kind::Name};
        TextEntry _code{TextEntry::Kind::Code};
        /// The player has edited the name or an earlier run stored it: it no longer follows the default of the language.
        bool _nameOwn = false;
        OnlineRoomOptions _options;
        eastl::vector<eastl::string> _pad;
        bool _padValid = false;
        TextEntry::Kind _padKind = TextEntry::Kind::Name;
        uint64_t _padStringsVersion = 0;
        uint64_t _defaultNameStringsVersion = 0;
        bool _hasStrings = false;
        eastl::string _storedRoom;
        uint32_t _seenRevision = 0;
        bool _revisionKnown = false;
        bool _canReturn = false;
        eastl::string _returnRoom;
        eastl::string_view _noticeKey;
        eastl::string _noticeArg;
        uint32_t _seenNotice = 0;
        eastl::string_view _statusKey;
        /// The client has been in a room (lobby or game) since the last request: the end of it goes to the `Menu`.
        bool _inRoom = false;
        uint64_t _version = 0;
    };

}  // namespace BECore::Mahjong
