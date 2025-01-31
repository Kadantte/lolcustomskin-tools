#define _CRT_SECURE_NO_WARNINGS
#include "modoverlay.hpp"
#include "patcher_utility/lineconfig.hpp"
#include "patcher_utility/process.hpp"
#include "patcher_utility/ppp.hpp"
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <filesystem>

using namespace LCS;

constexpr auto const find_open = &ppp::any<
        "6A 00 56 55 50 FF 15 o[u[?? ?? ?? ??]] 8B F8"_pattern>;
constexpr auto const find_ret = &ppp::any<
        "0F 84 ?? ?? ?? ?? 57 8B CB E8 ?? ?? ?? ?? o[84] C0 75 r[??]"_pattern>;
constexpr auto const find_wopen = &ppp::any<
        "6A 00 6A ?? 68 ?? ?? 12 00 ?? FF 15 u[?? ?? ?? ??]"_pattern>;
constexpr auto const find_free = &ppp::any<
        "A1 u[?? ?? ?? ??] 85 C0 74 09 3D ?? ?? ?? ?? 74 02 FF E0 o[FF 74 24 04] E8 ?? ?? ??"_pattern>;

struct ModOverlay::Impl {
    using Config = LineConfig<
        std::uint32_t, "lcs-patcher-win32-v7",
        "checksum", "open", "open_ref", "wopen", "ret", "ret_jmp", "free_ptr", "free_fn"
    >;
    Config config = {};

    void scan(LCS::Process const &process) {
        auto const base = process.Base();
        auto const data = process.Dump();
        auto const data_span = std::span<char const>(data);

        auto const open_match = find_open(data_span, base);
        if (!open_match) {
            throw std::runtime_error("Failed to find fopen!");
        }
        auto const wopen_match = find_wopen(data_span, base);
        if (!wopen_match) {
            throw std::runtime_error("Failed to find wfopen!");
        }
        auto const ret_match = find_ret(data_span, base);
        if (!ret_match) {
            throw std::runtime_error("Failed to find ret!");
        }
        auto const free_match = find_free(data_span, base);
        if (!free_match) {
            throw std::runtime_error("Failed to find free!");
        }

        config.get<"open_ref">() = process.Debase((PtrStorage)std::get<1>(*open_match));
        config.get<"open">() = process.Debase((PtrStorage)std::get<2>(*open_match));
        config.get<"wopen">() = process.Debase((PtrStorage)std::get<1>(*wopen_match));
        config.get<"ret">() = process.Debase((PtrStorage)std::get<1>(*ret_match));
        config.get<"ret_jmp">() = process.Debase((PtrStorage)std::get<2>(*ret_match));
        config.get<"free_ptr">() = process.Debase((PtrStorage)std::get<1>(*free_match));
        config.get<"free_fn">() = process.Debase((PtrStorage)std::get<2>(*free_match));

        config.get<"checksum">() = process.Checksum();
    }

    struct ImportTrampoline {
        uint8_t data[64] = {};
        static ImportTrampoline make(Ptr<uint8_t> where) {
            ImportTrampoline result = {
                { 0xB8u, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xE0, }
            };
            memcpy(result.data + 1, &where.storage, sizeof(where.storage));
            return result;
        }
    };

    struct CodePayload {
        // Pointers to be consumed by shellcode
        Ptr<uint8_t> org_open_ptr = {};
        Ptr<char16_t> prefix_open_ptr = {};
        Ptr<uint8_t> wopen_ptr = {};
        Ptr<uint8_t> org_free_ptr = {};
        PtrStorage find_ret_addr = {};
        PtrStorage hook_ret_addr = {};

        // Actual data and shellcode storage
        uint8_t hook_open_data[0x80] = {
            0xc8, 0x00, 0x10, 0x00, 0x53, 0x57, 0x56, 0xe8,
            0x00, 0x00, 0x00, 0x00, 0x5b, 0x81, 0xe3, 0x00,
            0xf0, 0xff, 0xff, 0x8d, 0xbd, 0x00, 0xf0, 0xff,
            0xff, 0x8b, 0x73, 0x04, 0x66, 0xad, 0x66, 0xab,
            0x66, 0x85, 0xc0, 0x75, 0xf7, 0x83, 0xef, 0x02,
            0x8b, 0x75, 0x08, 0xb4, 0x00, 0xac, 0x3c, 0x2f,
            0x75, 0x02, 0xb0, 0x5c, 0x66, 0xab, 0x84, 0xc0,
            0x75, 0xf3, 0x8d, 0x85, 0x00, 0xf0, 0xff, 0xff,
            0xff, 0x75, 0x20, 0xff, 0x75, 0x1c, 0xff, 0x75,
            0x18, 0xff, 0x75, 0x14, 0xff, 0x75, 0x10, 0xff,
            0x75, 0x0c, 0x50, 0xff, 0x53, 0x08, 0x83, 0xf8,
            0xff, 0x75, 0x17, 0xff, 0x75, 0x20, 0xff, 0x75,
            0x1c, 0xff, 0x75, 0x18, 0xff, 0x75, 0x14, 0xff,
            0x75, 0x10, 0xff, 0x75, 0x0c, 0xff, 0x75, 0x08,
            0xff, 0x13, 0x5e, 0x5f, 0x5b, 0xc9, 0xc2, 0x1c,
            0x00
        };
        uint8_t hook_free_data[0x80] = {
            0xc8, 0x00, 0x00, 0x00, 0x53, 0x56, 0xe8, 0x00,
            0x00, 0x00, 0x00, 0x5b, 0x81, 0xe3, 0x00, 0xf0,
            0xff, 0xff, 0x8b, 0x73, 0x10, 0x89, 0xe8, 0x05,
            0x80, 0x01, 0x00, 0x00, 0x83, 0xe8, 0x04, 0x39,
            0xe8, 0x74, 0x09, 0x3b, 0x30, 0x75, 0xf5, 0x8b,
            0x73, 0x14, 0x89, 0x30, 0x8b, 0x43, 0x0c, 0x5e,
            0x5b, 0xc9, 0xff, 0xe0
        };
        ImportTrampoline org_open_data = {};
        char16_t prefix_open_data[0x400] = {};
    };

    bool check(const Process & process) {
        return config.check() && process.Checksum() == config.get<"checksum">();
    }

    bool is_patchable(const Process &process) {
        try {
            auto const open_ref = process.Rebase<PtrStorage>(config.get<"open_ref">());
            if (process.Read(open_ref) != process.Rebase(config.get<"open">())) {
                return false;
            }
            auto const free_ptr = process.Rebase<PtrStorage>(config.get<"free_ptr">());
            if (process.Read(free_ptr) == 0) {
                return false;
            }
            auto const wopen = process.Rebase<PtrStorage>(config.get<"wopen">());
            if (process.Read(wopen) == 0) {
                return false;
            }
        } catch(std::runtime_error const&) {
            return false;
        }
        return true;
    }

    void patch(LCS::Process const &process, std::u16string const& prefix_str) const {
        if (!config.check()) {
            throw std::runtime_error("Config invalid to patch this executable!");
        }
        // Prepare pointers
        auto mod_code = process.Allocate<CodePayload>();
        auto ptr_open = Ptr<Ptr<ImportTrampoline>>(process.Rebase(config.get<"open">()));
        auto org_open = Ptr<ImportTrampoline>{};
        process.Read(ptr_open, org_open);
        auto ptr_wopen = Ptr<Ptr<uint8_t>>(process.Rebase(config.get<"wopen">()));
        auto wopen = Ptr<uint8_t>{};
        process.Read(ptr_wopen, wopen);
        auto find_ret_addr = process.Rebase(config.get<"ret">());
        auto jmp_ret_addr = process.Rebase(config.get<"ret_jmp">());
        auto ptr_free = Ptr<Ptr<uint8_t>>(process.Rebase(config.get<"free_ptr">()));
        auto org_free_ptr = Ptr<uint8_t>(process.Rebase(config.get<"free_fn">()));

        // Prepare payload
        auto payload = CodePayload {};
        payload.org_open_ptr = Ptr(mod_code->org_open_data.data);
        payload.prefix_open_ptr = Ptr(mod_code->prefix_open_data);
        payload.wopen_ptr = wopen;
        payload.org_free_ptr = org_free_ptr;
        payload.find_ret_addr = find_ret_addr;
        payload.hook_ret_addr = jmp_ret_addr;

        process.Read(org_open, payload.org_open_data);
        std::copy_n(prefix_str.data(), prefix_str.size(), payload.prefix_open_data);

        // Write payload
        process.Write(mod_code, payload);
        process.MarkExecutable(mod_code);

        // Write hooks
        process.Write(ptr_free, Ptr(mod_code->hook_free_data));
        process.Write(org_open, ImportTrampoline::make(mod_code->hook_open_data));
    }
};

ModOverlay::ModOverlay() : impl_(std::make_unique<Impl>()) {}

ModOverlay::~ModOverlay() = default;

std::string ModOverlay::to_string() const noexcept {
    return impl_->config.to_string();
}

void ModOverlay::from_string(std::string const & str) noexcept {
    impl_->config.from_string(str);
}

void ModOverlay::run(std::function<bool(Message)> update, std::filesystem::path const& profilePath) {
    std::u16string prefix_str = profilePath.generic_u16string();
    for (auto& c: prefix_str) {
        if (c == u'/') {
            c = u'\\';
        }
    }
    prefix_str = u"\\\\?\\" + prefix_str;
    if (!prefix_str.ends_with(u"\\")) {
        prefix_str.push_back(u'\\');
    }
    if ((prefix_str.size() + 1) * sizeof(char16_t) >= sizeof(Impl::CodePayload::prefix_open_data)) {
        throw std::runtime_error("Prefix too big!");
    }
    for (;;) {
        auto process = Process::Find("League of Legends.exe");
        if (!process) {
            if (!update(M_WAIT_START)) return;
            LCS::SleepMS(250);
            continue;
        }
        if (!update(M_FOUND)) return;
        if (!impl_->check(*process)) {
            for (std::uint32_t timeout = 3 * 60 * 1000; timeout; timeout -= 1) {
                if (!update(M_WAIT_INIT)) return;
                if (process->WaitInitialized(1)) {
                    break;
                }
            }
            if (!update(M_SCAN)) return;
            impl_->scan(*process);
            if (!update(M_NEED_SAVE)) return;
        } else {
            for (std::uint32_t timeout = 3 * 60 * 1000; timeout; timeout -= 1) {
                if (!update(M_WAIT_PATCHABLE)) return;
                if (impl_->is_patchable(*process)) {
                    break;
                }
                SleepMS(1);
            }
        }
        if (!update(M_PATCH)) return;
        impl_->patch(*process, prefix_str);
        for (std::uint32_t timeout = 3 * 60 * 60 * 1000; timeout; timeout -= 250) {
            if (!update(M_WAIT_EXIT)) return;
            if (process->WaitExit(250)) {
                break;
            }
        }
        if (!update(M_DONE)) return;
    }
}

