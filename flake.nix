{
  description = "Rouen project flake for reproducible C++/CMake/SDL2 development and CI";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.05";
    nixpkgs-unstable.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, nixpkgs-unstable, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        unstable = import nixpkgs-unstable { inherit system; };
        # Use clangStdenv on Darwin for a fully Nix-native toolchain
        stdenv = if unstable.stdenv.isDarwin then unstable.clangStdenv else unstable.stdenv;
        darwinFrameworks = [
          unstable.darwin.apple_sdk.frameworks.Foundation
          unstable.darwin.apple_sdk.frameworks.AppKit
        ];
        darwinCmakeFlags = [
          "-DCMAKE_OSX_SYSROOT=${unstable.darwin.apple_sdk.sdkRoot}"
          "-DOPENGL_INCLUDE_DIR=${unstable.darwin.apple_sdk.sdkRoot}/System/Library/Frameworks/OpenGL.framework/Headers"
        ];
        darwinEnv = {
          CXXFLAGS = "-isystem ${unstable.darwin.apple_sdk.sdkRoot}/System/Library/Frameworks/OpenGL.framework/Headers -isysroot ${unstable.darwin.apple_sdk.sdkRoot} -I${unstable.libcxx}/include/c++/v1";
        };
      in {
        devShells.default = unstable.mkShell {
          buildInputs = [
            unstable.cmake
            unstable.ninja
            unstable.SDL2
            unstable.pkg-config
            unstable.curl
            unstable.openssl
            unstable.glaze
          ] ++ (unstable.lib.optionals (unstable.stdenv.isDarwin) [ unstable.libcxx ])
            ++ (unstable.lib.optionals (!unstable.stdenv.isDarwin) [ unstable.gcc ]);
        };

        packages.default = stdenv.mkDerivation {
          pname = "rouen";
          version = "0.1.0";
          src = ./.;
          nativeBuildInputs = [ unstable.cmake unstable.ninja unstable.pkg-config unstable.git unstable.cacert ];
          buildInputs = [ unstable.SDL2 unstable.curl unstable.openssl unstable.sqlite unstable.SDL2_image unstable.libtiff unstable.lerc unstable.tinyxml-2 unstable.glaze unstable.imgui ]
            ++ (unstable.lib.optionals (unstable.stdenv.isDarwin) (darwinFrameworks ++ [ unstable.libcxx ]))
            ++ (unstable.lib.optionals (!unstable.stdenv.isDarwin) [ unstable.libGL ]);
          cmakeFlags = [ 
            "-DCMAKE_BUILD_TYPE=Release" 
            "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
          ] ++ (unstable.lib.optionals (unstable.stdenv.isDarwin) darwinCmakeFlags);
          env = unstable.lib.optionalAttrs (unstable.stdenv.isDarwin) darwinEnv;
          installPhase = ''
            mkdir -p $out/bin
            cp build-nix/rouen $out/bin/
          '';
        };

        checks.default = stdenv.mkDerivation {
          pname = "rouen-tests";
          version = "0.1.0";
          src = ./.;
          nativeBuildInputs = [ unstable.cmake unstable.ninja unstable.pkg-config unstable.git unstable.cacert ];
          buildInputs = [ unstable.SDL2 unstable.curl unstable.openssl unstable.sqlite unstable.SDL2_image unstable.libtiff unstable.tinyxml2 unstable.glaze unstable.imgui ]
            ++ (unstable.lib.optionals (unstable.stdenv.isDarwin) (darwinFrameworks ++ [ unstable.libcxx ]))
            ++ (unstable.lib.optionals (!unstable.stdenv.isDarwin) [ unstable.libGL ]);
          cmakeFlags = [ 
            "-DCMAKE_BUILD_TYPE=Debug"
            "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
          ] ++ (unstable.lib.optionals (unstable.stdenv.isDarwin) darwinCmakeFlags);
          env = unstable.lib.optionalAttrs (unstable.stdenv.isDarwin) darwinEnv;
          buildPhase = ''
            mkdir -p build-tests
            cd build-tests
            cmake ../tests -DCMAKE_BUILD_TYPE=Debug -DFETCHCONTENT_FULLY_DISCONNECTED=ON
            cmake --build . --parallel
          '';
          checkPhase = ''
            cd build-tests
            ctest --output-on-failure
          '';
        };
      });
}
