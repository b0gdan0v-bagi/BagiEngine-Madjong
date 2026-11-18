#include "HudValues.h"

#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Values/ValueLinkScope.h>
#include <Generated/HudValues.gen.hpp>

namespace BECore::Mahjong {

    HudValuesParse ParseHudValues(const pugi::xml_node& root, StringView documentName) {
        HudValuesParse parse;
        XmlDeserializer reader;
        reader.SetDiagnostics(DiagnosticsMode::Error);
        if (!reader.LoadFromXmlNode(XmlNode(root))) {
            parse.errors.push_back(Format("Config '{}': the document has no root element", documentName));
            return parse;
        }
        // The reader takes the first `<context>` and does not look at the others
        constexpr const char* kContextElement = "context";
        if (std::distance(root.children(kContextElement).begin(), root.children(kContextElement).end()) > 1) {
            parse.errors.push_back(Format("Config '{}': context: a document declares its context once", documentName));
        }
        {
            const ValueLinkScope scope(documentName);
            parse.values.Deserialize(reader);
            reader.FinishLoad();
        }
        for (const DeserializeError& error : reader.GetErrors()) {
            const StringView path = error.path;
            const String where = path.empty() ? String() : Format("{}: ", path);
            const String line = error.line != 0 ? Format(" (line {})", error.line) : String();
            parse.errors.push_back(Format("Config '{}': {}{}{}", documentName, where, error.errorMessage, line));
        }
        return parse;
    }

}  // namespace BECore::Mahjong
