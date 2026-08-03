#pragma once
#include <string>
#include <unordered_map>

namespace dfts::chat {

// Emotes from the community services (7TV, BetterTTV, FrankerFaceZ) that most
// Twitch chats rely on. Twitch's IRC tags only describe first-party emotes, so
// these are matched by word against the message text instead.
struct ThirdPartyEmote {
    std::string name;
    std::string url;
    std::string provider;   // "7TV", "BTTV", "FFZ"
    bool        animated = false;
};

// name -> emote. Word matching in chat is case sensitive, which is what all
// three services use.
using EmoteTable = std::unordered_map<std::string, ThirdPartyEmote>;

// Blocking network calls; run these on a worker thread.
//
// Each provider is fetched independently and a failure only costs that
// provider's emotes: chats routinely have all three and none of them are
// required for the message to render.
EmoteTable fetchGlobalEmotes();
EmoteTable fetchChannelEmotes(const std::string& twitchUserId);

// Merges `extra` into `base`, letting `extra` win. Channel emotes are loaded
// after the global ones and are expected to shadow them.
void mergeEmotes(EmoteTable& base, const EmoteTable& extra);

} // namespace dfts::chat
