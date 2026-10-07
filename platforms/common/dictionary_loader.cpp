#include "dictionary_loader.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <iterator>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace myswy {
namespace {
constexpr size_t MaxDictionaryBytes = 64 * 1024 * 1024;
class File {
public:
    explicit File(const std::string &path) : fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK)) {}
    ~File() { if (fd >= 0) { ::close(fd); } }
    const int fd;
};
} // namespace

DictionaryLoader::DictionaryLoader(Publish publish)
    : publish_(std::move(publish)), worker_([this] { run(); }) {}
DictionaryLoader::~DictionaryLoader() { stop(); }

void DictionaryLoader::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_.store(true);
    }
    changed_.notify_one();
    if (worker_.joinable()) { worker_.join(); }
}

void DictionaryLoader::request(uint64_t generation, std::string path) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_.store(generation);
        pending_ = Request{generation, std::move(path)};
    }
    changed_.notify_one();
}

void DictionaryLoader::retire(DictionaryPtr dictionary) {
    if (!dictionary) { return; }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retired_.push_back(std::move(dictionary));
    }
    changed_.notify_one();
}

DictionaryPtr DictionaryLoader::load(const Request &request) {
    MyswyDictionary *handle = nullptr;
    if (request.path.empty()) {
        handle = myswy_dictionary_new_demo();
    } else {
        if (request.path.front() != '/' || request.path.find('\0') != std::string::npos) {
            throw std::runtime_error("词典路径必须是绝对路径");
        }
        // O_NONBLOCK plus fstat prevents FIFOs/devices from blocking the worker.
        File file(request.path);
        if (file.fd < 0) { throw std::runtime_error("无法打开词典文件，请检查路径和权限"); }
        struct stat info {};
        if (::fstat(file.fd, &info) != 0 || !S_ISREG(info.st_mode)) {
            throw std::runtime_error("词典必须是普通文件");
        }
        if (info.st_size < 0 || static_cast<uint64_t>(info.st_size) > MaxDictionaryBytes) {
            throw std::runtime_error("词典超过 64 MiB 上限");
        }
        std::string source;
        source.reserve(static_cast<size_t>(info.st_size));
        std::array<char, 32768> buffer;
        for (;;) {
            if (stopping_.load() || latest_.load() != request.generation) { return {}; }
            const auto count = ::read(file.fd, buffer.data(), buffer.size());
            if (count == 0) { break; }
            if (count < 0) {
                if (errno == EINTR) { continue; }
                throw std::runtime_error("读取词典失败");
            }
            if (static_cast<size_t>(count) > MaxDictionaryBytes - source.size()) {
                throw std::runtime_error("词典超过 64 MiB 上限");
            }
            source.append(buffer.data(), static_cast<size_t>(count));
        }
        handle = myswy_dictionary_new_tsv(reinterpret_cast<const uint8_t *>(source.data()), source.size());
    }
    if (!handle) { throw std::runtime_error("词典无效：请检查 UTF-8、TSV 格式、重复项及大小限制"); }
    // Retain ownership if allocation of the C++ control block throws.
    std::unique_ptr<MyswyDictionary, decltype(&myswy_dictionary_free)> owned(handle, myswy_dictionary_free);
    auto result = std::make_shared<DictionarySnapshot>(nullptr, request.path);
    result->dictionary = std::move(owned);
    return result;
}

void DictionaryLoader::run() {
    std::vector<DictionaryPtr> retired;
    for (;;) {
        std::optional<Request> request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            const auto ready = [this] { return stopping_.load() || pending_ || !retired_.empty(); };
            if (retired.empty()) { changed_.wait(lock, ready); }
            else { changed_.wait_for(lock, std::chrono::milliseconds(250), ready); }
            if (stopping_.load()) { return; }
            request.swap(pending_);
            std::move(retired_.begin(), retired_.end(), std::back_inserter(retired));
            retired_.clear();
        }
        // Active sessions retain a snapshot until AFTER releasing their Rust Arc.
        // Therefore the final, potentially expensive dictionary drop happens here.
        retired.erase(std::remove_if(retired.begin(), retired.end(),
            [](const auto &entry) { return entry.use_count() == 1; }), retired.end());
        if (!request) { continue; }
        DictionaryPtr dictionary;
        std::string error;
        try { dictionary = load(*request); }
        catch (const std::exception &exception) { error = exception.what(); }
        catch (...) { error = "词典加载异常"; }
        if (!stopping_.load() && latest_.load() == request->generation) {
            publish_(request->generation, std::move(dictionary), std::move(error));
        }
    }
}
} // namespace myswy
