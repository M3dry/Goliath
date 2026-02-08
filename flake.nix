{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };
  outputs = {
    nixpkgs,
    flake-utils,
    ...
  }:
    flake-utils.lib.eachDefaultSystem (
      system: let
        pkgs = nixpkgs.legacyPackages.${system};
        llvm = pkgs.llvmPackages_20;
        pleaseKeepMyInputs = pkgs.writeTextDir "bin/.inputs" (builtins.concatStringsSep " " (builtins.attrValues {inherit nixpkgs;}));
      in {
        devShell = pkgs.mkShell {
          nativeBuildInputs = with pkgs;
            [
              glm
              xxHash
              vulkan-volk
              vulkan-headers
              vulkan-loader
              vulkan-memory-allocator
              vulkan-utility-libraries
              vulkan-validation-layers

              catch2_3

              pkg-config
              cmake
              gdb
              renderdoc
              valgrind
              shader-slang
              shaderc

              julia

              xorg.libX11.dev
              xorg.libXrandr.dev
              xorg.libXcursor.dev
              xorg.libXinerama.dev
              xorg.libXi.dev
              xorg.libXext.dev
              wayland
              wayland-scanner
              libdecor
              gtk3

              libxkbcommon
            ]
            ++ [
              llvm.clang-tools
              llvm.clang
              llvm.lld
              pleaseKeepMyInputs
            ];
          shellHook = ''
            export CC=clang
            export CXX=clang++

            export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath [pkgs.vulkan-loader pkgs.xorg.libX11 pkgs.xorg.libXrandr pkgs.xorg.libXcursor pkgs.xorg.libXinerama pkgs.xorg.libXi pkgs.xorg.libXext pkgs.wayland pkgs.libxkbcommon pkgs.libdecor]}"
          '';
        };
      }
    );
}
