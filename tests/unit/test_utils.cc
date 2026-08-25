#include <doctest/doctest.h>
#include "utils.h"

#include <filesystem>

TEST_CASE("string_to_file / load_string_from_file round trip")
{
  std::filesystem::path path =
    std::filesystem::temp_directory_path() / "pomelolib_test_utils_roundtrip.txt";

  std::string text = "hello\npomelo\n";
  string_to_file(text, path.string());
  std::string loaded = load_string_from_file(path.string());

  CHECK(loaded == text);
  std::filesystem::remove(path);
}

TEST_CASE("load_string_from_file on a missing file returns empty")
{
  std::string loaded =
    load_string_from_file("/nonexistent/pomelolib_test_utils_missing.txt");
  CHECK(loaded.empty());
}
