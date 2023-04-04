#include <algorithm>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/program_options.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "levenshtein_distance.hpp"

namespace po = boost::program_options;
namespace fs = std::filesystem;

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
    const std::map<fs::path, std::vector<fs::path>> &headers,
    bool prefer_relative_to_root, int *distance);

int fuzzy = 0;
bool dry_run = true;
bool verbose = false;
bool process_system_includes = false;
bool system_to_user = false;
bool prefer_relative_to_root = false;

const std::string RED = "\033[31m";
const std::string GREEN = "\033[32m";
const std::string YELLOW = "\033[33m";
const std::string CLEAR = "\033[0m";

int main(int argc, char **argv) {
  po::options_description desc("Allowed options");

  std::vector<std::string> include_paths;

  // clang-format off
  desc.add_options()
      ("help", "Produce help message.")
      ("src", po::value<std::string>(), "Set source directory.")
      ("include-path", po::value<std::vector<std::string>>(&include_paths), "Add include search path directory (cfr. gcc -Ipath).")
      ("fuzzy", po::value<int>()->default_value(0), "Maximal edit distance.")
      ("process-system-includes", "Also process #include <> statements.")
      ("system-to-user", "Replace #include <> with #include \"\" when the file is found. "
                         "Only when --process-system-includes.")
      ("prefer-relative-to-root", "Also rewrite correct includes to be relative to the root.")
      ("rename-hpp", "Rename .h headers files to .hpp.")
      ("no-dry-run", "Actually perform the changes.")
      ("verbose", "Be verbose.")
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

  if (vm.count("process-system-includes")) {
    process_system_includes = true;
    std::cout << "Process system includes." << src << std::endl;
  }
  if (vm.count("system-to-user")) {
    system_to_user = true;
    std::cout << "Convert system includes to user includes when file is found."
              << src << std::endl;
  }
  if (vm.count("prefer-relative-to-root")) {
    prefer_relative_to_root = true;
    std::cout
        << "Prefer include paths to be always written relative to the root."
        << src << std::endl;
  }

  if (vm.count("fuzzy")) {
    fuzzy = vm["fuzzy"].as<int>();
    std::cout << "Fuzzy search : " << fuzzy << std::endl;
  }

  if (vm.count("rename-hpp")) {
    rename = true;
    std::cout << "Rename to hpp." << std::endl;
  }

  if (vm.count("no-dry-run")) {
    dry_run = false;
    std::cout << "No dry run." << std::endl;
  } else {
    std::cout << "Dry run. (Use --no-dry-run to effectively write changes back "
                 "to disk.)"
              << std::endl;
  }

  if (vm.count("verbose")) {
    verbose = true;
    std::cout << "Be verbose." << std::endl;
  }

  for (auto &inc : include_paths) {
    if (!fs::is_directory(fs::path(inc))) {
      std::cout << RED << "Include path does not exist." << std::endl;
      std::cout << inc << CLEAR << std::endl;
      return 1;
    }
  }

  fs::path src_path(src);
  if (!fs::is_directory(src_path)) {
    std::cout << RED << "Source path does not exist." << std::endl;
    std::cout << src_path << CLEAR << std::endl;
    return 1;
  }

  std::cout << std::endl;

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

  std::cout << std::endl;

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
      std::cout << GREEN << "Rename: " << hdr << "  ->  " << newpath << CLEAR
                << std::endl;
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
      process_file(file, include_paths, headers, &total, &replaced, &untouched,
                   &failed);
    }
  }

  std::cout << YELLOW << std::endl;
  std::cout << "Replaced : " << replaced << " / " << total << std::endl;
  std::cout << "Untouched: " << untouched << " / " << total << std::endl;
  std::cout << "Failed   : " << failed << " / " << total << std::endl;
  std::cout << "Total    : " << (replaced + untouched + failed) << " / "
            << total << CLEAR << std::endl;
}

void process_file(fs::path file, const std::vector<fs::path> &include_paths,
                  const std::map<fs::path, std::vector<fs::path>> &headers,
                  size_t *total, size_t *replaced, size_t *untouched,
                  size_t *failed) {
  std::cout << "Process " << file.string() << " ..." << std::endl;
  std::stringstream buffer;
  std::ifstream in(file.string());
  std::string line;
  while (std::getline(in, line, '\n')) {
    std::string prefix_all = "#include ";
    std::string prefix_system = "#include <";
    std::string prefix_user = "#include \"";
    if (boost::starts_with(line, prefix_user) ||
        (boost::starts_with(line, prefix_system) && process_system_includes)) {
      std::string path, behind_path, path_with_quotes;
      size_t endPathPos = 0;
      bool system = false;
      if (boost::starts_with(line, prefix_user)) {
        endPathPos = line.find("\"", prefix_user.size());
        system = false;
      } else if (boost::starts_with(line, prefix_system)) {
        endPathPos = line.find(">", prefix_system.size());
        system = true;
      }

      path = line.substr(prefix_user.size(), endPathPos - prefix_user.size());
      path_with_quotes =
          line.substr(prefix_all.size(), endPathPos - prefix_all.size() + 1);
      behind_path = line.substr(endPathPos + 1);

      int distance;
      std::string fixed_path = fix_include(path, file, include_paths, headers,
                                           prefer_relative_to_root, &distance);

      if (fixed_path != "") {
        std::string fixed_path_with_quotes;
        if (system && !system_to_user) {
          fixed_path_with_quotes = "<" + fixed_path + ">";
        } else {
          fixed_path_with_quotes = "\"" + fixed_path + "\"";
        }
        buffer << "#include " << fixed_path_with_quotes << behind_path
               << std::endl;
        if (fixed_path != path) {
          (*replaced)++;

          std::cout << GREEN << "\tReplace include: " << path_with_quotes;
          std::cout << "  ->  " << fixed_path_with_quotes;
          std::cout << "  (distance: " << distance << ")" << CLEAR << std::endl;
        } else {
          (*untouched)++;

          std::cout << YELLOW << "\tUntouched include: " << path_with_quotes
                    << CLEAR << std::endl;
        }
      } else {
        buffer << line << std::endl;
        (*failed)++;

        std::cout << RED << "\tFailed to fix include: " << path_with_quotes
                  << CLEAR << std::endl;
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
    const std::map<fs::path, std::vector<fs::path>> &headers,
    bool prefer_relative_to_root, int *const distance) {
  fs::path dir = file.parent_path();
  if (!prefer_relative_to_root) {
    /* Check if the file is in this directory */
    fs::path local = dir / path;
    if (fs::exists(local)) {
      *distance = 0;
      return path;
    }
  }

  fs::path incpath_text(path);

  /* Find exact match in tree */
  for (auto &e : headers) {
    const auto incpath = e.first;
    const auto &hdrs = e.second;
    for (const fs::path &hdr : hdrs) {
      if (hdr.filename() == incpath_text.filename()) {
        *distance = 0;
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
        int dist1 = levenshtein_distance(key, incpath_text.filename().string());
        int dist2 = levenshtein_distance(key, incpath_text.string());
        int dist = std::min(dist1, dist2);
        if (dist < closest_distance) {
          closest_path = fs::relative(hdr, incpath).string();
          closest_distance = dist;
        }
      }
    }
    *distance = closest_distance;
    return closest_path;
  }

  *distance = 9999;
  return "";
}
