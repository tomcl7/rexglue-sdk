/**
 * @file        tests/unit/codegen/manifest_test.cpp
 * @brief       Unit tests for manifest TOML parser
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/manifest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

fs::path g_tmp_dir;

void setupTempDir() {
  g_tmp_dir = fs::temp_directory_path() / "manifest_test";
  fs::create_directories(g_tmp_dir);
}

void teardownTempDir() {
  fs::remove_all(g_tmp_dir);
}

void writeFile(const std::string& name, const std::string& content) {
  std::ofstream f(g_tmp_dir / name);
  f << content;
}

}  // namespace

TEST_CASE("Manifest: parse basic manifest", "[codegen][manifest]") {
  setupTempDir();

  writeFile("manifest.toml", R"(
[project]
projectName = "mygame"

[entrypoint]
config = "mygame_default_xex.toml"

[[modules]]
config = "mygame_somelib_dll.toml"
guestPath = "bin/somelib.dll"
  )");

  auto result = rex::codegen::ManifestConfig::Load(g_tmp_dir / "manifest.toml");
  REQUIRE(result.has_value());
  CHECK(result->projectName == "mygame");
  CHECK(result->entrypointConfig == g_tmp_dir / "mygame_default_xex.toml");
  REQUIRE(result->modules.size() == 1u);
  CHECK(result->modules[0].config == g_tmp_dir / "mygame_somelib_dll.toml");
  CHECK(result->modules[0].guestPath == "bin/somelib.dll");

  teardownTempDir();
}

TEST_CASE("Manifest: IsManifest detection", "[codegen][manifest]") {
  setupTempDir();
  writeFile("manifest.toml",
            "[project]\nprojectName = \"test\"\n[entrypoint]\nconfig = \"x.toml\"");
  writeFile("config.toml", "project_name = \"test\"\nfile_path = \"test.xex\"");

  CHECK(rex::codegen::ManifestConfig::IsManifest(g_tmp_dir / "manifest.toml"));
  CHECK_FALSE(rex::codegen::ManifestConfig::IsManifest(g_tmp_dir / "config.toml"));
  teardownTempDir();
}

TEST_CASE("Manifest: multiple modules", "[codegen][manifest]") {
  setupTempDir();
  writeFile("manifest.toml", R"(
[project]
projectName = "mygame"
[entrypoint]
config = "default_xex.toml"
[[modules]]
config = "lib_a.toml"
guestPath = "bin/lib_a.dll"
[[modules]]
config = "lib_b.toml"
guestPath = "data/lib_b.dll"
  )");

  auto result = rex::codegen::ManifestConfig::Load(g_tmp_dir / "manifest.toml");
  REQUIRE(result.has_value());
  CHECK(result->modules.size() == 2u);
  CHECK(result->modules[0].guestPath == "bin/lib_a.dll");
  CHECK(result->modules[1].guestPath == "data/lib_b.dll");
  teardownTempDir();
}

TEST_CASE("Manifest: missing project name fails", "[codegen][manifest]") {
  setupTempDir();
  writeFile("manifest.toml", "[entrypoint]\nconfig = \"x.toml\"");
  auto result = rex::codegen::ManifestConfig::Load(g_tmp_dir / "manifest.toml");
  CHECK_FALSE(result.has_value());
  teardownTempDir();
}
