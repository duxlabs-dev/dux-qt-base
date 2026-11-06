{ rev ? null }: (final: prev: {
  dux-qt-base = final.callPackage ./default.nix {
    gitRev = rev;
  };
})
