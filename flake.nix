{
  description = "A native Material 3 music player for Linux";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      forEachSupportedSystem = f: nixpkgs.lib.genAttrs supportedSystems (system: f {
        pkgs = import nixpkgs { inherit system; };
      });
    in
    {
      packages = forEachSupportedSystem ({ pkgs }:
        let
          pythonEnv = pkgs.python3.withPackages (ps: with ps; [
            ytmusicapi
            yt-dlp
          ]);

          sung = pkgs.stdenv.mkDerivation (finalAttrs: {
            pname = "sung";
            version = "0.12.0";

            src = ./.;

            nativeBuildInputs = [
              pkgs.cmake
              pkgs.ninja
              pkgs.pkg-config
              pkgs.qt6.wrapQtAppsHook
            ];

            buildInputs = [
              pkgs.qt6.qtbase
              pkgs.qt6.qtdeclarative
              pkgs.qt6.qtmultimedia
              pkgs.qt6.qtsvg
              pkgs.qt6.qtwayland
              pkgs.qt6.qtimageformats
              pkgs.ffmpeg
            ];

            cmakeFlags = [
              "-DBUILD_TESTING=OFF"
              "-DSUNG_DIAGNOSTICS=OFF"
            ];

            preFixup = ''
              qtWrapperArgs+=(
                --prefix PATH : ${pkgs.lib.makeBinPath [ pkgs.nodejs pkgs.ffmpeg ]}
                --set SUNG_PYTHON "${pythonEnv}/bin/python3"
                --set SUNG_HELPER "$out/lib/sung/catalog.py"
              )
            '';

            meta = with pkgs.lib; {
              description = "A native Material 3 music player for Linux (YouTube Music, local audio, synchronized lyrics)";
              homepage = "https://github.com/yappologistic/Sung";
              license = licenses.mit;
              platforms = platforms.linux;
              mainProgram = "sung";
            };
          });
        in
        {
          default = sung;
          sung = sung;
        }
      );

      apps = forEachSupportedSystem ({ pkgs }: {
        default = {
          type = "app";
          program = "${self.packages.${pkgs.system}.sung}/bin/sung";
        };
      });

      devShells = forEachSupportedSystem ({ pkgs }:
        let
          pythonEnv = pkgs.python3.withPackages (ps: with ps; [
            ytmusicapi
            yt-dlp
          ]);
        in
        {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${pkgs.system}.sung ];

            packages = with pkgs; [
              pythonEnv
              nodejs
              ffmpeg
            ];

            shellHook = ''
              export SUNG_HELPER="$PWD/helper/catalog.py"
              export SUNG_PYTHON="${pythonEnv}/bin/python3"
              export PATH="${pkgs.lib.makeBinPath [ pkgs.nodejs pkgs.ffmpeg ]}:$PATH"

              echo "🎵 Sung development environment active"
              echo "   Python: $SUNG_PYTHON"
              echo "   Helper: $SUNG_HELPER"
            '';
          };
        }
      );

      overlays.default = final: prev: {
        sung = self.packages.${final.system}.sung;
      };
    };
}
