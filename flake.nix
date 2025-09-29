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
              glfw
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

              julia

              xorg.libX11
              xorg.libXrandr
              wayland
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

            # needed for stupid renderdoc to work
            export LD_LIBRARY_PATH="${pkgs.xorg.libX11}/lib:${pkgs.xorg.libXext}/lib:${pkgs.xorg.libXi}/lib:${pkgs.xorg.libXrandr}/lib:${pkgs.vulkan-loader}/lib"
          '';
        };
      }
    );
}
