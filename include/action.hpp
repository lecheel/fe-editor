#pragma once
#include "types.hpp"
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <memory>
#include <unordered_map>

class VimEngine;

struct ActionContext {
    VimEngine& engine;
    Mode mode;
    std::string triggered_name;
};

using ActionHandler = std::function<bool(ActionContext&)>;

struct ActionDef {
    std::string canonical_name;
    std::vector<std::string> aliases;
    std::string category;
    std::string description;
    ActionHandler handler;
};

class ActionRegistry {
public:
    static ActionRegistry& instance() {
        static ActionRegistry reg;
        reg.ensure_init();
        return reg;
    }

    void ensure_init() {
        if (!initialized) {
            initialized = true;
            init_default_actions();
        }
    }

    void init_default_actions();

    void register_action(const std::string& canonical_name,
                         const std::vector<std::string>& aliases,
                         const std::string& category,
                         const std::string& description,
                         ActionHandler handler) {
        auto def = std::make_shared<ActionDef>(ActionDef{
            canonical_name, aliases, category, description, std::move(handler)
        });

        canonical_map[canonical_name] = def;
        alias_map[canonical_name] = def;
        for (const auto& alias : aliases) {
            alias_map[alias] = def;
        }
    }

    bool execute(const std::string& action_name, VimEngine& engine, Mode mode) {
        ensure_init();
        auto it = alias_map.find(action_name);
        if (it == alias_map.end()) {
            return false;
        }
        ActionContext ctx{engine, mode, action_name};
        return it->second->handler(ctx);
    }

    const std::map<std::string, std::shared_ptr<ActionDef>>& get_canonical_actions() {
        ensure_init();
        return canonical_map;
    }

    const std::unordered_map<std::string, std::shared_ptr<ActionDef>>& get_all_aliases() {
        ensure_init();
        return alias_map;
    }

private:
    bool initialized{false};
    std::map<std::string, std::shared_ptr<ActionDef>> canonical_map;
    std::unordered_map<std::string, std::shared_ptr<ActionDef>> alias_map;
};

struct ActionAutoRegistrar {
    ActionAutoRegistrar(const std::string& name,
                        const std::vector<std::string>& aliases,
                        const std::string& category,
                        const std::string& desc,
                        ActionHandler handler) {
        ActionRegistry::instance().register_action(name, aliases, category, desc, std::move(handler));
    }
};

#define REGISTER_ACTION(var_name, canonical_name, aliases, category, desc, ...) \

