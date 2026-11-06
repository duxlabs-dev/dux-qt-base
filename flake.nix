{
  inputs = {
    nixpkgs.url = "nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }: let
    overlayPkgs = p: p.appendOverlays [ self.overlays.default ];

    forEachSystem = fn:
      nixpkgs.lib.genAttrs
        nixpkgs.lib.platforms.linux
        (system: fn system (overlayPkgs nixpkgs.legacyPackages.${system}));
  in {
    overlays.default = import ./overlay.nix {
      rev = self.rev or self.dirtyRev;
    };

    packages = forEachSystem (system: pkgs: rec {
      dux-qt-base = pkgs.dux-qt-base;
      default = dux-qt-base;
    });

    devShells = forEachSystem (system: pkgs: rec {
      default = import ./shell.nix {
        inherit pkgs;
        dux-qt-base = self.packages.${system}.dux-qt-base.override {
          stdenv = pkgs.clangStdenv;
        };
      };
    });
  };
}
