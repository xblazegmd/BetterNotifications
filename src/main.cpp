#include <Geode/Geode.hpp>

#include <Geode/utils/async.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/web.hpp>

#include <arc/prelude.hpp>
#include <asp/time.hpp>

using namespace geode::prelude;

struct NotificationObject {
    std::string title;
    std::string desc;
    std::string icon;
};

class NotificationManager : public CCNode {
public:
    static NotificationManager* create() {
        auto ret = new NotificationManager();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
protected:
    async::TaskHolder<web::WebResponse> m_fetchTask;
    asp::Instant m_nextFetch = asp::Instant::now();
    std::vector<NotificationObject> m_queue;

    const std::unordered_map<std::string, std::string> m_emoteMap = {
        { "cube", "particle_213_001.png" },
        { "star", "particle_47_001.png" },
        { "featured", "particle_227_001.png" },
        { "epic", "particle_228_001.png" },
        { "legendary", "particle_229_001.png" },
        { "mythic", "particle_230_001.png" },
        { "download", "particle_109_001.png" },
        { "party", "particle_255_001.png" },
        { "handshake", "particle_302_001.png" },
        { "mail", "particle_207_001.png" }
    };

    bool init() {
        if (!CCNode::init()) return false;
        return true;
    }

    void update(float) {
        auto mod = Mod::get();
        if (m_nextFetch <= asp::Instant::now()) {
            fetchNotifications();
            m_nextFetch += asp::Duration::fromSecs(mod->getSettingValue<int>("interval"));
        }

        if (
            (mod->getSettingValue<bool>("notify-while-playing") && PlayLayer::get()) ||
            (mod->getSettingValue<bool>("notify-while-on-editor") && LevelEditorLayer::get())
        ) return;

        if (!m_queue.empty()) {
            for (const auto& notif : m_queue) {
                AchievementNotifier::sharedState()->notifyAchievement(
                    notif.title.c_str(),
                    notif.desc.c_str(),
                    notif.icon.c_str(),
                    true
                );
            }
            m_queue.clear();
        }
    }

    void fetchNotifications() {
        auto accMan = GJAccountManager::get();
        if (accMan->m_accountID == 0 || accMan->m_GJP2.empty()) {
            log::error("You are not logged in");
            return;
        }

        auto req = web::WebRequest()
            .userAgent("")
            .bodyString(fmt::format(
                "accountID={}&gjp2={}&secret=Wmfd2893gb7&mask={}",
                accMan->m_accountID,
                accMan->m_GJP2,
                GameManager::get()->getIntGameVariable(GameVar::NotificationsMask)
            ))
            .timeout(std::chrono::seconds(10));

        m_fetchTask.spawn(
            "fetchNotifications",
            req.post("https://www.boomlings.com/database/getGJNotifications.php"),
            [this](web::WebResponse res) {
                if (!res.ok()) {
                    if (res.code() == 429) {
                        log::error("Rate limited by rob's servers");
                        this->unscheduleUpdate();
                        return;
                    }
                    log::error("Failed to fetch notifications: {}", res.code());
                    return;
                }

                if (res.string().isErr()) {
                    log::error("Could not get response from server: {}", res.string().unwrapErr());
                }
                std::string str = res.string().unwrap();

                auto status = utils::numFromString<int>(str);
                if (status.isOk() && status.unwrap() < 0) {
                    log::error("Failed to fetch notifications: {}", str);
                    return;
                }

                auto meta = string::split(str, "#"); // Remove metadata
                auto notifications = string::split(meta[0], "|");

                for (const auto& notification : notifications) {
                    auto notif = parseKeyVal(notification);

                    // Field 7 = new notification
                    if (notif["7"] == "1") {
                        auto title = base64::decodeString(notif["4"]);
                        auto desc = base64::decodeString(notif["5"]);

                        if (title.isErr()) {
                            log::error("Failed to decode base64 for title: {}", title.unwrapErr());
                            continue;
                        }

                        if (desc.isErr()) {
                            log::error("Failed to decode base64 for description: {}", desc.unwrapErr());
                            continue;
                        }

                        m_queue.emplace_back(std::move(title).unwrap(), std::move(desc).unwrap(), getEmoteTexture(notif["3"]));
                    }
                }
            }
        );
    }

    std::unordered_map<std::string, std::string> parseKeyVal(const std::string& res) {
        auto pieces = string::split(res, ":");
        std::unordered_map<std::string, std::string> ret;
        ret.reserve(pieces.size() / 2);

        for (size_t i = 0; i + 1 < pieces.size(); i += 2) {
            ret.emplace(pieces[i], pieces[i + 1]);
        }
        return ret;
    }

    std::string getEmoteTexture(const std::string& icon) {
        auto it = m_emoteMap.find(icon);
        return it != m_emoteMap.end() ? it->second : m_emoteMap.at("mail");
    }
};

$on_game(Loaded) {
    CCScheduler::get()->scheduleUpdateForTarget(NotificationManager::create(), INT_MAX, false);
}
