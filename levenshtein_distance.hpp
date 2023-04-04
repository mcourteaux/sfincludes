#include <numeric>
#include <string>

int levenshtein_distance(const std::string &s1, const std::string &s2) {
  // To change the type this function manipulates and returns, change
  // the return type and the types of the two variables below.
  int s1len = s1.size();
  int s2len = s2.size();

  int column_start = 1;

  int *column = new int[s1len + 1];
  std::iota(column + column_start, column + s1len + 1, column_start);

  constexpr int insert_cost = 4;
  constexpr int change_cost = 2;
  constexpr int capitalize_cost = 1;

  for (int x = column_start; x <= s2len; x++) {
    column[0] = x;
    int last_diagonal = x - column_start;
    for (int y = column_start; y <= s1len; y++) {
      int old_diagonal = column[y];

      char c1 = s1[y - 1];
      char c2 = s2[x - 1];
      int diff_cost;
      if (c1 == c2) {
        diff_cost = 0;
      } else if (std::tolower(c1) == std::tolower(c2)) {
        diff_cost = capitalize_cost;
      } else {
        diff_cost = change_cost;
      }

      // clang-format off
      auto possibilities = {
        column[y] + insert_cost,
        column[y - 1] + insert_cost,
        last_diagonal + diff_cost
      };
      // clang-format on
      column[y] = std::min(possibilities);
      last_diagonal = old_diagonal;
    }
  }
  int result = column[s1len];
  delete[] column;
  return result;
}
