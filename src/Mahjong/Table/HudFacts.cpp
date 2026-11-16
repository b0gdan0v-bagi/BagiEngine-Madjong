#include "HudFacts.h"

#include <Generated/HudFacts.gen.hpp>

namespace BECore::Mahjong {

    HudFacts MakeHudFacts(const Match& match, const TableSession& session, bool roomLineUp) {
        HudFacts facts;
        if (match.GetPhase() == MatchPhase::NotStarted) {
            return facts;
        }
        facts.matchOver = match.GetPhase() == MatchPhase::Finished;
        facts.hasLastMove = roomLineUp || (session.GetPhase() == SessionPhase::Playing && !match.Hand().Moves().empty());
        return facts;
    }

}  // namespace BECore::Mahjong
