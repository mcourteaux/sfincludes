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

struct ProcessResult {
  size_t total{0};
  size_t replaced_path{0};
  size_t system_to_user{0};
  size_t user_to_system{0};
  size_t untouched{0};
  size_t failed{0};
};

void accumulate(ProcessResult *r, const ProcessResult &term) {
  r->total += term.total;
  r->replaced_path += term.replaced_path;
  r->system_to_user += term.system_to_user;
  r->user_to_system += term.user_to_system;
  r->untouched += term.untouched;
  r->failed += term.failed;
}

struct IncludePath {
  fs::path path;
  bool system{false};

  bool operator<(const IncludePath &other) const { return path < other.path; }
};

struct IncludeStmt {
  std::string path;
  bool system{false};
};

struct Candidate {
  IncludePath search_path{};
  std::string header{};
  int filename_distance{};
  int folder_distance{};

  int weighted_distance() const {
    return filename_distance * 200 + folder_distance;
  }
};

void find_headers(fs::path dir, std::vector<fs::path> &headers);

void rename_headers(std::vector<fs::path> &headers);

ProcessResult
process_dir(fs::path dir, const std::vector<IncludePath> &include_paths,
            const std::map<IncludePath, std::vector<fs::path>> &headers);

void process_file(fs::path file, const std::vector<IncludePath> &include_paths,
                  const std::map<IncludePath, std::vector<fs::path>> &headers,
                  ProcessResult *result);

std::vector<Candidate>
fix_include(const IncludeStmt &path, const fs::path &file,
            const std::vector<IncludePath> &include_paths,
            const std::map<IncludePath, std::vector<fs::path>> &headers,
            bool prefer_relative_to_root);

int fuzzy = 0;
bool dry_run = true;
bool verbose = false;
bool process_system_includes = false;
bool system_to_user = false;
bool user_to_system = false;
bool prefer_relative_to_root = false;

const std::string RED = "\033[31m";
const std::string GREEN = "\033[32m";
const std::string YELLOW = "\033[33m";
const std::string BLUE = "\033[34m";
const std::string DIM = "\033[2m";
const std::string CLEAR = "\033[0m";

void print_results(const ProcessResult &result);

int main(int argc, char **argv) {
  po::options_description desc("Allowed options");

  std::vector<std::string> src_paths;
  std::vector<std::string> user_include_paths;
  std::vector<std::string> sys_include_paths;

  // clang-format off
  desc.add_options()
      ("help", "Produce help message.")
      ("src", po::value<std::vector<std::string>>(&src_paths),
       "Add a source directory to process. [repeat --src to specify more]")
      ("user-include-path", po::value<std::vector<std::string>>(&user_include_paths),
       "Add user include search path directory (cfr. gcc -Ipath) [repeat --user-include-path to specify more]")
      ("sys-include-path", po::value<std::vector<std::string>>(&sys_include_paths),
       "Add system include search path directory (cfr. gcc -isystem). [repeat --sys-include-path to specify more]")
      ("fuzzy", po::value<int>()->default_value(0),
       "Maximal filename edit distance (costs: insert=4, change=2, capitalize=1).")
      ("process-system-includes", "Also process #include <> statements.")
      ("system-to-user",
       "Replace #include <> with #include \"\" when the file is found user include search path. "
       "Only when --process-system-includes.")
      ("user-to-system",
       "Replace #include \"\" with #include <> when the file is found in the system include search path.")
      ("prefer-relative-to-root",
       "Also rewrite correct includes to be relative to their corresponding search path root.")
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

  bool rename = false;

  std::vector<IncludePath> include_paths;

  if (user_include_paths.size()) {
    for (auto &p : user_include_paths) {
      std::cout << "User inlucde path : " << p << std::endl;
      include_paths.push_back({p, false});
    }
  } else {
    std::cout << "No user include paths given." << std::endl;
  }
  if (sys_include_paths.size()) {
    for (auto &p : sys_include_paths) {
      std::cout << "System inlucde path : " << p << std::endl;
      include_paths.push_back({p, true});
    }
  } else {
    std::cout << "No system include paths given." << std::endl;
  }
  if (include_paths.empty()) {
    std::cout << RED << "ERROR: No include paths given." << CLEAR << std::endl;
    std::cout << desc << std::endl;
    return 1;
  }

  bool good = true;
  if (vm.count("src")) {
    for (const std::string &src : src_paths) {
      if (fs::is_directory(src)) {
        std::cout << "Source : " << src << std::endl;
      } else {
        std::cout << RED << "ERROR: Source directory not found: " << src
                  << CLEAR << std::endl;
        good = false;
      }
    }
  } else {
    std::cout << RED << "ERROR: Source not set." << CLEAR << std::endl;
    std::cout << desc << std::endl;
    good = false;
  }
  if (!good) {
    return 1;
  }

  if (vm.count("process-system-includes")) {
    process_system_includes = true;
    std::cout << "Process system includes." << std::endl;
  }
  if (vm.count("system-to-user")) {
    system_to_user = true;
    std::cout << "Convert system includes to user includes when a "
                 "corresponding file is found in the user include search path."
              << std::endl;
  }
  if (vm.count("user-to-system")) {
    user_to_system = true;
    std::cout
        << "Convert user includes to system includes when a corresponding file "
           "is found in the system include search path."
        << std::endl;
  }
  if (vm.count("prefer-relative-to-root")) {
    prefer_relative_to_root = true;
    std::cout
        << "Prefer include paths to be always written relative to the root."
        << std::endl;
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
                 "to filesystem.)"
              << std::endl;
  }

  if (vm.count("verbose")) {
    verbose = true;
    std::cout << "Be verbose." << std::endl;
  }

  for (auto &inc : include_paths) {
    if (!fs::is_directory(fs::path(inc.path))) {
      std::cout << RED << "Include path does not exist." << std::endl;
      std::cout << inc.path << CLEAR << std::endl;
      return 1;
    }
  }

  std::cout << std::endl;

  std::map<IncludePath, std::vector<fs::path>> headers;
  for (auto &inc : include_paths) {
    std::cout << "Index headers in: " << inc.path << std::endl;
    std::vector<fs::path> hdrs;
    find_headers(inc.path, hdrs);
    if (verbose) {
      for (auto &f : hdrs) {
        std::cout << "    " << fs::relative(f, inc.path) << std::endl;
      }
    }
    headers[inc] = std::move(hdrs);
  }
  if (rename) {
    for (auto &e : headers) {
      if (!e.first.system) {
        rename_headers(e.second);
      }
    }
  }

  std::cout << std::endl;

  ProcessResult accum{};
  for (const std::string &src : src_paths) {
    std::cout << std::endl;
    std::cout << "Processing source directory: " << src << "..." << std::endl;
    ProcessResult result = process_dir(src, include_paths, headers);
    if (src_paths.size() > 1) {
      std::cout << std::endl;
      print_results(result);
    }
    accumulate(&accum, result);
  }
  std::cout << std::endl;
  std::cout << "[Summary]" << std::endl;
  print_results(accum);

  std::cout
      << std::endl
      << YELLOW
      << "⚠\ufe0f Always backup / git commit your work before applying with "
         "--no-dry-run. ⚠\ufe0f"
      << std::endl
      << "Carefully review the changed above before continuing." << CLEAR
      << std::endl;

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
      std::cout << GREEN << "🏷\ufe0f Rename: " << hdr << "  ->  " << newpath
                << CLEAR << std::endl;
      if (!dry_run) {
        fs::rename(hdr, newpath);
      }
      hdr = newpath;
    }
  }
}

void print_results(const ProcessResult &result) {
  std::cout << "Replaced path: " << result.replaced_path << " / "
            << result.total << std::endl;
  std::cout << "Sys-to-user  : " << result.system_to_user << " / "
            << result.total << std::endl;
  std::cout << "User-to-sys  : " << result.user_to_system << " / "
            << result.total << std::endl;
  std::cout << "Untouched    : " << result.untouched << " / " << result.total
            << std::endl;
  std::cout << "Failed       : " << result.failed << " / " << result.total
            << std::endl;
  std::cout << CLEAR;
}

ProcessResult
process_dir(fs::path dir, const std::vector<IncludePath> &include_paths,
            const std::map<IncludePath, std::vector<fs::path>> &headers) {
  const std::vector<std::string> EXT = {".cpp", ".cxx", ".cc", ".h", ".hpp"};

  ProcessResult result{};

  for (fs::recursive_directory_iterator it(dir);
       it != fs::recursive_directory_iterator(); ++it) {
    fs::path file = it->path();
    if (std::find(EXT.begin(), EXT.end(), file.extension()) != EXT.end()) {
      process_file(file, include_paths, headers, &result);
    }
  }

  return result;
}

void process_file(fs::path file, const std::vector<IncludePath> &include_paths,
                  const std::map<IncludePath, std::vector<fs::path>> &headers,
                  ProcessResult *result) {
  std::cout << "    Process " << file.string() << " ..." << std::endl;
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

      IncludeStmt current{path, system};
      std::vector<Candidate> candidate_fixes = fix_include(
          current, file, include_paths, headers, prefer_relative_to_root);

      if (!candidate_fixes.empty()) {
        Candidate fix = candidate_fixes[0];
        std::string fixed_path_with_quotes;
        bool emit_system_include = false;
        if ((system && fix.search_path.system) || // already good
            (!system && fix.search_path.system &&
             user_to_system) || // changed to system
            (system && !fix.search_path.system &&
             !user_to_system)) { // can't touch
          emit_system_include = true;
        }

        if (emit_system_include) {
          fixed_path_with_quotes = "<" + fix.header + ">";
        } else {
          fixed_path_with_quotes = "\"" + fix.header + "\"";
        }

        bool changed_include_type = system != emit_system_include;
        if (changed_include_type) {
          if (system) {
            result->system_to_user++;
          } else {
            result->user_to_system++;
          }
        }

        buffer << "#include " << fixed_path_with_quotes << behind_path
               << std::endl;
        if (fix.header != path) {
          (result->replaced_path)++;

          std::cout << YELLOW
                    << "        👕 Replace include path: " << path_with_quotes;
          std::cout << "  ->  " << fixed_path_with_quotes;
          std::cout << DIM << "  (distance: fn=" << fix.filename_distance
                    << "; dir=" << fix.folder_distance << ") from "
                    << fix.search_path.path << CLEAR << std::endl;
        } else if (changed_include_type) {
          std::cout << BLUE
                    << "        💄 Change include type: " << path_with_quotes;
          std::cout << "  ->  " << fixed_path_with_quotes << CLEAR << std::endl;
        } else {
          (result->untouched)++;

          std::cout << GREEN
                    << "        ✅ Untouched include: " << path_with_quotes
                    << CLEAR << std::endl;
        }

        for (int alt_idx = 1; alt_idx < candidate_fixes.size(); ++alt_idx) {
          Candidate &alt = candidate_fixes[alt_idx];
          std::cout << DIM << "           - Alternative: " << alt.header;
          std::cout << DIM << "  (distance: fn=" << alt.filename_distance
                    << "; dir=" << alt.folder_distance << ") from "
                    << alt.search_path.path << CLEAR << std::endl;
        }

      } else {
        buffer << line << std::endl;
        (result->failed)++;

        std::cout << RED
                  << "        ❓ Failed to fix include: " << path_with_quotes
                  << CLEAR << std::endl;
      }

      (result->total)++;
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

int calculate_path_distance(const fs::path &containing_file,
                            const IncludeStmt &current,
                            const std::string &candidate_include,
                            const IncludePath &candidate_search_path) {
  int dist_relative = 9999999;
  {
    fs::path full_path = candidate_search_path.path / candidate_include;
    fs::path relative_to_file = fs::relative(full_path, containing_file);
    if (relative_to_file != full_path) {
      dist_relative =
          levenshtein_distance(current.path, relative_to_file.string());
    }
  }

  int dist_root = levenshtein_distance(current.path, candidate_include);
  return std::min(dist_relative, dist_root);
}

std::vector<Candidate>
fix_include(const IncludeStmt &include, const fs::path &file,
            const std::vector<IncludePath> &include_paths,
            const std::map<IncludePath, std::vector<fs::path>> &headers,
            bool prefer_relative_to_root) {
  std::vector<Candidate> candidates;

  fs::path dir = file.parent_path();
  if (!include.system) {
    /* Check if the file is in this directory */
    fs::path local = dir / include.path;
    if (fs::exists(local)) {
      if (prefer_relative_to_root) {
        // First find a root to rewrite it to.
        bool found = false;
        for (const IncludePath &root : include_paths) {
          if (!root.system) {
            fs::path rel = fs::relative(local, root.path);
            std::string relpath = rel.string();
            if (rel != local && (relpath.size() < 3 || relpath.substr(0, 2) != "..")) {
              candidates.push_back({root, relpath, 0, 0});
              found = true;
            }
          }
        }
        if (!found) {
          // Not preferred, but still the only match available.
          candidates.push_back({IncludePath{dir, false}, include.path, 0, 0});
        }
      } else {
        candidates.push_back({IncludePath{dir, false}, include.path, 0, 0});
      }
    }
  }

  fs::path incpath_text(include.path);

  /* Try to find a header that is within the same implied folder or subfolder
   * thereof from the given file we are processing. */
  for (auto &e : headers) {
    const IncludePath &incpath = e.first;
    const auto &hdrs = e.second;
    for (const fs::path &hdr : hdrs) {
      if (hdr.filename() == incpath_text.filename()) {
        Candidate cand{incpath, fs::relative(hdr, incpath.path).string(), 0};
        cand.folder_distance = calculate_path_distance(
            file, include, cand.header, cand.search_path);
        candidates.push_back(cand);
      } else if (fuzzy > 0) {
        IncludeStmt closest_path;
        int closest_distance = fuzzy + 1;
        std::string key = hdr.filename().string();
        int dist1 = levenshtein_distance(key, incpath_text.filename().string());
        int dist2 = levenshtein_distance(key, incpath_text.string());
        int dist = std::min(dist1, dist2);
        if (dist <= fuzzy) {
          Candidate cand{incpath, fs::relative(hdr, incpath.path).string(),
                         dist};
          cand.folder_distance = calculate_path_distance(
              file, include, cand.header, cand.search_path);
          candidates.push_back(cand);
        }
      }
    }
  }

  /* Sort them on distance. */
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate &a, const Candidate &b) {
              return a.weighted_distance() < b.weighted_distance();
            });

  return candidates;
}
