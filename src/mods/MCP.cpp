// MCP.cpp — 内置 HTTP 服务实现。
//
// 与外部插件版的区别：直接用 REFramework 内部 SDK（sdk:: / reframework::），
// 不再需要单独的 DLL，并且把配置项并入了框架的「配置」面板。

#include <winsock2.h>
#include <ws2tcpip.h>

#include "MCP.hpp"

#include "../REFramework.hpp"
#include "Localization.hpp"

#include <sdk/RETypeDB.hpp>
#include <sdk/RETypeDefinition.hpp>
#include <sdk/REManagedObject.hpp>
#include <sdk/REContext.hpp>
#include <sdk/RETypes.hpp>
#include <sdk/Memory.hpp>
#include <utility/Config.hpp>

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------
std::string mcp_fmt(const char* f, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, f);
    _vsnprintf_s(buf, _TRUNCATE, f, ap);
    va_end(ap);
    return std::string(buf);
}

std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if ((unsigned char)c < 0x20) o += ' ';
        else o += c;
    }
    return o;
}

std::string url_decode(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '+') { out += ' '; }
        else if (in[i] == '%' && i + 2 < in.size()) {
            char h[3] = { in[i + 1], in[i + 2], 0 };
            out += (char)strtol(h, nullptr, 16);
            i += 2;
        } else out += in[i];
    }
    return out;
}

std::string qs_get(const std::string& qs, const std::string& key) {
    std::string pat = key + "=";
    size_t p = 0;
    while ((p = qs.find(pat, p)) != std::string::npos) {
        if (p == 0 || qs[p - 1] == '&') {
            size_t s = p + pat.size();
            size_t e = qs.find('&', s);
            return url_decode(qs.substr(s, e == std::string::npos ? std::string::npos : e - s));
        }
        p += pat.size();
    }
    return "";
}

int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void* parse_addr(const std::string& s) {
    std::string t = s;
    if (t.rfind("0x", 0) == 0 || t.rfind("0X", 0) == 0) t = t.substr(2);
    if (t.empty()) return nullptr;
    for (char c : t) if (!isxdigit((unsigned char)c)) return nullptr;
    return (void*)(uintptr_t)_strtoui64(t.c_str(), nullptr, 16);
}

void hex_into(std::string& s, const unsigned char* p, size_t n) {
    for (size_t i = 0; i < n; ++i) s += mcp_fmt("%02X ", p[i]);
}

sdk::RETypeDB* get_tdb() {
    auto& types = reframework::get_types();
    if (types == nullptr) {
        return nullptr;
    }
    return (sdk::RETypeDB*)types->get_type_db();
}

} // namespace

// ---------------------------------------------------------------------------
// 请求结构
// ---------------------------------------------------------------------------
struct MCP::InternalRequest {
    std::string path;
    std::string result;
    std::atomic<bool> done{ false };
};

// ---------------------------------------------------------------------------
// API 实现（只在游戏线程调用）
// ---------------------------------------------------------------------------
namespace {

std::string api_stats() {
    auto* tdb = get_tdb();
    const uint32_t num_types = tdb != nullptr ? tdb->get_num_types() : 0;
    return mcp_fmt("{\"ok\":true,\"service\":\"reframework-mcp\",\"types\":%u}", num_types);
}

std::string api_search(const std::string& q, int limit) {
    auto* tdb = get_tdb();
    if (tdb == nullptr) return "{\"ok\":false,\"err\":\"no tdb\"}";
    if (limit <= 0 || limit > 500) limit = 40;

    std::string needle = q;
    for (auto& c : needle) c = (char)tolower((unsigned char)c);

    std::string out = "{\"ok\":true,\"items\":[";
    int found = 0;
    const uint32_t n = tdb->get_num_types();
    for (uint32_t i = 0; i < n && found < limit; ++i) {
        auto* t = tdb->get_type(i);
        if (t == nullptr) continue;
        const char* nm = t->get_name();
        if (nm == nullptr) continue;
        std::string low = nm;
        for (auto& c : low) c = (char)tolower((unsigned char)c);
        if (!needle.empty() && low.find(needle) == std::string::npos) continue;

        const auto full = t->get_full_name();
        if (found) out += ",";
        out += mcp_fmt("{\"name\":\"%s\",\"full\":\"%s\",\"size\":%u}",
            json_escape(nm).c_str(), json_escape(full).c_str(), t->get_size());
        ++found;
    }
    out += mcp_fmt("],\"count\":%d}", found);
    return out;
}

std::string api_type(const std::string& name, bool with_fields, bool with_methods) {
    auto* t = sdk::find_type_definition(name);
    if (t == nullptr) return mcp_fmt("{\"ok\":false,\"err\":\"type not found: %s\"}", json_escape(name).c_str());

    const auto full = t->get_full_name();
    auto* parent = t->get_parent_type();

    std::string out = mcp_fmt("{\"ok\":true,\"full\":\"%s\",\"size\":%u",
        json_escape(full).c_str(), t->get_size());
    if (parent != nullptr) {
        out += mcp_fmt(",\"parent\":\"%s\"", json_escape(parent->get_full_name()).c_str());
    }

    // 字段数（注意：RE4 这类版本上 is_value_type() 会遍历继承链，比较慢，此处只用 size）
    uint32_t field_count = 0;
    for (auto* f : t->get_fields()) { (void)f; ++field_count; }
    out += mcp_fmt(",\"fieldCount\":%u", field_count);

    uint32_t method_count = 0;
    for (auto& m : t->get_methods()) { (void)m; ++method_count; }
    out += mcp_fmt(",\"methodCount\":%u", method_count);

    if (with_fields) {
        out += ",\"fields\":[";
        bool first = true;
        uint32_t emitted = 0;
        for (auto* f : t->get_fields()) {
            if (f == nullptr || emitted >= 512) break;
            auto* ft = f->get_type();
            if (!first) out += ",";
            first = false;
            out += mcp_fmt("{\"name\":\"%s\",\"type\":\"%s\",\"off\":%u,\"static\":%d}",
                json_escape(f->get_name() != nullptr ? f->get_name() : "").c_str(),
                json_escape(ft != nullptr ? ft->get_name() : "?").c_str(),
                f->get_offset_from_base(), (int)f->is_static());
            ++emitted;
        }
        out += "]";
    }

    if (with_methods) {
        out += ",\"methods\":[";
        bool first = true;
        uint32_t emitted = 0;
        for (auto& m : t->get_methods()) {
            if (emitted >= 1024) break;
            if (!first) out += ",";
            first = false;
            out += mcp_fmt("\"%s/%u\"", json_escape(m.get_name() != nullptr ? m.get_name() : "").c_str(), m.get_num_params());
            ++emitted;
        }
        out += "]";
    }

    out += "}";
    return out;
}

std::string api_singleton(const std::string& name) {
    void* nat = sdk::get_native_singleton<void>(name);
    ::REManagedObject* man = sdk::get_managed_singleton<::REManagedObject>(name);
    return mcp_fmt("{\"ok\":true,\"name\":\"%s\",\"native\":\"%p\",\"managed\":\"%p\"}",
        json_escape(name).c_str(), nat, man);
}

std::string api_mem(void* addr, size_t size) {
    if (addr == nullptr) return "{\"ok\":false,\"err\":\"bad addr\"}";
    if (size == 0) size = 64;
    if (size > 4096) size = 4096;
    if (IsBadReadPtr(addr, size)) return "{\"ok\":false,\"err\":\"unreadable\"}";

    const unsigned char* p = (const unsigned char*)addr;
    std::string hex, as_int, as_float;
    for (size_t i = 0; i < size; ++i) {
        hex += mcp_fmt("%02X ", p[i]);
        if ((i + 1) % 8 == 0) hex += " ";
    }
    for (size_t i = 0; i + 8 <= size; i += 8) {
        as_int += mcp_fmt("%llu ", *(const unsigned long long*)(p + i));
    }
    for (size_t i = 0; i + 4 <= size; i += 4) {
        as_float += mcp_fmt("%.4g ", *(const float*)(p + i));
    }
    return mcp_fmt("{\"ok\":true,\"addr\":\"%p\",\"size\":%u,\"hex\":\"%s\",\"ints\":\"%s\",\"floats\":\"%s\"}",
        addr, (unsigned)size, json_escape(hex).c_str(),
        json_escape(as_int).c_str(), json_escape(as_float).c_str());
}

std::string api_write(void* addr, const std::string& hex) {
    if (addr == nullptr) return "{\"ok\":false,\"err\":\"bad addr\"}";
    std::vector<unsigned char> bytes;
    int hi = -1;
    for (char c : hex) {
        if (c == ' ' || c == ',' || c == '-') continue;
        int v = hexval(c);
        if (v < 0) return "{\"ok\":false,\"err\":\"bad hex\"}";
        if (hi < 0) hi = v;
        else { bytes.push_back((unsigned char)((hi << 4) | v)); hi = -1; }
    }
    if (bytes.empty()) return "{\"ok\":false,\"err\":\"empty\"}";
    if (IsBadWritePtr(addr, bytes.size())) return "{\"ok\":false,\"err\":\"unwritable\"}";
    memcpy(addr, bytes.data(), bytes.size());
    return mcp_fmt("{\"ok\":true,\"written\":%u}", (unsigned)bytes.size());
}

std::string api_alloc(size_t size) {
    if (size == 0 || size > 65536) return "{\"ok\":false,\"err\":\"size 1..65536\"}";
    void* p = sdk::memory::allocate(size);
    if (p == nullptr) return "{\"ok\":false,\"err\":\"allocate failed\"}";
    memset(p, 0, size);
    return mcp_fmt("{\"ok\":true,\"addr\":\"%p\",\"size\":%u}", p, (unsigned)size);
}

std::string api_field(void* addr, const std::string& type_name, const std::string& field_name) {
    if (addr == nullptr) return "{\"ok\":false,\"err\":\"bad addr\"}";
    auto* t = sdk::find_type_definition(type_name);
    if (t == nullptr) return "{\"ok\":false,\"err\":\"type not found\"}";
    auto* f = t->get_field(field_name);
    if (f == nullptr) return "{\"ok\":false,\"err\":\"field not found\"}";

    auto* ft = f->get_type();
    uint32_t sz = ft != nullptr ? ft->get_size() : 8;
    if (sz == 0 || sz > 1024) sz = 1024;

    void* data = f->get_data_raw(addr, false);
    if (data == nullptr || IsBadReadPtr(data, sz)) return "{\"ok\":false,\"err\":\"unreadable\"}";

    std::string hex;
    hex_into(hex, (const unsigned char*)data, sz);
    std::string val, fval;
    if (sz >= 8)      val = mcp_fmt("%lld", *(const long long*)data);
    else if (sz >= 4) val = mcp_fmt("%d", *(const int*)data);
    else              val = mcp_fmt("%d", *(const unsigned char*)data);
    if (sz >= 4)      fval = mcp_fmt("%.4g", *(const float*)data);

    return mcp_fmt("{\"ok\":true,\"field\":\"%s\",\"type\":\"%s\",\"size\":%u,\"int\":\"%s\",\"float\":\"%s\",\"hex\":\"%s\"}",
        json_escape(field_name).c_str(),
        json_escape(ft != nullptr ? ft->get_name() : "?").c_str(),
        sz, json_escape(val).c_str(), json_escape(fval).c_str(), json_escape(hex).c_str());
}

std::string api_fields(void* addr, const std::string& type_name, int maxn) {
    if (addr == nullptr) return "{\"ok\":false,\"err\":\"bad addr\"}";
    auto* t = sdk::find_type_definition(type_name);
    if (t == nullptr) return "{\"ok\":false,\"err\":\"type not found\"}";

    uint32_t total = 0;
    for (auto* f : t->get_fields()) { (void)f; ++total; }
    if (total == 0) return "{\"ok\":true,\"items\":[],\"note\":\"no fields\"}";

    const uint32_t limit = (maxn > 0 && (uint32_t)maxn < total) ? (uint32_t)maxn : 200;

    std::string out = mcp_fmt("{\"ok\":true,\"addr\":\"%p\",\"type\":\"%s\",\"totalFields\":%u,\"items\":[",
        addr, json_escape(type_name).c_str(), total);

    uint32_t emitted = 0;
    for (auto* f : t->get_fields()) {
        if (f == nullptr || emitted >= limit) break;
        if (f->is_static()) continue;
        auto* ft = f->get_type();
        uint32_t sz = ft != nullptr ? ft->get_size() : 8;
        if (sz == 0 || sz > 1024) sz = 1024;

        void* data = f->get_data_raw(addr, false);
        std::string val = "?", fval = "?";
        if (data != nullptr && !IsBadReadPtr(data, sz)) {
            if (sz >= 8)      val = mcp_fmt("%lld", *(const long long*)data);
            else if (sz >= 4) val = mcp_fmt("%d", *(const int*)data);
            else              val = mcp_fmt("%d", *(const unsigned char*)data);
            if (sz >= 4)      fval = mcp_fmt("%.5g", *(const float*)data);
        }
        if (emitted) out += ",";
        out += mcp_fmt("{\"name\":\"%s\",\"type\":\"%s\",\"size\":%u,\"int\":\"%s\",\"float\":\"%s\"}",
            json_escape(f->get_name() != nullptr ? f->get_name() : "").c_str(),
            json_escape(ft != nullptr ? ft->get_name() : "?").c_str(),
            sz, json_escape(val).c_str(), json_escape(fval).c_str());
        ++emitted;
    }
    out += "]}";
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// MCP 类实现
// ---------------------------------------------------------------------------
std::string MCP::port_config_key() const {
    return std::string{ "MCP_Port" };
}

std::string MCP::allow_write_config_key() const {
    return std::string{ "MCP_AllowWrite" };
}

std::string MCP::enabled_config_key() const {
    return std::string{ "MCP_Enabled" };
}

std::shared_ptr<MCP>& MCP::get() {
    static std::shared_ptr<MCP> instance{ std::make_shared<MCP>() };
    return instance;
}

std::optional<std::string> MCP::on_initialize() {
    m_last_requested_port = (uint16_t)m_port_value;

    if (m_enabled->value()) {
        start_server(m_last_requested_port);
    }

    return Mod::on_initialize();
}
void MCP::on_config_load(const utility::Config& cfg) {
    for (IModValue& option : m_options) {
        option.config_load(cfg);
    }

    // 写开关：用字符串精确比较（Config::get<bool> 只认 "true"/"false"）
    if (auto raw = cfg.get(allow_write_config_key())) {
        m_allow_write->value() = (*raw == "true");
    }
    if (auto raw = cfg.get(enabled_config_key())) {
        m_enabled->value() = (*raw == "true");
    }

    // 端口：手动读取（不使用 ModInt32，避免 SliderInt 的 5..40 clamp）
    if (auto raw = cfg.get(port_config_key())) {
        try {
            const int v = std::stoi(*raw);
            if (v >= 1 && v <= 65535) {
                m_port_value = v;
            }
        } catch (...) {
            // 保留默认值
        }
    }

    spdlog::info("[MCP] config: enabled={} allowWrite={} port={}",
        m_enabled->value(), m_allow_write->value(), m_port_value);

    m_last_requested_port = (uint16_t)m_port_value;

    if (m_enabled->value() && !m_running.load()) {
        start_server(m_last_requested_port);
    }
}
void MCP::on_config_save(utility::Config& cfg) {
    for (IModValue& option : m_options) {
        option.config_save(cfg);
    }

    cfg.set(port_config_key(), std::to_string(m_port_value));
}

void MCP::on_frame() {
    pump_requests();
}

void MCP::on_draw_ui() {
    if (!ImGui::CollapsingHeader(l10n::tr("MCP (HTTP API)"))) {
        return;
    }

    ImGui::TreePush("MCP");

    bool changed = false;

    changed |= m_enabled->draw(l10n::tr("Enable MCP server"));

    // 运行中禁止修改端口（需先停服务）
    const bool running_now = m_running.load();
    if (running_now) {
        ImGui::BeginDisabled();
    }
    if (ImGui::InputInt(l10n::tr("Port"), &m_port_value)) {
        if (m_port_value < 1) m_port_value = 1;
        if (m_port_value > 65535) m_port_value = 65535;
        changed = true;
    }
    if (running_now) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", l10n::tr("(stop the server first to change the port)"));
    }

    changed |= m_allow_write->draw(l10n::tr("Allow write / invoke (use with care)"));

    // 开关变化 → 启停服务
    const bool want_running = m_enabled->value();
    if (want_running != m_running.load()) {
        if (want_running) {
            start_server((uint16_t)m_port_value);
        } else {
            stop_server();
        }
        m_last_requested_port = (uint16_t)m_port_value;
        changed = true;
    }

    // 端口变化 → 重启监听
    if (m_running.load() && (uint16_t)m_port_value != m_last_requested_port) {
        stop_server();
        start_server((uint16_t)m_port_value);
        m_last_requested_port = (uint16_t)m_port_value;
        changed = true;
    }

    if (m_running.load()) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s  http://127.0.0.1:%u",
            l10n::tr("Running:"), (unsigned)m_active_port.load());
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", l10n::tr("Stopped"));
    }

    ImGui::Text("%s %ld / %s %ld", l10n::tr("Requests:"), m_request_count.load(),
                                    l10n::tr("Errors:"), m_error_count.load());

    if (!m_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s %s", l10n::tr("Error:"), m_last_error.c_str());
    }

    ImGui::TextWrapped("%s", l10n::tr("Endpoints: /api/ping /stats /search /type /singleton /mem /fields /field /write /alloc"));

    if (changed) {
        g_framework->request_save_config();
    }

    ImGui::TreePop();
}

// ---
// HTTP 服务器
// ---------------------------------------------------------------------------
void MCP::start_server(uint16_t port) {
    if (m_running.load()) {
        return;
    }

    m_stop = false;
    m_last_error.clear();

    m_thread = std::thread([this, port]() {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            m_last_error = "WSAStartup failed";
            return;
        }

        SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (srv == INVALID_SOCKET) {
            m_last_error = mcp_fmt("socket failed (%d)", WSAGetLastError());
            WSACleanup();
            return;
        }

        int opt = 1;
        setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // 只监听本机
        addr.sin_port = htons(port);

        if (bind(srv, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            m_last_error = mcp_fmt("bind failed on port %u (%d)", port, WSAGetLastError());
            closesocket(srv);
            WSACleanup();
            return;
        }

        listen(srv, 8);

        m_listen_socket = (uintptr_t)srv;
        m_active_port = port;
        m_running = true;
        spdlog::info("[MCP] listening on 127.0.0.1:{}", port);

        while (!m_stop.load()) {
            sockaddr_in cli{};
            int clilen = sizeof(cli);
            SOCKET c = accept(srv, (sockaddr*)&cli, &clilen);
            if (c == INVALID_SOCKET) {
                if (m_stop.load()) break;
                Sleep(10);
                continue;
            }

            char buf[4096] = {};
            int n = recv(c, buf, sizeof(buf) - 1, 0);
            std::string resp = "{\"ok\":false,\"err\":\"bad request\"}";

            if (n > 0) {
                std::string req(buf, (size_t)n);
                const size_t sp1 = req.find(' ');
                if (sp1 != std::string::npos) {
                    const size_t sp2 = req.find(' ', sp1 + 1);
                    if (sp2 != std::string::npos) {
                        const std::string path = req.substr(sp1 + 1, sp2 - sp1 - 1);

                        // 交给游戏线程
                        InternalRequest rq;
                        rq.path = path;
                        {
                            std::lock_guard<std::mutex> lk(m_queue_mutex);
                            m_queue.push_back(&rq);
                        }

                        for (int i = 0; i < 1000 && !rq.done.load(); ++i) {
                            Sleep(10);   // 最多等 10 秒
                        }

                        if (rq.done.load()) {
                            resp = rq.result;
                            ++m_request_count;
                        } else {
                            resp = "{\"ok\":false,\"err\":\"timeout (game thread busy)\"}";
                            std::lock_guard<std::mutex> lk(m_queue_mutex);
                            for (auto it = m_queue.begin(); it != m_queue.end(); ++it) {
                                if (*it == &rq) { m_queue.erase(it); break; }
                            }
                            ++m_error_count;
                        }
                    }
                }
            }

            const std::string header = mcp_fmt(
                "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\n"
                "Access-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: %u\r\n\r\n",
                (unsigned)resp.size());
            const std::string full = header + resp;
            send(c, full.c_str(), (int)full.size(), 0);
            closesocket(c);
        }

        if (m_listen_socket.exchange(0) == (uintptr_t)srv) {
            // 已由 stop_server 关闭
        } else {
            closesocket(srv);
        }
        WSACleanup();
        m_running = false;
        m_active_port = 0;
        spdlog::info("[MCP] stopped");
    });

    m_thread.detach();
}

void MCP::stop_server() {
    m_stop = true;

    // 关键：关闭监听 socket，让阻塞在 accept() 上的线程立即返回。
    // 仅设置 m_stop 是不够的 —— accept() 是阻塞调用，不会主动检查标志位。
    const uintptr_t s = m_listen_socket.exchange(0);
    if (s != 0) {
        closesocket((SOCKET)s);
    }

    for (int i = 0; i < 300 && m_running.load(); ++i) {
        Sleep(10);
    }

    m_active_port = 0;
}

// 只能在游戏线程调用
void MCP::pump_requests() {
    InternalRequest* rq = nullptr;
    {
        std::lock_guard<std::mutex> lk(m_queue_mutex);
        if (m_queue.empty()) return;
        rq = m_queue.front();
        m_queue.pop_front();
    }
    if (rq == nullptr) return;

    std::string res;
    try {
        res = handle_request(rq->path);
    } catch (...) {
        res = "{\"ok\":false,\"err\":\"exception in handler\"}";
        ++m_error_count;
    }
    rq->result = res;
    rq->done = true;
}

std::string MCP::handle_request(const std::string& path) {
    std::string p = path, qs;
    const size_t q = path.find('?');
    if (q != std::string::npos) {
        p = path.substr(0, q);
        qs = path.substr(q + 1);
    }

    // ---- 基础 ----
    if (p == "/api/ping") {
        return mcp_fmt("{\"ok\":true,\"service\":\"reframework-mcp\",\"port\":%u}", (unsigned)m_active_port.load());
    }
    if (p == "/api/stats") {
        return api_stats();
    }
    if (p == "/api/search") {
        int lim = atoi(qs_get(qs, "limit").c_str());
        return api_search(qs_get(qs, "q"), lim);
    }
    if (p == "/api/type") {
        return api_type(qs_get(qs, "name"),
                        qs_get(qs, "fields") == "1",
                        qs_get(qs, "methods") == "1");
    }

    // ---- 单例 ----
    if (p == "/api/singleton") {
        return api_singleton(qs_get(qs, "name"));
    }

    // ---- 内存（读总是允许）----
    if (p == "/api/mem") {
        return api_mem(parse_addr(qs_get(qs, "addr")),
                       (size_t)strtoul(qs_get(qs, "size").c_str(), nullptr, 10));
    }

    // ---- 对象字段 ----
    if (p == "/api/field") {
        return api_field(parse_addr(qs_get(qs, "addr")), qs_get(qs, "type"), qs_get(qs, "name"));
    }
    if (p == "/api/fields") {
        return api_fields(parse_addr(qs_get(qs, "addr")), qs_get(qs, "type"),
                          atoi(qs_get(qs, "max").c_str()));
    }

    // ---- 写操作：需显式开启 ----
    if (p == "/api/write" || p == "/api/alloc") {
        if (!m_allow_write->value()) {
            return "{\"ok\":false,\"err\":\"write disabled (enable Allow write in Configuration -> MCP)\"}";
        }
        if (p == "/api/write") {
            return api_write(parse_addr(qs_get(qs, "addr")), qs_get(qs, "hex"));
        }
        return api_alloc((size_t)strtoul(qs_get(qs, "size").c_str(), nullptr, 10));
    }

    return "{\"ok\":false,\"err\":\"unknown endpoint\"}";
}

