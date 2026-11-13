#include "OnlineScreens.h"

#include <Mahjong/Online/OnlineWords.h>

namespace BECore::Mahjong {

    namespace {

        bool InRoomStage(OnlineStage stage) {
            return stage == OnlineStage::Lobby || stage == OnlineStage::Game;
        }

        /// The stages in which the panel shows the room. In `Game` the table is the scene's and the panel is closed; `Browsing` has no room.
        bool ShowsRoom(OnlineStage stage) {
            return stage != OnlineStage::Idle && stage != OnlineStage::Game && stage != OnlineStage::Browsing;
        }

    }  // namespace

    OnlineScreens::OnlineScreens() = default;

    // ---- What the game gives -----------------------------------------------------------------------------------------------------

    void OnlineScreens::SetStoredName(eastl::string_view name) {
        TextEntry candidate(TextEntry::Kind::Name);
        if (!candidate.Set(name) || !candidate.IsComplete()) {
            return;
        }
        Assign(_name, candidate);
        _nameOwn = true;
    }

    void OnlineScreens::SetStoredRoom(eastl::string_view room) {
        const eastl::string text(room.data(), room.size());
        if (_storedRoom != text) {
            _storedRoom = text;
            Bump();
        }
    }

    void OnlineScreens::Refresh(const UIStrings& strings) {
        const uint64_t stringsVersion = strings.GetVersion();
        if (!_nameOwn && (!_hasStrings || stringsVersion != _defaultNameStringsVersion)) {
            TextEntry candidate(TextEntry::Kind::Name);
            if (!candidate.Set(TextEntry::DefaultName(strings))) {
                candidate.Clear();
            }
            Assign(_name, candidate);
        }
        _defaultNameStringsVersion = stringsVersion;
        _hasStrings = true;
        if (_page == OnlinePage::Entry) {
            const TextEntry::Kind kind = FocusedEntry().GetKind();
            if (!_padValid || kind != _padKind || stringsVersion != _padStringsVersion) {
                _padKind = kind;
                _padStringsVersion = stringsVersion;
                RebuildPad(strings);
            }
        }
    }

    // ---- The player's moves ------------------------------------------------------------------------------------------------------

    void OnlineScreens::Open(const OnlineClient& client, bool keepNotice) {
        if (!keepNotice) {
            ClearNotice();
        }
        SetPage(ShowsRoom(client.Stage()) ? OnlinePage::Lobby : OnlinePage::Menu);
        _origin = OnlinePage::Menu;
        Sync(client);
    }

    void OnlineScreens::Close() {
        ClearNotice();
        SetPage(OnlinePage::Closed);
    }

    bool OnlineScreens::Back() {
        switch (_page) {
            case OnlinePage::Entry:
                ClearNotice();
                SetPage(_form);
                return true;
            case OnlinePage::Host:
            case OnlinePage::Join:
                ClearNotice();
                SetPage(OnlinePage::Menu);
                return true;
            case OnlinePage::Menu:
                Close();
                return true;
            case OnlinePage::Closed:
            case OnlinePage::Lobby:
                break;
        }
        return false;
    }

    bool OnlineScreens::ShowHost() {
        if (_page != OnlinePage::Menu) {
            return false;
        }
        ClearNotice();
        SetPage(OnlinePage::Host);
        return true;
    }

    bool OnlineScreens::ShowJoin() {
        if (_page != OnlinePage::Menu) {
            return false;
        }
        ClearNotice();
        SetPage(OnlinePage::Join);
        return true;
    }

    bool OnlineScreens::FocusField(OnlineField field) {
        if (!IsForm(_page) || (field == OnlineField::Code && _page != OnlinePage::Join)) {
            return false;
        }
        ClearNotice();
        _form = _page;
        _focus = field;
        SetPage(OnlinePage::Entry);
        return true;
    }

    bool OnlineScreens::PressKey(size_t index) {
        if (_page != OnlinePage::Entry || index >= _pad.size()) {
            return false;
        }
        // The pad is a member that is not touched while its key is typed
        return Type(eastl::string_view(_pad[index].data(), _pad[index].size()));
    }

    bool OnlineScreens::Type(eastl::string_view character) {
        if (_page != OnlinePage::Entry) {
            return false;
        }
        ClearPasteNote();
        if (!FocusedEntry().Append(character)) {
            return false;
        }
        _nameOwn = _nameOwn || _focus == OnlineField::Name;
        Bump();
        return true;
    }

    bool OnlineScreens::Backspace() {
        if (_page == OnlinePage::Entry) {
            ClearPasteNote();
        }
        if (_page != OnlinePage::Entry || !FocusedEntry().Backspace()) {
            return false;
        }
        _nameOwn = _nameOwn || _focus == OnlineField::Name;
        Bump();
        return true;
    }

    bool OnlineScreens::Done() {
        if (_page != OnlinePage::Entry) {
            return false;
        }
        ClearNotice();
        SetPage(_form);
        return true;
    }

    bool OnlineScreens::SetHands(int hands) {
        if (_page != OnlinePage::Host || (hands != kShortMatchHands && hands != kFullMatchHands)) {
            return false;
        }
        Assign(_options.hands, hands);
        return true;
    }

    bool OnlineScreens::SetLevel(int level) {
        if (_page != OnlinePage::Host || level < kMinLevel || level > kMaxLevel) {
            return false;
        }
        Assign(_options.level, level);
        return true;
    }

    bool OnlineScreens::SetHintsAllowed(bool allowed) {
        if (_page != OnlinePage::Host) {
            return false;
        }
        Assign(_options.hintsAllowed, allowed);
        return true;
    }

    bool OnlineScreens::Create(OnlineClient& client, eastl::string_view url) {
        if (_page != OnlinePage::Host || client.Stage() != OnlineStage::Idle) {
            return false;
        }
        ClearNotice();
        if (!CanCreate()) {
            SetLocalNotice(OnlineErrorKey(RoomErrors::kBadName));
            return false;
        }
        _origin = OnlinePage::Host;
        _inRoom = false;
        const bool sent = client.Host(url, _name.Text(), _options);
        Sync(client);
        return sent;
    }

    bool OnlineScreens::Enter(OnlineClient& client, eastl::string_view url) {
        if (_page != OnlinePage::Join || client.Stage() != OnlineStage::Idle) {
            return false;
        }
        ClearNotice();
        if (!_name.IsComplete()) {
            SetLocalNotice(OnlineErrorKey(RoomErrors::kBadName));
            return false;
        }
        if (!_code.IsComplete()) {
            SetLocalNotice(OnlineErrorKey(RoomErrors::kUnknownRoom));
            return false;
        }
        _origin = OnlinePage::Join;
        _inRoom = false;
        const bool sent = client.Join(url, _code.Text(), _name.Text());
        Sync(client);
        return sent;
    }

    bool OnlineScreens::Resume(OnlineClient& client, const RoomSession& session) {
        if (_page != OnlinePage::Menu || !_canReturn || client.Stage() != OnlineStage::Idle) {
            return false;
        }
        ClearNotice();
        _origin = OnlinePage::Menu;
        _inRoom = false;
        const bool sent = client.Resume(session);
        Sync(client);
        return sent;
    }

    bool OnlineScreens::Sit(OnlineClient& client, int seat) {
        if (_page != OnlinePage::Lobby || !client.CanChoose(seat)) {
            return false;
        }
        ClearNotice();
        const bool sent = client.ChooseSeat(seat);
        Sync(client);
        return sent;
    }

    bool OnlineScreens::Start(OnlineClient& client) {
        if (_page != OnlinePage::Lobby) {
            return false;
        }
        ClearNotice();
        const bool sent = client.Start();
        Sync(client);
        return sent;
    }

    bool OnlineScreens::Leave(OnlineClient& client) {
        if (_page != OnlinePage::Lobby) {
            return false;
        }
        ClearNotice();
        bool done = true;
        if (client.Stage() == OnlineStage::Connecting) {
            // Nothing is joined yet: giving up the attempt is closing the connection
            client.Disconnect();
        } else {
            done = client.Leave();
        }
        Sync(client);
        return done;
    }

    bool OnlineScreens::NoteCopy(bool ok) {
        if (_page != OnlinePage::Lobby) {
            return false;
        }
        Assign(_copyState, ok ? OnlineCopyState::Copied : OnlineCopyState::Failed);
        return true;
    }

    bool OnlineScreens::PasteCode(eastl::string_view text) {
        if (!TakesPaste()) {
            return false;
        }
        const std::optional<eastl::string> code = TextEntry::CleanRoomCode(text);
        if (!code) {
            if (_page == OnlinePage::Join) {
                // The form has no pad note: the line under its title says it
                SetLocalNotice(kOnlinePasteNotCodeKey);
            } else {
                Assign(_pasteNote, OnlinePasteNote::NotACode);
            }
            return false;
        }
        TextEntry candidate(TextEntry::Kind::Code);
        if (!candidate.Set(*code)) {
            return false;
        }
        ClearNotice();
        Assign(_code, candidate);
        return true;
    }

    bool OnlineScreens::NoteNothingToPaste(ClipboardStatus status) {
        if (!TakesPaste() || status == ClipboardStatus::Ok) {
            return false;
        }
        Assign(_pasteNote, status == ClipboardStatus::Empty ? OnlinePasteNote::Empty : OnlinePasteNote::Refused);
        return true;
    }

    eastl::string_view OnlineScreens::RoomCodeToCopy(const OnlineClient& client) const {
        if (_page != OnlinePage::Lobby) {
            return {};
        }
        const eastl::string& room = client.Lobby().room.empty() ? client.Room() : client.Lobby().room;
        return eastl::string_view(room.data(), room.size());
    }

    eastl::string_view OnlineScreens::PasteNoteKey() const {
        switch (_pasteNote) {
            case OnlinePasteNote::NotACode:
                return kOnlinePasteNotCodeKey;
            case OnlinePasteNote::Empty:
                return kOnlinePasteEmptyKey;
            case OnlinePasteNote::Refused:
                return kOnlinePasteRefusedKey;
            case OnlinePasteNote::None:
                break;
        }
        return {};
    }

    // ---- The client --------------------------------------------------------------------------------------------------------------

    void OnlineScreens::Sync(const OnlineClient& client) {
        const OnlineStage stage = client.Stage();

        const OnlineNotice& notice = client.Notice();
        if (notice.serial != _seenNotice) {
            _seenNotice = notice.serial;
            if (notice.serial != 0 && !notice.key.empty()) {
                Assign(_noticeKey, notice.key);
                Assign(_noticeArg, notice.arg);
            }
        }

        const eastl::string_view status = stage == OnlineStage::Connecting ? kOnlineConnectingKey : stage == OnlineStage::Reconnecting ? kOnlineReconnectingKey : eastl::string_view();
        Assign(_statusKey, status);

        if (InRoomStage(stage)) {
            _inRoom = true;
        }

        // The game keeps in its slot what the client keeps: once the client's session moved, a stored room of an earlier run is no longer on offer
        if (_revisionKnown && client.SessionRevision() != _seenRevision && !_storedRoom.empty()) {
            _storedRoom.clear();
            Bump();
        }
        _seenRevision = client.SessionRevision();
        _revisionKnown = true;

        const RoomSession& kept = client.SessionToKeep();
        const bool canReturn = stage == OnlineStage::Idle && (kept.IsValid() || !_storedRoom.empty());
        Assign(_canReturn, canReturn);
        const eastl::string_view room = !canReturn       ? eastl::string_view()
                                        : kept.IsValid() ? eastl::string_view(kept.room.data(), kept.room.size())
                                                         : eastl::string_view(_storedRoom.data(), _storedRoom.size());
        if (_returnRoom != room) {
            _returnRoom.assign(room.data(), room.size());
            Bump();
        }

        if (_page == OnlinePage::Closed) {
            return;
        }
        if (stage == OnlineStage::Game) {
            Close();  // a started game is shown by the table, not by the panel
        } else if (ShowsRoom(stage)) {
            SetPage(OnlinePage::Lobby);
        } else if (_page == OnlinePage::Lobby) {
            // The request failed, or the room is over
            const OnlinePage next = _inRoom ? OnlinePage::Menu : _origin;
            _inRoom = false;
            SetPage(next);
        }

        // The copy button speaks of one room
        const eastl::string_view copyRoom = RoomCodeToCopy(client);
        if (_copyRoom != copyRoom) {
            _copyRoom.assign(copyRoom.data(), copyRoom.size());
            ResetCopy();
        }
    }

    // ---- Private -----------------------------------------------------------------------------------------------------------------

    void OnlineScreens::SetPage(OnlinePage page) {
        if (_page == page) {
            return;
        }
        _page = page;
        if (page != OnlinePage::Entry) {
            ClearPad();
        }
        if (page != OnlinePage::Lobby) {
            ResetCopy();
        }
        ClearPasteNote();
        Bump();
    }

    void OnlineScreens::ClearPasteNote() {
        Assign(_pasteNote, OnlinePasteNote::None);
    }

    void OnlineScreens::ResetCopy() {
        Assign(_copyState, OnlineCopyState::None);
    }

    void OnlineScreens::ClearNotice() {
        ResetCopy();
        ClearPasteNote();
        if (!_noticeKey.empty()) {
            _noticeKey = {};
            _noticeArg.clear();
            Bump();
        }
    }

    void OnlineScreens::SetLocalNotice(eastl::string_view key) {
        _noticeKey = key;
        _noticeArg.clear();
        Bump();
    }

    void OnlineScreens::RebuildPad(const UIStrings& strings) {
        _pad = FocusedEntry().Pad(strings);
        _padValid = true;
        Bump();
    }

    void OnlineScreens::ClearPad() {
        _pad.clear();
        _padValid = false;
    }

}  // namespace BECore::Mahjong
