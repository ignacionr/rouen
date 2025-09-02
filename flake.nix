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
        
        # Use appropriate stdenv for each platform
        stdenv = if unstable.stdenv.isDarwin then unstable.clang19Stdenv else unstable.stdenv;
        
        # Modern Darwin frameworks - use the current approach that works
        darwinFrameworks = unstable.lib.optionals unstable.stdenv.isDarwin [
          unstable.darwin.apple_sdk.frameworks.Foundation
          unstable.darwin.apple_sdk.frameworks.AppKit
          unstable.darwin.apple_sdk.frameworks.IOKit
          unstable.darwin.apple_sdk.frameworks.CoreVideo
          unstable.darwin.apple_sdk.frameworks.AudioToolbox
          unstable.darwin.apple_sdk.frameworks.CoreHaptics
          unstable.darwin.apple_sdk.frameworks.GameController
          unstable.darwin.apple_sdk.frameworks.Metal
          unstable.darwin.apple_sdk.frameworks.ForceFeedback
          unstable.darwin.apple_sdk.frameworks.Carbon
          unstable.darwin.apple_sdk.frameworks.OpenGL
          unstable.darwin.apple_sdk.frameworks.Security
          unstable.darwin.apple_sdk.frameworks.CoreFoundation
          unstable.darwin.apple_sdk.frameworks.SystemConfiguration
        ];
        
        # macOS-specific build configuration - simplified
        darwinCmakeFlags = unstable.lib.optionals unstable.stdenv.isDarwin [
          "-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0"
        ];
        
        # No special environment needed - let Nix handle it automatically
        darwinEnv = {};
      in {
        devShells.default = unstable.mkShell {
          buildInputs = [
            unstable.cmake
            unstable.ninja
            unstable.SDL2
            unstable.SDL2_image
            unstable.pkg-config
            unstable.curl
            unstable.openssl
            unstable.sqlite
            unstable.libtiff
            unstable.lerc
            unstable.tinyxml-2  # TinyXML2 (version 2)
            unstable.glaze
            unstable.imgui
            unstable.gtest.dev  # Google Test development headers and CMake files
            unstable.mpv         # Media player for voice notes playback
            unstable.sox         # Audio processing tool for voice notes recording
          ] 
            ++ (unstable.lib.optionals (unstable.stdenv.isDarwin) [ unstable.libcxx ])
            ++ (unstable.lib.optionals (!unstable.stdenv.isDarwin) [ 
              unstable.gcc 
              unstable.libGL 
              unstable.xorg.libX11.dev
              unstable.xorg.libXext.dev
              unstable.xorg.libXrandr.dev
              unstable.xorg.libXinerama.dev
              unstable.xorg.libXcursor.dev
              unstable.xorg.libXi.dev
              unstable.xorg.libXScrnSaver
              unstable.xorg.libXdmcp
            ]);
        };

        packages.default = stdenv.mkDerivation {
          pname = "rouen";
          version = "0.1.0";
          src = ./.;
          nativeBuildInputs = [ 
            unstable.cmake 
            unstable.ninja 
            unstable.pkg-config 
            unstable.git 
            unstable.cacert 
          ];
          buildInputs = [ 
            unstable.SDL2 
            unstable.curl 
            unstable.openssl 
            unstable.sqlite 
            unstable.SDL2_image 
            unstable.libtiff 
            unstable.lerc 
            unstable.tinyxml-2  # TinyXML2 (version 2)
            unstable.glaze 
            unstable.imgui 
            unstable.mpv         # Media player for voice notes playback
          ] ++ darwinFrameworks
            ++ (unstable.lib.optionals (!unstable.stdenv.isDarwin) [ 
              unstable.libGL 
              unstable.xorg.libX11.dev
              unstable.xorg.libXext.dev
              unstable.xorg.libXrandr.dev
              unstable.xorg.libXinerama.dev
              unstable.xorg.libXcursor.dev
              unstable.xorg.libXi.dev
              unstable.xorg.libXScrnSaver
              unstable.xorg.libXdmcp
            ]);
          cmakeFlags = [ 
            "-DCMAKE_BUILD_TYPE=Release" 
            "-DCMAKE_TOOLCHAIN_FILE=cmake/nix-toolchain.cmake"
            "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
          ] ++ darwinCmakeFlags;
          env = darwinEnv // {
            # Disable dynamic icon generation for Nix builds to avoid sips/iconutil dependency
            ROUEN_SKIP_ICON_GENERATION = "1";
          };
          buildPhase = ''
            runHook preBuild
            cmake --build . --parallel
            runHook postBuild
          '';
          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin
            
            # Find the binary regardless of platform-specific directory structure
            ${if unstable.stdenv.isDarwin then ''
              # On macOS, look for app bundle
              if [ -f "rouen.app/Contents/MacOS/rouen" ]; then
                cp rouen.app/Contents/MacOS/rouen $out/bin/
              else
                echo "Error: Could not find rouen.app/Contents/MacOS/rouen"
                echo "Directory contents:"
                find . -name "rouen*" -type f || true
                ls -la .
                exit 1
              fi
            '' else ''
              # On Linux, look for the binary
              if [ -f "rouen" ]; then
                cp rouen $out/bin/
              else
                echo "Error: Could not find rouen binary"
                echo "Directory contents:"
                find . -name "rouen*" -type f || true
                ls -la .
                exit 1
              fi
            ''}
            
            runHook postInstall
          '';
        };

        checks.default = stdenv.mkDerivation {
          pname = "rouen-tests";
          version = "0.1.0";
          src = ./.;
          nativeBuildInputs = [ 
            unstable.cmake 
            unstable.pkg-config 
            unstable.git 
            unstable.cacert 
          ];
          buildInputs = [ 
            unstable.SDL2 
            unstable.curl 
            unstable.openssl 
            unstable.sqlite 
            unstable.SDL2_image 
            unstable.libtiff 
            unstable.lerc 
            unstable.tinyxml-2  # TinyXML2 (version 2)
            unstable.gtest.dev
            unstable.glaze 
            unstable.imgui 
          ] ++ darwinFrameworks
            ++ (unstable.lib.optionals (!unstable.stdenv.isDarwin) [ 
              unstable.libGL 
              unstable.xorg.libX11.dev
              unstable.xorg.libXext.dev
              unstable.xorg.libXrandr.dev
              unstable.xorg.libXinerama.dev
              unstable.xorg.libXcursor.dev
              unstable.xorg.libXi.dev
              unstable.xorg.libXScrnSaver
              unstable.xorg.libXdmcp
            ]);
          cmakeFlags = [ 
            "-DCMAKE_BUILD_TYPE=Debug"
            "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
          ] ++ darwinCmakeFlags;
          env = darwinEnv;
          configurePhase = ''
            runHook preConfigure
            mkdir -p build-tests
            cd build-tests
            cmake ../tests -DCMAKE_TOOLCHAIN_FILE=../cmake/nix-toolchain.cmake -DCMAKE_BUILD_TYPE=Debug -DFETCHCONTENT_FULLY_DISCONNECTED=ON
            runHook postConfigure
          '';
          buildPhase = ''
            runHook preBuild
            cmake --build . --parallel
            runHook postBuild
          '';
          checkPhase = ''
            ctest --output-on-failure
          '';
          installPhase = ''
            # Tests don't need installation, but Nix expects an output directory
            mkdir -p $out
            echo "Tests built successfully" > $out/test-results.txt
          '';
        };
      });
}
