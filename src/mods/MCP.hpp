// MCP.hpp — 内置的 HTTP/MCP 服务，让外部程序（AI 助手 / 脚本）读取游戏运行时数据。
//
// 设计：
//   - HTTP 线程收请求 → 放进队列 → 游戏线程（on_frame）安全处理 → 写回结果
//   - 配置项：Enabled（开关）/ Port（端口）/ AllowWrite（是否允许写操作）
//   - 改端口后自动重启监听
//
// 默认关闭，需在「配置 → MCP」里手动启用。
//
// 注意：端口【不能】用 ModInt32 —— 它的 draw() 是 ImGui::SliderInt(..., 5, 40)，
// 会把 17890 强制夹到 40。这里用普通 int 成员 + InputInt。

#pragma once

#include "Mod.hpp"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

class MCP : public Mod {
public:
    static std::shared_ptr<MCP>& get();

    std::string_view get_name() const override {
        return "MCP";
    }

    std::optional<std::string> on_initialize() override;
    void on_frame() override;
    void on_draw_ui() override;
    void on_config_load(const utility::Config& cfg) override;
    void on_config_save(utility::Config& cfg) override;

    bool is_running() const {
        return m_running.load();
    }

    uint16_t active_port() const {
        return m_active_port.load();
    }

    const std::string& last_error() const {
        return m_last_error;
    }

private:
    void start_server(uint16_t port);
    void stop_server();
    void pump_requests();                       // 只能在游戏线程调用
    std::string handle_request(const std::string& path);

    std::string port_config_key() const;
    std::string allow_write_config_key() const;
    std::string enabled_config_key() const;

    ModToggle::Ptr m_enabled{ ModToggle::create(generate_name("Enabled"), false) };
    ModToggle::Ptr m_allow_write{ ModToggle::create(generate_name("AllowWrite"), false) };

    // 端口：普通成员（不能用 ModInt32，见文件头注释）
    int m_port_value{ 17890 };

    ValueList m_options {
        *m_enabled,
        *m_allow_write,
    };

    std::thread m_thread;
    std::atomic<bool> m_stop{ false };
    std::atomic<bool> m_running{ false };
    std::atomic<uint16_t> m_active_port{ 0 };
    // 监听 socket（用 uintptr_t 保存，避免在头文件里引入 winsock）。
    // stop_server() 会关掉它，使阻塞中的 accept() 立即返回。
    std::atomic<uintptr_t> m_listen_socket{ 0 };
    std::atomic<long> m_request_count{ 0 };
    std::atomic<long> m_error_count{ 0 };
    std::string m_last_error;
    uint16_t m_last_requested_port{ 0 };

    struct InternalRequest;
    std::deque<InternalRequest*> m_queue;
    std::mutex m_queue_mutex;
};
