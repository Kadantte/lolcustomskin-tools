#include "wadindex.hpp"
#include "error.hpp"
#include <utility>

using namespace LCS;

static constexpr std::u8string_view INDEX_BLACKLIST[] = {
    u8"Map21.wad.client",
    u8"Map22.wad.client",
};

static bool is_blacklisted(std::u8string filename) noexcept {
    for (auto name: INDEX_BLACKLIST) {
        if (name == filename) {
            return true;
        }
    }
    return false;
}

WadIndex::WadIndex(fs::path const& path, bool blacklist, bool ignorebad) :
    path_(fs::absolute(path)), blacklist_(blacklist), ignorebad_(ignorebad) {
    lcs_trace_func(
                lcs_trace_var(path.generic_u8string()),
                lcs_trace_var(blacklist),
                lcs_trace_var(ignorebad)
                );
    last_write_time_ = fs::last_write_time(path_ / "DATA" / "FINAL");
    for (auto const& file : fs::recursive_directory_iterator(path_ / "DATA" / "FINAL")) {
        if (file.is_regular_file()) {
            if (auto wadpath = file.path(); wadpath.extension() == ".client") {
                this->add_wad(wadpath);
            }
        }
    }
    lcs_assert_msg("Not a wad directory!", lookup_.size() != 0);
}

void WadIndex::add_wad(fs::path const& wadpath) {
    lcs_trace_func(
        lcs_trace_var(wadpath)
        );
    lcs_hint(u8"Try deleting this file: ", wadpath);
    if (blacklist_ && is_blacklisted(wadpath.filename().generic_u8string())) {
        return;
    }
    last_write_time_ = std::max(last_write_time_, fs::last_write_time(wadpath));
    try {
        auto filename = wadpath.filename();
        if (auto old = wads_.find(filename); old != wads_.end()) {
            lcs_hint(u8"Try deleting this file: ", old->second->path());
            throw_error("Game contains duplicated wads!");
        } else {
            auto wad = std::make_unique<Wad>(wadpath, filename);
            if (wad->is_oldchecksum()) {
                return;
            }
            for(auto const& entry: wad->entries()) {
                lookup_.insert(std::make_pair(entry.xxhash, wad.get()));
                if (auto i = checksums_.find(entry.checksum); i != checksums_.end()) {
                    lcs_assert_msg("Inconsistent file checksum in Game folder!",  i->second == entry.checksum);
                } else {
                    checksums_.insert(std::make_pair(entry.xxhash, entry.checksum));
                }
            }
            wads_.insert_or_assign(wad->name(), std::move(wad));
        }
    } catch(std::runtime_error const& err) {
        if (err.what() != std::string_view("All zero .wad") && !ignorebad_) {
            throw;
        } else {
            error_stack().clear();
            hint_stack().clear();
        }
    }
}

bool WadIndex::is_uptodate() const {
    lcs_trace_func(
                lcs_trace_var(path_.generic_u8string()),
                lcs_trace_var(blacklist_)
                );
    auto new_last_write_time = fs::last_write_time(path_ / "DATA" / "FINAL");
    for (auto const& file : fs::recursive_directory_iterator(path_ / "DATA" / "FINAL")) {
        if (file.is_regular_file()) {
            if (auto filepath = file.path(); filepath.extension() == ".client") {
                if (blacklist_ && is_blacklisted(filepath.filename().generic_u8string())) {
                    continue;
                }
                new_last_write_time = std::max(new_last_write_time, fs::last_write_time(filepath));
            }
         }
    }
    return new_last_write_time == last_write_time_;
}
