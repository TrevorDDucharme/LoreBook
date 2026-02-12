#include <Editors/Markdown/Effect.hpp>
#include <plog/Log.h>

namespace Markdown {

// ────────────────────────────────────────────────────────────────────
// EffectRegistry implementation
// ────────────────────────────────────────────────────────────────────

EffectRegistry& EffectRegistry::get() {
    static EffectRegistry instance;
    return instance;
}

void EffectRegistry::registerFactory(const std::string& name, EffectFactory factory) {
    if (m_factories.count(name)) {
        PLOG_WARNING << "Effect factory already registered: " << name;
        return;
    }
    m_factories[name] = factory;
    PLOG_DEBUG << "Registered effect factory: " << name;
}

std::unique_ptr<Effect> EffectRegistry::create(const std::string& name) const {
    auto it = m_factories.find(name);
    if (it == m_factories.end()) {
        PLOG_WARNING << "Unknown effect type: " << name;
        return nullptr;
    }
    return it->second();
}

std::vector<std::string> EffectRegistry::getRegisteredNames() const {
    std::vector<std::string> names;
    names.reserve(m_factories.size());
    for (const auto& [name, _] : m_factories) {
        names.push_back(name);
    }
    return names;
}

} // namespace Markdown
