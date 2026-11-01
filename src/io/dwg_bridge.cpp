#include <asiacad/io/dwg_bridge.h>

#include <asiacad/core/error.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace asiacad::dwg {

namespace {
#ifdef _WIN32
constexpr char kSep = ';';
const char* kExeExt = ".exe";
#else
constexpr char kSep = ':';
const char* kExeExt = "";
#endif

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string inPath(const std::string& name) {
    const char* p = std::getenv("PATH");
    if (!p) return {};
    std::string all = p;
    std::size_t start = 0;
    while (start <= all.size()) {
        const auto end = all.find(kSep, start);
        const std::string dir = all.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!dir.empty()) {
            std::error_code ec;
            const fs::path f = fs::path(dir) / (name + kExeExt);
            if (fs::is_regular_file(f, ec)) return f.string();
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return {};
}

std::string quote(const std::string& s) { return "\"" + s + "\""; }

int run(const std::string& cmd) {
#ifdef _WIN32
    return std::system(("\"" + cmd + "\"").c_str());   // cmd.exe tashqi qo'shtirnoqni olib tashlaydi
#else
    return std::system((cmd + " >/dev/null 2>&1").c_str());
#endif
}

fs::path tempDir() {
    std::mt19937_64 rng(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path d = fs::temp_directory_path() / ("asiacad_dwg_" + std::to_string(rng() % 100000000ULL));
    fs::create_directories(d / "in");
    fs::create_directories(d / "out");
    return d;
}

// ODA: butun papkani aylantiradi — faylni bo'sh papkaga ko'chirib olinadi
fs::path odaConvert(const Converter& c, const fs::path& src, const std::string& outType, const std::string& version) {
    const fs::path d = tempDir();
    fs::copy_file(src, d / "in" / src.filename(), fs::copy_options::overwrite_existing);
    const std::string filter = outType == "DXF" ? "*.DWG" : "*.DXF";
    run(quote(c.exe) + " " + quote((d / "in").string()) + " " + quote((d / "out").string()) + " " + version + " " + outType + " 0 1 " + quote(filter));
    fs::path out = d / "out" / src.filename();
    out.replace_extension(outType == "DXF" ? ".dxf" : ".dwg");
    std::error_code ec;
    if (!fs::exists(out, ec)) {  // ba'zi versiyalar kengaytmani katta harf bilan yozadi
        for (const auto& e : fs::directory_iterator(d / "out", ec))
            if (lower(e.path().extension().string()) == (outType == "DXF" ? ".dxf" : ".dwg")) return e.path();
        throw Error("dwg.err.convert", {src.string()});
    }
    return out;
}
}  // namespace

void useBundled(const std::string& exeDir) {
    if (const char* env = std::getenv("ASIACAD_DWG_CONVERTER"); env && *env) return;
    std::error_code ec;
    const fs::path p = fs::path(exeDir) / "libredwg" / (std::string("dwg2dxf") + kExeExt);
    if (!fs::exists(p, ec)) return;
#ifdef _WIN32
    _putenv_s("ASIACAD_DWG_CONVERTER", p.string().c_str());
#else
    setenv("ASIACAD_DWG_CONVERTER", p.string().c_str(), 0);
#endif
}

std::string converterName(const Converter& c) {
    return c.kind == Converter::Oda ? "ODA File Converter" : c.kind == Converter::LibreDwg ? "LibreDWG" : "";
}

Converter find() {
    Converter c;
    std::error_code ec;
    if (const char* env = std::getenv("ASIACAD_DWG_CONVERTER"); env && *env && fs::exists(env, ec)) {
        c.exe = env;
        const std::string n = lower(fs::path(env).filename().string());
        if (n.find("dwg2dxf") != std::string::npos) {
            c.kind = Converter::LibreDwg;
            fs::path d2 = fs::path(env).parent_path() / (std::string("dxf2dwg") + kExeExt);
            c.exe2 = fs::exists(d2, ec) ? d2.string() : inPath("dxf2dwg");
        } else {
            c.kind = Converter::Oda;
        }
        return c;
    }
    if (std::string e = inPath("ODAFileConverter"); !e.empty()) return {Converter::Oda, e, {}};
    std::vector<fs::path> roots;
#ifdef _WIN32
    for (const char* v : {"ProgramFiles", "ProgramFiles(x86)", "ProgramW6432"})
        if (const char* p = std::getenv(v)) roots.push_back(fs::path(p) / "ODA");
#else
    roots = {"/opt", "/usr/bin", "/usr/local/bin", "/opt/ODAFileConverter"};
#endif
    for (const auto& r : roots) {
        if (!fs::is_directory(r, ec)) continue;
        if (fs::exists(r / (std::string("ODAFileConverter") + kExeExt), ec)) return {Converter::Oda, (r / (std::string("ODAFileConverter") + kExeExt)).string(), {}};
        std::vector<fs::path> cands;
        for (const auto& e : fs::directory_iterator(r, ec))
            if (e.is_directory(ec) && lower(e.path().filename().string()).find("odafileconverter") != std::string::npos) cands.push_back(e.path());
        std::sort(cands.rbegin(), cands.rend());   // eng yangi versiya
        for (const auto& d : cands)
            if (fs::exists(d / (std::string("ODAFileConverter") + kExeExt), ec)) return {Converter::Oda, (d / (std::string("ODAFileConverter") + kExeExt)).string(), {}};
    }
    if (std::string e = inPath("dwg2dxf"); !e.empty()) return {Converter::LibreDwg, e, inPath("dxf2dwg")};
    return c;
}

std::string toDxf(const std::string& dwgPath) {
    const Converter c = find();
    if (c.kind == Converter::None) throw Error("dwg.err.noconv");
    std::error_code ec;
    if (!fs::exists(dwgPath, ec)) throw Error("acad.err.open", {dwgPath});
    if (c.kind == Converter::Oda) return odaConvert(c, fs::path(dwgPath), "DXF", "ACAD2018").string();
    const fs::path out = tempDir() / "out" / fs::path(dwgPath).filename().replace_extension(".dxf");
    run(quote(c.exe) + " -y -o " + quote(out.string()) + " " + quote(dwgPath));
    if (!fs::exists(out, ec)) throw Error("dwg.err.convert", {dwgPath});
    return out.string();
}

void fromDxf(const std::string& dxfPath, const std::string& dwgPath, const std::string& version) {
    const Converter c = find();
    if (c.kind == Converter::None) throw Error("dwg.err.noconv");
    std::error_code ec;
    fs::path out;
    if (c.kind == Converter::Oda) {
        out = odaConvert(c, fs::path(dxfPath), "DWG", version);
    } else {
        if (c.exe2.empty()) throw Error("dwg.err.noconv");
        out = tempDir() / "out" / fs::path(dxfPath).filename().replace_extension(".dwg");
        run(quote(c.exe2) + " -y -o " + quote(out.string()) + " " + quote(dxfPath));
        if (!fs::exists(out, ec)) throw Error("dwg.err.convert", {dxfPath});
    }
    fs::copy_file(out, dwgPath, fs::copy_options::overwrite_existing, ec);
    if (ec) throw Error("err.write_failed", {dwgPath});
}

} // namespace asiacad::dwg
