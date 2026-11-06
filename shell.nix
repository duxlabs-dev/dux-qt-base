{
  pkgs ? import <nixpkgs> {},
  stdenv ? pkgs.clangStdenv, # faster compiles than gcc
  dux-qt-base ? pkgs.callPackage ./default.nix { inherit stdenv; },
  ...
}: let
  tidyfox = import (pkgs.fetchFromGitea {
    domain = "git.outfoxxed.me";
    owner = "outfoxxed";
    repo = "tidyfox";
    rev = "9d85d7e7dea2602aa74ec3168955fee69967e92f";
    hash = "sha256-77ERiweF6lumonp2c/124rAoVG6/o9J+Aajhttwtu0w=";
  }) { inherit pkgs; };
in pkgs.mkShell.override { stdenv = dux-qt-base.stdenv; } {
  inputsFrom = [ dux-qt-base ];

  nativeBuildInputs = with pkgs; [
    just
    clang-tools
    parallel
  ];

  TIDYFOX = "${tidyfox}/lib/libtidyfox.so";

  shellHook = ''
    export CMAKE_BUILD_PARALLEL_LEVEL=$(nproc)
    export QMLLS_BUILD_DIRS=$(pwd)/build
  '';
}
