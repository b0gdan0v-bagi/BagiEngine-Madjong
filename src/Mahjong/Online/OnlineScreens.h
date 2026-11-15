#pragma once

#include <BECore/Clipboard/ClipboardTypes.h>
#include <Mahjong/Online/OnlineClient.h>
#include <Mahjong/Online/TextEntry.h>

namespace BECore::Mahjong {

    /// The pages of the online panel. `Closed`: the panel is not shown.
    CORE_ENUM(OnlinePage, uint8_t, Closed, Menu, Host, Join,
              Entry,  // the pad that edits one field of the form the player came from
              Lobby,  // also while connecting and reconnecting before the game has started
              Rooms   // the open rooms of the server to pick from
    )

    /// The field the `Entry` page edits.
    enum class OnlineField : uint8_t { Name, Code };

    /// `Copied` and `Failed` last until the player's next move or another room.
    enum class OnlineCopyState : uint8_t { None, Copied, Failed };

    /// The note under the code pad: why a paste gave no code.
    enum class OnlinePasteNote : uint8_t {
        None,
        NotACode,
        Empty,
        Refused  // the platform or the user did not let the game read the clipboard
    };

    /**
     * @brief The pure logic of the online panel (the page, the two fields, the host's options, the pad): no ImGui, no scene, the
     *        component only shows it (`src/Mahjong/Online/CLAUDE.md`, "The screens").
     *
     * The commands take the `OnlineClient` (it lives in `MahjongGame`, not here); `Sync` follows it after every `OnlineClient::Update()`.
     * A request that failed returns to the page it was sent from (the `Rooms` page for a failed join of a listed room), a room that went
     * away (gone, replaced, left) to the `Menu`. An error is the client's notice, shown once and cleared by the player's next move.
     *
     * Holds no token: a session goes through the client only (`Resume` takes it from the caller).
     *
     * `Version()` grows with every change a screen shows; a frame in which nothing moved changes nothing and allocates nothing. One thread.
     */
    class OnlineScreens {
    public:
        /// All that a window 320 units high holds: the title, a line, the name, these rows and the button bar.
        static constexpr int kRoomsPerPage = 3;

        OnlineScreens();

        [[nodiscard]] uint64_t Version() const {
            return _version;
        }

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

        [[nodiscard]] const TextEntry& Focused() const {
            return _focus == OnlineField::Name ? _name : _code;
        }

        /// The keys of the focused field's pad in the language of the last `Refresh`; empty outside the `Entry` page.
        [[nodiscard]] const Vector<String>& Pad() const {
            return _pad;
        }

        /// False while the name is still the default of the language.
        [[nodiscard]] bool NameIsOwn() const {
            return _nameOwn;
        }

        [[nodiscard]] const OnlineRoomOptions& Options() const {
            return _options;
        }

        [[nodiscard]] bool CanCreate() const {
            return _name.IsComplete();
        }
        [[nodiscard]] bool CanEnter() const {
            return _name.IsComplete() && _code.IsComplete();
        }

        /// The client is idle and a session is kept (by the client, or by an earlier run: `SetStoredRoom`).
        [[nodiscard]] bool CanReturn() const {
            return _canReturn;
        }

        /// Empty with `CanReturn()` false.
        [[nodiscard]] const String& ReturnRoom() const {
            return _returnRoom;
        }

        /// A key of `OnlineWords.h` and its argument, never a text; the key is empty when there is no notice.
        [[nodiscard]] StringView NoticeKey() const {
            return _noticeKey;
        }
        [[nodiscard]] const String& NoticeArg() const {
            return _noticeArg;
        }

        /// A key of `OnlineWords.h`; empty when there is no status line.
        [[nodiscard]] StringView StatusKey() const {
            return _statusKey;
        }

        /// What the copy button puts on the clipboard: the room's code on the `Lobby` page, empty on any other.
        [[nodiscard]] StringView RoomCodeToCopy(const OnlineClient& client) const;

        /// The `Rooms` page (0-based), the count of pages (at least 1), the rows of this page and the index in `OnlineClient::Rooms().rooms`
        /// of the row @p row of this page.
        [[nodiscard]] int RoomsPage() const {
            return _roomsPage;
        }
        [[nodiscard]] int RoomsPageCount() const;
        [[nodiscard]] int RoomsOnPage() const;
        [[nodiscard]] size_t RoomIndex(int row) const {
            return static_cast<size_t>(_roomsPage * kRoomsPerPage + row);
        }
        /// An answer has come and no room is open.
        [[nodiscard]] bool RoomsEmpty() const {
            return _roomsLoaded && _roomCount == 0;
        }

        [[nodiscard]] OnlineCopyState CopyState() const {
            return _copyState;
        }

        [[nodiscard]] OnlinePasteNote PasteNote() const {
            return _pasteNote;
        }

        /// A key of `OnlineWords.h`; empty with no note.
        [[nodiscard]] StringView PasteNoteKey() const;

        /// The name an earlier run stored; one the field cannot hold changes nothing.
        void SetStoredName(StringView name);

        /// The room of a session an earlier run kept (empty: none). Dropped when the client's session moves (`SessionRevision()`), because
        /// the game stores what the client keeps.
        void SetStoredRoom(StringView room);

        /// Follows the language: the pad and the default name are made again when it moved. Called before the component publishes.
        void Refresh(const UIStrings& strings);

        /// The `Lobby` page while the client is in a room (or connecting), else the `Menu`. The notice is cleared unless @p keepNotice (a
        /// room that has just ended says why on the `Menu`).
        void Open(const OnlineClient& client, bool keepNotice = false);
        void Close();

        /// Escape, one page back; the `Rooms` page ends the browsing. The `Lobby` does nothing: a stray key must not give up a seat. True when
        /// something changed.
        bool Back(OnlineClient& client);

        /// `ShowRooms` starts browsing @p url; the page opens when that was sent.
        bool ShowHost();
        bool ShowJoin();
        bool ShowRooms(OnlineClient& client, StringView url);

        /// The `Code` only of the `Join` form; the `Name` of any form.
        bool FocusField(OnlineField field);
        /// Into the focused field: the key @p index of `Pad()`, or one UTF-8 character of the keyboard.
        bool PressKey(size_t index);
        bool Type(StringView character);
        bool Backspace();
        bool Done();

        /// The host's options, `Host` form only.
        bool SetHands(int hands);
        bool SetLevel(int level);
        bool SetHintsAllowed(bool allowed);
        /// Listed on the `Rooms` page of others, or joined by its code only (the default).
        bool SetPublic(bool isPublic);

        /// Sends the form; incomplete fields send nothing and say why in the notice (`bad_name`, `unknown_room`).
        bool Create(OnlineClient& client, StringView url);
        bool Enter(OnlineClient& client, StringView url);
        /// `Menu` only, with `CanReturn()`; @p session is the client's own `SessionToKeep()` or the one the game stored.
        bool Resume(OnlineClient& client, const RoomSession& session);
        /// `Lobby` only. `Leave` also gives up an attempt to enter.
        bool Sit(OnlineClient& client, int seat);
        bool Start(OnlineClient& client);
        bool Leave(OnlineClient& client);

        /// `Rooms` only. `RefreshRooms` also opens the connection again after it closed; nothing while a request is in flight.
        bool RefreshRooms(OnlineClient& client, StringView url);
        /// Wraps; false when the list has one page.
        bool NextRoomsPage();
        /// An incomplete name is the `bad_name` notice; a row that is not there now sends nothing.
        bool JoinRoom(OnlineClient& client, StringView url, int row);
        bool HostFromRooms(OnlineClient& client);

        /// `Lobby` only: the copy button's result.
        bool NoteCopy(bool ok);

        /// On the `Join` form or the `Entry` page of the `Code`, by `TextEntry::CleanRoomCode`. A text with no code changes nothing and says
        /// so (the pad note on `Entry`, the notice on `Join`); any other page: false, nothing said.
        bool PasteCode(StringView text);
        /// The clipboard gave no text (@p status not `Ok`): `Empty`, or `Refused` when it was not allowed or there is no clipboard. Same pages.
        bool NoteNothingToPaste(ClipboardStatus status);

        /// Each command calls it itself.
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
        static void EndBrowsing(OnlineClient& client);
        void ClearNotice();
        void ClearPasteNote();
        void ResetCopy();
        [[nodiscard]] bool TakesPaste() const {
            return _page == OnlinePage::Join || (_page == OnlinePage::Entry && _focus == OnlineField::Code);
        }
        void SetLocalNotice(StringView key);
        void RebuildPad(const UIStrings& strings);
        void ClearPad();
        [[nodiscard]] TextEntry& FocusedEntry() {
            return _focus == OnlineField::Name ? _name : _code;
        }
        [[nodiscard]] bool IsForm(OnlinePage page) const {
            return page == OnlinePage::Host || page == OnlinePage::Join || page == OnlinePage::Rooms;
        }

        OnlinePage _page = OnlinePage::Closed;
        /// Where a failed request returns.
        OnlinePage _origin = OnlinePage::Menu;
        /// The form an `Entry` page returns to.
        OnlinePage _form = OnlinePage::Host;
        OnlineField _focus = OnlineField::Name;
        TextEntry _name{TextEntry::Kind::Name};
        TextEntry _code{TextEntry::Kind::Code};
        /// The name no longer follows the default of the language.
        bool _nameOwn = false;
        OnlineRoomOptions _options;
        Vector<String> _pad;
        bool _padValid = false;
        TextEntry::Kind _padKind = TextEntry::Kind::Name;
        uint64_t _padStringsVersion = 0;
        uint64_t _defaultNameStringsVersion = 0;
        bool _hasStrings = false;
        String _storedRoom;
        uint32_t _seenRevision = 0;
        bool _revisionKnown = false;
        bool _canReturn = false;
        String _returnRoom;
        StringView _noticeKey;
        String _noticeArg;
        uint32_t _seenNotice = 0;
        StringView _statusKey;
        OnlineCopyState _copyState = OnlineCopyState::None;
        /// The room the page showed at the last `Sync`: another one ends the state of the copy button.
        String _copyRoom;
        OnlinePasteNote _pasteNote = OnlinePasteNote::None;
        /// In a room since the last request: its end goes to the `Menu`.
        bool _inRoom = false;
        /// What the last `Sync` read of the client's list; the page is clamped when a refreshed list is shorter.
        int _roomsPage = 0;
        int _roomCount = 0;
        bool _roomsLoaded = false;
        uint64_t _version = 0;
    };

}  // namespace BECore::Mahjong
