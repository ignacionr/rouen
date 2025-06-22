{
  "description": "Rouen project flake for reproducible C++/CMake/SDL2 development and CI",
  "inputs": {
    "nixpkgs": {
      "url": "github:NixOS/nixpkgs/nixos-24.05"
    },
    "flake-utils": {
      "url": "github:numtide/flake-utils"
    }
  },
  "outputs": {
    "self",
    "nixpkgs",
    "flake-utils",
    "...": "{ self, nixpkgs, flake-utils, ... }@inputs: flake-utils.lib.eachDefaultSystem (system: let
      pkgs = import nixpkgs { inherit system; };
      cmake = pkgs.cmake;
      ninja = pkgs.ninja;
      sdl2 = pkgs.sdl2;
      pkg-config = pkgs.pkg-config;
      curl = pkgs.curl;
      openssl = pkgs.openssl;
      # Add more dependencies as needed
      buildInputs = [ cmake ninja sdl2 pkg-config curl openssl ];
    in {
      devShells.default = pkgs.mkShell {
        buildInputs = buildInputs;
        shellHook = "export CC=${pkgs.gcc}/bin/gcc; export CXX=${pkgs.gcc}/bin/g++";
      };
      packages.default = pkgs.stdenv.mkDerivation {
        name = "rouen";
        src = ./.;
        nativeBuildInputs = [ cmake ninja pkg-config ];
        buildInputs = [ sdl2 curl openssl ];
        cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" ];
        installPhase = ''
          mkdir -p $out/bin
          cp build-nix/rouen $out/bin/
        '';
      };
      checks.default = pkgs.stdenv.mkDerivation {
        name = "rouen-tests";
        src = ./.;
        nativeBuildInputs = [ cmake ninja pkg-config ];
        buildInputs = [ sdl2 curl openssl ];
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
    })"
  }
}
