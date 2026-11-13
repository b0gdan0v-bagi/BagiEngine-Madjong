#pragma once

#include <BECore/Clipboard/ClipboardTypes.h>
#include <Mahjong/Online/OnlineClient.h>
#include <Mahjong/Online/TextEntry.h>

namespace BECore::Mahjong {

    /// The pages of the online panel. `Closed`: the panel is not shown.
    CORE_ENUM(OnlinePage, uint8_t, Closed,
              Menu,   // Host a room, Join a room, Return to the room of a kept session
              Host,   // the form of a new room: name, hands, level, Create
              Join,   // the form of a room to enter: name, code, Enter
              Entry,  // the pad that edits one field of the form the player came from
              Lobby,  // the room: the code, the seats, Start, Leave (also while connecting and reconnecting before the game has started)
              Rooms   // the open rooms of the server to pick from: the name, up to `kRoomsPerPage` rows, Refresh, a page button, Back
    )

    /// The field the `Entry` page edits.
    enum class OnlineField : uint8_t { Name, Code };

    /// What the lobby's copy button says: its own words, `Copied` or `Failed` until the player's next move or another room.
    enum class OnlineCopyState : uint8_t { None, Copied, Failed };

    /// The one-line note under the value of the code pad: what a paste did not give.
    enum class OnlinePasteNote : uint8_t {
        None,
        NotACode,  // the pasted text holds no room code
        Empty,     // the clipboard holds no text
        Refused    // the platform or the user did not let the game read it
    };

    /**
     * @brief What the pages of the online panel show and do: the page, the two fields, the host's options and the pad. Pure, no scene: the component only shows it.
     *
     * The commands take the `OnlineClient` (which lives in `MahjongGame`, not here) and turn the player's choices into its commands; `Sync` follows the client after every
     * `OnlineClient::Update()` and moves the page by the stage: Connecting, Lobby and Reconnecting show the `Lobby` page and a started game (`Game`) closes the panel (the table is the scene's); an
     * `Idle` client after a request that failed returns to the form it was sent from, after a room (gone, replaced, left) to the `Menu`. An error is the notice of the client (or of a field that
     * cannot be sent), shown once and cleared by the next move of the player.
     *
     * The `Rooms` page follows the client's `Browsing` stage and its list (`OnlineClient::Rooms()`): rows are paged by `kRoomsPerPage` and a row is joined by its index on the page. A join
     * that failed returns to this page (the client asks the list again), and so does a closed browsing connection (Refresh opens it again).
     *
     * Not here: the token. A session goes through the client only (`Resume` takes it from the caller).
     *
     * `Version()` grows with every change a screen shows; a frame in which nothing moved changes nothing and allocates nothing. One thread.
     */
    class OnlineScreens {
    public:
        /// Room rows on a page of the `Rooms` list: a window 320 units high holds the title, a line, the name, three rows and the button bar.
        static constexpr int kRoomsPerPage = 3;

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

        /// The code of the room on the `Lobby` page (the lobby's, else the client's); empty on any other page. The copy button puts exactly this on the clipboard.
        [[nodiscard]] eastl::string_view RoomCodeToCopy(const OnlineClient& client) const;

        /// The `Rooms` page: the page shown (0-based), how many pages the list has (at least 1), how many rows this page has (0..`kRoomsPerPage`) and the index in
        /// `OnlineClient::Rooms().rooms` of the row @p row of this page.
        [[nodiscard]] int RoomsPage() const {
            return _roomsPage;
        }
        [[nodiscard]] int RoomsPageCount() const;
        [[nodiscard]] int RoomsOnPage() const;
        [[nodiscard]] size_t RoomIndex(int row) const {
            return static_cast<size_t>(_roomsPage * kRoomsPerPage + row);
        }
        /// An answer has come and no room is open: the page offers to host one.
        [[nodiscard]] bool RoomsEmpty() const {
            return _roomsLoaded && _roomCount == 0;
        }

        [[nodiscard]] OnlineCopyState CopyState() const {
            return _copyState;
        }

        [[nodiscard]] OnlinePasteNote PasteNote() const {
            return _pasteNote;
        }

        /// The key of the paste note's words (`OnlineWords.h`); empty with no note.
        [[nodiscard]] eastl::string_view PasteNoteKey() const;

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

        /// Escape: an `Entry` page to its form, a form to the `Menu`, the `Rooms` page to the `Menu` (it ends the browsing: the connection is closed), the `Menu` closes the
        /// panel. The `Lobby` does nothing (leaving is a button: a stray key must not give up a seat). True when something changed.
        bool Back(OnlineClient& client);

        /// The `Menu`'s choices. `ShowRooms` starts browsing the open rooms of @p url; the page opens when that was sent.
        bool ShowHost();
        bool ShowJoin();
        bool ShowRooms(OnlineClient& client, eastl::string_view url);

        /// Opens the `Entry` page for @p field of the page on show: the `Name` of the `Host`, `Join` and `Rooms` pages, the `Code` of the `Join` form only.
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
        /// Whether the room is listed on the `Rooms` page of others (`Open room`) or joined by its code only (`By code`, the default).
        bool SetPublic(bool isPublic);

        /// Sends the form. Fields that are not complete send nothing and say why in the notice (`bad_name`, `unknown_room`).
        bool Create(OnlineClient& client, eastl::string_view url);
        bool Enter(OnlineClient& client, eastl::string_view url);
        /// `Menu` only, with `CanReturn()`: comes back into @p session (the client's own `SessionToKeep()`, or the one the game stored).
        bool Resume(OnlineClient& client, const RoomSession& session);
        /// `Lobby` only: takes @p seat when the lobby offers it (`CanChoose`), asks the host's start, leaves the room (or the attempt to enter one).
        bool Sit(OnlineClient& client, int seat);
        bool Start(OnlineClient& client);
        bool Leave(OnlineClient& client);

        /// `Rooms` only. `RefreshRooms` asks the list again (or opens the connection again after it closed); it does nothing while a list request is in flight.
        bool RefreshRooms(OnlineClient& client, eastl::string_view url);
        /// The next page of the list, the first one after the last; false when the list has one page.
        bool NextRoomsPage();
        /// Joins the room of the row @p row of this page, with the name on the page (an incomplete one is the `bad_name` notice). A row that is not there now sends nothing.
        bool JoinRoom(OnlineClient& client, eastl::string_view url, int row);
        /// The offer of an empty list: ends the browsing and shows the `Host` form.
        bool HostFromRooms(OnlineClient& client);

        /// `Lobby` only: the copy button has put the code on the clipboard (@p ok) or could not. Back to `None` at the player's next move, in another room and off the `Lobby`.
        bool NoteCopy(bool ok);

        /// Sets the code from a pasted @p text (`TextEntry::CleanRoomCode`): on the `Join` form, or the `Entry` page of the `Code`. A text with no code changes nothing and says so: the pad
        /// note `NotACode` on the `Entry` page, the notice on the `Join` form (which has no pad). Any other page: false, nothing said.
        bool PasteCode(eastl::string_view text);
        /// The read of the clipboard gave no text (@p status not `Ok`): the note `Empty`, or `Refused` when it was not allowed or there is no clipboard. Same pages as `PasteCode`.
        bool NoteNothingToPaste(ClipboardStatus status);

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
        /// Closes the browsing connection of @p client, if there is one.
        static void EndBrowsing(OnlineClient& client);
        void ClearNotice();
        void ClearPasteNote();
        void ResetCopy();
        [[nodiscard]] bool TakesPaste() const {
            return _page == OnlinePage::Join || (_page == OnlinePage::Entry && _focus == OnlineField::Code);
        }
        void SetLocalNotice(eastl::string_view key);
        void RebuildPad(const UIStrings& strings);
        void ClearPad();
        [[nodiscard]] TextEntry& FocusedEntry() {
            return _focus == OnlineField::Name ? _name : _code;
        }
        /// A form is a page the `Entry` page can be opened from.
        [[nodiscard]] bool IsForm(OnlinePage page) const {
            return page == OnlinePage::Host || page == OnlinePage::Join || page == OnlinePage::Rooms;
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
        OnlineCopyState _copyState = OnlineCopyState::None;
        /// The room the page showed at the last `Sync`: another one ends the state of the copy button.
        eastl::string _copyRoom;
        OnlinePasteNote _pasteNote = OnlinePasteNote::None;
        /// The client has been in a room (lobby or game) since the last request: the end of it goes to the `Menu`.
        bool _inRoom = false;
        /// The `Rooms` page: the page shown, and what the last `Sync` read of the client's list (the page is clamped when a refreshed list is shorter).
        int _roomsPage = 0;
        int _roomCount = 0;
        bool _roomsLoaded = false;
        uint64_t _version = 0;
    };

}  // namespace BECore::Mahjong
