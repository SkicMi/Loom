#pragma once
//=============================================================================================
// AUTOSAVE.
//
// Every couple of minutes, when the scene has changes that are not saved, a copy of it is written
// NEXT TO the project as a hidden file: ".<name>.autosave.usda" (".untitled.autosave.usda" in
// ~/.local/share/loom for a scene that was never saved). The project file itself is never
// touched - that is still only Save's job, so an autosave can never overwrite something the user
// meant to keep.
//
// WRITTEN FROM A THREAD, from a COPY. Saving C0257 (266 k points as text) takes a noticeable
// fraction of a second; the copy takes milliseconds, so the window does not stutter.
//
// RESTORE. When a project is opened and its autosave is NEWER than it, the editor offers to open
// the autosave instead. A normal Save removes the autosave, since it is then older news.
//=============================================================================================
#include <Warp/Project.h>
#include <Warp/Stage.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace Loom{

inline std::filesystem::path autosavePathFor(const std::filesystem::path& project){
    if(!project.empty()) return project.parent_path() / ("." + project.stem().string() + ".autosave.usda");
    const char* home = std::getenv("HOME");
    const std::filesystem::path base = home ? std::filesystem::path(home) / ".local/share/loom" : std::filesystem::temp_directory_path() / "loom";
    return base / ".untitled.autosave.usda";
}

//An autosave newer than the project (or any autosave for a never-saved scene)
inline bool newerAutosave(const std::filesystem::path& project, std::filesystem::path& found){
    std::error_code error;
    const std::filesystem::path candidate = autosavePathFor(project);
    if(!std::filesystem::is_regular_file(candidate, error)) return false;
    if(!project.empty() && std::filesystem::is_regular_file(project, error) &&
       std::filesystem::last_write_time(candidate, error) <= std::filesystem::last_write_time(project, error)) return false;
    found = candidate;
    return true;
}

class Autosave{
public:
    explicit Autosave(double intervalSeconds = 120.0) : interval(intervalSeconds){}
    ~Autosave(){ if(writer.joinable()) writer.join(); }

    //Once per frame. dirty = the scene differs from the last Save. Returns true when a write
    //was started
    bool tick(const Warp::Stage& stage, const std::filesystem::path& project, bool dirty){
        const auto now = std::chrono::steady_clock::now();
        if(!dirty){ changedSince = now; return false; }
        if(writing) return false;
        const uint64_t print = stage.fingerprint();
        if(print == writtenPrint) return false;                         //this state is already on disk
        if(std::chrono::duration<double>(now - changedSince).count() < interval) return false;
        start(stage, project, print);
        changedSince = now;
        return true;
    }

    //Write right now (used before risky operations and in tests); waits for the previous write
    void now(const Warp::Stage& stage, const std::filesystem::path& project){
        start(stage, project, stage.fingerprint());
        if(writer.joinable()) writer.join();
    }

    //After a real Save the autosave is stale; after opening another project it belongs elsewhere
    void discard(const std::filesystem::path& project){
        if(writer.joinable()) writer.join();
        std::error_code error;
        std::filesystem::remove(autosavePathFor(project), error);
        writtenPrint = 0;
    }

    bool isWriting() const {return writing;}
    std::string lastError() const{ std::lock_guard<std::mutex> guard(lock); return error; }
    std::filesystem::path lastPath() const{ std::lock_guard<std::mutex> guard(lock); return path; }

private:
    void start(const Warp::Stage& stage, const std::filesystem::path& project, uint64_t print){
        if(writer.joinable()) writer.join();
        writing = true;
        writtenPrint = print;
        const std::filesystem::path target = autosavePathFor(project);
        writer = std::thread([this, copy = stage, target]{
            std::error_code ignored;
            std::filesystem::create_directories(target.parent_path(), ignored);
            std::string problem;
            //Written beside and renamed: a crash halfway leaves the previous autosave intact
            const std::filesystem::path partial = target.string() + ".part";
            if(Warp::saveProject(copy, partial.string(), problem)) std::filesystem::rename(partial, target, ignored);
            {
                std::lock_guard<std::mutex> guard(lock);
                error = problem;
                path = target;
            }
            writing = false;
        });
    }

    double interval;
    std::chrono::steady_clock::time_point changedSince = std::chrono::steady_clock::now();
    uint64_t writtenPrint = 0;
    std::thread writer;
    std::atomic<bool> writing{false};
    mutable std::mutex lock;
    std::string error;
    std::filesystem::path path;
};

}
