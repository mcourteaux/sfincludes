
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include "levenshtein_distance.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace po = boost::program_options;
namespace fs = boost::filesystem;

void find_headers(fs::path dir, std::vector<fs::path> &headers);

void rename_headers(std::vector<fs::path> &headers);

void process_dir(fs::path dir, const std::vector<fs::path> &include_paths,
                 const std::map<fs::path, std::vector<fs::path>> &headers);

void process_file(fs::path file, const std::vector<fs::path> &include_paths,
                  const std::map<fs::path, std::vector<fs::path>> &headers,
                  size_t *total, size_t *replaced, size_t *untouched,
                  size_t *failed);

std::string fix_include(
    std::string path, fs::path file, const std::vector<fs::path> &include_paths,
    const std::map<fs::path, std::vector<fs::path>> &headers);

int fuzzy = 0;
bool dry_run = false;
bool verbose = false;

int main(int argc, char **argv) {
    po::options_description desc("Allowed options");

    std::vector<std::string> include_paths;

    // clang-format off
    desc.add_options()
        ("help", "produce help message") ("root", po::value<std::string>(), "set root directory")
        ("src", po::value<std::string>(), "set source directory")
        ("include-path", po::value<std::vector<std::string>>(&include_paths), "add include search path directory (cfr. gcc -Ipath)")
        ("fuzzy", po::value<int>()->default_value(0), "maximal edit distance")
        ("rename-hpp", "rename headers files to .hpp")
        ("dry-run", "perform a dry-run")
        ("verbose", "be verbose")
        ;
    // clang-format on

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 1;
    }

    std::string src;
    bool rename = false;

    if (include_paths.size()) {
        for (auto &p : include_paths) {
            std::cout << "Inlucde path : " << p << std::endl;
        }
    } else {
        std::cout << "No include paths given." << std::endl;
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
        std::cout << "Fuzzy search : " << fuzzy << std::endl;
    }

    if (vm.count("rename-hpp")) {
        rename = true;
        std::cout << "Rename to hpp" << std::endl;
    }

    if (vm.count("dry-run")) {
        dry_run = true;
        std::cout << "Dry run" << std::endl;
    }

    if (vm.count("verbose")) {
        verbose = true;
        std::cout << "Be verbose" << std::endl;
    }

    for (auto &inc : include_paths) {
        if (!fs::is_directory(fs::path(inc))) {
            std::cout << "Include path does not exist." << std::endl;
            std::cout << inc << std::endl;
            return 1;
        }
    }

    fs::path src_path(src);
    if (!fs::is_directory(src_path)) {
        std::cout << "Source path does not exist." << std::endl;
        std::cout << src_path << std::endl;
        return 1;
    }

    std::map<fs::path, std::vector<fs::path>> headers;
    std::vector<fs::path> incpaths;
    for (auto &inc : include_paths) {
        incpaths.push_back(inc);
        std::cout << "Index headers in: " << inc << std::endl;
        std::vector<fs::path> hdrs;
        find_headers(inc, hdrs);
        if (verbose) {
            for (auto &f : hdrs) {
                std::cout << "    " << fs::relative(f, inc) << std::endl;
            }
        }
        headers[inc] = std::move(hdrs);
    }
    if (rename) {
        for (auto &e : headers) {
            rename_headers(e.second);
        }
    }
    process_dir(src_path, incpaths, headers);

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

void process_dir(fs::path dir, const std::vector<fs::path> &include_paths,
                 const std::map<fs::path, std::vector<fs::path>> &headers) {
    const std::vector<std::string> EXT = {".cpp", ".cxx", ".cc", ".h", ".hpp"};

    size_t total = 0;
    size_t replaced = 0;
    size_t untouched = 0;
    size_t failed = 0;

    for (fs::recursive_directory_iterator it(dir);
         it != fs::recursive_directory_iterator(); ++it) {
        fs::path file = it->path();
        if (std::find(EXT.begin(), EXT.end(), file.extension()) != EXT.end()) {
            process_file(file, include_paths, headers, &total, &replaced,
                         &untouched, &failed);
        }
    }

    std::cout << "Replaced : " << replaced << " / " << total << std::endl;
    std::cout << "Untouched: " << untouched << " / " << total << std::endl;
    std::cout << "Failed   : " << failed << " / " << total << std::endl;
    std::cout << "Total    : " << (replaced + untouched + failed) << " / "
              << total << std::endl;
}

void process_file(fs::path file, const std::vector<fs::path> &include_paths,
                  const std::map<fs::path, std::vector<fs::path>> &headers,
                  size_t *total, size_t *replaced, size_t *untouched,
                  size_t *failed) {
    std::stringstream buffer;
    std::ifstream in(file.string());
    std::string line;
    while (std::getline(in, line, '\n')) {
        std::string prefix = "#include \"";
        if (boost::starts_with(line, prefix)) {
            size_t endQuotePos = line.find("\"", prefix.size());
            std::string path =
                line.substr(prefix.size(), endQuotePos - prefix.size());
            std::string behindQuote = line.substr(endQuotePos + 1);
            std::string fixed_path =
                fix_include(path, file, include_paths, headers);

            if (fixed_path != "") {
                std::string nis = "#include \"" + fixed_path + "\"";
                buffer << nis << behindQuote << std::endl;
                if (fixed_path != path) {
                    (*replaced)++;

                    std::cout << "Replace include: " << path;
                    std::cout << "  ->  " << fixed_path << std::endl;
                } else {
                    (*untouched)++;

                    if (verbose) {
                        std::cout << "Untouched include: " << path << std::endl;
                    }
                }
            } else {
                buffer << line << std::endl;
                (*failed)++;

                std::cout << "Failed to fix include: " << path << std::endl;
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

std::string fix_include(
    std::string path, fs::path file, const std::vector<fs::path> &include_paths,
    const std::map<fs::path, std::vector<fs::path>> &headers) {
    fs::path dir = file.parent_path();
    /* Check if the file is in this directory */
    fs::path local = dir / path;
    if (fs::exists(local)) {
        return path;
    }

    fs::path incpath_text(path);

    /* Find exact match in tree */
    for (auto &e : headers) {
        const auto incpath = e.first;
        const auto &hdrs = e.second;
        for (const fs::path &hdr : hdrs) {
            if (hdr.filename() == incpath_text.filename()) {
                return fs::relative(hdr, incpath).string();
            }
        }
    }

    if (fuzzy > 0) {
        std::string closest_path = "";
        int closest_distance = fuzzy + 1;
        for (auto &e : headers) {
            const auto incpath = e.first;
            const auto &hdrs = e.second;
            for (const fs::path &hdr : hdrs) {
                std::string key = hdr.filename().string();
                int dist1 =
                    levenshtein_distance(key, incpath_text.filename().string());
                int dist2 = levenshtein_distance(key, incpath_text.string());
                int dist = std::min(dist1, dist2);
                if (dist < closest_distance) {
                    closest_path = fs::relative(hdr, incpath).string();
                    closest_distance = dist;
                }
            }
        }
        return closest_path;
    }

    return "";
}
