{ pkgs ? import (fetchTarball "https://github.com/NixOS/nixpkgs/archive/nixos-24.05.tar.gz") {} }:

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
    pkgs.tinyxml-2
    pkgs.libtiff
    pkgs.lerc
    pkgs.gtest.dev
    pkgs.glaze
    pkgs.imgui
    # Add macOS SDK frameworks for proper header isolation
  ] ++ (if isDarwin then [
    # Use current Darwin frameworks - these should be available in modern nixpkgs
    # The legacy apple_sdk_11_0 stub has been removed, but frameworks are still accessible
    pkgs.darwin.apple_sdk.frameworks.Foundation
    pkgs.darwin.apple_sdk.frameworks.AppKit
    pkgs.darwin.apple_sdk.frameworks.IOKit
    pkgs.darwin.apple_sdk.frameworks.CoreVideo
    pkgs.darwin.apple_sdk.frameworks.AudioToolbox
    pkgs.darwin.apple_sdk.frameworks.CoreHaptics
    pkgs.darwin.apple_sdk.frameworks.GameController
    pkgs.darwin.apple_sdk.frameworks.Metal
    pkgs.darwin.apple_sdk.frameworks.ForceFeedback
    pkgs.darwin.apple_sdk.frameworks.Carbon
    pkgs.darwin.apple_sdk.frameworks.OpenGL
  ] else [
    # Linux X11 dependencies for SDL2
    pkgs.xorg.libX11.dev
    pkgs.xorg.libXext.dev
    pkgs.xorg.libXrandr.dev
    pkgs.xorg.libXinerama.dev
    pkgs.xorg.libXcursor.dev
    pkgs.xorg.libXi.dev
    pkgs.xorg.libXScrnSaver
    # Additional X11 dependencies
    pkgs.xorg.libXdmcp
  ]);
  shellHook = ''
    export CC=${envVars.CC}
    export CXX=${envVars.CXX}
    export PKG_CONFIG_PATH="${pkgs.tinyxml-2}/lib/pkgconfig:${pkgs.openssl}/lib/pkgconfig:${pkgs.sqlite}/lib/pkgconfig:${pkgs.SDL2}/lib/pkgconfig:${pkgs.SDL2_image}/lib/pkgconfig:${pkgs.curl}/lib/pkgconfig:${pkgs.gtest.dev}/lib/pkgconfig:${pkgs.glaze}/lib/pkgconfig:${pkgs.imgui}/lib/pkgconfig"
    export CMAKE_PREFIX_PATH="${pkgs.cmake}/lib/cmake:${pkgs.tinyxml-2}:${pkgs.openssl}:${pkgs.sqlite}:${pkgs.SDL2}:${pkgs.SDL2_image}:${pkgs.curl}:${pkgs.gtest.dev}:${pkgs.glaze}:${pkgs.glaze}/share:${pkgs.imgui}:${pkgs.imgui}/share"
    # Remove Homebrew from PATH for full Nix isolation
    export PATH=$(echo "$PATH" | tr ':' '\n' | grep -v '/opt/homebrew' | grep -v '/usr/local' | paste -sd ':' -)
    echo "[Nix] Using compiler: $CC ($($CC --version | head -1))"
    echo "[Nix] CMAKE_PREFIX_PATH: $CMAKE_PREFIX_PATH"
    echo "[Nix] PKG_CONFIG_PATH: $PKG_CONFIG_PATH"
  '';
}
