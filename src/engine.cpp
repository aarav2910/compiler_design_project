#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <thread>
#include <mutex>
#include <future>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

std::size_t get_file_hash(const std::string &path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return 0;
    std::stringstream buf;
    buf << file.rdbuf();
    return std::hash<std::string>{}(buf.str());
}

std::unordered_map<std::string, std::size_t> read_manifest(const std::string &manifest_path)
{
    std::unordered_map<std::string, std::size_t> manifest;
    std::ifstream in(manifest_path);
    std::string file;
    std::size_t hash_val;
    while (in >> file >> hash_val)
        manifest[file] = hash_val;
    return manifest;
}

void write_manifest(const std::string &manifest_path, const std::unordered_map<std::string, std::size_t> &manifest)
{
    std::ofstream out(manifest_path);
    for (const auto &[f, h] : manifest)
        out << f << " " << h << "\n";
}

struct Graph
{
    std::unordered_map<std::string, std::vector<std::string>> adj;
    std::unordered_map<std::string, std::vector<std::string>> dependencies;
    std::unordered_set<std::string> nodes;

    void parse_file(const std::string &filepath)
    {
        nodes.insert(filepath);
        std::ifstream file(filepath);
        std::string line;
        while (std::getline(file, line))
        {
            size_t pos = line.find("#include \"");
            if (pos != std::string::npos)
            {
                size_t start = pos + 10;
                size_t end = line.find("\"", start);
                if (end != std::string::npos)
                {
                    std::string dep = "demo/" + line.substr(start, end - start);
                    dependencies[filepath].push_back(dep);
                    adj[dep].push_back(filepath);
                    nodes.insert(dep);
                }
            }
        }
    }

    std::unordered_set<std::string> get_transitive_set(const std::unordered_set<std::string> &dirty)
    {
        std::unordered_set<std::string> affected = dirty;
        std::queue<std::string> q;
        for (const auto &d : dirty)
            q.push(d);

        while (!q.empty())
        {
            std::string curr = q.front();
            q.pop();
            if (adj.count(curr))
            {
                for (const auto &nxt : adj[curr])
                {
                    if (!affected.count(nxt))
                    {
                        affected.insert(nxt);
                        q.push(nxt);
                    }
                }
            }
        }
        return affected;
    }
};

std::vector<std::vector<std::string>> get_parallel_batches(
    const std::unordered_set<std::string> &targets, const Graph &graph)
{
    std::unordered_map<std::string, int> in_degree;
    std::unordered_map<std::string, std::vector<std::string>> local_edges;

    for (const auto &t : targets)
        in_degree[t] = 0;
    for (const auto &u : targets)
    {
        if (graph.dependencies.count(u))
        {
            for (const auto &dep : graph.dependencies.at(u))
            {
                if (targets.count(dep))
                {
                    local_edges[dep].push_back(u);
                    in_degree[u]++;
                }
            }
        }
    }

    std::queue<std::string> q;
    for (const auto &[node, deg] : in_degree)
    {
        if (deg == 0)
            q.push(node);
    }

    std::vector<std::vector<std::string>> levels;
    while (!q.empty())
    {
        size_t sz = q.size();
        std::vector<std::string> level;
        for (size_t i = 0; i < sz; ++i)
        {
            std::string u = q.front();
            q.pop();
            level.push_back(u);
            for (const auto &v : local_edges[u])
            {
                if (--in_degree[v] == 0)
                    q.push(v);
            }
        }
        levels.push_back(level);
    }
    return levels;
}

int main()
{
    std::cout << "\n======================================================\n";
    std::cout << " [BCSE307L] INCREMENTAL & PARALLEL COMPILER ENGINE\n";
    std::cout << "======================================================\n";

    fs::create_directory(".cache");
    const std::string manifest_file = ".cache/manifest.txt";
    auto old_manifest = read_manifest(manifest_file);
    std::unordered_map<std::string, std::size_t> new_manifest;

    Graph graph;
    std::vector<std::string> cpp_files;
    for (const auto &entry : fs::directory_iterator("demo"))
    {
        std::string p = entry.path().string();
        if (entry.path().extension() == ".cpp")
        {
            cpp_files.push_back(p);
            graph.parse_file(p);
        }
        else if (entry.path().extension() == ".h")
        {
            graph.parse_file(p);
        }
    }

    std::unordered_set<std::string> directly_changed;
    for (const auto &node : graph.nodes)
    {
        std::size_t h = get_file_hash(node);
        new_manifest[node] = h;
        if (!old_manifest.count(node) || old_manifest[node] != h)
        {
            directly_changed.insert(node);
        }
    }

    auto affected = graph.get_transitive_set(directly_changed);
    std::unordered_set<std::string> compile_set;
    for (const auto &f : affected)
    {
        if (f.find(".cpp") != std::string::npos)
            compile_set.insert(f);
    }

    if (compile_set.empty())
    {
        std::cout << "\n[CACHE HIT] All modules up to date. Recompilation skipped!\n\n";
        return 0;
    }

    std::cout << "\n[AFFECTED SET] Modules queued for compilation:\n";
    for (const auto &f : compile_set)
        std::cout << "  * " << f << "\n";

    auto batches = get_parallel_batches(compile_set, graph);

    std::mutex log_mtx;
    for (size_t i = 0; i < batches.size(); ++i)
    {
        std::cout << "\n[SCHEDULER] Executing Batch Level " << i << " (" << batches[i].size() << " parallel tasks)\n";
        std::vector<std::future<void>> futures;

        for (const auto &file : batches[i])
        {
            futures.push_back(std::async(std::launch::async, [file, &log_mtx]()
                                         {
                std::string obj = ".cache/" + fs::path(file).stem().string() + ".o";
                std::string cmd = "g++ -c " + file + " -I demo -o " + obj;
                {
                    std::lock_guard<std::mutex> lock(log_mtx);
                    std::cout << "  -> [THREAD " << std::this_thread::get_id() << "] Compiling: " << file << "\n";
                }
                std::system(cmd.c_str()); }));
        }
        for (auto &f : futures)
            f.get();
    }

    std::cout << "\n[LINKER] Linking objects into final binary (app.out)...\n";
    std::string link_cmd = "g++ ";
    for (const auto &f : cpp_files)
    {
        link_cmd += ".cache/" + fs::path(f).stem().string() + ".o ";
    }
    link_cmd += "-o app.exe";

    if (std::system(link_cmd.c_str()) == 0)
    {
        write_manifest(manifest_file, new_manifest);
        std::cout << "[SUCCESS] Build successful -> Run with: ./app.out\n\n";
    }
    return 0;
}