#include "modunzip.hpp"
#include "error.hpp"
#include "progress.hpp"
#include <numeric>
#include <utility>

using namespace LCS;

ModUnZip::ModUnZip(fs::path path)
    : path_(fs::absolute(path)), zip_archive{}, infile_(std::make_unique<InFile>(path))
{
    lcs_trace_func(
                lcs_trace_var(path)
                );
    lcs_assert_msg("Invalid zip file!", mz_zip_reader_init_cfile(&zip_archive, infile_->raw(), 0, 0));
    mz_uint numFiles = mz_zip_reader_get_num_files(&zip_archive);
    mz_zip_archive_file_stat stat = {};
    for (mz_uint i = 0; i != numFiles; i++) {
        mz_zip_reader_file_stat(&zip_archive, i, &stat);
        if (stat.m_is_directory || !stat.m_is_supported) {
            continue;
        }
        std::u8string spath = to_u8string(&stat.m_filename[0]);
        for (auto& c: spath) {
            if (c == '\\') {
                c = '/';
            }
        }
        fs::path fpath = fs::path(spath).lexically_normal();
        size_ += stat.m_uncomp_size;
        files_.push_back(CopyFile { fpath, i, stat.m_uncomp_size });
    }
}

ModUnZip::~ModUnZip() {
    mz_zip_reader_end(&zip_archive);
}

void ModUnZip::extract(fs::path const& dstpath, ProgressMulti& progress) {
    lcs_trace_func(
                lcs_trace_var(dstpath)
                );
    progress.startMulti(files_.size(), size_);
    for (auto const& file: files_) {
        extractFile(dstpath / file.path, file, progress);
    }
    progress.finishMulti();
}

void ModUnZip::extractFile(fs::path const& outpath, CopyFile const& file, Progress& progress) {
    lcs_trace_func(
                lcs_trace_var(file.path)
                );
    progress.startItem(outpath, file.size);
    fs::create_directories(outpath.parent_path());
    struct CallbackCtx {
        OutFile outfile;
        Progress& progress;
    } ctx = { outpath, progress };
    auto callback = [] (void* opaq, mz_uint64 file_ofs, const void *pBuf, size_t n) -> size_t {
        CallbackCtx* ctx = (CallbackCtx*)opaq;
        ctx->outfile.seek(file_ofs, SEEK_SET);
        ctx->outfile.write(pBuf, n);
        ctx->progress.consumeData(n);
        return n;
    };
    lcs_assert(mz_zip_reader_extract_to_callback(&zip_archive, file.index, callback, &ctx, 0));
    progress.finishItem();
}
