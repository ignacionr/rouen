{ pkgs ? import <nixpkgs> {} }:

let
  # Platform detection
  isDarwin = pkgs.stdenv.isDarwin;
  isLinux = pkgs.stdenv.isLinux;
  compiler = if isDarwin then pkgs.llvmPackages.clang else pkgs.gcc;
  # Set CC/CXX for CMake to pick the right compiler
  envVars = if isDarwin then {
    CC = "${compiler}/bin/clang";
    CXX = "${compiler}/bin/clang++";
  } else {
    CC = "${compiler}/bin/gcc";
    CXX = "${compiler}/bin/g++";
  };
in
pkgs.mkShell {
  buildInputs = [
    pkgs.cmake
    compiler
    pkgs.pkg-config
    pkgs.curl
    pkgs.openssl
    pkgs.sqlite
    pkgs.SDL2
    pkgs.SDL2_image
    pkgs.tinyxml2
    pkgs.gtest
    pkgs.glaze
    # Add macOS SDK frameworks for proper header isolation
  ] ++ (if isDarwin then [
    pkgs.darwin.apple_sdk.frameworks.Cocoa
    pkgs.darwin.apple_sdk.frameworks.IOKit
    pkgs.darwin.apple_sdk.frameworks.CoreVideo
    pkgs.darwin.apple_sdk.frameworks.AudioToolbox
    pkgs.darwin.apple_sdk.frameworks.CoreHaptics
    pkgs.darwin.apple_sdk.frameworks.GameController
    pkgs.darwin.apple_sdk.frameworks.Metal
    pkgs.darwin.apple_sdk.frameworks.ForceFeedback
    pkgs.darwin.apple_sdk.frameworks.Carbon
    pkgs.darwin.apple_sdk.frameworks.OpenGL
  ] else []);
  shellHook = ''
    export CC=${envVars.CC}
    export CXX=${envVars.CXX}
    export PKG_CONFIG_PATH="${pkgs.tinyxml2}/lib/pkgconfig:${pkgs.openssl}/lib/pkgconfig:${pkgs.sqlite}/lib/pkgconfig:${pkgs.SDL2}/lib/pkgconfig:${pkgs.SDL2_image}/lib/pkgconfig:${pkgs.curl}/lib/pkgconfig:${pkgs.gtest}/lib/pkgconfig:${pkgs.glaze}/lib/pkgconfig"
    export CMAKE_PREFIX_PATH=$(IFS=:; echo ${pkgs.cmake}/lib/cmake:${pkgs.tinyxml2}:${pkgs.openssl}:${pkgs.sqlite}:${pkgs.SDL2}:${pkgs.SDL2_image}:${pkgs.curl}:${pkgs.gtest}:${pkgs.glaze})
    # Remove Homebrew from PATH for full Nix isolation
    export PATH=$(echo "$PATH" | tr ':' '\n' | grep -v '/opt/homebrew' | grep -v '/usr/local' | paste -sd ':' -)
    echo "[Nix] Using compiler: $CC ($($CC --version | head -1))"
  '';
}
