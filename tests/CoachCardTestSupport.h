#pragma once

#include "MahjongTableTestSupport.h"
#include "StringsTestSupport.h"

#include <Mahjong/Table/HintCard.h>
#include <string>
#include <vector>

/**
 * The variables of the card of the coach (`CoachCard`, node `HintsWhy`) for the scene tests: their names in one order, what a `HintCard`
 * puts into them, what they hold now, and the longest text of each over a set of cards (for the size matrix).
 */
namespace BECore::Mahjong::Tests {

    /// The names of the variables of the card: the title, the head, each cell of each row, the lines under the rows.
    inline std::vector<std::string> CardVarNames() {
        std::vector<std::string> names{"cardTitle", "cardNow"};
        for (const char* cell : {"cardWho", "cardMove", "cardSteps", "cardUseful"}) {
            for (size_t i = 0; i < kMaxCardRows; ++i) {
                names.push_back(cell + std::to_string(i));
            }
        }
        for (size_t i = 0; i < kMaxCardFooter; ++i) {
            names.push_back("cardFoot" + std::to_string(i));
        }
        return names;
    }

    /// What `card` puts into the variables, in the order of `CardVarNames`, empty where it has nothing.
    inline std::vector<std::string> CardTexts(const HintCard& card) {
        const auto text = [](const eastl::string& value) { return std::string(value.data(), value.size()); };
        std::vector<std::string> texts{text(card.title), text(card.now)};
        const auto cells = [&](auto member) {
            for (size_t i = 0; i < kMaxCardRows; ++i) {
                texts.push_back(i < card.rows.size() ? text(card.rows[i].*member) : std::string());
            }
        };
        cells(&CardRow::who);
        cells(&CardRow::move);
        cells(&CardRow::steps);
        cells(&CardRow::useful);
        for (size_t i = 0; i < kMaxCardFooter; ++i) {
            texts.push_back(i < card.footer.size() ? text(card.footer[i]) : std::string());
        }
        return texts;
    }

    /// What the variables of the card hold now.
    inline std::vector<std::string> CardVars() {
        std::vector<std::string> values;
        for (const std::string& name : CardVarNames()) {
            values.push_back(Var(name.c_str()));
        }
        return values;
    }

    inline bool NoCardText() {
        for (const std::string& value : CardVars()) {
            if (!value.empty()) {
                return false;
            }
        }
        return true;
    }

    /// Sets every variable of the card to the longest text it takes over `cards` (each cell of every row to the longest of that cell).
    inline void SetLongestCardTexts(const std::vector<HintCard>& cards) {
        const std::vector<std::string> names = CardVarNames();
        std::vector<std::vector<eastl::string>> candidates(names.size());
        for (const HintCard& card : cards) {
            const std::vector<std::string> texts = CardTexts(card);
            for (size_t at = 0; at < texts.size(); ++at) {
                candidates[at].emplace_back(texts[at].c_str());
            }
        }
        // A cell of a row takes the longest of that cell in any row, a line under the rows the longest of any line
        const auto widen = [&](size_t first, size_t count) {
            std::vector<eastl::string> all;
            for (size_t at = first; at < first + count; ++at) {
                all.insert(all.end(), candidates[at].begin(), candidates[at].end());
            }
            for (size_t at = first; at < first + count; ++at) {
                candidates[at] = all;
            }
        };
        for (size_t cell = 0; cell < 4; ++cell) {
            widen(2 + cell * kMaxCardRows, kMaxCardRows);
        }
        widen(2 + 4 * kMaxCardRows, kMaxCardFooter);
        for (size_t at = 0; at < names.size(); ++at) {
            const eastl::string longest = LongestText(candidates[at]);
            CoreManager::GetUIVars().Set(PoolString::Intern(eastl::string_view(names[at].data(), names[at].size())), eastl::string_view(longest));
        }
    }

}  // namespace BECore::Mahjong::Tests
