
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include "levenshtein_distance.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace po = boost::program_options;
namespace fs = boost::filesystem;

void find_headers(fs::path dir, std::vector<fs::path> &headers);
void rename_headers(std::vector<fs::path> &headers);
void process_dir(fs::path dir, fs::path root, std::vector<fs::path> &headers);
void process_file(fs::path file, fs::path root, std::vector<fs::path> &headers,
                  size_t *total, size_t *replaced, size_t *untouched);
std::string fix_include(std::string path, fs::path file, fs::path root,
                        std::vector<fs::path> &headers);

int fuzzy = 0;
bool dry_run = false;

int main(int argc, char **argv) {
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
        ("help", "produce help message") ("root", po::value<std::string>(), "set root directory")
        ("src", po::value<std::string>(), "set source directory")
        ("root", po::value<std::string>(), "set root directory")
        ("fuzzy", po::value<int>()->default_value(0), "maximal edit distance")
        ("rename-hpp", "rename headers files to .hpp")
        ("dry-run", "perform a dry-run")
        ;
    // clang-format on

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 1;
    }

    std::string root;
    std::string src;
    bool rename = false;

    if (vm.count("root")) {
        root = vm["root"].as<std::string>();
        std::cout << "Root   : " << root << std::endl;
    } else {
        std::cout << "Root not set." << std::endl;
        std::cout << desc << std::endl;
        return 1;
    }

    if (vm.count("src")) {
        src = vm["src"].as<std::string>();
        std::cout << "Source : " << src << std::endl;
    } else {
        std::cout << "Source not set." << std::endl;
        std::cout << desc << std::endl;
        return 1;
    }

    if (vm.count("fuzzy")) {
        fuzzy = vm["fuzzy"].as<int>();
        std::cout << "Fuzzy search :" << fuzzy << std::endl;
    }

    if (vm.count("rename-hpp")) {
        rename = true;
        std::cout << "Rename to hpp" << std::endl;
    }

    if (vm.count("dry-run")) {
        dry_run = true;
        std::cout << "Dry run" << std::endl;
    }

    fs::path root_path(root);
    fs::path src_path(src);
    if (!fs::is_directory(root_path)) {
        std::cout << "Root path does not exist." << std::endl;
        std::cout << root_path << std::endl;
        return 1;
    }
    if (!fs::is_directory(src_path)) {
        std::cout << "Source path does not exist." << std::endl;
        std::cout << src_path << std::endl;
        return 1;
    }

    std::vector<fs::path> headers;
    find_headers(src_path, headers);
    for (auto f : headers) {
        std::cout << "Header: " << f << std::endl;
    }
    if (rename) {
        rename_headers(headers);
    }
    process_dir(src_path, root_path, headers);

    return 0;
}

void find_headers(fs::path dir, std::vector<fs::path> &headers) {
    const std::vector<std::string> HDR_EXT = {".h", ".hpp"};
    for (fs::recursive_directory_iterator it(dir);
         it != fs::recursive_directory_iterator(); ++it) {
        fs::path file = it->path();
        if (std::find(HDR_EXT.begin(), HDR_EXT.end(), file.extension()) !=
            HDR_EXT.end()) {
            headers.push_back(file);
        }
    }
}

void rename_headers(std::vector<fs::path> &headers) {
    for (auto &hdr : headers) {
        fs::path newpath = hdr;
        newpath = newpath.replace_extension(".hpp");
        if (newpath != hdr) {
            std::cout << "Rename: " << hdr << "  ->  " << newpath << std::endl;
            if (!dry_run) {
                fs::rename(hdr, newpath);
            }
            hdr = newpath;
        }
    }
}

void process_dir(fs::path dir, fs::path root, std::vector<fs::path> &headers) {
    const std::vector<std::string> EXT = {".cpp", ".cxx", ".cc", ".h", ".hpp"};
    size_t replaced = 0;
    size_t untouched = 0;
    size_t total = 0;
    for (fs::recursive_directory_iterator it(dir);
         it != fs::recursive_directory_iterator(); ++it) {
        fs::path file = it->path();
        if (std::find(EXT.begin(), EXT.end(), file.extension()) != EXT.end()) {
            process_file(file, root, headers, &total, &replaced, &untouched);
        }
    }

    std::cout << "Replaced : " << replaced << " / " << total << std::endl;
    std::cout << "Untouched: " << untouched << " / " << total << std::endl;
    std::cout << "Total    : " << (replaced + untouched) << " / " << total
              << std::endl;
}

void process_file(fs::path file, fs::path root, std::vector<fs::path> &headers,
                  size_t *total, size_t *replaced, size_t *untouched) {
    std::stringstream buffer;
    std::ifstream in(file.string());
    std::string line;
    while (std::getline(in, line, '\n')) {
        std::string prefix = "#include \"";
        if (boost::starts_with(line, prefix)) {
            std::string path = line.substr(prefix.size());
            path = path.substr(0, path.size() - 1);
            std::string fixed_path = fix_include(path, file, root, headers);
            std::string newline = "#include \"" + fixed_path + "\"";

            std::cout << "Replace include: " << path;
            std::cout << "  ->  " << fixed_path << std::endl;

            if (fixed_path.size()) {
                if (fixed_path == path) {
                    (*untouched)++;
                } else {
                    (*replaced)++;
                }
                buffer << newline << std::endl;
            } else {
                buffer << line << std::endl;
            }
            (*total)++;
        } else {
            buffer << line << std::endl;
        }
    }
    in.close();

    if (!dry_run) {
        std::ofstream out(file.string());
        out << buffer.str();
        out.close();
    }
}

std::string fix_include(std::string path, fs::path file, fs::path root,
                        std::vector<fs::path> &headers) {
    fs::path dir = file.parent_path();
    /* Check if the file is in this directory */
    fs::path local = dir / path;
    if (fs::exists(local)) {
        return path;
    }

    fs::path incpath(path);

    /* Find exact match in tree */
    for (fs::path &hdr : headers) {
        if (hdr.filename() == incpath.filename()) {
            return fs::relative(hdr, root).string();
        }
    }

    if (fuzzy > 0) {
        std::string closest_path = "";
        int closest_distance = fuzzy + 1;
        for (fs::path &hdr : headers) {
            std::string key = hdr.filename().string();
            int dist1 = levenshtein_distance(key, incpath.filename().string());
            int dist2 = levenshtein_distance(key, incpath.string());
            int dist = std::min(dist1, dist2);
            if (dist < closest_distance) {
                closest_path = fs::relative(hdr, root).string();
                closest_distance = dist;
            }
        }
        return closest_path;
    }

    return "";
}
