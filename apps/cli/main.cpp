// AsiaCAD CLI
//   asiacad                              — interaktiv rejim
//   asiacad skript.acs -o natija.dxf     — skriptni bajarib, DXF ga saqlash
//   --lang uz|ru|kk|ky|tr|en             — interfeys tili (yoki ASIACAD_LANG muhit o'zgaruvchisi)
//   --i18n <papka>                       — lug'atlar papkasi (standart: dastur yonidagi i18n/)
#include <asiacad/io/dwg_bridge.h>
#include "interpreter.h"

#include <asiacad/io/dxf_writer.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {
fs::path findI18nDir(const std::string& cli, const char* argv0) {
    std::vector<fs::path> candidates;
    if (!cli.empty()) candidates.emplace_back(cli);
    std::error_code ec;
    const fs::path exe = fs::weakly_canonical(fs::path(argv0), ec);
    if (!ec) candidates.push_back(exe.parent_path() / "i18n");
    candidates.emplace_back("i18n");
#ifdef ASIACAD_I18N_DIR
    candidates.emplace_back(ASIACAD_I18N_DIR);  // ishlab chiqish paytida manba papkasi
#endif
    for (const auto& c : candidates)
        if (fs::is_directory(c, ec)) return c;
    return {};
}
} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    if (argc > 0) asiacad::dwg::useBundled(std::filesystem::absolute(argv[0]).parent_path().string());
    std::string script, outPath, lang, i18nDir;
    bool showHelp = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "-o" || a == "--output") && i + 1 < argc) outPath = argv[++i];
        else if (a == "--lang" && i + 1 < argc) lang = argv[++i];
        else if (a == "--i18n" && i + 1 < argc) i18nDir = argv[++i];
        else if (a == "-h" || a == "--help") showHelp = true;
        else script = a;
    }
    if (lang.empty())
        if (const char* env = std::getenv("ASIACAD_LANG")) lang = env;
    if (lang.empty()) lang = "uz";

    asiacad::i18n::Localizer loc;
    if (const fs::path dir = findI18nDir(i18nDir, argv[0]); !dir.empty()) {
        std::vector<std::string> errors;
        loc.loadDirectory(dir, &errors);
        for (const auto& e : errors) std::cerr << "i18n: " << e << '\n';
    }
    if (loc.languages().empty()) {
        std::cerr << "i18n: dictionaries not found, English command names only\n";
    } else if (!loc.setLanguage(lang)) {
        std::cerr << "i18n: unknown language '" << lang << "', using en\n";
        loc.setLanguage("en");
    }

    if (showHelp) {
        std::cout << loc.tr("app.usage") << '\n';
        return 0;
    }

    asiacad::Document doc;
    asiacad::CommandStack stack(doc);
    asiacad::cli::Interpreter interp(doc, stack, loc, std::cout);

    if (!script.empty()) {
        std::ifstream f(script);
        if (!f) {
            std::cerr << loc.tr("app.script_open_failed", {script}) << '\n';
            return 1;
        }
        std::string line;
        int n = 0;
        while (std::getline(f, line)) {
            ++n;
            if (line.find_first_not_of(" \t\r") != std::string::npos && line[0] != '#')
                std::cout << "[" << n << "] " << line << '\n';
            if (!interp.run(line)) break;
        }
    } else {
        std::cout << loc.tr("app.banner", {"0.31"}) << '\n';
        std::string line;
        while (std::cout << loc.tr("app.prompt") << std::flush, std::getline(std::cin, line))
            if (!interp.run(line)) break;
    }

    if (!outPath.empty()) {
        if (!asiacad::saveDxf(doc, outPath)) {
            std::cerr << loc.tr("app.dxf_failed", {outPath}) << '\n';
            return 1;
        }
        std::cout << loc.tr("app.dxf_written", {outPath}) << '\n';
    }
    return 0;
}
