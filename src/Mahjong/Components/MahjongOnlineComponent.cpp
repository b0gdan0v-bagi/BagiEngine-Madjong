#include "MahjongOnlineComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/MahjongOnlineComponent.gen.hpp>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/Online/OnlineServerUrl.h>
#include <Mahjong/Online/OnlineWords.h>
#include <Mahjong/Table/Words.h>

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::string_view kOpenAction = "MahjongOnline";
        constexpr eastl::string_view kCloseAction = "MahjongOnlineClose";
        constexpr eastl::string_view kHostAction = "MahjongOnlineHost";
        constexpr eastl::string_view kJoinAction = "MahjongOnlineJoin";
        constexpr eastl::string_view kResumeAction = "MahjongOnlineResume";
        constexpr eastl::string_view kFieldAction = "MahjongOnlineField";
        constexpr eastl::string_view kKeyAction = "MahjongOnlineKey";
        constexpr eastl::string_view kBackspaceAction = "MahjongOnlineBackspace";
        constexpr eastl::string_view kDoneAction = "MahjongOnlineDone";
        constexpr eastl::string_view kHandsAction = "MahjongOnlineHands";
        constexpr eastl::string_view kLevelAction = "MahjongOnlineLevel";
        constexpr eastl::string_view kHintsAction = "MahjongOnlineHints";
        constexpr eastl::string_view kCreateAction = "MahjongOnlineCreate";
        constexpr eastl::string_view kEnterAction = "MahjongOnlineEnter";
        constexpr eastl::string_view kSitAction = "MahjongOnlineSit";
        constexpr eastl::string_view kStartAction = "MahjongOnlineStart";
        constexpr eastl::string_view kLeaveAction = "MahjongOnlineLeave";
        constexpr eastl::string_view kNameValue = "name";
        constexpr eastl::string_view kCodeValue = "code";
        constexpr eastl::string_view kHintsAllowedValue = "allowed";
        constexpr eastl::string_view kHintsOffValue = "off";

        /// The elements of the canvas `Online` the pages show and hide among themselves, in the order of `kItemNames`.
        enum Item : size_t {
            TitleMenu,
            TitleHost,
            TitleJoin,
            TitleLobby,
            Notice,
            Status,
            Waiting,
            HostLine,
            MenuHost,
            MenuJoin,
            MenuReturn,
            NameRow,
            CodeRow,
            HandsRow,
            LevelRow,
            HintsRow,
            SeatRow0,
            SeatRow1,
            SeatRow2,
            SeatRow3,
            Sit0,
            Sit1,
            Sit2,
            Sit3,
            Gap,
            Buttons,
            Create,
            Enter,
            Start,
            Leave,
            EntryCaptionName,
            EntryCaptionCode,
            ItemCount
        };

        constexpr const char* kItemNames[] = {
            "OnlineTitleMenu", "OnlineTitleHost", "OnlineTitleJoin",  "OnlineTitleLobby", "OnlineNotice",  "OnlineStatus",   "OnlineWaiting",    "OnlineHostLine",
            "OnlineMenuHost",  "OnlineMenuJoin",  "OnlineMenuReturn", "OnlineNameRow",    "OnlineCodeRow", "OnlineHandsRow", "OnlineLevelRow",   "OnlineHintsRow",
            "OnlineSeatRow0",  "OnlineSeatRow1",  "OnlineSeatRow2",   "OnlineSeatRow3",   "OnlineSit0",    "OnlineSit1",     "OnlineSit2",       "OnlineSit3",
            "OnlineGap",       "OnlineButtons",   "OnlineCreate",     "OnlineEnter",      "OnlineStart",   "OnlineLeave",    "EntryCaptionName", "EntryCaptionCode",
        };
        static_assert(sizeof(kItemNames) / sizeof(kItemNames[0]) == ItemCount, "one name per item");

        constexpr eastl::string_view kOnlineCanvas = "Online";
        constexpr eastl::string_view kEntryCanvas = "OnlineEntry";
        constexpr eastl::string_view kSetupOnlineButton = "SetupOnline";

        PoolString IndexedName(const char* base, size_t index) {
            return PoolString::Intern(Format("{}{}", base, index));
        }

        std::optional<size_t> ParseIndex(eastl::string_view text) {
            size_t number = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
            if (error != std::errc() || end != text.data() + text.size()) {
                return std::nullopt;
            }
            return number;
        }

        /// The kept room of this run, else the one an earlier run stored: what `Return` enters.
        const RoomSession& SessionToReturnTo(const MahjongGame& game, const OnlineClient& client) {
            const RoomSession& kept = client.SessionToKeep();
            return kept.IsValid() ? kept : game.StoredOnline().session;
        }

    }  // namespace

    MahjongOnlineComponent* MahjongOnlineComponent::s_active = nullptr;

    MahjongOnlineComponent::~MahjongOnlineComponent() {
        if (s_active == this) {
            s_active = nullptr;
        }
    }

    void MahjongOnlineComponent::OnAttached() {
        static_assert(kItemCount == ItemCount, "the header sizes its arrays by the items");
        s_active = this;
        for (size_t i = 0; i < kItemCount; ++i) {
            _itemNames[i] = PoolString::Intern(kItemNames[i]);
            _items[i] = -1;
        }
        for (size_t i = 0; i < kSeatCount; ++i) {
            _seatVars[i] = IndexedName("onlineSeat", i);
        }
        for (size_t i = 0; i < TextEntry::kPadMaxKeys; ++i) {
            _padVars[i] = IndexedName("padKey", i);
            _padKeyNames[i] = IndexedName("PadKey", i);
            _padKeys[i] = -1;
        }
        for (size_t i = 0; i < TextEntry::kPadRowCount; ++i) {
            _padRowNames[i] = IndexedName("PadRow", i);
            _padRows[i] = -1;
        }
        SubscribeInScene<InputEvents::ActionEvent, &MahjongOnlineComponent::OnAction>(this);
        SubscribeInScene<SDLEvents::KeyDownEvent, &MahjongOnlineComponent::OnKeyDown>(this);
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &MahjongOnlineComponent::OnUpdate>(this);
    }

    // ---- Frame -------------------------------------------------------------------------------------------------------------------

    void MahjongOnlineComponent::OnUpdate(const SceneEvents::SceneUpdateEvent& /*event*/) {
        BE_PROFILE_SCOPE("MahjongOnline::OnUpdate");
        MahjongGame* game = MahjongGame::GetActive();
        if (game == nullptr) {
            return;
        }
        OnlineClient& client = game->GetOnline();
        client.Update();
        game->SyncOnlineTable();  // before Follow: a table that goes live closes the panel
        Follow(*game);
        _screens.Sync(client);
        _screens.Refresh(CoreManager::GetUIStrings());
    }

    void MahjongOnlineComponent::OnDraw() {
        BE_PROFILE_SCOPE("MahjongOnline::OnDraw");
        MahjongGame* game = MahjongGame::GetActive();
        if (!_setupButtonChecked) {
            _setupButtonChecked = true;
            if (OnlineServerUrl().empty()) {
                // No address of a server (the browser build without `webServer` in `MahjongOnlineConfig`): the button is not offered
                UICanvasComponent::SetElementVisibleByName(GetScene(), PoolString::Intern(kSetupOnlineButton), false);
            }
        }
        if (game != nullptr) {
            Follow(*game);
        }
        Publish(game);
    }

    void MahjongOnlineComponent::Follow(MahjongGame& game) {
        if (_closeRequested) {
            _closeRequested = false;
            game.CloseOnline();
        }
        if (game.IsOnlineOpen() && !_screens.IsOpen()) {
            OpenPages(game);
        } else if (!game.IsOnlineOpen() && _screens.IsOpen()) {
            _screens.Close();
        }
    }

    void MahjongOnlineComponent::OpenPages(MahjongGame& game) {
        const UIStrings& strings = CoreManager::GetUIStrings();
        const OnlineValues& stored = game.StoredOnline();
        _screens.Refresh(strings);
        _screens.Sync(game.GetOnline());  // the revision of this client is known before a stored room is offered
        if (!stored.name.empty()) {
            _screens.SetStoredName(stored.name);
        }
        _screens.SetStoredRoom(stored.session.IsValid() ? eastl::string_view(stored.session.room.data(), stored.session.room.size()) : eastl::string_view());
        _screens.Open(game.GetOnline(), game.TakeRoomEndNotice());
        _screens.Refresh(strings);
    }

    void MahjongOnlineComponent::StoreName(MahjongGame& game) const {
        if (_screens.NameIsOwn()) {
            game.SetOnlineName(_screens.Name().Text());
        }
    }

    // ---- Input -------------------------------------------------------------------------------------------------------------------

    void MahjongOnlineComponent::OnKeyDown(const SDLEvents::KeyDownEvent& event) {
        MahjongGame* game = MahjongGame::GetActive();
        if (game == nullptr || !game->IsOnlineOpen() || !_screens.IsOpen()) {
            return;
        }
        if (event.key == SDLK_ESCAPE) {
            // The Menu was the last page: the game's flag goes at the next `Follow`, so the HUD, which hears this Escape too, does not take it for its own
            if (!event.repeat && _screens.Back() && !_screens.IsOpen()) {
                _closeRequested = true;
            }
        } else if (_screens.Page() == OnlinePage::Entry) {
            if (event.key == SDLK_BACKSPACE) {
                _screens.Backspace();
            } else if (event.repeat) {
                // A held key types once; Enter does not repeat
            } else if (event.key == SDLK_RETURN || event.key == SDLK_KP_ENTER) {
                if (_screens.Done()) {
                    StoreName(*game);
                }
            } else if (const std::optional<char> typed = TextEntry::CharFromKeycode(static_cast<uint32_t>(event.key), (event.mod & SDL_KMOD_SHIFT) != 0)) {
                const char character = *typed;
                _screens.Type(eastl::string_view(&character, 1));
            }
        }
        _screens.Refresh(CoreManager::GetUIStrings());
    }

    void MahjongOnlineComponent::OnAction(const InputEvents::ActionEvent& event) {
        BE_PROFILE_SCOPE("MahjongOnline::OnAction");
        MahjongGame* game = MahjongGame::GetActive();
        if (game == nullptr) {
            return;
        }
        const eastl::string_view action = event.action.ToStringView();
        if (action == kOpenAction) {
            // The button of `Setup`: under the panel, without `Setup` or with no server address it cannot be pressed, an event that says otherwise is out of date
            if (game->IsSetupOpen() && !game->IsOnlineOpen() && !OnlineServerUrl().empty()) {
                game->OpenOnline();
                Follow(*game);
            }
            return;
        }
        if (!game->IsOnlineOpen() || !_screens.IsOpen()) {
            return;
        }
        OnlineClient& client = game->GetOnline();
        const eastl::string_view value = event.value.ToStringView();
        if (action == kCloseAction) {
            game->CloseOnline();
            _screens.Close();
        } else if (action == kHostAction) {
            _screens.ShowHost();
        } else if (action == kJoinAction) {
            _screens.ShowJoin();
        } else if (action == kResumeAction) {
            const RoomSession& session = SessionToReturnTo(*game, client);
            if (session.IsValid()) {
                _screens.Resume(client, session);
            }
        } else if (action == kFieldAction) {
            if (value == kNameValue || value == kCodeValue) {
                _screens.FocusField(value == kNameValue ? OnlineField::Name : OnlineField::Code);
            }
        } else if (action == kKeyAction) {
            if (const std::optional<size_t> index = ParseIndex(value)) {
                _screens.PressKey(*index);
            }
        } else if (action == kBackspaceAction) {
            _screens.Backspace();
        } else if (action == kDoneAction) {
            if (_screens.Done()) {
                StoreName(*game);
            }
        } else if (action == kHandsAction) {
            // Only the two numbers the panel offers; client text is never parsed into another number
            if (const std::optional<size_t> hands = ParseIndex(value); hands && (*hands == kFullMatchHands || *hands == kShortMatchHands)) {
                _screens.SetHands(static_cast<int>(*hands));
            }
        } else if (action == kLevelAction) {
            if (const std::optional<ComputerLevel> level = TryParseLevel(std::string_view(value.data(), value.size()))) {
                _screens.SetLevel(LevelNumber(*level));
            }
        } else if (action == kHintsAction) {
            // Only the two ids the panel offers
            if (value == kHintsAllowedValue || value == kHintsOffValue) {
                _screens.SetHintsAllowed(value == kHintsAllowedValue);
            }
        } else if (action == kCreateAction) {
            _screens.Create(client, OnlineServerUrl());
            StoreName(*game);
        } else if (action == kEnterAction) {
            _screens.Enter(client, OnlineServerUrl());
            StoreName(*game);
        } else if (action == kSitAction) {
            if (const std::optional<size_t> seat = ParseIndex(value); seat && *seat < kSeatCount) {
                _screens.Sit(client, static_cast<int>(*seat));
            }
        } else if (action == kStartAction) {
            _screens.Start(client);
        } else if (action == kLeaveAction) {
            _screens.Leave(client);
        }
        _screens.Refresh(CoreManager::GetUIStrings());
    }

    // ---- Publishing --------------------------------------------------------------------------------------------------------------

    void MahjongOnlineComponent::SetItem(size_t item, bool visible) {
        if (_items[item] != static_cast<int8_t>(visible)) {
            _items[item] = static_cast<int8_t>(visible);
            UICanvasComponent::SetElementVisibleByName(GetScene(), _itemNames[item], visible);
        }
    }

    void MahjongOnlineComponent::SetPadKey(size_t key, bool visible) {
        if (_padKeys[key] != static_cast<int8_t>(visible)) {
            _padKeys[key] = static_cast<int8_t>(visible);
            UICanvasComponent::SetElementVisibleByName(GetScene(), _padKeyNames[key], visible);
        }
    }

    void MahjongOnlineComponent::SetPadRow(size_t row, bool visible) {
        if (_padRows[row] != static_cast<int8_t>(visible)) {
            _padRows[row] = static_cast<int8_t>(visible);
            UICanvasComponent::SetElementVisibleByName(GetScene(), _padRowNames[row], visible);
        }
    }

    void MahjongOnlineComponent::Publish(MahjongGame* game) {
        const bool open = game != nullptr && game->IsOnlineOpen() && _screens.IsOpen();
        const OnlinePage page = open ? _screens.Page() : OnlinePage::Closed;
        const bool onlineShown = open && page != OnlinePage::Entry;
        const bool entryShown = open && page == OnlinePage::Entry;
        Scene& scene = GetScene();
        if (onlineShown != _onlineShown) {
            _onlineShown = onlineShown;
            UICanvasComponent::SetVisibleByName(scene, PoolString::Intern(kOnlineCanvas), onlineShown);
        }
        if (entryShown != _entryShown) {
            _entryShown = entryShown;
            UICanvasComponent::SetVisibleByName(scene, PoolString::Intern(kEntryCanvas), entryShown);
        }
        if (!open) {
            _published = false;
            _padPublished = false;
            return;
        }

        const OnlineClient& client = game->GetOnline();
        const UIStrings& strings = CoreManager::GetUIStrings();
        if (_published && _publishedFor == game && _publishedScreens == _screens.Version() && _publishedClient == client.Version() && _publishedStrings == strings.GetVersion()) {
            return;
        }
        _published = true;
        _publishedFor = game;
        _publishedScreens = _screens.Version();
        _publishedClient = client.Version();
        _publishedStrings = strings.GetVersion();

        PublishVariables(client, strings);
        if (page == OnlinePage::Entry) {
            PublishPad(strings);
        } else {
            _padPublished = false;
            PublishPages(client);
        }
    }

    void MahjongOnlineComponent::PublishVariables(const OnlineClient& client, const UIStrings& strings) {
        UIVars& vars = CoreManager::GetUIVars();
        const OnlineLobby& lobby = client.Lobby();
        vars.Set("onlineNotice"_intern, eastl::string_view(_screens.NoticeKey().empty() ? eastl::string() : OnlineNoticeText(strings, _screens.NoticeKey(), _screens.NoticeArg())));
        vars.Set("onlineStatus"_intern, eastl::string_view(_screens.StatusKey().empty() ? eastl::string() : Tr(strings, _screens.StatusKey())));
        vars.Set("onlineName"_intern, eastl::string_view(_screens.Name().Text()));
        vars.Set("onlineCode"_intern, eastl::string_view(_screens.Code().Text()));
        vars.Set("onlineEntryValue"_intern, eastl::string_view(_screens.Focused().Text()));
        vars.Set("onlineHands"_intern, _screens.Options().hands);
        vars.Set("onlineLevel"_intern, _screens.Options().level);
        vars.Set("onlineHints"_intern, _screens.Options().hintsAllowed ? kHintsAllowedValue : kHintsOffValue);
        const OnlineRoomOptions* hosted = client.HostedOptions();
        vars.Set("onlineHostLine"_intern, eastl::string_view(hosted == nullptr ? eastl::string() : Tr(strings, hosted->hintsAllowed ? kOnlineLobbyHintsAllowedKey : kOnlineLobbyHintsOffKey)));
        // A guest reads the host's choice about hints from the lobby, on the one line under the title
        const std::optional<bool> guestHints = lobby.hintsAllowed;
        vars.Set("onlineWaiting"_intern, eastl::string_view(Tr(strings, !guestHints ? kOnlineWaitHostKey : (*guestHints ? kOnlineWaitHostHintsAllowedKey : kOnlineWaitHostHintsOffKey))));
        // The room: the title of the lobby while there is one, else the room `Return` would enter
        const eastl::string& room = _screens.Page() == OnlinePage::Lobby ? (lobby.room.empty() ? client.Room() : lobby.room) : _screens.ReturnRoom();
        vars.Set("onlineRoom"_intern, eastl::string_view(room));
        for (size_t seat = 0; seat < kSeatCount; ++seat) {
            const OnlineLobbySeat& at = lobby.seats[seat];
            vars.Set(_seatVars[seat], eastl::string_view(OnlineSeatText(strings, at.kind, at.name, at.mine, at.connected)));
        }
    }

    void MahjongOnlineComponent::PublishPages(const OnlineClient& client) {
        const OnlinePage page = _screens.Page();
        const OnlineLobby& lobby = client.Lobby();
        const bool menu = page == OnlinePage::Menu;
        const bool host = page == OnlinePage::Host;
        const bool join = page == OnlinePage::Join;
        const bool inLobby = page == OnlinePage::Lobby;
        SetItem(TitleMenu, menu);
        SetItem(TitleHost, host);
        SetItem(TitleJoin, join);
        SetItem(TitleLobby, inLobby);
        SetItem(MenuHost, menu);
        SetItem(MenuJoin, menu);
        SetItem(MenuReturn, menu && _screens.CanReturn());
        SetItem(NameRow, host || join);
        SetItem(CodeRow, join);
        SetItem(HandsRow, host);
        SetItem(LevelRow, host);
        SetItem(HintsRow, host);

        // One line under the title: the notice, else the status, else (in the lobby) who is waited for
        const bool notice = !_screens.NoticeKey().empty();
        const bool status = !notice && !_screens.StatusKey().empty();
        const OnlineStage stage = client.Stage();
        const bool lobbyLine = !notice && !status && inLobby;
        SetItem(Notice, notice);
        SetItem(Status, status);
        SetItem(Waiting, lobbyLine && stage == OnlineStage::Lobby && !client.IsHost());
        // The slot of "Waiting for the host" is free for the host: it says what he decided about hints
        SetItem(HostLine, lobbyLine && stage == OnlineStage::Lobby && client.IsHost() && client.HostedOptions() != nullptr);

        const bool seats = inLobby && !lobby.room.empty();
        for (size_t seat = 0; seat < kSeatCount; ++seat) {
            SetItem(SeatRow0 + seat, seats);
            SetItem(Sit0 + seat, seats && client.CanChoose(static_cast<int>(seat)));
        }
        SetItem(Gap, !menu);
        SetItem(Buttons, !menu);
        SetItem(Create, host);
        SetItem(Enter, join);
        SetItem(Start, inLobby && lobby.canStart && stage == OnlineStage::Lobby);
        SetItem(Leave, inLobby);
    }

    void MahjongOnlineComponent::PublishPad(const UIStrings& strings) {
        const OnlineField field = _screens.Focus();
        SetItem(EntryCaptionName, field == OnlineField::Name);
        SetItem(EntryCaptionCode, field == OnlineField::Code);
        if (_padPublished && _padField == field && _padStrings == strings.GetVersion()) {
            return;
        }
        _padPublished = true;
        _padField = field;
        _padStrings = strings.GetVersion();
        const eastl::vector<eastl::string>& pad = _screens.Pad();
        UIVars& vars = CoreManager::GetUIVars();
        for (size_t key = 0; key < TextEntry::kPadMaxKeys; ++key) {
            const bool has = key < pad.size();
            vars.Set(_padVars[key], has ? eastl::string_view(pad[key]) : eastl::string_view());
            SetPadKey(key, has);
        }
        for (size_t row = 0; row < TextEntry::kPadRowCount; ++row) {
            SetPadRow(row, row * TextEntry::kPadKeysPerRow < pad.size());
        }
    }

}  // namespace BECore::Mahjong
