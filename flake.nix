{
  description = "Rouen project flake for reproducible C++/CMake/SDL2 development and CI";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in {
        devShells.default = pkgs.mkShell {
          buildInputs = [
            pkgs.cmake
            pkgs.ninja
            pkgs.sdl2
            pkgs.pkg-config
            pkgs.curl
            pkgs.openssl
            pkgs.gcc
          ];
        };

        packages.default = pkgs.stdenv.mkDerivation {
          pname = "rouen";
          version = "0.1.0";
          src = ./.;
          nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.pkg-config ];
          buildInputs = [ pkgs.sdl2 pkgs.curl pkgs.openssl ];
          cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" ];
          installPhase = ''
            mkdir -p $out/bin
            cp build-nix/rouen $out/bin/
          '';
        };

        checks.default = pkgs.stdenv.mkDerivation {
          pname = "rouen-tests";
          version = "0.1.0";
          src = ./.;
          nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.pkg-config ];
          buildInputs = [ pkgs.sdl2 pkgs.curl pkgs.openssl ];
          cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Debug" ];
          buildPhase = ''
            mkdir -p build-tests
            cd build-tests
            cmake ../tests -DCMAKE_BUILD_TYPE=Debug
            cmake --build . --parallel
          '';
          checkPhase = ''
            cd build-tests
            ctest --output-on-failure
          '';
        };
      });
}
