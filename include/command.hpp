#pragma once
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <memory>
#include <sstream>

class VimEngine;

struct CommandContext {
    VimEngine& engine;
    std::string name;
    std::string args;
    std::vector<std::string> argv;
};

using CommandHandler = std::function<void(CommandContext&)>;

struct CommandDef {
    std::vector<std::string> names;
    std::string description;
    CommandHandler handler;
};

class CommandRegistry {
public:
    static CommandRegistry& instance() {
        static CommandRegistry reg;
        return reg;
    }

    void register_cmd(const std::vector<std::string>& names,
                      const std::string& description,
                      CommandHandler handler) {
        auto def = std::make_shared<CommandDef>(CommandDef{names, description, std::move(handler)});
        for (const auto& name : names) {
            command_map[name] = def;
        }
    }

    bool execute(VimEngine& engine, const std::string& cmd_line) {
        std::istringstream iss(cmd_line);
        std::string cmd;
        if (!(iss >> cmd)) return false;

        auto it = command_map.find(cmd);
        if (it == command_map.end()) {
            return false;
        }

        std::string args;
        std::string token;
        std::vector<std::string> argv;
        while (iss >> token) {
            if (!args.empty()) args += " ";
            args += token;
            argv.push_back(token);
        }

        CommandContext ctx{engine, cmd, args, argv};
        it->second->handler(ctx);
        return true;
    }

    std::vector<std::string> get_completions(const std::string& prefix) const {
        std::vector<std::string> matches;
        for (const auto& pair : command_map) {
            if (pair.first.rfind(prefix, 0) == 0) {
                matches.push_back(pair.first);
            }
        }
        return matches;
    }

    const std::map<std::string, std::shared_ptr<CommandDef>>& get_commands() const {
        return command_map;
    }

private:
    std::map<std::string, std::shared_ptr<CommandDef>> command_map;
};

struct CommandAutoRegistrar {
    CommandAutoRegistrar(const std::string& name,
                         const std::string& desc,
                         CommandHandler handler) {
        CommandRegistry::instance().register_cmd({name}, desc, std::move(handler));
    }
    CommandAutoRegistrar(const std::vector<std::string>& names,
                         const std::string& desc,
                         CommandHandler handler) {
        CommandRegistry::instance().register_cmd(names, desc, std::move(handler));
    }
};

#define REGISTER_COMMAND(cmd_var_name, names, description, ...) static CommandAutoRegistrar _reg_##cmd_var_name(names, description, __VA_ARGS__);