#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include "../src/helpers/vc_toolchain.hpp"
#include "../src/helpers/syntax_checker.hpp"
#include "../src/helpers/conventional_commit.hpp"

using namespace rouen::helpers;

TEST(VCProjectWorkflowTest, ParseProjectFileXML) {
    // Locate the sample C++23 task runner project
    std::filesystem::path proj_path = "examples/cpp23_win_task_runner/Cpp23TaskRunner.vcxproj";
    if (!std::filesystem::exists(proj_path)) {
        proj_path = "../examples/cpp23_win_task_runner/Cpp23TaskRunner.vcxproj";
    }

    ASSERT_TRUE(std::filesystem::exists(proj_path)) << "Project file not found at " << proj_path.string();

    auto info = VCToolchainService::parse_project_file(proj_path.string());
    EXPECT_TRUE(info.is_valid);
    EXPECT_EQ(info.project_name, "Cpp23TaskRunner");
    EXPECT_EQ(info.platform_toolset, "v145");
    EXPECT_EQ(info.language_standard, "stdcpplatest");

    // Check configurations
    EXPECT_FALSE(info.configurations.empty());
    EXPECT_NE(std::find(info.configurations.begin(), info.configurations.end(), "Release"), info.configurations.end());

    // Check platforms
    EXPECT_FALSE(info.platforms.empty());
    EXPECT_NE(std::find(info.platforms.begin(), info.platforms.end(), "x64"), info.platforms.end());

    // Check source files
    EXPECT_FALSE(info.source_files.empty());
    EXPECT_NE(std::find(info.source_files.begin(), info.source_files.end(), "main.cpp"), info.source_files.end());

    // Check header files
    EXPECT_FALSE(info.header_files.empty());
    EXPECT_NE(std::find(info.header_files.begin(), info.header_files.end(), "task_runner.hpp"), info.header_files.end());
}

TEST(VCProjectWorkflowTest, MSBuildCommandGeneration) {
    VSInstallation vs;
    vs.msbuild_path = "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\MSBuild\\Current\\Bin\\amd64\\MSBuild.exe";
    vs.toolset = "v145";

    std::string proj = "C:\\src\\rouen\\examples\\cpp23_win_task_runner\\Cpp23TaskRunner.vcxproj";

    // 1. Build command
    std::string build_cmd = VCToolchainService::build_command(vs, proj, "build", "Release", "x64");
    EXPECT_NE(build_cmd.find("MSBuild.exe"), std::string::npos);
    EXPECT_NE(build_cmd.find("/t:Build"), std::string::npos);
    EXPECT_NE(build_cmd.find("/p:Configuration=Release"), std::string::npos);
    EXPECT_NE(build_cmd.find("/p:Platform=x64"), std::string::npos);
    EXPECT_NE(build_cmd.find("/m:2"), std::string::npos); // Enforce memory constraint!

    // 2. Rebuild command
    std::string rebuild_cmd = VCToolchainService::build_command(vs, proj, "rebuild", "Debug", "ARM64");
    EXPECT_NE(rebuild_cmd.find("/t:Rebuild"), std::string::npos);
    EXPECT_NE(rebuild_cmd.find("/p:Configuration=Debug"), std::string::npos);
    EXPECT_NE(rebuild_cmd.find("/p:Platform=ARM64"), std::string::npos);
    EXPECT_NE(rebuild_cmd.find("/m:2"), std::string::npos);

    // 3. Clean command
    std::string clean_cmd = VCToolchainService::build_command(vs, proj, "clean", "Release", "x64");
    EXPECT_NE(clean_cmd.find("/t:Clean"), std::string::npos);
    EXPECT_NE(clean_cmd.find("/m:2"), std::string::npos);

    // 4. Check syntax command for single file
    std::string syntax_cmd = VCToolchainService::build_command(vs, proj, "check_syntax", "Release", "x64", "main.cpp");
    EXPECT_NE(syntax_cmd.find("/t:ClCompile"), std::string::npos);
    EXPECT_NE(syntax_cmd.find("/p:SelectedFiles=\"main.cpp\""), std::string::npos);
    EXPECT_NE(syntax_cmd.find("/m:2"), std::string::npos);
}

TEST(VCProjectWorkflowTest, ParseMSVCColumnDiagnostics) {
    std::string msvc_output = 
        "1>C:\\src\\rouen\\examples\\cpp23_win_task_runner\\main.cpp(35,91): error C2100: you cannot dereference an operand of type 'int' [Cpp23TaskRunner.vcxproj]\n"
        "1>C:\\src\\rouen\\examples\\cpp23_win_task_runner\\task_runner.hpp(75,18): warning C4834: discarding return value of function with [[nodiscard]] attribute [Cpp23TaskRunner.vcxproj]\n";

    auto diags = SyntaxChecker::parse_compiler_output(msvc_output, "fallback.cpp");
    ASSERT_EQ(diags.size(), 2);

    // Diagnostic 1: Error C2100
    EXPECT_EQ(diags[0].file, "C:\\src\\rouen\\examples\\cpp23_win_task_runner\\main.cpp");
    EXPECT_EQ(diags[0].line, 35);
    EXPECT_EQ(diags[0].column, 91);
    EXPECT_EQ(diags[0].severity, "error");
    EXPECT_NE(diags[0].message.find("C2100"), std::string::npos);
    EXPECT_NE(diags[0].message.find("cannot dereference"), std::string::npos);

    // Diagnostic 2: Warning C4834
    EXPECT_EQ(diags[1].file, "C:\\src\\rouen\\examples\\cpp23_win_task_runner\\task_runner.hpp");
    EXPECT_EQ(diags[1].line, 75);
    EXPECT_EQ(diags[1].column, 18);
    EXPECT_EQ(diags[1].severity, "warning");
    EXPECT_NE(diags[1].message.find("C4834"), std::string::npos);
}

TEST(VCProjectWorkflowTest, VCToolchainServiceInstallationsDiscovery) {
    auto& svc = VCToolchainService::instance();
    auto insts = svc.get_installations();
    // Discovery should execute safely without crashing on any platform
    EXPECT_GE(insts.size(), 0);
}
