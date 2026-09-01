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
        stdenv = if unstable.stdenv.isDarwin then unstable.llvmPackages_20.stdenv else unstable.gcc15Stdenv;
        
        # Modern Darwin frameworks - removed in Nixpkgs 25.11 as they are now integrated into stdenv
        darwinFrameworks = [];
        
        # macOS-specific build configuration - simplified
        darwinCmakeFlags = unstable.lib.optionals unstable.stdenv.isDarwin [
          "-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0"
        ];
        
        # No special environment needed - let Nix handle it automatically
        darwinEnv = {};
      in {
        devShells.default = unstable.mkShell.override { inherit stdenv; } {
          buildInputs = [
            unstable.cmake
            unstable.ninja
            unstable.sdl3
            unstable.sdl3-image
            unstable.ffmpeg
            unstable.pkg-config
            unstable.curl
            unstable.openssl
            unstable.sqlite
            unstable.libtiff
            unstable.lerc
            unstable.tinyxml-2  # TinyXML2 (version 2)
            unstable.glaze
            unstable.imgui
            unstable.pdfium-binaries
            unstable.gtest.dev  # Google Test development headers and CMake files
            unstable.ccache     # Compiler cache to speed up compilation
            unstable.clang-tools # Clang-tidy and other analysis tools
            unstable.python3     # Python 3 interpreter (required by run-clang-tidy)
            unstable.git        # Git binary for source control and tests
          ] 
            ++ (unstable.lib.optionals (unstable.stdenv.isDarwin) [ unstable.libcxx ])
            ++ (unstable.lib.optionals (!unstable.stdenv.isDarwin) [ 
              unstable.gcc15 
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
          
          shellHook = ''
            echo "[Nix Flake] Entering Rouen development environment..."
            export CMAKE_BUILD_PARALLEL_LEVEL=2
            export MAKEFLAGS="-j2"
            export NIX_BUILD_CORES=2
            
            # Load secrets and environment variables
            if [ -f scripts/nix-setup-secrets.sh ]; then
              echo "[Nix Flake] Loading secrets and environment..."
              source scripts/nix-setup-secrets.sh
            else
              echo "[Nix Flake] Warning: secrets setup script not found"
            fi
            
            echo "[Nix Flake] Development environment ready!"
          '';
        };

        devShells.mingw = unstable.pkgsCross.mingwW64.mkShell {
          buildInputs = [
            unstable.pkgsCross.mingwW64.buildPackages.cmake
            unstable.pkgsCross.mingwW64.buildPackages.ninja
            unstable.pkgsCross.mingwW64.buildPackages.pkg-config
          ];
          shellHook = ''
            echo "[Nix Flake] Entering MinGW-w64 Windows cross-compilation environment..."
            export CC=x86_64-w64-mingw32-gcc
            export CXX=x86_64-w64-mingw32-g++
            echo "Cross compiler: $(x86_64-w64-mingw32-gcc --version | head -n 1)"
          '';
        };

        packages.default = stdenv.mkDerivation {
          pname = "rouen";
          version = "0.1.0";
          src = unstable.lib.cleanSourceWith {
            filter = name: type: let
              relPath = unstable.lib.removePrefix (toString ./. + "/") (toString name);
              baseName = baseNameOf (toString name);
            in
              ! (type == "directory" && (
                relPath == "build" ||
                relPath == "build-debug" ||
                relPath == "build-tests" ||
                relPath == "build-cmake-tools" ||
                relPath == "cache" ||
                relPath == "result" ||
                baseName == ".git" ||
                baseName == "vcpkg_installed" ||
                baseName == ".direnv"
              ));
            src = ./.;
          };
          nativeBuildInputs = [ 
            unstable.cmake 
            unstable.ninja 
            unstable.pkg-config 
            unstable.git 
            unstable.cacert 
            unstable.makeWrapper
          ];
          buildInputs = [ 
            unstable.sdl3
            unstable.curl 
            unstable.openssl 
            unstable.sqlite 
            unstable.sdl3-image
            unstable.ffmpeg
            unstable.libtiff 
            unstable.lerc 
            unstable.tinyxml-2  # TinyXML2 (version 2)
            unstable.glaze 
            unstable.imgui 
            unstable.pdfium-binaries
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
            "-DCOMPILE_GIT_HASH=${self.rev or "unknown"}"
          ] ++ darwinCmakeFlags;
          env = darwinEnv // {
            # Disable dynamic icon generation for Nix builds to avoid sips/iconutil dependency
            ROUEN_SKIP_ICON_GENERATION = "1";
          };
          buildPhase = ''
            runHook preBuild
            CORES=''${NIX_BUILD_CORES:-2}
            if [ "$CORES" -eq 0 ]; then
              CORES=2
            fi
            cmake --build . --parallel "$CORES"
            runHook postBuild
          '';
          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin
            
            # Find the binary regardless of platform-specific directory structure
            ${if unstable.stdenv.isDarwin then ''
              # On macOS, copy the app bundle and symlink the binary
              if [ -d "rouen.app" ]; then
                mkdir -p $out/Applications
                cp -R rouen.app $out/Applications/Rouen.app
                wrapProgram $out/Applications/Rouen.app/Contents/MacOS/rouen --prefix PATH : ${unstable.lib.makeBinPath [ unstable.git ]}
                ln -s ../Applications/Rouen.app/Contents/MacOS/rouen $out/bin/rouen
              else
                echo "Error: Could not find rouen.app"
                echo "Directory contents:"
                find . -name "rouen*" -type f || true
                ls -la .
                exit 1
              fi
            '' else ''
              # On Linux, look for the binary
              if [ -f "rouen" ]; then
                cp rouen $out/bin/
                wrapProgram $out/bin/rouen --prefix PATH : ${unstable.lib.makeBinPath [ unstable.git ]}
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
            unstable.ninja
            unstable.pkg-config 
            unstable.git 
            unstable.cacert 
          ];
          buildInputs = [ 
            unstable.sdl3
            unstable.curl 
            unstable.openssl 
            unstable.sqlite 
            unstable.sdl3-image
            unstable.ffmpeg
            unstable.libtiff 
            unstable.lerc 
            unstable.tinyxml-2  # TinyXML2 (version 2)
            unstable.gtest.dev
            unstable.glaze 
            unstable.imgui 
            unstable.pdfium-binaries
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
            "-DCOMPILE_GIT_HASH=${self.rev or "unknown"}"
          ] ++ darwinCmakeFlags;
          env = darwinEnv;
          configurePhase = ''
            runHook preConfigure
            mkdir -p build-tests
            cd build-tests
            cmake -G Ninja ../tests -DCMAKE_TOOLCHAIN_FILE=../cmake/nix-toolchain.cmake -DCMAKE_BUILD_TYPE=Debug -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DCOMPILE_GIT_HASH=${self.rev or "unknown"}
            runHook postConfigure
          '';
          buildPhase = ''
            runHook preBuild
            CORES=''${NIX_BUILD_CORES:-2}
            if [ "$CORES" -eq 0 ]; then
              CORES=2
            fi
            cmake --build . --parallel "$CORES"
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
