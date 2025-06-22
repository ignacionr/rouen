{ pkgs ? import <nixpkgs> {} }:

let
  # Platform detection
  isDarwin = pkgs.stdenv.isDarwin;
  isLinux = pkgs.stdenv.isLinux;
  compiler = if isDarwin then pkgs.llvmPackages.clang else pkgs.gcc;
  # Set CC/CXX for CMake to pick the right compiler
  envVars = if isDarwin then {
    CC = "clang";
    CXX = "clang++";
  } else {
    CC = "gcc";
    CXX = "g++";
  };
in
pkgs.mkShell {
  buildInputs = [
    pkgs.cmake
    compiler
    pkgs.vcpkg
    pkgs.pkg-config
    # Add more tools as needed
  ];
  shellHook = ''
    export CC=${envVars.CC}
    export CXX=${envVars.CXX}
    echo "[Nix] Using compiler: $CC ($($CC --version | head -1))"
  '';
}
