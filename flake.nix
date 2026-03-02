{
  description = "UNHU — Mach microkernel OS development environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        devShells.default = pkgs.mkShell {
          name = "unhx-dev";

          packages = with pkgs; [
            # Cross-compilation toolchain
            llvmPackages_18.clang
            llvmPackages_18.lld
            llvmPackages_18.llvm

            # Build system
            cmake
            ninja
            gnumake

            # Emulation & testing
            qemu

            # ISO creation (for bootable images)
            xorriso
            grub2

            # Debugging
            gdb

            # Utilities
            git
            coreutils
          ];

          shellHook = ''
            echo "UNHU development environment"
            echo "  clang:  $(clang --version | head -1)"
            echo "  qemu:   $(qemu-system-x86_64 --version | head -1)"
            echo "  cmake:  $(cmake --version | head -1)"
            echo ""
            echo "Build:  cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-elf-clang.cmake -DUNHU_BOOT_TESTS=ON && cmake --build build"
            echo "Run:    ./tools/run-qemu.sh"
          '';
        };
      }
    );
}
