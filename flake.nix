{
  description = "Nix Game Boy LLM Development Kit";

  nixConfig = {
    extra-substituters = [
      "https://cache.nixos.org"
      "https://rstats-on-nix.cachix.org"
    ];
    extra-trusted-public-keys = [
      "cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY="
      "rstats-on-nix.cachix.org-1:vdiiVgocg6WeJrODIqdprZRUrhi1JzhBnXv7aWI6+F0="
    ];
  };

  inputs = {
    nixpkgs.url = "github:rstats-on-nix/nixpkgs/2026-09-04";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
        };

        gbdk =
          if system == "aarch64-linux" then
            let
              gbdkVersion = "4.3.0";
              sdccArm64 = pkgs.fetchzip {
                url = "https://github.com/gbdk-2020/gbdk-2020-sdcc/releases/download/sdcc-patched-gbdk-${gbdkVersion}/sdcc-14650-Linux-arm64.tar.gz";
                sha256 = "0xfkx6w679d0cnhnidb3q0rq4gqvs8v5nn2q7cp7hifq4hqx70ka";
              };
              gbdkSource = pkgs.fetchzip {
                url = "https://github.com/gbdk-2020/gbdk-2020/archive/refs/tags/${gbdkVersion}.tar.gz";
                sha256 = "0ygkxyqbjlmii9zdskzwk74hldwnymyla2n9glb2l4p33n2a3948";
              };
              gbdkPrecompiled = pkgs.fetchzip {
                url = "https://github.com/gbdk-2020/gbdk-2020/releases/download/${gbdkVersion}/gbdk-linux64.tar.gz";
                sha256 = "0slw2ag8ljgcb6v8qz35f3k3zm8y9nc0j451cgnval7q086ar5xp";
              };
              gbdkSupport =
                let
                  tools = [ "lcc" "ihxcheck" "bankpack" "png2asset"
                            "gbcompress" "makecom" "makebin"
                            "png2hicolorgb" "romusage" ];
                  buildList = builtins.concatStringsSep " " tools;
                in
                pkgs.stdenv.mkDerivation {
                  pname = "gbdk-support";
                  version = gbdkVersion;
                  src = gbdkSource;
                  buildPhase = ''
                    for d in ${buildList}; do
                      make -C gbdk-support/$d TARGETDIR=$out --no-print-directory
                    done
                  '';
                  installPhase = ''
                    mkdir -p $out/bin
                    for d in ${buildList}; do
                      cp gbdk-support/$d/$d $out/bin/
                    done
                  '';
                };
            in
            pkgs.stdenv.mkDerivation {
              pname = "gbdk";
              version = gbdkVersion;
              dontUnpack = true;
              nativeBuildInputs = [ pkgs.autoPatchelfHook ];
              buildInputs = [ pkgs.stdenv.cc.cc.lib pkgs.zlib ];
              installPhase = ''
                mkdir -p $out
                cp -r ${gbdkPrecompiled}/* $out/
                chmod -R u+w $out
                cp -f ${sdccArm64}/bin/* $out/bin/
                if [ -d "${sdccArm64}/libexec" ]; then
                  mkdir -p $out/libexec
                  cp -rf ${sdccArm64}/libexec/* $out/libexec/
                fi
                cp -f ${gbdkSupport}/bin/* $out/bin/
              '';
              meta = with pkgs.lib; {
                description = "GBDK-2020 Game Boy Development Kit";
                homepage = "https://github.com/gbdk-2020/gbdk-2020";
                license = licenses.mit;
                platforms = platforms.linux;
              };
            }
          else
            pkgs.stdenv.mkDerivation rec {
              pname = "gbdk";
              version = "4.3.0";

              src = pkgs.fetchzip {
                url = "https://github.com/gbdk-2020/gbdk-2020/releases/download/${version}/gbdk-linux64.tar.gz";
                sha256 = "0slw2ag8ljgcb6v8qz35f3k3zm8y9nc0j451cgnval7q086ar5xp";
              };

              nativeBuildInputs = [ pkgs.autoPatchelfHook ];
              buildInputs = [ pkgs.stdenv.cc.cc.lib pkgs.zlib ];

              installPhase = ''
                mkdir -p $out
                cp -r * $out/
              '';

              meta = with pkgs.lib; {
                description = "GBDK-2020 Game Boy Development Kit";
                homepage = "https://github.com/gbdk-2020/gbdk-2020";
                license = licenses.mit;
                platforms = platforms.linux;
              };
            };

        # Wheel ABI must match pkgs.python3 (nixpkgs 2026-08-05 ships 3.14,
        # rstats-on-nix 2026-09-04 ships 3.13). Pick per (python, arch).
        pyTag = "cp${builtins.replaceStrings ["."] [""] pkgs.python3.pythonVersion}";
        pyboyWheel =
          if pyTag == "cp313" then
            if system == "aarch64-linux" then {
              url = "https://files.pythonhosted.org/packages/8f/cd/21129161d2e25dc8b851b3c13375c3388dfc55c6f58f7a19726d0326161e/pyboy-2.7.0-cp313-cp313-manylinux2014_aarch64.manylinux_2_17_aarch64.manylinux_2_28_aarch64.whl";
              hash = "sha256-ajL6y3fKc0Mo0IwAoT2iEXCE920osHevWXnduUQQQyI=";
            } else {
              url = "https://files.pythonhosted.org/packages/d4/1c/084ca1c56ed0ba45cf9ea2493698bf96e70e25881c760d83d15ac7ff9b20/pyboy-2.7.0-cp313-cp313-manylinux2014_x86_64.manylinux_2_17_x86_64.manylinux_2_28_x86_64.whl";
              hash = "sha256-/PKa6ZjCfWPz1lmLzPurOJ70HE+lNbSsFXEbqQls8jk=";
            }
          else if pyTag == "cp314" then
            if system == "aarch64-linux" then {
              url = "https://files.pythonhosted.org/packages/ae/d7/adef77aa6ab916032e8dda5ec112daf42cf0c0767067caed5321f9b71069/pyboy-2.7.0-cp314-cp314-manylinux2014_aarch64.manylinux_2_17_aarch64.manylinux_2_28_aarch64.whl";
              hash = "sha256-T98YnkB1DqcybVGUwOxGuvNmUgftL8nxWO4+8FgA0Ho=";
            } else {
              url = "https://files.pythonhosted.org/packages/51/da/ce77683a235cbbf797c8eab25bd6dceabfd2c5109d75e06abbf7b27ff174/pyboy-2.7.0-cp314-cp314-manylinux2014_x86_64.manylinux_2_17_x86_64.manylinux_2_28_x86_64.whl";
              hash = "sha256-ivS1WtgnCzawDo0fnThu7Of3I1EYcuN+8twdR1bQnfc=";
            }
          else throw "pyboy: no wheel pinned for python ${pyTag} on ${system}";

        pyboy = pkgs.python3.pkgs.buildPythonPackage rec {
          pname = "pyboy";
          version = "2.7.0";

          format = "wheel";

          src = pkgs.fetchurl {
            inherit (pyboyWheel) url hash;
          };

          nativeBuildInputs = [
            pkgs.autoPatchelfHook
          ];

          propagatedBuildInputs = with pkgs.python3.pkgs; [
            numpy
          ];

          pythonImportsCheck = [ "pyboy" ];

          # The wheel's METADATA declares pysdl2/pysdl2-dll, but both are
          # guarded by try/except ImportError in PyBoy and unnecessary for
          # headless (window="null") use; skip the runtime deps check.
          dontCheckRuntimeDeps = true;

          doCheck = false;

          meta = with pkgs.lib; {
            description = "Game Boy emulator written in Python";
            homepage = "https://github.com/Baekalfen/PyBoy";
            license = licenses.lgpl3Only;
            platforms = platforms.linux;
          };
        };

        # uge2source CLI built from source on all Linux arches.
        # The old x86_64-only prebuilt hUGETracker-*-linux.zip is x86_64-only,
        # pulls in the full GUI closure (gtk2/pango/cairo/SDL2), and ships a
        # stale bundled rgbasm. Building the tiny CLI from source via
        # fpc/lazarus works identically on x86_64-linux and aarch64-linux and
        # lets rgbasm-huge track nixpkgs rgbds like the rest of the toolchain.
        hugetracker = pkgs.stdenv.mkDerivation rec {
          pname = "hugetracker";
          version = "1.0.11";

          src = pkgs.fetchzip {
            url = "https://github.com/SuperDisk/hUGETracker/archive/refs/tags/v${version}.tar.gz";
            sha256 = "010qva5qqmrg3h5dmhmi4in2y2i1fas4197bjc0vjjx71szn1vln";
          };

          nativeBuildInputs = [ pkgs.fpc pkgs.lazarus ];

          buildPhase = ''
            runHook preBuild
            HOME=$TMPDIR lazbuild --lazarusdir=${pkgs.lazarus}/share/lazarus --build-mode=Release src/uge2source/uge2source.lpi
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin
            # lazbuild --build-mode=Release links to
            # src/uge2source/Release/uge2source, but the exact output dir
            # varies with lazarus version/build mode, so locate it instead
            # of hardcoding one path.
            bin=$(find src/uge2source -maxdepth 2 -type f -name uge2source -print -quit)
            if [ -z "$bin" ]; then
              echo "hugetracker: uge2source binary not found under src/uge2source" >&2
              find src/uge2source -maxdepth 3 -ls >&2
              exit 1
            fi
            cp "$bin" $out/bin/uge2source
            chmod +x $out/bin/uge2source
            ln -s ${pkgs.rgbds}/bin/rgbasm $out/bin/rgbasm-huge
            runHook postInstall
          '';

          meta = with pkgs.lib; {
            description = "A music tracker for the Nintendo Game Boy (uge2source CLI)";
            homepage = "https://github.com/SuperDisk/hUGETracker";
            license = licenses.gpl3Only;
            platforms = platforms.linux;
          };
        };
      in
      {
        packages.gbdk = gbdk;
        packages.hugetracker = hugetracker;

        devShells.default = pkgs.mkShell {
          name = "gb-dev-shell";

          GBDKDIR = "${gbdk}/";
          GBDK_HOME = "${gbdk}/";

          buildInputs = [
            gbdk
            hugetracker
            pkgs.rgbds
            pkgs.sameboy
            pkgs.mgba
            pkgs.gnumake
            pkgs.git
            pkgs.xvfb-run
            pkgs.imagemagick
            pkgs.python3
            pkgs.python3Packages.pillow
            pyboy
            pkgs.nodejs
          ];

          shellHook = ''
            export GBDKDIR="${gbdk}/"
            export GBDK_HOME="${gbdk}/"
          '';
        };
      }
    );
}
